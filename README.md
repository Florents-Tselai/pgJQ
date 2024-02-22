# pgJQ: Use `jq` in Postgres

<a href="https://opensource.org/licenses/MIT license"><img src="https://img.shields.io/badge/MIT license-blue.svg"></a>
<img alt="GitHub Repo stars" src="https://img.shields.io/github/stars/Florents-Tselai/pgJQ">

The **pgJQ**  extension embeds the standard jq compiler and brings the much loved [jq](https://github.com/jqlang/jq) lang to Postgres.

It adds a `jqprog` data type to express `jq` programs 
and a `jq(jsonb, jqprog)` function to execute them on `jsonb` objects.
It works seamlessly with standard `jsonb` functions, operators, and `jsonpath`.

```sql
SELECT jq('[{"bar": "baz", "balance": 7.77, "active":false}]'::jsonb, '.[0].bar');
```

```
  jq   
-------
 "baz"
(1 row)
```

## Table of Contents

- [Usage](#usage)
  - [Filters](#filters)
  - [Complex Programs](#complex-programs)
  - [Passing Arguments to `jqprog`](#passing-arguments)
  - [`jsonb` `@@` `jqprog`](#jsonb--jqprog)
  - [Working with Files](#working-with-files)
- [Installation](#installation)
- [How it Works](#how-it-works)
- [Issues](#issues)

## Usage

### Filters

You can run basic filters:

```sql
SELECT jq('[{"bar": "baz", "balance": 7.77, "active":false}]'::jsonb, '.[0].bar');
```

```
  jq   
-------
 "baz"
(1 row)
```

### `jsonb` `@@` `jqprog`

If you're a syntactic sugar addict, you can use the `@@` operator to achieve the same.
It's better be explicit with the `::jqprog` when using operators.

```sql
SELECT '[{"bar": "baz", "balance": 7.77, "active":false}]' @@ '.[0].bar'::jqprog;
```

```
  jq   
-------
 "baz"
(1 row)
```

### Complex Programs

You can run more complex `jq` programs too:

```sql
SELECT jq('[true,false,[5,true,[true,[false]],false]]',
          '(..|select(type=="boolean")) |= if . then 1 else 0 end');
```
```
             jq              
-----------------------------
 [1, 0, [5, 1, [1, [0]], 0]]
(1 row)
```

```sql
SELECT jq('[1,5,3,0,7]' , '(.[] | select(. >= 2)) |= empty');
```
```
   jq   
--------
 [1, 0]
(1 row)
```

### Passing Arguments to `jqprog`

If you want to pass dynamic arguments to `jqprog`,
you can pass them as a `jsonb` object
and refer to them as `$var`.

```sql
select jq('{
  "runner": 1,
  "message": "jobStatus",
  "jobs": [
    {
      "id": 9,
      "is_successfull": true
    },
    {
      "id": 100,
      "is_successfull": false,
      "metdata": {
        "environ": "prod"
      }
    }
  ]
}'::jsonb, '.jobs[] | select(.is_successfull == $is_success and .id == 100) | .', '{"is_success": false, "id": 100}');
```
```
                                  jq                                  
----------------------------------------------------------------------
 {"id": 100, "metdata": {"environ": "prod"}, "is_successfull": false}
(1 row)
```

### `jq` and `jsonpath`

You can even chain `jq` and `jsonpath` together!

Note here that the later part `- '{trans}' @> '{"cust": "baz"}'` is `jsonpath`, not `jq` code.
```sql
SELECT jq('[
  {
    "cust": "baz",
    "trans": {
      "balance": 100,
      "date": "2023-08-01"
    },
    "active": true,
    "geo": {
      "branch": "paloukia"
    }
  }
]', '(.[] | select(.active == true))') - '{trans}' @> '{"cust": "baz"}';
```
```
 ?column? 
----------
 t
(1 row)
```

If you opt for using operators here, you should help the parser by adding parentheses and explicit casts.

```sql
SELECT ('[
  {
    "cust": "baz",
    "trans": {
      "balance": 100,
      "date": "2023-08-01"
    },
    "active": true,
    "geo": {
      "branch": "paloukia"
    }
  }
]' @@ '(.[] | select(.active == true))'::jqprog) - '{trans}' @> '{"cust": "baz"}';
```

It is strongly recommended to be explicit 
with type casts and ordering when using overloaded operators,
especially when you're working a lot with text.
Otherwise, you'll find yourself in an obfuscated labyrinth of
`jqprog`s, `jsonb`s,  `jsonpath`s and possibly `tsvector`s ,
impossible to escape from.

### Working with Files

If you have superuser privileges in Postgres you can use the `pg_read_file` 
to run your queries on JSON files.

```sql
SELECT jq(pg_read_file('/path/to/f.json', '.[]'))
```

You can see more examples in the [test cases](test/sql/basic.sql) 
or try reproducing the [`jq` manual](https://jqlang.github.io/jq/manual/) .

## Installation

```sh
git clone --recurse-submodules https://github.com/Florents-Tselai/pgJQ.git
```

Start by building a slightly-patched version of `jq`.
See here [why](#how-it-works).

```sh
cd pgjq
cd jq
git switch jq-api
autoreconf -i
./configure --with-oniguruma=builtin --prefix=$(PWD)/build
make -j
make install
```

Then build install the pgJQ extension

```sh
cd ..
make install # set PG_CONFIG=/path/to/bin/pg_config if necessary.
make installcheck
```

In a Postgres session run

```sql
CREATE EXTENSION pgjq
```

## How it Works

pgJQ does not re-implement the `jq` lang in Postgres.
It instead embeds the standard `jq` compiler and uses it to parse `jq` programs supplied in SQL queries.
These programs are fed with `jsonb` objects as input.  

`jq`, however, is stream-oriented, and its [C API](https://github.com/jqlang/jq/wiki/C-API:-jv) is tailored around the CLI version of `jq` as a tool.
To make this work with Postgres, I had to trick the `jq` compiler into thinking it reads from a stream instead of a SQL text variable.
Check [this commit](https://github.com/Florents-Tselai/jq/commit/43155c0c96f7daf9353ad990128b15eca1b6df0c)

The [`fmemopen`](https://man7.org/linux/man-pages/man3/fmemopen.3.html) came in handy for this:

```c
FILE *file_json = fmemopen(json_string, strlen(json_string), "r");
```

Then, the final `jv` result (`jv` is the equivalent of `Datum` for `jq`) is passed to the below function
that recursively parses it to build a `JsonbValue` which is the result we're interested in.

```c
JsonbValue *JvObject_to_JsonbValue(jv *obj, JsonbParseState **jsonb_state, bool is_elem)
```

## Issues

`jq` has evolved from *just a cli tool* to a full-fledged DSL,
but it still remains a 20-80 tool.

**pgJQ** has been TDDed against those 20% of the cases.
If you come across regressions between vanilla `jq` and pgJQ,
especially around piped filters or complex functions,
please do add an issue, along with a test case!

Keeping in mind, though, that there's probably not much point reproducing the whole 
DSL in an RDBMS context.

Some known issues are:
* Only string, bool and numeric arguments can be passed to `jqprog`.
* Currently, `jq` programs including pipes, like `.[] | .name` are buggy and unpredictable.
* Modules are not supported, but they could be theoretically supported, given that Postgres is fairly open to dynamic loading.

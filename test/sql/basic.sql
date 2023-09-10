
/* --- Type creation and casts  --- */

select 'gsdfgf'::jqprog;
select 1345::jqprog;

 /* --- Arrays as input  --- */

select jq('[{"bar": "baz", "balance": 7.77, "active":false}]'::jsonb, '.[0].bar');
select jq('[{"bar": "baz", "balance": 7.77, "active":false}]'::jsonb, '.[0].balance');
select jq('[{"bar": "baz", "balance": 7.77, "balance_int": 7, "active":false}]'::jsonb, '.[0].balance_int');
select jq('[{"bar": "baz", "balance": 7.77, "active":false}]'::jsonb, '.[0].active');
select jq('[{"bar": "baz", "balance": 7.77, "active":false, "keno": null}]'::jsonb, '.[0].keno');
select jq('[{"bar": "baz", "balance": 7.77, "active":false, "keno": null}]'::jsonb, '.[0].keno');
select jq('[["a", "b", "c"]]'::jsonb, '.[0]');
select jq('[["a", "b", "c"]]'::jsonb, '.[0].[0]');
select jq('[["a", "b", "c"]]'::jsonb, '.[0].[1]');
select jq('[["a", "b", "c"]]'::jsonb, '.[0].[1000]');

select jq('[{"bar": "baz", "balance": 7.77, "active":false}]'::jsonb, '.[0]');
select jq('[{"bar": "baz", "balance": 7.77, "active":false}]'::jsonb, '.[100]');
select jq('[{"bar": "baz", "balance": 7.77, "active":false}]'::jsonb, '.[0].bar');
select jq('[{"bar": "baz", "balance": 7.77, "active":false}]'::jsonb, '.[0].gdfgdf');

/* --- Single object as input --- */

select jq('{"bar": "baz", "balance": 7.77, "active":false}'::jsonb, '.');
select jq('{"bar": "baz", "balance": 7.77, "active":false}'::jsonb, '.baz');
select jq('{"bar": "baz", "balance": 7.77, "active":false}'::jsonb, '.balance');
select jq('{"bar": "baz", "balance": 7.77, "active":false}'::jsonb, '.gdfgfd');

select jq('{"user":"stedolan", "projects": ["jq", "wikiflow"]}', '.user, .projects[]');

/* --- Nested object --- */

select jq('{"bar": "baz", "balance": 7.77, "active":false, "inner": {"key": "value"}}'::jsonb, '.gdfgfd');
select jq('{"bar": "baz", "balance": 7.77, "active":false, "inner": {"key": "value"}}'::jsonb, '.inner');
select jq('{"bar": "baz", "balance": 7.77, "active":false, "inner": {"key": "value"}}'::jsonb, '.inner.key');
select jq('{"bar": "baz", "balance": 7.77, "active":false, "inner": {"key": "value"}}'::jsonb, '.gdfgfd');

/* --- pick --- */

select jq('{"a": [123]}', '.a[]');

/* --- assignment --- */

select jq('[true,false,[5,true,[true,[false]],false]]', '(..|select(type=="boolean")) |= if . then 1 else 0 end');
select jq('{"foo": 42}', '.foo += 1');
select jq('{"a": {"b": 10}, "b": 20}', '.a = .b');

/* --- select --- */

select jq('[1,5,3,0,7]' , '(.[] | select(. >= 2)) |= empty');
select jq('[1,5,3,0,7]', '.[] |= select(. >= 4)');
select jq('[1,5,3,0,7]', '.[] |= select(. == 2)');

/* --- if/else conditionals --- */
select jq('2', '.<5');
select jq('{"prod":  "life", "price":  10}', 'if .price > 0 then "it is expensive" else {"oh": "well"} end');
select jq('{"prod":  "life", "price":  10}', 'if .price > 100 then "it is expensive" else {"oh": "well"} end');

select jq('2', 'if . == 0 then "zero" elif . == 1 then "one" else "many" end');
select jq('3', 'if . == 0 then "zero" elif . == 1 then "one" else {"big": "object"} end');

/* --- builtin functions --- */

select jq('1', '[repeat(.*2, error)?]');
select jq('4', '[.,1]|until(.[0] < 1; [.[0] - 1, .[1] * .[0]])|.[1]');
select jq('[[4, 1, 7], [8, 5, 2], [3, 6, 9]]', 'walk(if type == "array" then sort else . end)');
select jq('[ { "_a": { "__b": 2 } } ]', 'walk( if type == "object" then with_entries( .key |= sub( "^_+"; "") ) else . end )');

select jq('[0,1]', 'bsearch(0)');
select jq('[1,2,3]', 'bsearch(0)');
select jq('[1,2,3]', 'bsearch(0)');
select jq('[1,2,3]', 'bsearch(4) as $ix | if $ix < 0 then .[-(1+$ix)] = 4 else . end');

/* --- convert to/from JSON --- */
select jq('[1, "foo", ["foo"]]', '[.[]|tostring]');
select jq('[1, "foo", ["foo"]]', '[.[]|tojson]');
select jq('[1,"foo",["foo"]]', '[.[]|tojson|fromjson]');

/* --- chain with jsonpath --- */

select jq('[
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
  }]',
    '(.[] | select(.active == true))') #- '{trans}' @> '{"cust": "baz"}';

/* --- ---argjson --- */

-- test numeric argument
select jq('{
  "success": true,
  "message": "jobStatus",
  "jobStatus": [
    {
      "ID": 9,
      "status": "Successful"
    },
    {
      "ID": 2,
      "status": "Successful"
    },
    {
      "ID": 99,
      "status": "Failed"
    }
  ]
}'::jsonb, '.jobStatus[] | select(.ID == $job_id) | .status', '{"job_id": 2}');

-- test string argument


select jq('{
  "success": true,
  "message": "jobStatus",
  "jobStatus": [
    {
      "ID": 9,
      "status": "Successful"
    },
    {
      "ID": 2,
      "status": "Successful"
    },
    {
      "ID": 99,
      "status": "Failed"
    }
  ]
}'::jsonb, '.jobStatus[] | select(.status == $status) | .', '{"status": "Failed"}');

-- test boolean argument

select jq('{
  "success": true,
  "message": "jobStatus",
  "jobs": [
    {
      "ID": 9,
      "is_successfull": true
    },
    {
      "ID": 100,
      "is_successfull": false
    }
  ]
}'::jsonb, '.jobs[] | select(.is_successfull == $is_success) | .', '{"is_success": true}');

-- test multiple arguments

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
CREATE TYPE jqprog;

CREATE FUNCTION jqprog_in(cstring) RETURNS jqprog
    IMMUTABLE
    STRICT
    LANGUAGE C
AS
'MODULE_PATHNAME';

CREATE FUNCTION jqprog_out(jqprog) RETURNS cstring
    IMMUTABLE
    STRICT
    LANGUAGE C
AS
'MODULE_PATHNAME';

CREATE TYPE jqprog
(
    INTERNALLENGTH = -1,
    INPUT = jqprog_in,
    OUTPUT = jqprog_out
);


CREATE CAST (jqprog AS text) WITH INOUT AS ASSIGNMENT;
CREATE CAST (text AS jqprog) WITH INOUT AS ASSIGNMENT;

CREATE FUNCTION jq(jsonb, jqprog, jsonb DEFAULT '{}') RETURNS jsonb
AS
'MODULE_PATHNAME' LANGUAGE C IMMUTABLE
                             STRICT
                             PARALLEL SAFE;

CREATE FUNCTION __op_jq_jsonb_jqprog(jsonb, jqprog) RETURNS jsonb
AS
'SELECT jq($1, $2)'
    LANGUAGE SQL
    IMMUTABLE
    STRICT
    PARALLEL SAFE;

CREATE OPERATOR @@ (
    LEFTARG = jsonb,
    RIGHTARG = jqprog,
    FUNCTION = __op_jq_jsonb_jqprog
    );


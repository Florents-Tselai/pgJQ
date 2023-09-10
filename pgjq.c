#include "postgres.h"
#include "utils/jsonb.h"

#include "utils/builtins.h"

#include "stdlib.h"
#include "stdio.h"
#include "utils/numeric.h"

#include "jq.h"


PG_MODULE_MAGIC;

/* This is useed recursively so we have to declare it here first */
static JsonbValue *JvObject_to_JsonbValue(jv *obj, JsonbParseState **jsonb_state, bool is_elem);

/* --------------
 * Missing JQ API
 *
 * As JQ misses an API intended to be used as a library,
 * The following have been copied & reverse-engineered from jq/src/main.c
 *
 * You'll see a lot "variable not used" warnings there.
 * I know. Don't remove them just to make the compiler happy.
 * We may need them once bugs are discovered.
 *
 * --------------
 * */
enum {
    SLURP = 1,
    RAW_INPUT = 2,
    PROVIDE_NULL = 4,
    RAW_OUTPUT = 8,
    RAW_OUTPUT0 = 16,
    ASCII_OUTPUT = 32,
    COLOR_OUTPUT = 64,
    NO_COLOR_OUTPUT = 128,
    SORTED_OUTPUT = 256,
    FROM_FILE = 512,
    RAW_NO_LF = 1024,
    UNBUFFERED_OUTPUT = 2048,
    EXIT_STATUS = 4096,
    EXIT_STATUS_EXACT = 8192,
    SEQ = 16384,
    RUN_TESTS = 32768,
    /* debugging only */
    DUMP_DISASM = 65536,
};

enum {
    JQ_OK = 0,
    JQ_OK_NULL_KIND = -1, /* exit 0 if --exit-status is not set*/
    JQ_ERROR_SYSTEM = 2,
    JQ_ERROR_COMPILE = 3,
    JQ_OK_NO_OUTPUT = -4, /* exit 0 if --exit-status is not set*/
    JQ_ERROR_UNKNOWN = 5,
};

/* jq/src/util.h:priv_fwrite() */
static void jq_priv_fwrite(const char *s, size_t len, FILE *fout, int is_tty) {
#ifdef WIN32
    if (is_tty)
    WriteFile((HANDLE)_get_osfhandle(fileno(fout)), s, len, NULL, NULL);
  else
    fwrite(s, 1, len, fout);
#else
    fwrite(s, 1, len, fout);
#endif
}

static int jq_process(jq_state *jq, jv value, int flags, int dumpopts, int options, jv *out_result) {
    jv result;

    int ret = JQ_OK_NO_OUTPUT; // No valid results && -e -> exit(4)
    jq_start(jq, value, flags);
    while (jv_is_valid(result = jq_next(jq))) {

        if ((options & RAW_OUTPUT) && jv_get_kind(result) == JV_KIND_STRING) {

            if (options & ASCII_OUTPUT) {
                jv_dumpf(jv_copy(result), stdout, JV_PRINT_ASCII);
            } else if ((options & RAW_OUTPUT0) &&
                       strlen(jv_string_value(result)) != (unsigned long) jv_string_length_bytes(jv_copy(result))) {
                jv_free(result);
                result = jv_invalid_with_msg(jv_string(
                        "Cannot dump a string containing NUL with --raw-output0 option"));
                break;
            } else {
                jq_priv_fwrite(jv_string_value(result), jv_string_length_bytes(jv_copy(result)),
                               stdout, dumpopts & JV_PRINT_ISATTY);
            }
            ret = JQ_OK;
            jv_free(result);
        } else {
            if (jv_get_kind(result) == JV_KIND_FALSE || jv_get_kind(result) == JV_KIND_NULL)
                ret = JQ_OK_NULL_KIND;
            else
                ret = JQ_OK;
            if (options & SEQ)
                jq_priv_fwrite("\036", 1, stdout, dumpopts & JV_PRINT_ISATTY);

            *out_result = jv_copy(result);

        }
        if (!(options & RAW_NO_LF))
            jq_priv_fwrite("\n", 1, stdout, dumpopts & JV_PRINT_ISATTY);
        if (options & RAW_OUTPUT0)
            jq_priv_fwrite("\0", 1, stdout, dumpopts & JV_PRINT_ISATTY);
        if (options & UNBUFFERED_OUTPUT)

            fflush(stdout);
    }
    if (jq_halted(jq)) {
        // jq program invoked `halt` or `halt_error`
        options |= EXIT_STATUS_EXACT;
        jv exit_code = jq_get_exit_code(jq);
        if (!jv_is_valid(exit_code))
            ret = JQ_OK;
        else if (jv_get_kind(exit_code) == JV_KIND_NUMBER)
            ret = jv_number_value(exit_code);
        else
            ret = JQ_ERROR_UNKNOWN;
        jv_free(exit_code);
        jv error_message = jq_get_error_message(jq);
        if (jv_get_kind(error_message) == JV_KIND_STRING) {
            // No prefix should be added to the output of `halt_error`.
            jq_priv_fwrite(jv_string_value(error_message), jv_string_length_bytes(jv_copy(error_message)),
                           stderr, dumpopts & JV_PRINT_ISATTY);
        } else if (jv_get_kind(error_message) == JV_KIND_NULL) {
            // Halt with no output
        } else if (jv_is_valid(error_message)) {
            error_message = jv_dump_string(error_message, 0);
            ereport(ERROR,
                    (errmsg("%s\n", jv_string_value(error_message))));

        } // else no message on stderr; use --debug-trace to see a message
        fflush(stderr);
        jv_free(error_message);
    } else if (jv_invalid_has_msg(jv_copy(result))) {
        // Uncaught jq exception
        jv msg = jv_invalid_get_msg(jv_copy(result));
        jv input_pos = jq_util_input_get_position(jq);
        if (jv_get_kind(msg) == JV_KIND_STRING) {

            fprintf(stderr, "jq: BOOOM error (at %s): %s\n",
                    jv_string_value(input_pos), jv_string_value(msg));

        } else {
            msg = jv_dump_string(msg, 0);
            fprintf(stderr, "jq: error (at %s) (not a string): %s\n",
                    jv_string_value(input_pos), jv_string_value(msg));
        }
        ret = JQ_ERROR_UNKNOWN;
        jv_free(input_pos);
        jv_free(msg);
    }
    jv_free(result);
    return ret;
}

/*
 * jqprog is basically a text
 *
 * */

typedef text jqprog;
#define DatumGetJqProgP(X)        ((jqprog *) PG_DETOAST_DATUM(X))
#define DatumGetJqProgPP(X)    ((jqprog *) PG_DETOAST_DATUM_PACKED(X))
#define JqPGetDatum(X)        PointerGetDatum(X)

#define PG_GETARG_JQPROG_P(n)    DatumGetJqP(PG_GETARG_DATUM(n))
#define PG_GETARG_JQPROG_PP(n)    DatumGetJqPP(PG_GETARG_DATUM(n))
#define PG_RETURN_JQPROG_P(x)    PG_RETURN_POINTER(x)

PG_FUNCTION_INFO_V1(jqprog_in);

Datum
jqprog_in(PG_FUNCTION_ARGS) {
    char *s = PG_GETARG_CSTRING(0);
    jqprog *vardata;

    /* TODO: validate here = compile the expression */

    vardata = (jqprog *) cstring_to_text(s);
    PG_RETURN_JQPROG_P(vardata);
}

PG_FUNCTION_INFO_V1(jqprog_out);

Datum
jqprog_out(PG_FUNCTION_ARGS) {
    Datum arg = PG_GETARG_DATUM(0);

    PG_RETURN_CSTRING(TextDatumGetCString(arg));
}

/*
 * A recursive parser that converts a jv (jq) object to a JsonbValue (Postgres)
 */

static void JvNumber_ToJsonbValue(jv *obj, JsonbValue *jbvElem) {
    jbvElem->type = jbvNumeric;

    jbvElem->val.numeric = DatumGetNumeric(DirectFunctionCall1(float8_numeric, Float8GetDatum(jv_number_value(*obj))));
}

static void JvString_ToJsonbValue(jv *obj, JsonbValue *jbvElem) {
    jbvElem->type = jbvString;
    jbvElem->val.string.val = jv_string_value(*obj);
    jbvElem->val.string.len = strlen(jbvElem->val.string.val);
}

static JsonbValue *JvArray_ToJsonbValue(jv *obj, JsonbParseState **jsonb_state) {
    Assert(jv_get_kind(*obj) == JV_KIND_ARRAY);

    jv *volatile value = NULL;

    value = palloc(sizeof(jv));

    pushJsonbValue(jsonb_state, WJB_BEGIN_ARRAY, NULL);

    /* FIXME: is there maybe a memory leak around here with how value is handled ? */
    jv_array_foreach(*obj, idx, elem) {
            *value = jv_copy(elem);
            (void) JvObject_to_JsonbValue(value, jsonb_state, true);
        }

    return pushJsonbValue(jsonb_state, WJB_END_ARRAY, NULL);
}

static JsonbValue *JvJsonObject_ToJsonbValue(jv *obj, JsonbParseState **jsonb_state) {
    Assert(jv_get_kind(*obj) == JV_KIND_OBJECT);

    JsonbValue *volatile out;

    pushJsonbValue(jsonb_state, WJB_BEGIN_OBJECT, NULL);

    jv_object_foreach(*obj, obj_key, obj_item) {
            JsonbValue jbvKey;

            if (jv_get_kind(obj_item) == JV_KIND_NULL) {
                /* FIXME: should this ever happend? */
            } else {
                JvString_ToJsonbValue(&obj_key, &jbvKey);
            }

            (void) pushJsonbValue(jsonb_state, WJB_KEY, &jbvKey);
            (void) JvObject_to_JsonbValue(&obj_item, jsonb_state, false);
        }
    out = pushJsonbValue(jsonb_state, WJB_END_OBJECT, NULL);

    return out;
}

static JsonbValue *JvObject_to_JsonbValue(jv *obj, JsonbParseState **jsonb_state, bool is_elem) {
    JsonbValue *out;

    if (jv_get_kind(*obj) != JV_KIND_STRING) {
        if (jv_get_kind(*obj) == JV_KIND_ARRAY)
            return JvArray_ToJsonbValue(obj, jsonb_state);
        else if (jv_get_kind(*obj) == JV_KIND_OBJECT)
            return JvJsonObject_ToJsonbValue(obj, jsonb_state);

    }

    out = palloc(sizeof(JsonbValue));

    if (jv_get_kind(*obj) == JV_KIND_NULL)
        out->type = jbvNull;

    else if (jv_get_kind(*obj) == JV_KIND_STRING)
        JvString_ToJsonbValue(obj, out);

    else if ((jv_get_kind(*obj) == JV_KIND_TRUE) || (jv_get_kind(*obj) == JV_KIND_FALSE)) {
        out->type = jbvBool;
        if (jv_get_kind(*obj) == JV_KIND_TRUE)
            out->val.boolean = true;
        else if (jv_get_kind(*obj) == JV_KIND_FALSE)
            out->val.boolean = false;
        else
            /* Shouldn't happen */
            ereport(ERROR,
                    (errcode(ERRCODE_ASSERT_FAILURE),
                            errmsg("something really bad happened with jv boolean stuff")));

    } else if (jv_get_kind(*obj) == JV_KIND_NUMBER)
        JvNumber_ToJsonbValue(obj, out);

    else
        ereport(ERROR,
                (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
                        errmsg("JV type \"%s\" cannot be transformed to jsonb",
                               jv_kind_name(jv_get_kind(*obj)))));
    return (*jsonb_state ?
            pushJsonbValue(jsonb_state, is_elem ? WJB_ELEM : WJB_VALUE, out) :
            out);
}

PG_FUNCTION_INFO_V1(jq);

Datum
jq(PG_FUNCTION_ARGS) {

    /* Input JSON and accompanying metadata */
    Jsonb *jsonb = PG_GETARG_JSONB_P(0);
    JsonbValue *jbvIn;
    jbvIn = palloc(sizeof(JsonbValue));

    if (jbvIn == NULL)
        ereport(ERROR, (errmsg("could not allocate jsonbValueIn")));

    JsonbToJsonbValue(jsonb, jbvIn);

    char *json_string = JsonbToCString(NULL, &jsonb->root, VARSIZE(jsonb));

    /* The JQ program to execute */
    char *program = text_to_cstring(PG_GETARG_TEXT_PP(1));

    /* --argjson --- */
    Jsonb *jsonbArgs = PG_GETARG_JSONB_P(2);
    JsonbValue *jbvArgs;
    JsonbIterator *argsIt;
    JsonbValue argsV;
    JsonbIteratorToken argsToken;

    /* As jq works with stream, let's make the input json char* act like a stream */
    FILE *file_json = fmemopen(json_string, strlen(json_string), "r");


    if (file_json == NULL)
        ereport(ERROR, (errmsg("error in fmemopen(file_json))")));

    /* Output */

    /* The jv_result above is converted to this datum which is returned */
    JsonbValue *jbvOut;
    JsonbParseState *jsonb_state = NULL;

    /* Now start building the jq processor like in src/jq/main.c */

    jv ARGS;
    jv program_arguments;

    jq_state *jq = NULL;
    int ret = JQ_OK_NO_OUTPUT;
    int compiled = 0;
    int parser_flags = 0;
    int last_result = -1; /* -1 = no result, 0=null or false, 1=true */
    int options = 0;
    int dumpopts;
    int jq_flags;

    jv value; /* The json value being parsed by jq parsers */
    jv obj; /* The result of jq processing */

    /* Now start jq processing */

    /* Init jq */
    ARGS = jv_object(); /* This is actually not used in pgjq, probably never will */
    program_arguments = jv_object(); /*looks like that's the object storing --argson args*/

    /* set program_arguments
     * Iterate over argsV and set them to program_arguments
     * */
    jbvArgs = palloc(sizeof(JsonbValue));
    argsIt = JsonbIteratorInit(&jsonbArgs->root);
    bool skipNested = true;
    while ((argsToken = JsonbIteratorNext(&argsIt, &argsV, skipNested)) != WJB_DONE) {
        skipNested = true;
        if (argsToken == WJB_KEY) {
            text *key;
            Datum values[2]; /* TODO: looks like this is not used */
            bool nulls[2] = {false, false};


            key = cstring_to_text_with_len(argsV.val.string.val, argsV.val.string.len);

            /*
             * The next thing the iterator fetches should be the value, no
             * matter what shape it is.
             */
            argsToken = JsonbIteratorNext(&argsIt, &argsV, skipNested);
            Assert(argsToken != WJB_DONE);

            values[0] = PointerGetDatum(key);

            if (argsV.type == jbvNull) {
                /* a json null is an sql null in text mode */
                nulls[1] = true;
                values[1] = (Datum) NULL;
            } else {
                values[1] = PointerGetDatum(JsonbValueToJsonb(&argsV));

                if (argsV.type == jbvNumeric) {
                    /* TODO: extract the conversion as a separate function */
                    jv_object_set(program_arguments, jv_string(text_to_cstring(key)),
                                  jv_number(DatumGetFloat8(
                                          DirectFunctionCall1(numeric_float8, NumericGetDatum(argsV.val.numeric)))));
                } else if (argsV.type == jbvString) {
                    jv_object_set(program_arguments, jv_string(text_to_cstring(key)),
                                  jv_string(argsV.val.string.val));

                } else if (argsV.type == jbvBool) {
                    jv_object_set(program_arguments,
                                  jv_string(text_to_cstring(key)),
                                  jv_bool(argsV.val.boolean));

                } else {
                    ereport(ERROR,
                            (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
                                    errmsg("Currently only numeric, string and bool arguments can be passed to jqprog")));

                }

            }


        }

    }


    jq = jq_init();
    if (jq == NULL)
        ereport(ERROR, (errmsg("pgjq: error initializing JQ (malloc)")));


    dumpopts = JV_PRINT_INDENT_FLAGS(2);

    /* This if-else has been implemented in a:
     * this-combination-looks-like-it-works
     * Need to make this more predictable.
     * */
    if (jbvIn->type == jbvArray)
        options |= SLURP;
    else
        options |= PROVIDE_NULL;

    dumpopts &= ~(JV_PRINT_TAB | JV_PRINT_INDENT_FLAGS(7)); /* --compact-output */

    jq_flags = 0;

    /* Compile the program */
    compiled = jq_compile_args(jq, program, jv_copy(program_arguments));

    if (compiled < 1)
        ereport(ERROR,
                (errmsg("pgjq: could not compile program: %s . Check your syntax\n", program)));


    jq_util_input_state *input_state = jq_util_input_init(NULL, NULL); // XXX add err_cb

    if ((options & RAW_INPUT))
        jq_util_input_set_parser(input_state, NULL, (options & SLURP) ? 1 : 0);
    else
        jq_util_input_set_parser(input_state, jv_parser_new(parser_flags), (options & SLURP) ? 1 : 0);

    /* Let jq program read from inputs */
    jq_set_input_cb(jq, jq_util_input_next_input_cb, input_state);

    jq_util_input_set_input_file(input_state, file_json);

    while (jq_util_input_errors(input_state) == 0 &&
           (jv_is_valid((value = jq_util_input_next_input(input_state))) || jv_invalid_has_msg(jv_copy(value)))) {
        if (jv_is_valid(value)) {
            ret = jq_process(jq, value, jq_flags, dumpopts, options, &obj);
            if (ret <= 0 && ret != JQ_OK_NO_OUTPUT)
                last_result = (ret != JQ_OK_NULL_KIND);
            if (jq_halted(jq))
                break;
            continue;
        }

        /* Parse error */
        jv msg = jv_invalid_get_msg(value);
        if (!(options & SEQ)) {
            ret = JQ_ERROR_UNKNOWN;
            ereport(ERROR,
                    (errmsg("pgjq: parse error: %s\n", jv_string_value(msg))));
        }
        // --seq -> errors are not fatal
        ereport(WARNING,
                (errmsg("jq: ignoring parse error: %s\n", jv_string_value(msg))));
        jv_free(msg);
    }

    /* Start building the result */
    jbvOut = JvObject_to_JsonbValue(&obj, &jsonb_state, true);

    /* free */
    jv_free(ARGS);
    jv_free(program_arguments);
    jq_util_input_free(&input_state);
    jq_teardown(&jq);
    jv_free(obj);
    pfree(jbvIn);
    pfree(jbvArgs);
    fclose(file_json);

    PG_RETURN_JSONB_P(JsonbValueToJsonb(jbvOut));
}

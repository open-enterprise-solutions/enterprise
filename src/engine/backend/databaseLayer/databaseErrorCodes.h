#ifndef __DATABASE_ERROR_CODES_H__
#define __DATABASE_ERROR_CODES_H__

#define DATABASE_LAYER_OK 0
#define DATABASE_LAYER_ERROR 1
#define DATABASE_LAYER_INVALID_USER 2
#define DATABASE_LAYER_BAD_PASSWORD 3
#define DATABASE_LAYER_CONSTRAINT_VIOLATION 4
#define DATABASE_LAYER_SQL_SYNTAX_ERROR 5
#define DATABASE_LAYER_ALLOCATION_ERROR 6
#define DATABASE_LAYER_INCOMPATIBLE_FIELD_TYPE 7
#define DATABASE_LAYER_FIELD_NOT_IN_RESULTSET 8
#define DATABASE_LAYER_NO_ROWS_FOUND 9
#define DATABASE_LAYER_NON_UNIQUE_RESULTSET 10
#define DATABASE_LAYER_UNSUPPORTED_OPERATION 11
#define DATABASE_LAYER_ERROR_LOADING_LIBRARY 12

// NOT a failure signal for RunQuery / a prepared statement's RunQuery.
//
// Those return the AFFECTED-ROW COUNT, and 0 is a legitimate count: DDL, a SET, an UPDATE
// whose WHERE matched nothing. A driver reports failure by THROWING (ThrowDatabaseException),
// so testing a return value against this constant asks the wrong question — it cannot tell a
// successful statement that touched no rows from a failed one.
//
// The value stays 0 for the inherited call sites that still return it as their own "no" (an
// early exit before any statement ran). Do not add new ones.
#define DATABASE_LAYER_QUERY_RESULT_ERROR 0

#endif // __DATABASE_ERROR_CODES_H__

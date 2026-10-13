#ifndef _FRMCLIENT_BACKEND_QUERY_AST_H__
#define _FRMCLIENT_BACKEND_QUERY_AST_H__

// THE PARSED QUERY — the engine's AST (backend/query/queryAST.h): the server's, which parses a text and runs it. What
// the client's windows name of it is the pointer an expression editor is handed and gives back — never filled here.

#include <memory>

struct ibQueryAstExpr;
using ibQueryAstExprPtr = std::shared_ptr<ibQueryAstExpr>;

#endif

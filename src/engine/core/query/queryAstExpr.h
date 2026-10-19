#ifndef __QUERY_AST_EXPR_H__
#define __QUERY_AST_EXPR_H__

////////////////////////////////////////////////////////////////////////////
//	Description : AN EXPRESSION OF A PARSED QUERY, BY NAME - what every side hands about
////////////////////////////////////////////////////////////////////////////
//
// The tree itself is the engine's (backend/query/queryAST.h): it holds values, and the parser and the
// lowering that build and read it are the server's. What the other sides name of it is the pointer — an
// expression editor is handed one and gives one back — so the name lives here and the body stays there.

#include <memory>

struct ibQueryAstExpr;
using ibQueryAstExprPtr = std::shared_ptr<ibQueryAstExpr>;

#endif // __QUERY_AST_EXPR_H__

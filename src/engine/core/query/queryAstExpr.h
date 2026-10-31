#ifndef __QUERY_AST_EXPR_H__
#define __QUERY_AST_EXPR_H__

////////////////////////////////////////////////////////////////////////////
//	Description : AN EXPRESSION OF A PARSED QUERY, BY NAME - what every side hands about
////////////////////////////////////////////////////////////////////////////
//
// The tree itself is core/query/queryAST.h, with the parser that builds it; the lowering that runs it is the
// engine's. What a side that only hands an expression about names of it is the pointer — an expression editor is
// handed one and gives one back — so the name lives here, apart from the tree.

#include <memory>

struct ibQueryAstExpr;
using ibQueryAstExprPtr = std::shared_ptr<ibQueryAstExpr>;

#endif // __QUERY_AST_EXPR_H__

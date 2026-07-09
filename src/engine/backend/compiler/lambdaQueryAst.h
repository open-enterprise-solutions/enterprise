#ifndef __LAMBDA_QUERY_AST_H__
#define __LAMBDA_QUERY_AST_H__

// L4-2 — lambda expression recorder (the dual-compile front half).
//
// The script compiler is single-pass and keeps no AST. To push a LINQ lambda
// down to the database, the lambda's BODY must exist as a structured
// expression — so, right after the body is compiled to bytecode, this recorder
// re-reads the SAME lexeme span and builds the L4 query AST (ibQueryAstExpr)
// when the body fits the translatable subset:
//
//     [ '{' ] Return <expr> [ ';' ] [ '}' | EndFunction ]
//
//     expr: OR/AND/NOT (keywords or !, &&, ||), comparisons
//           (= == <> != < <= > >=), [NOT] arithmetic (+ - * / %),
//           parentheses, literals (numbers / strings / TRUE / FALSE / NULL),
//           the row parameter's member chains (x.Producer.Region -> a Column
//           path WITHOUT the parameter segment), and captured outer
//           identifiers (-> Param nodes, resolved by name at execute time
//           from the lambda's captured frames — the &parameter analogy).
//
// ANYTHING else (calls, indexing, assignments, several statements, a foreign
// object's member access, a bare row parameter) returns null — the lambda
// stays bytecode-only and the pipeline runs in RAM. The bail-out is
// deliberately conservative: a false "translatable" would produce wrong rows;
// a false "untranslatable" only costs performance.
//
// The produced AST is the SAME type the text query language parses into, so
// the pushdown lowers it through the existing L4-1 builders — no second
// lowering. Multi-character operators arrive as ADJACENT single-character
// delimiter lexemes (the script lexer splits them); adjacency is verified by
// line + column so "< =" with a space does not silently pair.
//
// See docs/query-language-arc.md §23.5 (L4-2) and §14 (the dual-compile lean).

#include "translateCode.h"   // ibLexem

#include <memory>
#include <vector>

struct ibQueryAstExpr;   // backend/query/queryAst.h — fwd only (the .cpp includes it)

// Record the body span [from, to) of a single-parameter lambda into the L4
// query AST. rowParamName = the lambda's first parameter (the row variable);
// its member chains become Column paths. Returns null when the body is
// outside the translatable subset (never throws on shape — shape is data).
// The LINQ lambda body `[ '{' ] Return expr [ ';' ] [ '}' | EndFunction ]` -> the L4 pushdown AST
// (single row parameter).
BACKEND_API std::shared_ptr<ibQueryAstExpr> ibBuildLambdaQueryAst(
	const std::vector<ibLexem>& lexems, size_t from, size_t to,
	const wxString& rowParamName);

// A `restrict` clause — a BARE `expr` span (no `Return`, no braces; the compiler emits
// `Return <span>` itself, only the span feeds the AST). rowParamName2 (optional) is a SECOND alias:
// a join-ON predicate `(s, a) => s.k <op> a.k` where the comparison operator comes from the shared
// parser (no hand-read op) and both `s.*` / `a.*` deref to Column paths.
BACKEND_API std::shared_ptr<ibQueryAstExpr> ibBuildRestrictedQueryAst(
	const std::vector<ibLexem>& lexems, size_t from, size_t to,
	const wxString& rowParamName, const wxString& rowParamName2 = wxEmptyString);

#endif

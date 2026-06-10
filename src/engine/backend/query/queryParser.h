#ifndef __QUERY_PARSER_H__
#define __QUERY_PARSER_H__

// L4-1 — text query language parser.
//
// A hand-written recursive-descent parser in the style of the script compiler
// (compiler/compileCode.cpp): it consumes the ibQueryToken stream from
// ibQueryLexer and builds the ibQuerySelect AST. No metadata is touched —
// names stay as strings; resolution is the lowering's job (queryLowering).
//
// Grammar (EN canon; keywords are locale-table driven — queryKeywords.h):
//   statement:= selectCore { UNION [ALL] selectCore } [ORDER BY orderList] [TOTALS …]
//   selectCore := SELECT [DISTINCT] selList FROM source { join }
//                 [WHERE predicate] [GROUP BY exprList [HAVING predicate]]
//   totalDim := columnPath [HIERARCHY | ELEMENTS]
//   selList  := '*' | proj { ',' proj }
//   proj     := (aggregate | expr) [ [AS] alias ]
//   aggregate:= (SUM|MIN|MAX|AVG) '(' expr ')' | COUNT '(' ('*'|expr) ')'
//   source   := dottedName [ '(' arg {',' arg} ')' ] [ [AS] alias ] | '(' statement ')' [ [AS] alias ]
//   join     := [INNER|LEFT] JOIN source [ON predicate]
//   predicate:= andExpr { OR andExpr }
//   andExpr  := notExpr { AND notExpr }
//   notExpr  := NOT notExpr | comparison
//   comparison := expr [ cmpOp expr
//                      | [NOT] LIKE expr
//                      | [NOT] IN '(' (expr {',' expr} | statement) ')'
//                      | IS [NOT] NULL
//                      | [NOT] BETWEEN expr AND expr ]
//   expr     := mulDiv { ('+'|'-') mulDiv }            (arithmetic — parsed, not yet executed)
//   mulDiv   := primary { ('*'|'/'|'%') primary }
//   primary  := columnPath | literal | param | '(' predicate ')' | aggregate | case
//   case     := CASE { WHEN predicate THEN expr } [ELSE expr] END   (parsed, not yet executed)
//
// The parser accepts the FULL grammar above; the lowering (queryLowering) realizes the subset the
// column-based L3 door executes and throws a clear "not yet executed" for the rest (arithmetic, CASE,
// UNION, IN-subquery) — keeping the surface honest while the parser stays complete.
//
// Throws ibBackendCoreException (line / position) on a syntax error.
//
// See docs/query-language-arc.md §14 / §23.

#include "queryLexer.h"
#include "queryAst.h"

class BACKEND_API ibQueryParser
{
public:
	ibQueryParser() = default;

	// Lex + parse the text into a SELECT statement AST. Throws on a lex / syntax error.
	ibQuerySelectPtr Parse(const wxString& queryText);

private:
	std::vector<ibQueryToken> m_toks;
	size_t                    m_pos = 0;

	// --- token cursor ----------------------------------------------------
	// All three clamp to the final End token — a malformed advance never reads out of bounds.
	const ibQueryToken& Cur()  const { return m_toks[m_pos < m_toks.size() ? m_pos : m_toks.size() - 1]; }
	const ibQueryToken& Peek() const { return m_toks[m_pos + 1 < m_toks.size() ? m_pos + 1 : m_toks.size() - 1]; }
	const ibQueryToken& Next()       { const ibQueryToken& t = Cur(); if (m_pos < m_toks.size()) ++m_pos; return t; }
	bool   AcceptKw(ibQueryKeyword kw);
	void   ExpectKw(ibQueryKeyword kw, const wxChar* what);
	bool   AcceptPunct(wxChar c);
	void   ExpectPunct(wxChar c, const wxChar* what);
	bool   AcceptOp(const wxChar* op);
	// Reports a syntax error: formats line / position and throws
	// ibBackendCoreException (always throws — any code after a Fail() call is
	// logically unreachable, mirroring the codebase's "Error(); return false;" idiom).
	void   Fail(const ibQueryToken& at, const wxString& msg) const;

	// --- productions -----------------------------------------------------
	ibQuerySelectPtr           ParseSelectStatement();   // a full SELECT (+ UNION branches + trailing ORDER/TOTALS)
	ibQuerySelectPtr           ParseSelectCore();        // one SELECT body up to HAVING (a UNION branch)
	void                       ParseSelectList(ibQuerySelect& sel);
	ibQueryProjection          ParseProjection();
	ibQuerySource              ParseSource();
	void                       ParseJoins(ibQuerySelect& sel);
	void                       ParseOrderBy(ibQuerySelect& sel);
	void                       ParseTotals(ibQuerySelect& sel);
	std::vector<wxString>      ParseDottedName();

	ibQueryAstExprPtr             ParsePredicate();   // OR level
	ibQueryAstExprPtr             ParseAnd();
	ibQueryAstExprPtr             ParseNot();
	ibQueryAstExprPtr             ParseComparison();
	ibQueryAstExprPtr             ParseAddSub();      // + - (lower precedence)
	ibQueryAstExprPtr             ParseMulDiv();      // * / % (higher precedence)
	ibQueryAstExprPtr             ParsePrimary();
	ibQueryAstExprPtr             ParseAggregate();   // SUM/COUNT/... ( ... )
	ibQueryAstExprPtr             ParseCase();        // CASE WHEN … THEN … [ELSE …] END
};

#endif

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
//   package  := statement { ';' statement }            (a trailing ';' is allowed)
//   statement:= DROP name
//             | selectCore { UNION [ALL] selectCore } [ORDER BY orderList] [TOTALS …] [FOR UPDATE]
//   selectCore := SELECT [ALLOWED] [TOP n] [DISTINCT] selList [INTO name] FROM source { join }
//                 [WHERE predicate] [GROUP BY exprList [HAVING predicate]]
//   totalDim := columnPath [HIERARCHY | ELEMENTS]
//   selList  := '*' | proj { ',' proj }
//   proj     := (aggregate | expr) [ [AS] alias ]
//   aggregate:= (SUM|MIN|MAX|AVG) '(' expr ')' | COUNT '(' ('*'|expr) ')'
//   source   := dottedName [ '(' arg {',' arg} ')' ] [ [AS] alias ] | '(' statement ')' [ [AS] alias ]
//   join     := [INNER|LEFT|RIGHT|FULL] [OUTER] JOIN source [ON predicate]
//                                                  (ON TRUE = cross; omitted ON = auto-join by reference)
//   predicate:= andExpr { OR andExpr }
//   andExpr  := notExpr { AND notExpr }
//   notExpr  := NOT notExpr | comparison
//   comparison := expr [ cmpOp expr
//                      | [NOT] LIKE expr
//                      | [NOT] IN '(' (expr {',' expr} | statement) ')'
//                      | IS [NOT] NULL
//                      | [NOT] BETWEEN expr AND expr ]
//   expr     := mulDiv { ('+'|'-') mulDiv }
//   mulDiv   := primary { ('*'|'/'|'%') primary }
//   primary  := columnPath | literal | param | '(' predicate ')' | aggregate | case
//   case     := CASE { WHEN predicate THEN expr } [ELSE expr] END
//
// The parser accepts the FULL grammar above and the lowering (queryLowering) EXECUTES it — arithmetic,
// CASE, UNION and IN-subquery all run. What still throws a clear "not yet executed" is the residual
// tail only (a computed expression across a JOIN's leaves, a computed column over aggregates, a
// dot-walk leaf inside a boolean WHERE over a non-co-located JOIN). See docs/query-language-arc.md §23.4.
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
	// ONE statement — text holding a package (or a bare DROP) is a syntax error here, so an
	// existing single-query caller cannot silently swallow the rest of a package.
	ibQuerySelectPtr Parse(const wxString& queryText);

	// Lex + parse a PACKAGE — one or more statements separated by ';'. A single select parses
	// to a package of one, so this is the general door: the constructor validates through it,
	// which is what keeps the check the ENGINE's rather than a second, softer opinion.
	ibQueryPackage ParsePackage(const wxString& queryText);

	// ONE expression on its own — the inverse of ibRenderQueryExpr, for a constructor's
	// "arbitrary condition" cell and anywhere else a fragment is authored by hand. Same
	// parser, same errors: a free-typed predicate is read by the engine, never by a
	// hand-rolled mini-checker beside it.
	ibQueryAstExprPtr ParseExpression(const wxString& exprText);
	// ONE FIELD OF A TOTALS LEVEL from text — `Date`, `Store HIERARCHY`, `Period PERIODS(Month, &A, &B)`.
	// For the query constructor, which edits a level's fields as text: it reads them back through the
	// LANGUAGE instead of picking out the part it recognises, so nothing a query may say is lost by
	// passing through the form.
	ibQueryTotalField ParseTotalsField(const wxString& fieldText);

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
	// Reports a syntax error: formats line / position and throws ibBackendQuerySourceException —
	// L4's own variety, carrying the token's span as data (query/queryException.h). Always throws, so
	// any code after a call is logically unreachable, mirroring the codebase's "Error(); return false;"
	// idiom. Named for what it DOES: "Fail" reads like a status the caller might inspect.
	void   ThrowQueryException(const ibQueryToken& at, const wxString& msg) const;

	// --- productions -----------------------------------------------------
	ibQueryAstStatement           ParseStatement();         // one package statement: a DROP or a full SELECT
	// A package-level LINK between two named results: `JOIN T1 AND T2 ON …`. Recognised by POSITION
	// (a statement begins with SELECT or DROP), so the language gains no new word.
	ibQueryPackageLink            ParsePackageLink();
	ibQuerySelectPtr           ParseSelectStatement();   // a full SELECT (+ UNION branches + trailing ORDER/TOTALS)
	ibQuerySelectPtr           ParseSelectCore();        // one SELECT body up to HAVING (a UNION branch)
	void                       ParseSelectList(ibQuerySelect& sel);
	ibQueryProjection          ParseProjection();
	ibQuerySource              ParseSource();
	void                       ParseJoins(ibQuerySelect& sel);
	void                       ParseOrderBy(ibQuerySelect& sel);
	void                       ParseTotals(ibQuerySelect& sel);
	ibQueryTotalField          ParseTotalField();   // one field + how it is read (unfold / PERIODS)
	// `firstMayBeKeyword` — the caller has ALREADY consumed a dot, so this path is a CONTINUATION and
	// its first segment obeys the after-a-dot rule like every later one (CAST(x AS T).Order).
	std::vector<wxString>      ParseDottedName(bool firstMayBeKeyword = false);

	ibQueryAstExprPtr             ParsePredicate();   // OR level
	ibQueryAstExprPtr             ParseAnd();
	ibQueryAstExprPtr             ParseNot();
	ibQueryAstExprPtr             ParseComparison();
	ibQueryAstExprPtr             ParseAddSub();      // + - (lower precedence)
	ibQueryAstExprPtr             ParseMulDiv();      // * / % (higher precedence)
	ibQueryAstExprPtr             ParsePrimary();
	ibQueryAstExprPtr             ParseAggregate();   // SUM/COUNT/... ( ... ) [OVER (...)]
	ibQueryAstExprPtr             ParseRanking();     // ROW_NUMBER/RANK/DENSE_RANK () OVER ( ... )
	void                          ParseWindowSuffix(ibQueryAstExpr& call);   // the optional OVER (...) after a call
	ibQueryAstExprPtr             ParseValueConstant();// VALUE( <Kind>.<Name>.<Member> ) — literal reference constant
	// CAST( <expr> AS <Kind>.<Name> ) [ . field ... ] - narrow a composite reference so the walk a
	// composite forbids becomes possible. A trailing path makes the result a COLUMN rooted on the cast.
	ibQueryAstExprPtr             ParseCast();
	ibQueryAstExprPtr             ParseCase();        // CASE WHEN … THEN … [ELSE …] END
	ibQueryAstExprPtr             ParseIsNullCall();  // ISNULL(a, b) — read as the CASE it is
};

#endif

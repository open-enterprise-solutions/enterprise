#ifndef __QUERY_RENDER_H__
#define __QUERY_RENDER_H__

////////////////////////////////////////////////////////////////////////////
// L4-1, the other direction — AST back to QUERY TEXT.
////////////////////////////////////////////////////////////////////////////
//
// The parser turns text into an ibQuerySelect; the lowering turns that into a
// plan; ibDatabaseQueryBuilder renders SQL. What was missing is the way BACK to
// the language a person writes — and it is the one that decides whether a query
// constructor is a tool or a toy.
//
// A constructor that can only GENERATE text is used once: the moment somebody
// edits the query by hand, it can no longer read it, and from then on the
// constructor is in the way. A constructor that can read an existing query,
// show it, take a change and write it back is a tool people keep using. That
// round trip is this header.
//
// TWO RULES the renderer holds to, because they are what make the trip safe:
//
//   1. WHAT IT WRITES, THE PARSER READS. Keywords come from the active keyword
//      table (ibQueryKeywordText), never from literals here — so a localized
//      table renders in the same language it parses.
//
//   2. IT NEVER INVENTS. Everything rendered is in the AST; nothing is added
//      "for readability" and nothing the AST holds is dropped. A round trip
//      that loses a clause is worse than no round trip: the loss is silent and
//      lands in somebody's report.
//
// Formatting is deliberately plain — one clause per line, no alignment. The
// text is meant to be re-parsed and diffed, and clever layout is what makes a
// diff unreadable.
//
////////////////////////////////////////////////////////////////////////////

#include "queryAst.h"

#include <wx/string.h>

// Render a parsed statement back to query text.
//
// The result parses to an equivalent AST — that is the contract, and the tests
// pin it as text -> AST -> text -> AST. Not byte-identity with the original
// source: comments, line breaks and redundant parentheses are the author's, and
// the AST does not keep them.
BACKEND_API wxString ibRenderQuery(const ibQuerySelect& select);

// A PACKAGE back to text — statements separated by ';', in the order they run. Same
// contract: what this writes, ibQueryParser::ParsePackage reads.
BACKEND_API wxString ibRenderQueryPackage(const ibQueryPackage& package);

// One expression on its own — for a constructor's filter row, an editable cell,
// or a diagnostic that wants to name the offending term.
BACKEND_API wxString ibRenderQueryExpr(const ibQueryAstExpr& expr);

// THE NAME A PROJECTION IS READ BACK BY — its alias, else the leaf of its column path; EMPTY when
// it has no natural name (an expression, a literal, an aggregate before it is aliased).
//
// One answer, in one place. It was written out three times — twice byte-for-byte — in the
// constructor, the unions field map and the lowering, and the whole language leans on it: a union
// lines its branches up by this name, a temp table's columns and index are these names, a totals
// level answers to one, and two projections may not share one. Three copies of a rule that load
// bearing is three chances to disagree.
//
// (The lowering's OutputNameFor builds ON this: where there is no natural name it invents one, which
// is a different question — "what shall we call it" rather than "what is it called".)
BACKEND_API wxString ibQueryOutputName(const ibQueryProjection& projection);

// THE NAME A SOURCE ANSWERS TO — its alias, else the LAST segment of its path. EMPTY for a nested
// table that has not been given one (it has no path to fall back on).
//
// `Catalog.Products` is where a table came FROM; in the query it is `Products`, and that is what
// every qualified field is written against, what a rename replaces, what the alias numbering
// compares against, and what a table row is labelled with. One rule, and it was written out EIGHT
// times — in the model, the render, the alias generator, and four places in the constructor.
//
// Beside ibQueryOutputName because it is the same kind of question: what is this thing CALLED, as
// opposed to what shall we call it (queryRewrite.h answers that one).
BACKEND_API wxString ibQuerySourceName(const ibQuerySource& source);

// THE SAME NAME, FOR SOMETHING THAT HAS TO SHOW ONE. A nested table with no alias answers to
// nothing — correct for the query text, and unreadable as a row in a list, where a blank line is
// the one thing that makes a table list useless. So it says what it IS.
//
// Two callers wrote this out identically on either side of the DLL boundary (the constructor model
// stamping each field with its table, the window labelling the table rows), which is exactly the
// case for putting it where both can ask: the difference between them was nothing at all.
BACKEND_API wxString ibQuerySourceLabel(const ibQuerySource& source);

// THE NAME A TOTALS LEVEL ANSWERS TO — its own alias, else the leaf of the column it groups by.
// The same shape as a projection's name and for the same reason: a level IS an output column, so
// two levels over one column (Date by month, Date by day) need two names or the second answers to
// the first's. Written out in the grid and in the lowering before this.
BACKEND_API wxString ibQueryDimensionName(const ibQueryTotalDim& dim);

// THE INVERSE OF RENDERING A COLUMN: a dotted path (`Catalog1.Owner.Code`) as the AST node a query
// carries. Six places in the constructor were splitting that string by hand, five of them with the
// identical loop — and every host that builds a query without typing it needs the same thing (the
// web front, a script that assembles a query, a report's composer).
//
// It belongs beside the render because it is the same fact read the other way, and here it can be
// used by ANY host rather than living in one dialog.
BACKEND_API ibQueryAstExprPtr ibQueryColumnFromPath(const wxString& dottedPath);

#endif

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

// One expression on its own — for a constructor's filter row, an editable cell,
// or a diagnostic that wants to name the offending term.
BACKEND_API wxString ibRenderQueryExpr(const ibQueryAstExpr& expr);

#endif

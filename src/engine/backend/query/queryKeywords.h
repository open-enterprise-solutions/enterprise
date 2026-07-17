#ifndef __QUERY_KEYWORDS_H__
#define __QUERY_KEYWORDS_H__

// L4-1 — text query language keyword set.
//
// The CANON is the English SQL-like set below; the lexer (queryLexer.{h,cpp})
// matches a word — uppercased — against the ACTIVE keyword table. A second
// locale (UK / RU) is added later by registering another table that maps the
// SAME ibQueryKeyword enum to localized spellings — the grammar never changes
// (mirrors the VES/CES dual surface that compiles to one bytecode).
//
// Kept DELIBERATELY SEPARATE from the script lexer's s_listKeyWord
// (compiler/translateCode.cpp) so query keywords (SELECT/FROM/WHERE/…) never
// collide with the script language's keywords, and the query parser is not
// subject to the script's CES-vs-VES keyword gate.
//
// See docs/query-language-arc.md §14 / §23.

#include "backend/backend.h"     // BACKEND_API
#include <wx/string.h>

// Every keyword of the L4-1 grammar. Two-word SQL phrases are split into single
// keywords (ORDER + BY, GROUP + BY) — `By` is shared. Boolean / NULL literals are
// keywords (the lexer keeps them as keyword tokens; the parser turns them into
// literal ibValues).
enum class ibQueryKeyword
{
	None = 0,

	// clauses / structure
	Select, From, As, Where, Order, By, Asc, Desc, Group, Having, Distinct,
	Top,   // SELECT TOP n — row-count limit on the SELECT core

	// hierarchical TOTALS (subtotal rows per level + grand total — door SelectTotals)
	// the dimension VID: Hierarchy (folders + items) / HierarchyOnly (folders only) / Elements (flat, default)
	Totals, Hierarchy, HierarchyOnly, Elements,

	// joins
	Join, Inner, Left, Right, Full, Outer, On,

	// set operations
	Union, All,

	// boolean / predicate operators
	And, Or, Not, In, Is, Null, Like, Between,

	// literals
	True, False,

	// CASE expression
	Case, When, Then, Else, End,

	// aggregate functions
	Sum, Count, Min, Max, Avg,

	// literal-reference constant: value(<Kind>.<Name>.<Member>) — an empty reference / a predefined item
	Value,
};

// One row of a keyword table: the enum and its (canonical / localized) spelling.
struct ibQueryKeyWordEntry
{
	ibQueryKeyword m_kw;
	const wxChar*  m_text;
};

// Look a word up in the ACTIVE keyword table. The lexer passes an UPPERCASED
// word (matching is case-insensitive, like the script lexer). Returns
// ibQueryKeyword::None when the word is an ordinary identifier.
BACKEND_API ibQueryKeyword ibFindQueryKeyword(const wxString& upperWord);

// The canonical (active-table) spelling of a keyword — for diagnostics / error
// messages. Empty for ibQueryKeyword::None.
BACKEND_API const wxString& ibQueryKeywordText(ibQueryKeyword kw);

#endif

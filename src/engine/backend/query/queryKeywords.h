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

	// SELECT ALLOWED — skip what the reader may not see instead of refusing the whole query
	Allowed,
	// package verbs: SELECT … INTO <name> materialises a temp table, DROP <name> releases it early
	Into, Drop,
	// INDEX BY … — the columns a materialised temp table is indexed by
	Index,
	// FOR UPDATE — the select HOLDS the rows it returned until the transaction ends
	For, Update,

	// hierarchical TOTALS (subtotal rows per level + grand total — door SelectTotals)
	// the dimension VID: Hierarchy (folders + items) / HierarchyOnly (folders only) / Elements (flat, default)
	// OVERALL — the level ABOVE every dimension: one row folding the whole result. Written first in
	// the BY list (`TOTALS SUM(x) BY OVERALL, Warehouse`), because that is where it sits.
	Totals, Hierarchy, HierarchyOnly, Elements, Overall,

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

	// ISNULL(<expr>, <value when null>) — the substitution, distinct from the `IS NULL` predicate
	// above. Written as itself and read as a CASE: one spelling for the common case, no new node.
	IsNull,

	// aggregate functions
	Sum, Count, Min, Max, Avg,

	// literal-reference constant: value(<Kind>.<Name>.<Member>) — an empty reference / a predefined item
	Value,

	// CAST(<expr> AS <Kind>.<Name>) — NARROW a composite reference to one of its types, which is what
	// makes the walk a composite forbids possible: `CAST(Recorder AS Document.Order).Number`.
	Cast,
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

// IS THIS KEYWORD AN AGGREGATE — one of the five calls that fold many rows into one.
//
// A property OF THE KEYWORD, so it is asked of the keyword table rather than re-spelled at each
// place that cares. It was written out twice before this: the parser matched a token against the
// five, and the lowering's AggFn mapped the same five (with a `default` that quietly answered
// COUNT — so a non-aggregate reaching it became a count instead of an error). Two spellings of one
// closed set, and neither could tell the other when the set grew.
BACKEND_API bool ibIsAggregateKeyword(ibQueryKeyword kw);

// DOES `DISTINCT` CHANGE THIS AGGREGATE'S ANSWER — is `FN(DISTINCT x)` a different question from
// `FN(x)`?
//
// It is for SUM, AVG and COUNT (duplicates carry weight in each); it is NOT for MIN and MAX, whose
// answer is the same value whether or not it appears twice. So `MIN(DISTINCT x)` is legal, harmless
// and pointless — and a window that offered it would be padding a list with a choice that changes
// nothing.
//
// ⚠ A PROPERTY OF THE FUNCTION, asked of the same table the function itself comes from — NOT a
// hand-kept list of exceptions in whichever dialog is drawing a dropdown this week. Two surfaces
// already need it (the aggregate cell's choices, the expression editor's palette) and every later
// host will need the same answer.
BACKEND_API bool ibDistinctMattersFor(ibQueryKeyword aggregate);

// EVERY WORD OF THE ACTIVE TABLE, space-separated. Written for the editor's syntax highlighting,
// which needs the whole set at once — and asked of the TABLE rather than typed out again, so a
// keyword added to the language lights up the day it is added, and a localized table lights up its
// own words rather than the English ones.
BACKEND_API wxString ibAllQueryKeywords();

#endif

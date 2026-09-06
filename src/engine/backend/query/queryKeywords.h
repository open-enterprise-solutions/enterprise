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
	// SELECT … ONTO <name> — NAME THIS STATEMENT'S FINISHED RESULT. The pair to INTO and deliberately
	// not the same thing: INTO makes a table for later statements to read, ONTO names the result a
	// reader asks for BY NAME instead of by its position in the package.
	Onto,
	// LINK <name> [INNER|LEFT|RIGHT|FULL] JOIN <name> ON <condition> [ … ] — a relation BETWEEN two
	// results this package named, written where a statement is written.
	//
	// ⭐⭐ IT IS AN OPERATOR OF THE PACKAGE, and that is what the word was missing (Max, 2026-08-27:
	// *"we have an operator that drops a temporary table — and LINK is the operator that relates"*).
	// The package has four of them and they are the whole of its vocabulary: `INTO` makes, `DROP`
	// releases, `ONTO` names, `LINK` relates. A relation that was recognised by position alone was
	// the only one of the four with nothing to call it.
	//
	// ⭐ A WORD HERE COSTS NOTHING, and that is why there is one. The link used to be recognised by
	// POSITION alone (a top-level `JOIN A AND B ON …`, since a statement can only begin with SELECT
	// or DROP) — to avoid taking a name away from every configuration that has an attribute called
	// `Link`. But at the START OF A STATEMENT no name can stand in the first place, so nothing is
	// taken: the reasoning applied to words read where a FIELD may be, and this is not one of those
	// positions (Max, 2026-08-27).
	//
	// ⭐⭐ AND THE WORD PAYS FOR ITSELF BY REMOVING A SPELLING. With `LINK` in front, the relation is
	// written the way this language writes every other relation — `A LEFT JOIN B ON …`, a chain of
	// them if there are several. The old form needed `AND` between the two names for no reason but
	// syntax (both stood after one JOIN), so a package link did not look like a join anywhere else.
	Link,
	// INDEX BY … — the columns a materialised temp table is indexed by
	Index,
	// FOR UPDATE — the select HOLDS the rows it returned until the transaction ends
	For, Update,

	// hierarchical TOTALS (subtotal rows per level + grand total — door SelectTotals)
	// the dimension VID: Hierarchy (folders + items) / HierarchyOnly (folders only) / Elements (flat, default)
	// OVERALL — the level ABOVE every dimension: one row folding the whole result. Written first in
	// the BY list (`TOTALS SUM(x) BY OVERALL, Warehouse`), because that is where it sits.
	// PERIODS(<unit>[, <from>, <to>]) — a level field read as a CALENDAR PERIOD: the values are
	// truncated to the unit, and the level is PADDED so a period nothing happened in still gets its
	// row (which is the whole point — a chart with a gap where a quiet month was is a wrong chart).
	// It sits where the unfold sits, after the field, because it says how that field is READ.
	// SPLIT — where the ladder of levels stops being ONE. The levels before it are common to
	// everything that follows; each SPLIT opens a branch that folds the SAME rows its own way, with
	// its own order of groupings and its own selection. It is not a second query: the rows are read
	// once and every branch is fed from that one walk.
	Totals, Hierarchy, HierarchyOnly, Elements, Overall, Periods, Split,

	// joins
	Join, Inner, Left, Right, Full, Outer, On,

	// set operations
	Union, All,

	// boolean / predicate operators
	And, Or, Not, In, Is, Null, Like, Between,

	// ⭐ `<expr> REFS <Kind>.<Name>` — the type TEST beside the type NARROWING (`CAST`). A word here
	// costs nothing for the same reason LINK's does: it is read where an operator stands, never
	// where a field may, and ParsePrimary's closing rule hands any keyword still standing in a value
	// position back as a name — so a configuration may still have an attribute called `Refs`.
	Refs,

	// literals
	True, False,

	// CASE expression
	Case, When, Then, Else, End,

	// ISNULL(<expr>, <value when null>) — the substitution, distinct from the `IS NULL` predicate
	// above. Written as itself and read as a CASE: one spelling for the common case, no new node.
	IsNull,

	// aggregate functions
	Sum, Count, Min, Max, Avg,

	// ⭐ WINDOWS — `<call> OVER (PARTITION BY … ORDER BY … [ROWS|RANGE])`. The same five aggregates,
	// folded over a partition of the result instead of over a group of it, plus three RANKING calls
	// that only make sense inside one.
	//
	// ⚠ THE FRAME IS TWO WORDS AND NO MORE. SQL spells the boundaries in full
	// (`ROWS BETWEEN UNBOUNDED PRECEDING AND CURRENT ROW`), and this grammar deliberately does not:
	// the engine's IR offers exactly two frames, so accepting arbitrary boundaries would be a
	// promise nothing below can keep. `ROWS` and `RANGE` each mean "from the start of the partition
	// through this row"; the difference is whether rows sharing this one's sort key count with it.
	Over, Partition, Rows, Range,

	// Ranking calls. They take NO argument and NO frame, and they are meaningless without an
	// OVER — all three facts are asked of the keyword table (ibIsRankingKeyword), never re-listed.
	RowNumber, Rank, DenseRank,

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

// ⭐⭐ THE SCALAR CALLS — A SECOND VOCABULARY, AND DELIBERATELY NOT KEYWORDS.
//
// `YEAR`, `MONTH`, `DAY`, `WEEK`, `HOUR`, `TYPE` are ordinary words. A configuration is entitled to
// an attribute called `Year`; a virtual table already spells its periodicity with the very same
// words (`Turnovers(&From, &To, Month)`), read there as plain identifiers. Putting them in the
// keyword table would take every one of those names away from every configuration at once — the
// exact cost the `LINK` note weighed and refused to pay for a word read where a FIELD may stand.
//
// So they are recognised BY POSITION instead: an identifier immediately followed by `(`. A column
// is never called, so no name is taken — `SELECT Year FROM …` still reads the attribute, and
// `SELECT YEAR(Date) …` reads the call. The same reasoning, and the same conclusion, as LINK's:
// a word costs nothing exactly where a name cannot stand.
enum class ibQueryScalarFn
{
	None = 0,

	// the calendar, taken apart — each returns a NUMBER out of a date
	Year, Quarter, Month, DayOfYear, Day, Week, WeekDay, Hour, Minute, Second,

	// the calendar, moved about — each returns a DATE (or, for DateDiff, a count of units)
	BeginOfPeriod,   // BEGINOFPERIOD(<date>, <unit>)      — the first moment of the unit holding it
	EndOfPeriod,     // ENDOFPERIOD(<date>, <unit>)        — the last moment of that same unit
	DateAdd,         // DATEADD(<date>, <unit>, <count>)   — move by whole units, calendar-aware
	DateDiff,        // DATEDIFF(<from>, <to>, <unit>)     — how many whole units between them
	DateTime,        // DATETIME(<y>,<m>,<d>[,<h>,<mi>,<s>]) — a date written as its parts

	// text
	Substring,       // SUBSTRING(<string>, <from>, <length>)

	// what a value IS, and how it READS
	ValueType,       // VALUETYPE(<expr>)            — the type of a composite value
	Type,            // TYPE(<Kind.Name>)            — a type named as a constant, to compare against
	Presentation,    // PRESENTATION(<expr>)         — what a person reads instead of the raw value
	RefPresentation, // REFPRESENTATION(<expr>)      — the reference's own presentation

	// the fold's own questions
	Grouping,        // GROUPING(<field>) — is THIS row folded over that field, or does it hold a value
	RecordAutoNumber // RECORDAUTONUMBER() — the row's number within the reading
};

// Look a word up in the scalar-call table. The caller has already established that the word stands
// where a call may (an identifier followed by `(`); None means it is an ordinary name after all.
BACKEND_API ibQueryScalarFn ibFindQueryScalarFn(const wxString& upperWord);

// The canonical spelling of a scalar call — for diagnostics and for the editor's palette. Empty for
// ibQueryScalarFn::None.
BACKEND_API const wxString& ibQueryScalarFnText(ibQueryScalarFn fn);

// HOW MANY ARGUMENTS THE CALL TAKES — asked of the table rather than re-listed at each check, for
// the same reason ibIsAggregateKeyword is: the parser validates the count, the palette writes the
// skeleton, and a third reader (the syntax helper) states it. Returns false for None.
BACKEND_API bool ibQueryScalarFnArity(ibQueryScalarFn fn, size_t& outMin, size_t& outMax);

// DOES THIS CALL NAME A PERIOD UNIT, and in WHICH argument? `BEGINOFPERIOD(x, Month)` and
// `DATEADD(x, Month, 3)` say the unit second; `DATEDIFF(a, b, Month)` says it third. The position is
// a property of the call, so it is answered here — a parser that hard-codes "argument 2" reads
// DATEDIFF wrong and does it silently.
BACKEND_API bool ibQueryScalarFnUnitArg(ibQueryScalarFn fn, size_t& outIndex);

// EVERY SCALAR CALL, space-separated — the twin of ibAllQueryKeywords, and for the same reader: the
// editor highlights what the language HAS rather than a list somebody remembered to update.
BACKEND_API wxString ibAllQueryScalarFns();

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

// IS THIS A RANKING CALL — ROW_NUMBER / RANK / DENSE_RANK?
//
// Asked of the table for the same reason the aggregate question is: three separate rules hang off
// the answer (no argument, no frame, an OVER is mandatory), and a hand-kept list of three names in
// the parser would be a fourth place to update when the set grows.
BACKEND_API bool ibIsRankingKeyword(ibQueryKeyword kw);

// EVERY WORD OF THE ACTIVE TABLE, space-separated. Written for the editor's syntax highlighting,
// which needs the whole set at once — and asked of the TABLE rather than typed out again, so a
// keyword added to the language lights up the day it is added, and a localized table lights up its
// own words rather than the English ones.
BACKEND_API wxString ibAllQueryKeywords();

#endif

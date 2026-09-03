#ifndef __QUERY_REWRITE_H__
#define __QUERY_REWRITE_H__

// L4 — optimizer rewrite pass: rule-based, pure AST -> AST, no metadata, no database.
//
// Sits between the parser and the lowering. The lowering entries (ibQueryLowering::
// Execute / ExecuteTotals) run it on the parsed tree before resolving names, so BOTH
// L4 front-ends (text now, LINQ push-down later) inherit every rule. Rules are
// SEMANTICS-PRESERVING simplifications that widen the executable subset and push more
// of the query into one server-side SELECT:
//
//   1. Negation normalization — NOT is pushed down and absorbed:
//      NOT (a = b)  ->  a <> b        (and the other comparison inversions)
//      NOT NOT p    ->  p
//      NOT (p AND q) -> NOT p OR NOT q   (De Morgan; recursed until absorbed)
//      NOT (x LIKE / IN / IS NULL / BETWEEN …) -> the node's own negated flag
//      NOT col      ->  col = FALSE   (truthy boolean column; OES attributes hold
//                                      typed empties, never SQL NULL, so this is exact)
//      Payoff: a Not-free WHERE is far more often a FLAT AND-chain (IsFlatAndWhere),
//      which rides the door's verb conditions — the path that works across JOINs and
//      the RAM stitch, where the boolean predicate tree is still restricted.
//
//   2. FROM-subquery flattening — FROM (SELECT cols FROM X WHERE p) AS s WHERE q
//      becomes FROM X WHERE q' AND p, with s's output names substituted back to the
//      inner paths (an aliased dot-walk projection re-expands into a dot-walk).
//      Conservative: the inner SELECT must be a plain projection (no aggregates /
//      GROUP BY / HAVING / DISTINCT / JOIN / UNION / TOTALS / ORDER BY) and the outer
//      a single source. Payoff: the query runs as ONE server-side SELECT instead of
//      RAM-materialising the inner through ibSubqueryQueryable.
//
// The pass deep-clones the input — the cached parse on the Query value object is
// NEVER mutated (one parse feeds many executes). (docs/query-language-arc.md §23)

#include "queryAst.h"

class BACKEND_API ibQueryRewrite
{
public:
	// Deep-clone + apply all rules (recursing into FROM/JOIN subqueries, UNION
	// branches, and IN (subquery) trees). Always returns a valid tree.
	static ibQuerySelectPtr Rewrite(const ibQuerySelect& ast);

	// THE DEEP CLONE ALONE, no rules applied. The pass already owns one (it has to: the cached
	// parse must never be mutated), and a caller that wants a COPY of a query — the constructor
	// duplicating a union branch — wants exactly that and emphatically not the rewriting. Running
	// Rewrite() for a copy would hand the author back a query rephrased behind their back.
	static ibQuerySelectPtr Clone(const ibQuerySelect& ast);

	// THE JOIN REORDER ALONE — a clone whose runs of INNER joins are ordered so every link stands on
	// tables already read, and nothing else touched. Rewrite() does this as one of its rules; this
	// door exists for the CHECK, which must judge the query that will RUN rather than the text as
	// typed. Without it the constructor reports a forward reference the execution never sees.
	// An OUTER join never moves and a cycle is left as written — see the pass for why.
	static ibQuerySelectPtr ReorderJoins(const ibQuerySelect& ast);
};

// A PREDICATE'S TOP-LEVEL `AND` CHAIN, AND ITS INVERSE. A WHERE is a LIST of conditions to whoever
// reads it — that is how the constructor shows it, how one broken link is dropped without losing
// the rest, and how the lowering decides a filter is flat enough to push down.
//
// Written out three times before this (the conditions model, the constructor, the lowering), which
// is three chances for "what counts as one condition" to drift apart.
BACKEND_API void ibQueryFlattenAnd(const ibQueryAstExprPtr& expr, std::vector<ibQueryAstExprPtr>& out);
BACKEND_API ibQueryAstExprPtr ibQueryFoldAnd(const std::vector<ibQueryAstExprPtr>& rows);

// A CONDITION RUNS WHERE THE VALUE IT NAMES EXISTS — deal the AND-terms of WHERE and HAVING back
// out by what each one needs, in place. A term that mentions an aggregate becomes a HAVING wherever
// it was written; a term over a group key becomes a WHERE, for the same reason read the other way
// (filtering on a key before the fold removes exactly the same groups — a key cannot vary inside
// its group). An `OR` across the two kinds is left whole (splitting it would change which rows
// survive).
//
// ⚠ PUBLIC BECAUSE THE WINDOW HAS TO SEE IT HAPPEN. `Rewrite` applies this on a CLONE at execution
// time, which is right for a query typed by hand and wrong for one being BUILT: the constructor
// shows the author's own AST, so the condition it had just written stayed visibly in WHERE while
// the engine quietly ran it as HAVING. A window that shows a different query from the one that runs
// teaches people to distrust it. Same rule, one spelling, applied by whoever holds the AST.
BACKEND_API void ibQuerySortConditionsByFold(ibQuerySelect& select);

// …AND THE QUESTION THAT RULE TURNS ON: does this expression FOLD — is an aggregate call anywhere
// inside it (a nested SELECT excepted, it folds its own rows)?
//
// Public because the rule above is not its only reader. The LOWERING asks the same thing of a
// PROJECTION: a computed column built from group keys (`ISNULL(Balance, 0)`) IS a group key and
// groups by itself, while one built from the fold (`ISNULL(SUM(Qty), 0)`) can only be evaluated
// after it. Different decisions, one fact about the tree — kept in one place for the reason
// ibQueryFlattenAnd's note gives.
BACKEND_API bool ibQueryMentionsAggregate(const ibQueryAstExprPtr& expr);

// ⭐⭐ "NO LINK" IS SPELLED `TRUE`, and that is why these are here rather than written out wherever
// somebody needs a boolean.
//
// A join with NO condition at all is a hole in the text: the reader cannot tell whether the author
// meant every combination of rows or simply had not finished. So the engine refuses one, and the
// product is written down — `JOIN X ON TRUE` — which says the same thing out loud and reads the
// same to the parser, the constructor and the person.
//
// The constructor SHOWS such a join as an unlinked table (no row on the Links tab) and its cell as
// empty; the text carries the word. One shape, two honest readings.
//
// (The maker — `ibQueryTrueLiteral()` — is gone: nothing built one. What writes `ON TRUE` is the
//  PARSER, from the keyword; only the READING side had a caller, and that is this one.)
BACKEND_API bool ibQueryIsTrueLiteral(const ibQueryAstExprPtr& expr);

// ===========================================================================
//  NAMING — the other half of ibQueryLowering::CheckNames
// ===========================================================================
//
// The check REFUSES a query whose output names collide or whose sources answer to one name. These
// two make sure it never has to: they are what a host calls when it ADDS something, so the query is
// legal by construction rather than legal-if-the-author-notices.
//
// Backend on purpose. Assembling a query without typing it is not a desktop feature — the web front
// does it, a script that builds a query does it, a report's composer does it, and every one of them
// has to number a duplicate the same way or two hosts will disagree about what the same click
// produced. (It is also the rule the constructor could not have kept to itself: a name generated in
// the dialog has to match the one the engine checks for, and matching by copying is how they drift.)

// WHAT SHOULD THIS COLUMN BE CALLED — the one proposal, for every host and for the runtime.
//
//   * its ALIAS when it has one — the author said so, and nothing here overrides that;
//   * a WALK, glued: `Reference.PredefinedName` is `ReferencePredefinedName`. The leaf alone is the
//     obvious answer and it is wrong for the case that matters — every step of a walk ends in the
//     same word, so `PredefinedName` and `Reference.PredefinedName` both asked to be called
//     `PredefinedName`, which the engine then refuses as duplicate output names;
//   * a plain column: its own name;
//   * empty for anything with no natural name (an expression, a literal, an aggregate) — naming
//     THOSE is a different question, and ibQueryEnsureUniqueName answers it (`Field1`, `Field2`).
//
// ⚠ A LEADING SOURCE NAME IS NEVER PART OF IT. `Catalog1.Reference.PredefinedName` is
// `ReferencePredefinedName` — the qualifier says WHICH TABLE, which is a fact about where the column
// is read from, not about what it is. Gluing it in would name the table in front of every column
// (`Catalog1Code`, `Catalog1Description`) for the sake of the rare case where two tables contribute
// the same walk — and that case is what the numbering is for.
//
// It takes the SELECT because that is where the answer lives: whether the first segment names a
// SOURCE, or is the first hop of a walk, is a fact about the query and not about the projection.
BACKEND_API wxString ibQueryProposedName(const ibQuerySelect& select, const ibQueryProjection& projection);

// EVERY OUTPUT COLUMN GETS A NAME, and no two get the same one.
//
// An expression with no natural name is called `Field1`, `Field2`, … — a column the author cannot
// refer to is a column they cannot order by, total by, or line a union branch up against. A name
// already in use is numbered the way a DBMS numbers it.
//
// The number goes on the ALIAS, because that is what an output name IS — and it is also what a
// UNION lines its branches up by, so a duplicate here would quietly break the branch map too.
//
// ⚠ `projection` is excluded from its own search: called on a projection already in the list, a
// plain scan finds itself, decides its own name is taken, and renumbers it — every time.
BACKEND_API void ibQueryEnsureUniqueName(ibQuerySelect& select, ibQueryProjection& projection);

// A NAME NO OTHER SOURCE IN THIS QUERY IS USING. `except` is the source being renamed, so its own
// current name does not count as taken — otherwise renaming a table to what it already is would
// number it. Two tables answering to one name makes every qualified field ambiguous, and the engine
// would be right to refuse the text; numbering the second is what a DBMS does.
BACKEND_API wxString ibQueryUniqueSourceAlias(const ibQuerySelect& select, const wxString& wanted,
                                              const ibQuerySource* except = nullptr);

#endif

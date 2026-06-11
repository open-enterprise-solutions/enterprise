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
};

#endif

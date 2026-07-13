#ifndef __QUERY_LOWERING_H__
#define __QUERY_LOWERING_H__

// L4 — lowering: AST + parameter values -> the L3 door (ibDataQueryBuilder),
// executed. This is the metadata layer §14 calls for: it resolves metaobject /
// attribute NAMES against activeMetaData into queryables / columns, maps AST verbs
// onto the door, runs it, and returns the selection plus an OUTPUT SCHEMA (what the
// script sees) for the result wrapper. Runs at EXECUTE time (the &parameter values
// must be known), so one parse feeds many executes.
//
// BOTH L4 front-ends lower through here: L4-1 (the text query language — the parser's
// ibQuerySelect AST via Execute / ExecuteTotals) and L4-2 (LINQ push-down — the lambda
// recorder's ibQueryAstExpr via LowerLambdaPredicate / LowerLambdaColumnPath, reusing
// the same builders; bail = empty, the fold falls back to RAM). L5 (the data composer)
// reaches it only THROUGH rendered L4-1 text — never as a third entry.
//
// The executable subset has long outgrown the MVP: full boolean WHERE (OR / IN /
// IS NULL / NOT, predicate tree), JOIN / subquery sources / UNION, register virtual
// tables with source args, dot-walk across projection / WHERE / ORDER / aggregates,
// flat GROUP BY + HAVING, hierarchical TOTALS, TOP, and the optimizer rewrite pass.
// The realized state lives in docs/query-language-arc.md §23.4 / §23.8 / §23.9 —
// grow that doc, not this list.
//
// See docs/query-language-arc.md §14 / §22 / §23.

#include "queryAst.h"
#include "dataQueryBuilder.h"   // ibDataQueryResult / ibDataQueryBuilder
#include "queryable.h"          // ibBackendQueryColumn

#include <map>
#include <memory>
#include <vector>

class ibQueryLowering
{
public:
	// One output column the script reads back. A plain / dot-walk / aggregate column
	// is read either by source column (GetValue) or by door alias (GetColumn).
	struct OutputColumn
	{
		wxString                    m_name;             // script-visible output name
		const ibBackendQueryColumn* m_col   = nullptr;  // source column (GetValue) — null when by-alias
		wxString                    m_alias;            // door alias (GetColumn) — dot-walk / aggregate / explicit
		bool                        m_byAlias = false;  // read via GetColumn(alias) vs GetValue(col)
		// Non-empty for a dot-walk leaf that is a reference / enum / composite: read via
		// GetColumnObject(m_objectPrefix, m_col), which reassembles the object from its prefixed field spread
		// (the provider projects it so). Empty => a plain scalar leaf / aggregate / column read by alias.
		wxString                    m_objectPrefix;
		// A SYNTHETIC source column (a computed TOTALS measure) the lowering built — owned HERE so it
		// outlives the door AND the result: the schema travels with the selection, and m_col points into
		// this. Null for a real metadata column (owned by the metadata) or a by-alias read.
		std::shared_ptr<ibBackendQueryColumn> m_ownedCol;
	};

	// Resolve + build + run. Fills outSchema (in projection order). Throws
	// ibBackendException on a resolution failure, a missing &parameter, or an
	// unsupported construct (with the AST source span). Returns the move-only selection.
	static ibDataQueryResult Execute(const ibQuerySelect& ast,
	                                 const std::map<wxString, ibValue>& params,
	                                 std::vector<OutputColumn>& outSchema);

	// The PAGED read — the same lowering with an external page ENVELOPE (anchor /
	// direction / count) threaded into the door's terminal. This is how a paged
	// consumer (the L5 list driver) cursors a query WITHOUT any grammar change; a
	// `TOP n` in the text still caps the page (the smaller count wins). The envelope
	// applies to the plain SELECT path only — an aggregate / UNION read ignores it
	// (no row cursor to anchor).
	static ibDataQueryResult Execute(const ibQuerySelect& ast,
	                                 const std::map<wxString, ibValue>& params,
	                                 std::vector<OutputColumn>& outSchema,
	                                 const ibReadPageRequest& page);

	// The paged read WITH the build-once page cache (Lever 1, docs §20): the door
	// reuses the rendered SQL keyed by `signature`, rebinding only the anchor.
	// The caller owns the cache and the signature (it must capture every
	// SQL-determining input — the L5 composer signs its rendered text + params).
	static ibDataQueryResult Execute(const ibQuerySelect& ast,
	                                 const std::map<wxString, ibValue>& params,
	                                 std::vector<OutputColumn>& outSchema,
	                                 const ibReadPageRequest& page,
	                                 ibRenderedPageCache& cache, const wxString& signature);

private:
	static ibDataQueryResult ExecuteImpl(const ibQuerySelect& ast,
	                                     const std::map<wxString, ibValue>& params,
	                                     std::vector<OutputColumn>& outSchema,
	                                     const ibReadPageRequest& page,
	                                     ibRenderedPageCache* cache, const wxString& signature);

public:

	// Hierarchical TOTALS read — builds the door's Totals()/TotalBy()/aggregates and runs ONE read,
	// returning the result with the totals config STAMPED (the runtime's QueryResult.Select() folds it
	// into a grouped selection — ByGroupsHierarchy — no second query). Fills outSchema (dimension +
	// aggregate columns, in order) for Field / property lookup. Single source. (§22.1b)
	static ibDataQueryResult ExecuteTotals(const ibQuerySelect& ast,
	                                       const std::map<wxString, ibValue>& params,
	                                       std::vector<OutputColumn>& outSchema);

	// === L4-2 (LINQ pushdown) — recorded-lambda lowering against ONE source ===
	// The lambda recorder (compiler/lambdaQueryAst.*) emits the same
	// ibQueryAstExpr the text parser does; these wrap the file-local builders so
	// the Queryable fold reuses them verbatim. `captured` maps the lambda's
	// captured outer locals (Param nodes) to their values — the &parameter
	// analogy. Both return EMPTY (null / {}) instead of throwing on anything
	// untranslatable: the fold then falls back to RAM (bail-out, not an error).
	static ibQueryPredicatePtr LowerLambdaPredicate(const ibBackendQueryable* source,
	                                                const ibQueryAstExpr& expr,
	                                                const std::map<wxString, ibValue>& captured);
	// A pure column / dot-walk path lambda body (OrderBy / aggregate selectors).
	static std::vector<const ibBackendQueryColumn*> LowerLambdaColumnPath(
	                                                const ibBackendQueryable* source,
	                                                const ibQueryAstExpr& expr);
	// A computed (arithmetic / CASE) projection lambda body (x => x.A * 2, x => IIF(c, a, b)) —
	// lowered to a server-side output-column expr, read back by its alias. Returns null for a plain
	// column / dot-walk path / structure / chained projection (the caller handles those / falls to RAM).
	static ibQueryColumnExprPtr LowerLambdaColumnExpr(
	                                                const ibBackendQueryable* source,
	                                                const ibQueryAstExpr& expr,
	                                                const std::map<wxString, ibValue>& captured);
};

#endif

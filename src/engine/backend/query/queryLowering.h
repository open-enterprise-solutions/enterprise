#ifndef __QUERY_LOWERING_H__
#define __QUERY_LOWERING_H__

// L4-1 — lowering: parsed AST + parameter values -> the L3 door (ibDataQueryBuilder),
// executed. This is the metadata layer §14 calls for: it resolves metaobject /
// attribute NAMES against activeMetaData into queryables / columns, maps AST verbs
// onto the door, runs it, and returns the selection plus an OUTPUT SCHEMA (what the
// script sees) for the result wrapper. Runs at EXECUTE time (the &parameter values
// must be known), so one parse feeds many executes.
//
// MVP subset (the L3 door's current capability): single source (Catalog/Document/
// Register records), projected columns + reference dot-walk (SelectPath), WHERE as an
// AND-chain of comparisons / LIKE / BETWEEN, ORDER BY, DISTINCT, and flat GROUP BY
// aggregates. Constructs the door supports but the lowering does NOT yet map throw a
// clear "not yet supported" with the source span: OR / IN / IS NULL / NOT in WHERE,
// JOIN, virtual tables (.Balance/…), dot-walk in WHERE/ORDER, and hierarchical TOTALS
// EXECUTION (the tree result wrapper is the next sub-step). As the door grows, this
// grows with it — the parser already accepts the full grammar.
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

	// Hierarchical TOTALS read — builds the door's Totals()/TotalBy()/aggregates and runs ONE read,
	// returning the result with the totals config STAMPED (the runtime's QueryResult.Select() folds it
	// into a grouped selection — ByGroupsHierarchy — no second query). Fills outSchema (dimension +
	// aggregate columns, in order) for Field / property lookup. Single source. (§22.1b)
	static ibDataQueryResult ExecuteTotals(const ibQuerySelect& ast,
	                                       const std::map<wxString, ibValue>& params,
	                                       std::vector<OutputColumn>& outSchema);
};

#endif

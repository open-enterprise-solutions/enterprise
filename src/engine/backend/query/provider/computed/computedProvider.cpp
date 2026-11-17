////////////////////////////////////////////////////////////////////////////
//	Description : the computed (RAM) provider — rows computed in memory, read with the composer's RAM
//	              engine (queryProviderInternal.h). What only metadata answers is the interface's
//	              default. See docs/private/query-language-arc.md §22.
////////////////////////////////////////////////////////////////////////////

#include "computedProvider.h"
#include "backend/query/provider/queryProviderInternal.h"    // ibProvider — the RAM engine it computes with
#include "backend/query/provider/queryComposer.h"            // ibQueryComposer::FilterRows — a WHERE tree over the rows in memory
#include "backend/query/columnLayout.h"                      // ibSqlAliasOf — what the statement calls an output

#include <algorithm>                                          // stable_sort, rotate — ORDER BY over the rows, a page from its anchor
#include <unordered_set>                                      // DISTINCT — keyed by a sequence of values (ibValueSeqHash)

using namespace ibProvider;

// ibComputedProvider — the RAM-computed virtual table (register slice / balance /
// turnover / subquery). Stateless: computes the rows from the spec, no physical scan, no
// L2. The flat conditions push into ComputeRows; the boolean predicate TREE, the ORDER BY
// and the page row limit (TOP) apply on the materialised RAM rows HERE — without this they
// would silently drop on a computed source.
ibDataQueryResult ibComputedProvider::ExecuteRead(const ibDataQuerySpec& given, const ibReadPageRequest& req)
{
	// Every row here is filtered in memory, so a hierarchy named in the conditions is resolved first.
	const ibDataQuerySpec spec = WithSubtreesResolved(given, { given.m_queryable });

	// ComputeRows + dot-walk resolution + dot-walk WHERE: plain conditions push into ComputeRows, a dot-walk
	// Ref.Field condition joins the reference leaf and filters in RAM. `cols` receives the source columns +
	// every joined leaf, so the DISTINCT / sort / limit rebuilds below keep them.
	std::vector<std::shared_ptr<const ibBackendQueryColumn>> cols;
	ibJournalStopwatch compute;
	compute.Resume();
	ibQueryRamTable rows = ComputeRowsResolved(spec, cols);
	compute.Pause();

	// ⭐ A COMPUTED SOURCE IS RAM BY NATURE — a register's slice / balance / turnover, a nested query,
	// a temp we filled. Journalled all the same, and for the same reason as the folds: what a reader
	// needs is not that this source is unusual, but how many rows it brought home before anything
	// above it began filtering.
	ibJournalInfo(ibT("query.road"), ibT("RAM: computed source '%s' read in memory - %ld rows; compute %lld ms"),
	              spec.m_queryable != nullptr ? spec.m_queryable->GetQueryName() : ibString(ibT("<none>")),
	              rows.RowCount(), compute.Ms());
	// …and what this read does to them afterwards, stage by stage — see the line before each return.
	ibJournalStopwatch exprs, filter, distinct, sort, rebuild;
	exprs.Resume();

	// COMPUTED output columns (SELECT Qty * Price, a CASE) — evaluate the expression per row and add it as a
	// column NAMED by its alias (the RAM analog of the DB provider projecting `expr AS alias`). Added to `cols`
	// so the sort / limit rebuild keeps it; read back by GetColumn(alias), resolved by name.
	std::vector<std::shared_ptr<const ibComputedExprColumn>> exprCols;
	if (!spec.m_selectExprs.empty()) {
		exprCols.reserve(spec.m_selectExprs.size());
		// ⭐⭐ …UNLESS A MEASURE ALREADY READS IT THROUGH A HANDLE OF ITS OWN. A computed measure, a
		// second aggregate over one column, an in-place `SUM(CASE …)` — the lowering projects the
		// expression and folds it through a handle named by that projection in the statement's
		// spelling (ProjectionHandle, queryLowering.cpp). The SQL road reads the handle by that name;
		// here the fold reads it by its ID, and a column keyed by its place in this list is not that
		// id: the measure found nothing and every figure came out 0 (a receivables report over a
		// nested query, 2026-09-11). So the expression is keyed by the handle that will ask for it.
		const auto handleFor = [&spec](const ibString& alias) -> ibMetaID {
			const ibString spelled = ibSqlAliasOf(alias);
			for (const std::vector<ibAggregateItem>* list : { &spec.m_aggregates, &spec.m_totalAggregates })
				for (const ibAggregateItem& a : *list)
					if (a.m_col != nullptr && ibBackendQueryColumn::IsSyntheticId(a.m_col->GetColumnId())
					    && a.m_col->GetPhysicalName() == spelled)
						return a.m_col->GetColumnId();
			return 0;
		};
		for (size_t k = 0; k < spec.m_selectExprs.size(); ++k) {
			const ibString& alias = spec.m_selectExprs[k].m_alias;
			const ibMetaID handle = handleFor(alias);
			exprCols.push_back(std::make_shared<ibComputedExprColumn>(alias,
				handle != 0 ? handle : ibSynthSlotId(ibSynthKind::Stitch, k)));   // its place in this list IS its number
		}
		// ⭐⭐ AND THE WINDOWS ARE FOLDED FIRST, over the whole table, exactly as the stitch does it.
		// A window has no value on one row — it is a fold over that row's PARTITION — so an evaluator
		// handed a row and nothing else cannot answer it. The rows are all in hand here, which is what
		// the fold needs; the projection loop below then only READS the answer for its row.
		//
		// 🛑 This road did not fold them, and it is the road a single computed source takes — a read
		// of a temp table, a register's slice. `SUM(x) OVER (PARTITION BY c ORDER BY p)` over a temp
		// table therefore came back EMPTY on every row, while the same window over the register
		// itself answered correctly (measured 2026-09-06). One fold, written once, standing on one of
		// the two roads that need it.
		ibRamWindowValues windows;
		for (const ibQueryColumnSelect& sc : spec.m_selectExprs)
			RamComputeWindows(sc.m_expr, rows, windows);
		for (size_t k = 0; k < exprCols.size(); ++k) {
			rows.AddColumn(exprCols[k]->GetColumnId(), exprCols[k]->GetName(), exprCols[k]->GetTypeDesc());
			for (long i = 0; i < rows.RowCount(); ++i) {
				windows.m_row = i;   // which row the folded windows are being read for
				rows.SetCell(i, exprCols[k]->GetColumnId(),
				             EvalColumnExprRow(spec.m_selectExprs[k].m_expr, rows, i, &windows));
			}
			cols.push_back(exprCols[k]);
		}
	}

	// A DOT-WALK TOTALS DIMENSION IS PUBLISHED UNDER ITS OWN ALIAS — the RAM twin of the DB
	// provider's `<leaf fields> AS dim0…`. The walk above brought the leaf in keyed by its own id,
	// which is the very thing that cannot identify the level (a self-reference walks back to the
	// SAME column). The reader asks for the alias — Column(prefix, col) falls back to Column(prefix) on a
	// RAM source — so the value is copied into a column NAMED for the dimension.
	std::vector<std::shared_ptr<const ibComputedExprColumn>> dimCols;
	if (!spec.m_dimWalks.empty()) {
		dimCols.reserve(spec.m_dimWalks.size());
		// They share one table with the computed columns above, so they continue the SAME count —
		// which is also why nothing here has to know a "band": the kind already separates them from
		// every other minted id, and the slot separates them from each other.
		for (size_t k = 0; k < spec.m_dimWalks.size(); ++k)
			dimCols.push_back(std::make_shared<ibComputedExprColumn>(spec.m_dimWalks[k].m_alias,
				ibSynthSlotId(ibSynthKind::Stitch, exprCols.size() + k)));
		for (size_t k = 0; k < dimCols.size(); ++k) {
			std::shared_ptr<const ibBackendQueryColumn> leaf = spec.m_dimWalks[k].m_path.empty()
				? nullptr : spec.m_dimWalks[k].m_path.back();
			if (leaf == nullptr)
				continue;
			rows.AddColumn(dimCols[k]->GetColumnId(), dimCols[k]->GetName(), leaf->GetTypeDesc());
			for (long i = 0; i < rows.RowCount(); ++i)
				rows.SetCell(i, dimCols[k]->GetColumnId(), rows.GetCell(i, leaf->GetColumnId()));
			cols.push_back(dimCols[k]);
		}
	}
	exprs.Pause();

	filter.Resume();
	if (spec.m_predicate)
		rows = ibQueryComposer::FilterRows(rows, spec.m_predicate);
	// ⭐ …AND ONE LEVEL OF A TREE, as the database provider reads it (the page's parent scope): a folder's children
	// when a folder is browsed, the rows with no parent at the top. Not taken, a list of a query read in memory
	// answered every folder with the whole list — the folder in it, opened in itself without end (2026-10-29).
	if (req.m_hierarchyFilter && !req.m_flatScan && req.m_hierarchyCol != nullptr && spec.m_queryable != nullptr) {
		const std::shared_ptr<const ibBackendQueryColumn> parentCol = spec.m_queryable->ContainColumn(req.m_hierarchyCol)
			? req.m_hierarchyCol : spec.m_queryable->ResolveColumnByName(req.m_hierarchyCol->GetName());
		if (parentCol != nullptr) {
			ibQueryRamTable level = RamTableOf(cols);
			for (long i = 0; i < rows.RowCount(); ++i) {
				const ibValue parent = rows.GetCell(i, parentCol->GetColumnId());
				if (req.m_isTopLevel ? (parent.IsNull() || parent.IsEmpty()) : ibValueEqual()(parent, req.m_hierarchyKey))
					level.AppendRowFrom(rows, i);
			}
			rows = std::move(level);
		}
	}
	filter.Pause();

	// The stages after the read, said once whichever return this read leaves by.
	auto stages = [&](const ibChar* how) {
		ibJournalInfo(ibT("query.road"), ibT("RAM: computed source '%s' %s - exprs %lld ms, filter %lld ms, ")
			ibT("distinct %lld ms, sort %lld ms, rebuild %lld ms"),
			spec.m_queryable != nullptr ? spec.m_queryable->GetQueryName() : ibString(ibT("<none>")), how,
			exprs.Ms(), filter.Ms(), distinct.Ms(), sort.Ms(), rebuild.Ms());
	};

	// SELECT DISTINCT over a computed source (subquery / slice) -- dedup by the output columns while keeping
	// ALL columns (the sort below may key on one not in the select list). First occurrence wins.
	//
	// ⭐ THE OUTPUTS ARE WHAT Distinct() WAS TOLD — the author's fields, a walk's leaf among them — and the computed
	// ones, as the database provider projects them under DISTINCT. The select list alone missed the walks: a walk
	// is no select column, so `SELECT DISTINCT T.Item.Parent` over a source in memory kept every row (2026-09-29).
	distinct.Resume();
	std::vector<ibMetaID> distinctBy;
	if (spec.m_distinct && !spec.m_distinctBy.empty()) {
		for (const std::shared_ptr<const ibBackendQueryColumn>& c : spec.m_distinctBy)
			if (c != nullptr)
				distinctBy.push_back(c->GetColumnId());
		for (const std::shared_ptr<const ibComputedExprColumn>& e : exprCols)
			distinctBy.push_back(e->GetColumnId());
	}
	else if (spec.m_distinct)
		for (const auto& sc : spec.m_selectCols)
			distinctBy.push_back(sc.first->GetColumnId());
	if (!distinctBy.empty()) {
		ibQueryRamTable deduped = RamTableOf(cols);
		// A row identity is the SEQUENCE of its cells — see ibValueSeqHash (value.h).
		// It used to be those cells folded into one string through GetHashKey and
		// joined with \x1f: a text conversion per cell per row, then a tree of
		// strings compared character by character.
		std::unordered_set<std::vector<ibValue>, ibValueSeqHash, ibValueSeqEqual> seen;
		for (long i = 0; i < rows.RowCount(); ++i) {
			std::vector<ibValue> key;
			key.reserve(distinctBy.size());
			for (const ibMetaID id : distinctBy)
				key.push_back(rows.GetCell(i, id));
			if (!seen.insert(std::move(key)).second) continue;
			AppendRowByCols(rows, i, deduped, cols);
		}
		rows = std::move(deduped);
	}
	distinct.Pause();

	// ⭐ A PAGE IS READ FROM ITS ANCHOR, as the database provider keysets it: in the cursor's total order (EffectiveSort —
	// the sort and the key after it, the order the anchor's values come in), turned round for a page read backwards
	// (m_reverseSort), and only the rows past the anchor. Not taken, every page of a list read in memory was the whole
	// list: the rows read ahead of a page came again before it, turned round (2026-10-29).
	std::vector<ibQuerySortItem> sorting = spec.m_sorts;
	if (req.m_hasAnchor && spec.m_queryable != nullptr)
		sorting = ibDataQueryBuilder::EffectiveSort(spec.m_queryable, sorting);

	if (!sorting.empty()) {
		sort.Resume();
		// ⭐ THE KEYS ARE TAKEN OUT OF THE ROWS ONCE, not once per comparison. The comparator used to ask
		// the table for both cells of every key on every call — a lookup in the row's map and a COPY of
		// the value each time — and a sort makes n·log n calls: the payroll sheet's 126 thousand rows by
		// three keys did that some thirteen million times. Read into one flat array first, the comparator
		// only looks at what is already there.
		std::vector<const ibQuerySortItem*> by;
		std::vector<size_t> anchorAt;   // where each key's anchor value stands — the anchor has one per sort item
		for (size_t i = 0; i < sorting.size(); ++i)
			if (sorting[i].m_col != nullptr || sorting[i].m_expr != nullptr) {
				by.push_back(&sorting[i]);
				anchorAt.push_back(i);
			}
		const size_t width = by.size();
		std::vector<ibValue> keys(static_cast<size_t>(rows.RowCount()) * width);
		// A key that is an expression (ORDER BY CASE …, a sum of two fields) is computed for its row, as the
		// statement computes it — over the rows of a source whose walks brought it here, as over any other.
		for (long i = 0; i < rows.RowCount(); ++i)
			for (size_t k = 0; k < width; ++k)
				keys[static_cast<size_t>(i) * width + k] = by[k]->m_expr != nullptr
					? EvalColumnExprRow(by[k]->m_expr, rows, i)
					: rows.GetCell(i, by[k]->m_col->GetColumnId());
		std::vector<long> order(static_cast<size_t>(rows.RowCount()));
		for (long i = 0; i < rows.RowCount(); ++i) order[static_cast<size_t>(i)] = i;
		std::stable_sort(order.begin(), order.end(), [&](long a, long b) {
			const ibValue* const ka = keys.data() + static_cast<size_t>(a) * width;
			const ibValue* const kb = keys.data() + static_cast<size_t>(b) * width;
			for (size_t k = 0; k < width; ++k) {
				const int c = RamSortCompareKey(ka[k], kb[k], by[k]->m_ascending != req.m_reverseSort);
				if (c != 0) return c < 0;
			}
			return false;
		});
		// …and the page starts at the first row past the anchor — turned to the front, the rows before it after the page.
		size_t from = 0;
		if (req.m_hasAnchor && width != 0) {
			while (from < order.size()) {
				const ibValue* const key = keys.data() + static_cast<size_t>(order[from]) * width;
				int c = 0;
				for (size_t k = 0; k < width && c == 0; ++k)
					if (anchorAt[k] < req.m_anchorSortValues.size())
						c = RamSortCompareKey(key[k], req.m_anchorSortValues[anchorAt[k]],
							by[k]->m_ascending != req.m_reverseSort);
				if (c > 0)
					break;
				++from;
			}
			std::rotate(order.begin(), order.begin() + static_cast<std::ptrdiff_t>(from), order.end());
		}
		sort.Pause();
		rebuild.Resume();
		// ⚠ THE ROWS ARE PUT IN THE NEW ORDER WHERE THEY STAND, NOT COPIED — `rows` is not read again, and
		// a copy was every cell of the list made and freed once more (1.6 s of the payroll sheet, 2026-09-12,
		// Debug). Reordered in place (ReorderRows) and handed to the result whole.
		ibQueryRamTable sorted = RamTableOf(cols);
		const long past = rows.RowCount() - static_cast<long>(from);
		const long limit = (req.m_count > 0 && req.m_count < past) ? req.m_count : past;
		rows.ReorderRows(order, limit);
		sorted.AppendRowsFrom(std::move(rows));
		rebuild.Pause();
		stages(ibT("sorted"));
		return ibDataQueryResult(std::move(sorted), spec.m_queryable);
	}

	if (req.m_count > 0 && req.m_count < rows.RowCount()) {
		rebuild.Resume();
		ibQueryRamTable limited = RamTableOf(cols);
		for (long i = 0; i < req.m_count; ++i)
			limited.AppendRowFrom(rows, i);   // moved, as the sort's rebuild above
		rebuild.Pause();
		stages(ibT("limited"));
		return ibDataQueryResult(std::move(limited), spec.m_queryable);
	}

	stages(ibT("as read"));
	return ibDataQueryResult(std::move(rows), spec.m_queryable);
}

// The shared stateless computed (RAM) provider — vended by ibComputedRegisterQueryable (in
// queryable.h, which may not name the concrete ibComputedProvider) and the temp / subquery
// computed sources. Declared in queryable.h, defined here where ibComputedProvider is complete.
ibBackendQueryProvider& ibComputedProviderInstance()
{
	static ibComputedProvider s_computedProvider;
	return s_computedProvider;
}

// Aggregated read over a COMPUTED source (a register slice / a subquery): compute the rows
// (door conditions pushed as compute filters), then the RAM GROUP-BY fold. Without this
// override the base default would return the RAW rows — silently dropping the aggregation.
// HAVING is not folded on the RAM path (the lowering gates it).
ibDataQueryResult ibComputedProvider::ExecuteAggregate(const ibDataQuerySpec& given)
{
	const ibDataQuerySpec spec = WithSubtreesResolved(given, { given.m_queryable });   // a named hierarchy, resolved as in ExecuteRead

	// ComputeRows + dot-walk resolution + dot-walk WHERE: GROUP BY Ref.Field, SUM(Ref.Field) and WHERE
	// Ref.Field = X over the computed source all resolve the reference leaves in RAM before the fold.
	std::vector<std::shared_ptr<const ibBackendQueryColumn>> cols;
	ibQueryRamTable rows = ComputeRowsResolved(spec, cols);
	if (spec.m_predicate)   // boolean WHERE (OR / NOT / IS NULL) — filter before the fold (the register can't)
		rows = ibQueryComposer::FilterRows(rows, spec.m_predicate);
	ibJournalInfo(ibT("query.road"), ibT("RAM: aggregate folded in memory over '%s' - %ld rows read"),
	              spec.m_queryable != nullptr ? spec.m_queryable->GetQueryName() : ibString(ibT("<none>")),
	              rows.RowCount());
	return RamAggregate(rows, spec);
}

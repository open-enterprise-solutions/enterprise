////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : ibDataRamComposer — the in-place RAM composition engine (L5-2)
////////////////////////////////////////////////////////////////////////////
//
// The RAM realisation of ibDataComposer, defined next to its declaration (ramComposer.h). It reads the RAM
// VALUE-STORAGE's live nodes DIRECTLY — never a query text / parse / lowering / queryable / ComputeRows copy —
// and produces the display ORDER (filtered + stable multi-key sorted storage indices). Grouping + the windowing
// of this order into a page live on the model side (ibValueModelStorage::RunComposerPage, modelRam.cpp); the
// per-row field resolution (incl. dot-walk over references) lives on the storage (ibRamValueStorage::SplitField
// / ResolveField). slice: filter + sort, flat order; dot-walk up to references.

#include "backend/composition/ramComposer.h"   // ibDataRamComposer
#include "backend/tabularModel.h"           // ibRamValueStorage — RowCount / SplitField / ResolveField
#include "backend/composition/drivers/compositionDriver.h"   // ibCompositionDriver — a composition is printed through
#include "backend/query/queryConstructorModel.h"            // WalkFrom — the hops of a path, a reference by its type
#include "backend/query/queryRender.h"                      // ibQueryColumnFromPath — a dotted path becomes its hops, once

// ⚠ NAMED, NOT INHERITED — MSVC hands these over transitively and GCC / Clang do not.
#include <algorithm>    // std::find / std::distance — the grouping paths are looked up by value
#include <deque>        // the printed list's conditional appearance, one per rung
#include <functional>   // std::function — the per-level walk is recursive

// The DISPLAY-order source: filter + stable multi-key sort the storage's rows → their STORAGE indices in display
// order (index i ↔ storage node i). A field path is split into a HEAD storage column + a dotted TAIL walked over
// references per row (ibRamValueStorage::ResolveField). A path whose head is not a storage column is skipped: an
// unevaluable filter passes (never hides a row), an unevaluable sort drops out.
//
// ⭐ THE FILTER TREE IS READ BY THE ONE ENGINE (ibCompositionFilterHolds) — the one a DB list's walk hides its
// headings by and a conditional appearance marks cells by. A DB source hands the condition to the server; RAM
// has the rows right here, so the same description is read against them. Same tree, same meaning, same reader.
//
// 🛑 IT HAD A READER OF ITS OWN: the filter built into a query AST — every value registered as a parameter on
// every pass — and walked by a second evaluator, two-valued, with no «in» and no «in hierarchy», so a NOT over
// a line it could not read hid the row. The same setting filtered a table and a list differently (2026-09-30).

std::vector<long> ibDataRamComposer::ComputeOrder() const
{
	if (m_storage == nullptr)
		return {};
	DropMemos();   // a new order reads the options — and the catalogs' trees — as they stand now

	// ⭐⭐ THE USER'S SETTING IS READ HERE TOO, and that is what makes it ONE construction (Max,
	// 2026-08-23: "saved settings apply to the RAM table as well"). A value table, a tabular section
	// and a record set are filtered and ordered by the same setting a list is — nothing about a
	// saved setting is about where the rows came from, and a section only the DB side honoured would
	// have been a setting that silently did nothing on half the models.
	// …AND IT IS THE CURRENT SETTING, not the user's section alone — a RAM table honours what its
	// author declared exactly as a list does.
	const ibSettingsDescription settings = GetCurrentSettingsDesc();

	struct RamFilter { ibMetaID m_col; std::vector<wxString> m_tail; wxString m_op; ibValue m_value; };
	std::vector<RamFilter> filters;
	// WHAT THIS READ IS SCOPED TO — the engine's own, ANDed with the setting below. Never the
	// reader's filter: that is a tree, and it comes through `settings.m_filter`.
	for (size_t i = 0; i < ScopeCount(); ++i) {
		wxString path, op; ibValue value;
		if (!GetScopeAt(i, path, op, value)) continue;
		ibMetaID col; std::vector<wxString> tail;
		if (!m_storage->SplitField(path, col, tail)) continue;
		filters.push_back({ col, std::move(tail), op, value });
	}
	struct RamSort { ibMetaID m_col; std::vector<wxString> m_tail; bool m_ascending; };
	std::vector<RamSort> sorts;

	// THE ORDER IN FORCE, and there is only one place it can come from. This had a SECOND road under
	// it — a flat sort store reached when neither section said anything — and everything the
	// imperative `Sort()` wrote went there, so a RAM table with a sort setting ignored being sorted
	// (2026-08-24). One store now: `Sort()` writes the reader's section, which is what this reads.
	for (const ibSortLineDescription& line : settings.m_sort.m_lines) {
		ibMetaID col; std::vector<wxString> tail;
		if (line.m_path.IsEmpty() || !m_storage->SplitField(line.m_path, col, tail)) continue;
		sorts.push_back({ col, std::move(tail), line.m_ascending });
	}

	// THE FILTER IN FORCE — the tree, read by the one engine (see the note above the function). The USER's filter
	// replaces the declared one, exactly as the sort above does; the scope conditions are separate and AND with it,
	// as they do in the rendered query. Each field it names is split ONCE — a switched-off line's too
	// (AppendFilterFields) — and read once per row into a slot that stays where it is while the condition reads it.
	const ibFilterDescription& filter = settings.m_filter;
	std::vector<wxString> filterPaths;
	for (const ibFilterNodeDescription& node : filter.m_nodes)
		AppendFilterFields(filterPaths, node);
	struct RamField { ibMetaID m_col = wxNOT_FOUND; std::vector<wxString> m_tail; bool m_resolved = false; };
	std::vector<RamField> filterFields(filterPaths.size());
	for (size_t i = 0; i < filterPaths.size(); ++i)
		filterFields[i].m_resolved = m_storage->SplitField(filterPaths[i], filterFields[i].m_col, filterFields[i].m_tail);
	std::vector<ibValue> filterCells(filterPaths.size());
	const ibCompositionValueOf valueOf = [&](const wxString& path) -> const ibValue* {
		for (size_t i = 0; i < filterPaths.size(); ++i)
			if (filterFields[i].m_resolved && filterPaths[i].IsSameAs(path, false))
				return &filterCells[i];
		return nullptr;   // no column of this storage — unknown, and the row is kept
	};
	const ibCompositionSubtreeOf subtreeOf = SubtreeOf();

	// One pass over the rows: evaluate the filters, and stash the sort keys (so the sort reads each row's key
	// ONCE up front, not per comparison). Reads cells straight off the storage's nodes (dot-tail hops references).
	const long n = m_storage->RowCount();
	struct Row { long m_idx; std::vector<ibValue> m_sortKeys; };
	std::vector<Row> rows;
	rows.reserve(static_cast<size_t>(n));
	for (long r = 0; r < n; ++r) {
		bool pass = true;

		// THE TREE, when there is one — evaluated first because it is the whole condition, not one line of it. A
		// condition this row cannot answer keeps it: hiding a row for a reason nobody can see is the one outcome a
		// user cannot debug.
		if (filter.IsOk()) {
			for (size_t i = 0; i < filterFields.size(); ++i)
				if (filterFields[i].m_resolved)
					filterCells[i] = m_storage->ResolveField(r, filterFields[i].m_col, filterFields[i].m_tail);
			if (!ibCompositionFilterHolds(filter, valueOf, subtreeOf, /*whenUnknown*/ true))
				continue;
		}

		for (const RamFilter& f : filters) {
			// ONE SPELLING OF THE OPERATORS, shared with everything else that reads a filter line
			// (ibCompositionCompare, dataComposer.h). It used to be written out here as well, and
			// two copies of "what does <= mean" is exactly the kind of pair that drifts.
			const ibValue cell = m_storage->ResolveField(r, f.m_col, f.m_tail);
			if (!ibCompositionCompare(cell, f.m_op, f.m_value)) { pass = false; break; }
		}
		if (!pass)
			continue;
		Row row;
		row.m_idx = r;
		row.m_sortKeys.reserve(sorts.size());
		for (const RamSort& s : sorts)
			row.m_sortKeys.push_back(m_storage->ResolveField(r, s.m_col, s.m_tail));
		rows.push_back(std::move(row));
	}

	if (!sorts.empty()) {
		std::stable_sort(rows.begin(), rows.end(), [&](const Row& a, const Row& b) {
			for (size_t i = 0; i < sorts.size(); ++i) {
				const int c = a.m_sortKeys[i].CompareValueLS(b.m_sortKeys[i]);
				if (c != 0) return sorts[i].m_ascending ? (c < 0) : (c > 0);
			}
			return false;   // equal on all keys — stable_sort keeps the storage order
		});
	}

	std::vector<long> order;
	order.reserve(rows.size());
	for (const Row& row : rows)
		order.push_back(row.m_idx);
	return order;
}

// ⭐ WHAT THIS BASE DOES NOT USE IS NOT SHOWN — the question the DB side asks its source (see
// ibDataComposer::IsAvailable), asked of the storage the same way: the first hop is the storage's column (a
// storage names its columns singly), the rest are hops by TYPE through the query model's walk, one hidden hop
// hiding the walk. A value table belongs to no configuration, so the door never walks for one. A name the
// storage does not have is not this question. …And the leaf's TYPE comes off the same walk (SubtreeAt asks it).
ibQueryConstructorField ibDataRamComposer::WalkPath(const std::vector<wxString>& hops) const
{
	const ibValueModel::ibValueModelColumnCollection* columns = m_storage != nullptr ? m_storage->Columns() : nullptr;
	if (columns == nullptr || hops.empty())
		return ibQueryConstructorField();

	const ibValueModel::ibValueModelColumnCollection::ibValueModelColumnInfo* head = columns->GetColumnByName(hops.front());
	if (head == nullptr)
		return ibQueryConstructorField();

	ibQueryConstructorField start;
	start.m_name      = head->GetColumnName();
	start.m_type      = head->GetColumnTypeValue();
	start.m_available = head->IsColumnAvailable();
	return ibQueryConstructorModel(m_metaData).WalkFrom(start, std::vector<wxString>(hops.begin() + 1, hops.end()));
}

// ---------------------------------------------------------------------------
// THE DRIVER WALK — the same schema, filled from a degenerate table
// ---------------------------------------------------------------------------
//
// ⭐⭐ A DRIVER DOES NOT CARE WHERE ROWS COME FROM. It is handed a SCHEMA and then ROWS, and everything it
// draws it draws out of those two: the column band, the titles, the grouping, the totals. So a table of
// values prints onto a sheet exactly as a query does — the source is simply a degenerate table instead of a
// database (Max, 2026-08-29: *"the driver works the same, its source just gets the data the same way"*).
//
// 🛑 THIS USED TO BE THE BASE'S NO-OP, and the note here said so as if it were a property rather than a gap:
// the RAM DISPLAY path is ComputeOrder + live nodes windowed on the model side, so nothing had ever needed a
// walk. Anything that wanted the composition PRINTED — «output list», and the search after it — got an empty
// answer out of a table that plainly had rows.
//
// The schema is built from the fields the composition SELECTS, in their order; nothing selected means every
// column of the storage, because a table of values that says nothing about its columns means all of them.
// Each is a Detail column: a RAM composition folds nothing (grouping happens on the display side, in
// RunStoragePage), so there are no dimensions and no measures to declare.
bool ibDataRamComposer::Run(ibCompositionDriver& driver)
{
	if (m_storage == nullptr)
		return false;
	DropMemos();   // a run reads the options — and the catalogs' trees — as they stand now

	// The fields to print — asked of the composition through the same accessor the DB side uses, so a
	// reader's selected-fields table means the same thing on both.
	std::vector<wxString> fields = m_outputs.empty()
		? std::vector<wxString>() : SelectedFor(m_outputs.front());

	if (fields.empty()) {
		// Nothing chosen: every column the storage has, in its own order — but what this base does not use.
		if (ibValueModel::ibValueModelColumnCollection* columns = m_storage->Columns())
			for (unsigned int index = 0; index < columns->GetColumnCount(); ++index)
				if (ibValueModel::ibValueModelColumnCollection::ibValueModelColumnInfo* column =
						columns->GetColumnInfo(index))
					if (IsAvailable(column->GetColumnName()))
						fields.push_back(column->GetColumnName());
	}
	if (fields.empty())
		return false;   // a table with no columns has nothing to print, and that is not a failure to report

	// ⭐⭐ …AND WHAT IT GROUPS BY, asked through the same door the DB side asks (`GroupCount` /
	// `GetGroupAt`). A RAM table read them nowhere at all, so a value table and a tabular section printed
	// as a flat list whatever a person had set — the setting was accepted, stored and shown, with no
	// reader anywhere (Max, 2026-08-29: *"the RAM composer cannot do groupings"*).
	//
	// A grouping FIELD is a field like any other: it is printed in its own column and it also opens a
	// heading. So the grouping paths are folded into the printed set rather than kept beside it — a
	// column a person grouped by is one they plainly want to see.
	std::vector<wxString> groupPaths;
	for (size_t i = 0; i < GroupCount(); ++i) {
		wxString path; ibQueryDimUnfold kind = ibQueryDimUnfold::Elements;
		if (!GetGroupAt(i, path, kind) || path.IsEmpty())
			continue;
		groupPaths.push_back(path);
		// …and it is PRINTED, wherever it ends up standing. Where that is is the printer's business —
		// it stacks the dimensions into the leftmost column itself — so this only has to make sure the
		// field is in the set at all.
		if (std::find(fields.begin(), fields.end(), path) == fields.end())
			fields.push_back(path);
	}

	// The schema, said in the vocabulary every driver already reads.
	ibCompositionOutputInfo info;
	info.m_kind = ibCompositionOutputKind::Grouping;
	for (const wxString& field : fields) {
		// ⭐⭐ EVERY COLUMN IS AN ORDINARY ONE, the grouped ones included (Max, 2026-08-29: *"the same
		// way they lie in the table now"*). A LIST IS A TABLE, and a table's columns are its columns:
		// the sheet shows what the grid shows, in the order the grid shows it.
		//
		// 🛑 STAMPING THE GROUPED ONES `Dimension` said something true of a REPORT and false of a list.
		// The printer answers that role by stacking the dimensions into one leftmost column and
		// SUPPRESSING them on every record — which is right for a report (repeating the group's name on
		// each of its rows is noise beside the figures) and wrong here: it moved the grouped column out
		// of its place, reordered the sheet against the grid, and left each record a row with a hole
		// where its own field had been. Three visible faults from one wrong word.
		//
		// The grouping is still there — it is the LEVELS that carry it (a heading line, the records
		// under it, the outline that folds them), which is where a list's structure has always lived.
		ibQueryLowering::OutputColumn column;
		column.m_name     = field;
		column.m_alias    = field;
		column.m_byAlias  = true;    // read by name — a RAM table has no query column to point at
		column.m_role     = ibQueryLowering::ibColumnRole::Detail;
		// ⭐ …OF ITS DECLARED TYPE, as a query's column is (queryLowering: `oc.m_type = c->GetTypeDesc()`): the leaf of the
		// field's walk — Number(15,2), a date's fractions. With none, a driver had no type to write a figure by
		// (ibFormatString::FromTypeDesc): a tabular section's price printed `55`, a quantity `300.000` here and `250` there.
		const ibQueryAstExprPtr hops = ibQueryColumnFromPath(field);
		if (hops && !hops->m_path.empty())
			column.m_type = WalkPath(hops->m_path).m_type;
		info.m_schema.push_back(column);
		info.m_titles.push_back(field);
		info.m_paths.push_back(field);
		info.m_shown.push_back(true);
		// …and how it is shown — the field's appearance (its Format), as the DB side reads it (dataComposerRun).
		info.m_appearances.push_back(AppearanceForPath(field));
	}

	driver.OnOutputBegin(info);

	// Split each field ONCE — the walk is per row, and the split is per field.
	struct RamField { ibMetaID m_col; std::vector<wxString> m_tail; bool m_resolved; };
	std::vector<RamField> resolved;
	resolved.reserve(fields.size());
	for (const wxString& field : fields) {
		ibMetaID col = wxNOT_FOUND; std::vector<wxString> tail;
		const bool ok = m_storage->SplitField(field, col, tail);
		resolved.push_back({ col, std::move(tail), ok });
	}

	// ⭐⭐ AND THE GROUPING KEYS ARE SPLIT ON THEIR OWN PATHS. They were read off `resolved[level]`
	// before, on the strength of having been folded into the front of `fields` — which happens only
	// where the field was NOT already among the printed ones. A table that shows the column it is
	// grouped by (every ordinary one) therefore grouped by whatever field happened to sit at that
	// position: here `NumberLine`, empty on every row, so three rows came out as ONE group while the
	// screen plainly showed two (Max, 2026-08-29: *"it does not work"*).
	//
	// A key is a field in its own right; asking for it BY NAME is the whole fix, and it cannot drift
	// out of step with the printed set again.
	std::vector<RamField> keys;
	keys.reserve(groupPaths.size());
	for (const wxString& path : groupPaths) {
		ibMetaID col = wxNOT_FOUND; std::vector<wxString> tail;
		const bool ok = m_storage->SplitField(path, col, tail);
		keys.push_back({ col, std::move(tail), ok });
	}

	// The rows, in the order in force — the SAME order the screen is showing, filter and sort included.
	const std::vector<long> order = ComputeOrder();

	// One row's printed values, read once and used by whoever writes it — a heading or a record.
	const auto valuesOf = [&](long index) {
		std::vector<ibValue> values;
		values.reserve(resolved.size());
		for (const RamField& field : resolved)
			values.push_back(field.m_resolved
				? m_storage->ResolveField(index, field.m_col, field.m_tail) : ibValue());
		return values;
	};

	// ⭐⭐ A HEADING SAYS ITS KEY AND NOTHING ELSE. It is read off the first row of the part — that row's
	// value for the grouping field IS the key — but the REST of that row belongs to the row, not to the
	// group standing over it. Handed over whole, the first record's fields were printed twice: once on
	// the heading and again on the line under it (Max, 2026-08-29: *"it outputs oddly"*).
	//
	// This is what a folded READ hands over by construction — a group node holds dimensions and
	// resources, and its detail columns are empty — so blanking them here is not a special case for RAM,
	// it is the same line the DB road already draws.
	// ⚠ …WITH THE SAME WORD: NULL, a column the heading does not hold (ibSelector::GetValue). An empty value
	// is a value the heading would HAVE, and a condition read on the heading compared it (ibCompositionFilterHolds).
	std::vector<bool> isGroupField(resolved.size(), false);
	for (size_t i = 0; i < fields.size() && i < isGroupField.size(); ++i)
		isGroupField[i] = std::find(groupPaths.begin(), groupPaths.end(), fields[i]) != groupPaths.end();

	const auto keyValuesOf = [&](long index) {
		std::vector<ibValue> values = valuesOf(index);
		for (size_t i = 0; i < values.size(); ++i)
			if (!isGroupField[i])
				values[i] = ibValue(ibValueTypes::TYPE_NULL);
		return values;
	};

	// ⭐ …AND WHAT STANDS OUT, read by the walk's own reader — the rules made ready ONCE (ConditionalAppearanceFor) —
	// so a printed list is painted as the grid showing it is. ONE BUFFER PER RUNG, as the DB walk keeps them: a
	// heading's is handed over again at its close, and what is under it must not have written over it in between.
	// Asked only where some storey declares a rule — a list with none makes nothing for it, not even the buffer.
	const Output* const output = m_outputs.empty() ? nullptr : &m_outputs.front();
	const ConditionalAppearance appearance = output != nullptr ? ConditionalAppearanceFor(*output) : ConditionalAppearance();
	std::deque<ibCompositionLineAttr> attrAt;
	const auto attrFor = [&](size_t rung, const GroupNode* node, const std::vector<ibValue>& values)
		-> ibCompositionLineAttr* {
		if (appearance.IsEmpty())
			return nullptr;
		while (attrAt.size() <= rung)
			attrAt.emplace_back();
		return appearance.AttrFor(node, info.m_schema, values, attrAt[rung]) ? &attrAt[rung] : nullptr;
	};

	if (groupPaths.empty()) {
		for (const long index : order) {
			ibCompositionLine line;                  // a flat read: rung 0, no hierarchy step
			line.m_kind = ibSelectorNodeKind::Detail;
			const std::vector<ibValue> values = valuesOf(index);
			line.m_attr = attrFor(0, output != nullptr ? DetailLevelOf(*output) : nullptr, values);
			driver.OnRow(line, values);
		}
		driver.OnOutputEnd(false);
		return true;
	}

	// ⭐⭐ GROUPED — one heading per distinct key, IN FIRST-SEEN ORDER. That is the fold's rule everywhere
	// else in the house (*a group stands where its first row stood*), and it is what carries the sort a
	// person set: `ComputeOrder` has already put the rows in it, so walking them in arrival order is all
	// that is needed. Keyed by a VECTOR of positions rather than by a rendered string — a text key would
	// make `1` and `"1"` one group.
	//
	// ⚠ RECURSIVE BY LEVEL, because that is what nesting IS: at level k the rows are split by that
	// level's key, and each part is walked again one level deeper. The bottom level writes the records.
	std::function<void(const std::vector<long>&, size_t)> walk =
		[&](const std::vector<long>& rows, size_t level) {
		if (level >= groupPaths.size()) {
			for (const long index : rows) {
				ibCompositionLine line;
				// PAST THE LAST DIMENSION, and one rung past it. A printer reads the row's own
				// dimension as `level - 1`, so a record left AT the last grouping's rung claims that
				// grouping's field as its own and writes the heading's value a second time, on its
				// own line — the doubling that reads as "the grouping did nothing".
				line.m_level = static_cast<int>(groupPaths.size()) + 1;
				line.m_kind  = ibSelectorNodeKind::Detail;
				const std::vector<ibValue> values = valuesOf(index);
				line.m_attr = attrFor(groupPaths.size(), output != nullptr ? DetailLevelOf(*output) : nullptr, values);
				driver.OnRow(line, values);
			}
			return;
		}

		const RamField& key = keys[level];            // THIS level's own field, split on its own path
		std::vector<ibValue>            seen;
		std::vector<std::vector<long>>  parts;
		for (const long index : rows) {
			const ibValue value = key.m_resolved
				? m_storage->ResolveField(index, key.m_col, key.m_tail) : ibValue();
			size_t at = 0;
			for (; at < seen.size(); ++at)
				if (seen[at] == value) break;
			if (at == seen.size()) { seen.push_back(value); parts.emplace_back(); }
			parts[at].push_back(index);
		}

		for (size_t at = 0; at < seen.size(); ++at) {
			ibCompositionLine line;
			line.m_level       = static_cast<int>(level) + 1;   // rung 1 is the first grouping
			line.m_kind        = ibSelectorNodeKind::Group;
			line.m_hasChildren = true;
			line.m_showsWhatIsUnder = true;
			// A HEADING CARRIES ITS OWN KEY and nothing of the rows beneath it — read off the first row
			// of the part, whose value for this field IS the key by construction.
			const std::vector<ibValue> heading = keyValuesOf(parts[at].front());
			line.m_attr = attrFor(level, output != nullptr ? LevelAt(*output, line.m_level) : nullptr, heading);
			driver.OnGroupBegin(line, heading);
			walk(parts[at], level + 1);
			driver.OnGroupEnd(line, heading);
		}
	};
	walk(order, 0);

	driver.OnOutputEnd(false);
	return true;
}

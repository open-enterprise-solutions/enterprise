////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : RAM value-model + its value-storage
////////////////////////////////////////////////////////////////////////////
//
// The RAM half of the value-model, split out of tabularModel.cpp: ibValueModelStorage (the in-memory model whose live
// rows fetch straight from ibRamValueStorage) + the storage's own out-of-line methods (ColumnIdByName /
// SplitField / ResolveField). The in-place engine itself — ibDataRamComposer — lives WITH its declaration in
// composition/ramComposer.cpp; a RAM fetch here just windows the order it computes. The DB half lives in
// tabularModelDb.cpp; the shared ibValueModel base + the node lives in tabularModel.cpp.

#include "tabularModel.h"

#include "backend/tabularModelView.h"           // s_constIgnoreParent / ibDataViewItem
#include "backend/composition/ramComposer.h"    // ibDataRamComposer (pulls dataComposer.h) — m_composer.ComputeOrder()
#include "backend/srcDataObject.h"               // ibSourceDataObject / ibSourceExplorer — ResolveField dot-walk over reference cells

// ibRamValueStorage::ColumnIdByName — resolve a field name → column id via the model's column collection
// (re-pointed each fetch by RunComposerPage). wxNOT_FOUND for an unknown / dot-walk path (the composer skips it).
ibMetaID ibRamValueStorage::ColumnIdByName(const wxString& name) const
{
	if (m_columns == nullptr) return wxNOT_FOUND;
	const ibValueModel::ibValueModelColumnCollection::ibValueModelColumnInfo* info = m_columns->GetColumnByName(name);
	return info != nullptr ? static_cast<ibMetaID>(info->GetColumnID()) : ibMetaID(wxNOT_FOUND);
}

// ibRamValueStorage::SplitField — cut a field path on '.' → HEAD storage column id + the dotted TAIL (reference
// hops). wxNOT_FOUND head (the first segment is not a storage column) → false, the caller drops this spec.
// Shared by the RAM composer's ComputeOrder (filter/sort) AND the model's grouping (RunComposerPage dims).
bool ibRamValueStorage::SplitField(const wxString& path, ibMetaID& headCol, std::vector<wxString>& tail) const
{
	const wxString head = path.BeforeFirst(wxT('.'));
	headCol = ColumnIdByName(head.IsEmpty() ? path : head);
	if (headCol == wxNOT_FOUND) return false;
	tail.clear();
	wxString remaining = path.AfterFirst(wxT('.'));
	while (!remaining.IsEmpty()) {
		tail.push_back(remaining.BeforeFirst(wxT('.')));
		remaining = remaining.AfterFirst(wxT('.'));
	}
	return true;
}

// ibRamValueStorage::ResolveField — read ONE stored row's value for a split field, walking references down the
// tail (the "simple implementation up to references", Max). The HEAD is the row's own cell; each further TAIL
// segment hops into the previous value AS A SOURCE — a reference cell IS an ibSourceDataObject, so it self-
// describes its columns through GetSourceExplorer (name → id) and fetches the next value by that id
// (GetValueByMetaID, the very hop ResolvePath runs). A primitive mid-path / unknown segment ends the walk with
// an empty value (an unevaluable filter passes; an unevaluable sort key sorts as empty — never a crash).
ibValue ibRamValueStorage::ResolveField(long row, ibMetaID headCol, const std::vector<wxString>& tail) const
{
	ibValue current = GetCell(row, headCol);
	for (const wxString& seg : tail) {
		ibSourceDataObject* source = nullptr;
		current.ConvertToValue<ibSourceDataObject>(source);
		if (source == nullptr)
			return ibValue();                        // primitive mid-path — cannot dot into it
		const ibSourceDataObject::ibSourceExplorer* explorer = source->GetSourceExplorer();
		if (explorer == nullptr)
			return ibValue();
		// name → id via the reference-as-source's own columns. Through the explorer's own door, so the
		// case rule is the model layer's one rule (this loop used to compare exactly, and a segment
		// written in another case silently ended the walk instead of resolving).
		const ibSourceDataObject::ibSourceExplorer* child = explorer->FindByName(seg);
		const ibMetaID segId = child != nullptr ? child->GetSourceId() : wxNOT_FOUND;
		ibValue next;
		if (segId == wxNOT_FOUND || !source->GetValueBySourceHop(ibSourceHop{ segId }, next))
			return ibValue();
		current = next;
	}
	return current;
}

// The `want` display positions in [0,total) around the browsed anchor position `p` (or the top / bottom when
// there is no anchor, p==-1), honouring the fetch direction — the ONE windowing rule the flat / grouped /
// detail levels all page by, in BOTH the RAM half (in-place sorted rows) and the DB half (a grouped level's
// folded group list; declared in tabularModel.h). Backward gathers nearest-first then flips to display order.
std::vector<long> ibComputePageWindow(long total, long p, ibFetchDirection dir, int want)
{
	std::vector<long> win;
	if (dir == ibFetchDirection::Backward) {
		for (long k = (p >= 0 ? p - 1 : total - 1); k >= 0 && static_cast<int>(win.size()) < want; --k)
			win.push_back(k);
		std::reverse(win.begin(), win.end());
	}
	else {
		// RESET (GetFirstFetch / refresh) fetches FROM the anchor INCLUSIVE (>=): the anchor IS the top-of-view row
		// and must stay in the portion so the viewport does not drift on every refresh (the anchor is a constant
		// that only a real scroll changes). FORWARD (scroll-down) stays EXCLUSIVE (>): it appends the NEXT page
		// after the last loaded row.
		const long start = (p < 0) ? 0 : (dir == ibFetchDirection::Reset ? p : p + 1);
		for (long k = start; k < total && static_cast<int>(win.size()) < want; ++k)
			win.push_back(k);
	}
	return win;
}

// ---------------------------------------------------------------------------
// ibValueModel::RunStoragePage — the RAM paging primitive (the RAM sibling of RunComposerPage). ComputeOrder (filter +
// sort in place) gives the display order over `storage`; then:
//   • FLAT (no grouping / a flat-view sentinel): window the LIVE nodes by the browsed anchor and return them —
//     the node IS the storage row (direct edit + notify; selection survives by the stable pointer).
//   • GROUPED: the browsed parent node carries a group PATH (dim values root→parent). Scope the ordered rows to
//     it; at a group LEVEL emit ONE synthetic group node per distinct dims[depth] value (container, drillable);
//     at the DETAIL level (drilled through every dim) return the LIVE scoped rows. A dim field may itself dot-
//     walk a reference (ibRamValueStorage::ResolveField). No queryable, no SQL — RAM is in memory.
// `storage`/`composer` are a bound pair: the native RAM model passes its own m_storage/m_composer; a DB model's
// whole-list snapshot (DynamicRead off) passes m_snapshot/m_snapshotComposer — same paging, same nodes.
unsigned int ibValueModel::RunStoragePage(ibRamValueStorage& storage, ibDataRamComposer& composer,
	const ibDataViewItem& parent, const ibDataViewItem& anchor,
	int count, ibFetchDirection dir, ibDataViewItemArray& out) const
{
	storage.SetColumns(GetColumnCollection());          // let the composer resolve field names → column ids

	// Grouping dims (field paths → head storage column + dotted tail). The VIEW MODE decides flat-vs-grouped: a
	// flat LIST view passes the ignore-parent SENTINEL → grouping is OFF (the user set the Flat view → a flat table,
	// even with a grouping configured); a TREE / Hierarchical view passes an empty/real parent → grouping is ON.
	// So the flat toggle always wins over a stored grouping. No dims → the flat live-node path below. slice:
	// Elements grouping; a dot-tail dim groups by the walked value.
	const bool flatView = (parent == s_constIgnoreParent);
	std::vector<std::pair<ibMetaID, std::vector<wxString>>> dims;
	if (!flatView) {
		for (size_t i = 0; i < composer.GroupCount(); ++i) {
			wxString field; ibQueryDimUnfold kind = ibQueryDimUnfold::Elements;
			if (!composer.GetGroupAt(i, field, kind) || field.IsEmpty()) continue;
			ibMetaID head; std::vector<wxString> tail;
			if (storage.SplitField(field, head, tail))
				dims.emplace_back(head, std::move(tail));
		}
	}

	const std::vector<long> ord = composer.ComputeOrder();   // filtered + sorted storage indices, display order
	const int want = (count > 0 ? count : defaultCountPerPage);

	// ---- FLAT: no grouping — window the live storage nodes by the anchor and return them ----
	if (dims.empty()) {
		long p = -1;
		if (anchor.IsOk()) {
			const long aidx = storage.IndexOf(GetViewData<ibComposerNode>(anchor));
			for (size_t k = 0; k < ord.size(); ++k)
				if (ord[k] == aidx) { p = static_cast<long>(k); break; }
		}
		const std::vector<long> win = ibComputePageWindow(static_cast<long>(ord.size()), p, dir, want);
		out.Alloc(win.size());
		unsigned int fetched = 0;
		for (const long pos : win) {
			ibComposerNode* node = storage.GetNode(ord[pos]);
			if (node == nullptr) continue;
			out.Add(ibDataViewItem(node));   // the LIVE storage node (ctor IncRefs)
			++fetched;
		}
		for (size_t i = 0; i < out.GetCount(); ++i) SetItemParent(out[i], parent);
		return fetched;
	}

	// ---- GROUPED: the browsed parent's group path (dim values root->parent) scopes this level ----
	std::vector<ibValue> parentPath;
	if (parent.IsOk() && !flatView)
		if (const ibComposerNode* pnode = GetViewData<ibComposerNode>(parent))
			parentPath = pnode->GetGroupPath();
	const size_t depth = parentPath.size();

	// The ordered rows whose dim[k] == parentPath[k] for every already-drilled level k (the drill scope).
	std::vector<long> scoped;
	scoped.reserve(ord.size());
	for (const long idx : ord) {
		bool inScope = true;
		for (size_t k = 0; k < depth && k < dims.size(); ++k) {
			const ibValue v = storage.ResolveField(idx, dims[k].first, dims[k].second);
			if (!(v == parentPath[k])) { inScope = false; break; }
		}
		if (inScope) scoped.push_back(idx);
	}

	// ---- DETAIL level (drilled through every dim): return the LIVE scoped rows, windowed by the anchor node ----
	if (depth >= dims.size()) {
		long p = -1;
		if (anchor.IsOk()) {
			const long aidx = storage.IndexOf(GetViewData<ibComposerNode>(anchor));
			for (size_t k = 0; k < scoped.size(); ++k)
				if (scoped[k] == aidx) { p = static_cast<long>(k); break; }
		}
		const std::vector<long> win = ibComputePageWindow(static_cast<long>(scoped.size()), p, dir, want);
		out.Alloc(win.size());
		unsigned int fetched = 0;
		for (const long pos : win) {
			ibComposerNode* node = storage.GetNode(scoped[pos]);
			if (node == nullptr) continue;
			out.Add(ibDataViewItem(node));
			++fetched;
		}
		for (size_t i = 0; i < out.GetCount(); ++i) SetItemParent(out[i], parent);
		return fetched;
	}

	// ---- GROUP level: one synthetic group node per DISTINCT value of dims[depth] among the scoped rows, in the
	//      (already sorted) scoped order. Each carries the parent path + its own dim value + a container flag ----
	const ibMetaID dimCol = dims[depth].first;
	const std::vector<wxString>& dimTail = dims[depth].second;
	std::vector<ibValue> groupValues;
	for (const long idx : scoped) {
		const ibValue v = storage.ResolveField(idx, dimCol, dimTail);
		bool seen = false;
		for (const ibValue& g : groupValues) if (g == v) { seen = true; break; }
		if (!seen) groupValues.push_back(v);
	}

	// Position the page at the browsed anchor group (identified by its own last group-path value).
	long p = -1;
	if (anchor.IsOk())
		if (const ibComposerNode* anode = GetViewData<ibComposerNode>(anchor)) {
			const std::vector<ibValue>& apath = anode->GetGroupPath();
			if (!apath.empty())
				for (size_t k = 0; k < groupValues.size(); ++k)
					if (groupValues[k] == apath.back()) { p = static_cast<long>(k); break; }
		}
	const std::vector<long> win = ibComputePageWindow(static_cast<long>(groupValues.size()), p, dir, want);

	out.Alloc(win.size());
	unsigned int fetched = 0;
	for (const long pos : win) {
		const ibValue& gv = groupValues[pos];
		std::map<ibMetaID, ibValue> vals;
		// Inherit the ANCESTOR grouping fields (Max): a subgroup carries the dim values from the levels above, so
		// a column grouped higher (Attribute1) still renders in this subgroup's rows (Attribute2's level) — the
		// parent path holds each ancestor value keyed positionally, dims[k] is its column. (Mirrors the DB
		// FoldDimLevel inherit.)
		for (size_t k = 0; k < depth && k < dims.size() && k < parentPath.size(); ++k)
			vals[dims[k].first] = parentPath[k];
		vals[dimCol] = gv;                                  // stamp the group's own dim value (the group label cell)
		std::vector<ibValue> groupPath = parentPath;
		groupPath.push_back(gv);
		ibComposerNode* node = new ibComposerNode(vals, groupPath, /*container*/true);
		out.Add(ibDataViewItem(node));                      // ctor IncRefs to 2
		node->DecRef();                                     // → out owns exactly one reference
		++fetched;
	}
	for (size_t i = 0; i < out.GetCount(); ++i) SetItemParent(out[i], parent);
	return fetched;
}

// ibValueModelStorage::RunComposerPage — a native RAM model pages its OWN storage + composer through the shared primitive.
unsigned int ibValueModelStorage::RunComposerPage(const ibDataViewItem& parent, const ibDataViewItem& anchor,
	int count, ibFetchDirection dir, ibDataViewItemArray& out) const
{
	return RunStoragePage(m_storage, m_composer, parent, anchor, count, dir, out);
}

// ibValueModelStorage::BuildAncestorBreadcrumb — the ancestor GROUP chain of a RAM row, so a selection inside a
// group survives a view-mode switch (the top-level fetch returns group headers, not the row, so the plain restore
// misses). Mirrors the DB cursor's grouped branch: one crumb per grouping dimension, keyed by the row's own value
// for that dim (via the storage's SplitField / ResolveField dot-walk). A plain (ungrouped) RAM list is flat — no
// folder hierarchy — so it has no ancestors. out[0] = the innermost group the row sits directly in.
void ibValueModelStorage::BuildAncestorBreadcrumb(const ibDataViewItem& fromRow, ibDataViewItemArray& out) const
{
	if (m_composer.GroupCount() == 0)
		return;
	m_storage.SetColumns(GetColumnCollection());   // resolve field names → column ids (parity with RunComposerPage)
	const long idx = StorageIndexOf(fromRow);
	if (idx < 0)
		return;
	std::vector<ibValue> groupPath;
	std::vector<ibComposerNode*> crumbs;
	for (size_t i = 0; i < m_composer.GroupCount(); ++i) {
		wxString field; ibQueryDimUnfold kind = ibQueryDimUnfold::Elements;
		if (!m_composer.GetGroupAt(i, field, kind) || field.IsEmpty()) continue;
		ibMetaID head; std::vector<wxString> tail;
		if (!m_storage.SplitField(field, head, tail)) continue;
		const ibValue v = m_storage.ResolveField(idx, head, tail);
		groupPath.push_back(v);
		std::map<ibMetaID, ibValue> vals;
		vals[head] = v;                                        // the group's own label cell
		crumbs.push_back(new ibComposerNode(vals, groupPath, /*container*/true));   // groupPath copied
	}
	for (size_t i = crumbs.size(); i > 0; --i) {              // deepest → shallowest into out
		out.Add(ibDataViewItem(crumbs[i - 1]));
		crumbs[i - 1]->DecRef();
	}
}

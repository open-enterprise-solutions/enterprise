#include "configCompareModel.h"

#include "backend/appData.h"
#include "frontend/artProvider/artProvider.h"

#include <wx/artprov.h>
#include <wx/translation.h>

// --- ibDataViewDiffRowObject -----------------------------------------

bool ibDataViewDiffRowObject::IsContainer() const
{
	return m_model != nullptr && m_model->RecordHasChildren(m_recordIndex);
}

ibDataViewItem ibDataViewDiffRowObject::GetParentItem() const
{
	if (m_model == nullptr)
		return ibDataViewItem();
	return m_model->GetParentItemForRecord(m_recordIndex);
}

bool ibDataViewDiffRowObject::IsEqualTo(const ibDataViewObject& other) const
{
	const ibDataViewDiffRowObject* o =
		dynamic_cast<const ibDataViewDiffRowObject*>(&other);
	return o != nullptr && o->m_recordIndex == m_recordIndex;
}

// --- ibDataViewMetaDiffModel -----------------------------------------

ibDataViewMetaDiffModel::ibDataViewMetaDiffModel(std::vector<ibMetaDiffRecord> records)
	: ibDataViewModel(), m_records(std::move(records))
{
	m_selected.assign(m_records.size(), false);

	// Build the parent → children index. Slot 0 holds children of the
	// invisible root (records with parentIndex == -1); slot i+1 holds
	// children of record i.
	m_childrenByParent.assign(m_records.size() + 1, {});
	for (int i = 0; i < static_cast<int>(m_records.size()); ++i) {
		const int parent = m_records[i].m_parentIndex;
		const size_t bucket = (parent < 0)
			? 0
			: static_cast<size_t>(parent + 1);
		m_childrenByParent[bucket].push_back(i);
	}

	// One row object per record, kept alive for the model's lifetime.
	// AssociateModel + ibDataViewItem copies bump the refcount higher;
	// when those go away the row stays alive at refcount 1 (held by
	// the wxObjectDataPtr below).
	m_rowObjects.reserve(m_records.size());
	for (int i = 0; i < static_cast<int>(m_records.size()); ++i) {
		m_rowObjects.emplace_back(new ibDataViewDiffRowObject(this, i));
	}

	// Initial filter = All → every record visible.
	m_visibleByFilter.assign(m_records.size(), true);
}

void ibDataViewMetaDiffModel::SetFilterMode(FilterMode mode)
{
	if (mode == m_filterMode)
		return;
	BeforeReset();
	m_filterMode = mode;
	RecomputeVisibility();
	AfterReset();
}

bool ibDataViewMetaDiffModel::IsRecordVisible(int recordIndex) const
{
	if (recordIndex < 0 || recordIndex >= static_cast<int>(m_visibleByFilter.size()))
		return false;
	return m_visibleByFilter[recordIndex];
}

void ibDataViewMetaDiffModel::RecomputeVisibility()
{
	m_visibleByFilter.assign(m_records.size(), false);

	if (m_filterMode == FilterMode::All) {
		m_visibleByFilter.assign(m_records.size(), true);
		return;
	}

	// Pass 1: direct match. Each record either passes the filter by
	// itself or doesn't; ancestors get promoted in pass 2.
	for (size_t i = 0; i < m_records.size(); ++i) {
		const ibMetaDiffRecord& rec = m_records[i];
		bool match = false;
		switch (m_filterMode) {
		case FilterMode::Differences:
			// Show every non-Same row directly. Group rows are lifted
			// to Changed by the walker when any descendant differs, so
			// matching on status != Same naturally surfaces only
			// branches with content. Same group rows have no diff in
			// their subtree, so they (and everything under them) hide.
			match = (rec.m_status != ibMetaDiffStatus::Same);
			break;
		case FilterMode::SameOnly:
			match = (rec.m_status == ibMetaDiffStatus::Same);
			break;
		case FilterMode::All:
			break;  // handled above
		}
		m_visibleByFilter[i] = match;
	}

	// Pass 2: bottom-up ancestor promotion. Records are emitted in
	// pre-order, so iterating reverse lets each child light up its
	// parent chain before the parent is processed.
	for (size_t i = m_records.size(); i-- > 0;) {
		if (!m_visibleByFilter[i])
			continue;
		int p = m_records[i].m_parentIndex;
		while (p >= 0 && !m_visibleByFilter[p]) {
			m_visibleByFilter[p] = true;
			p = m_records[p].m_parentIndex;
		}
	}
}

// --- helpers ----------------------------------------------------------

ibDataViewDiffRowObject* ibDataViewMetaDiffModel::RowObjectAt(int recordIndex) const
{
	if (recordIndex < 0 || recordIndex >= static_cast<int>(m_rowObjects.size()))
		return nullptr;
	return m_rowObjects[recordIndex].get();
}

int ibDataViewMetaDiffModel::IndexFromItem(const ibDataViewItem& item) const
{
	if (!item.IsOk())
		return -1;

	// item.GetID returns the wrapped pointer regardless of mode; we
	// dynamic_cast to confirm it's one of our row objects rather than
	// trusting the raw pointer. In practice every refcounted item we
	// hand out is a DiffRowObject, but defensive cast lets a stray
	// item from another model fall through cleanly.
	const ibDataViewDiffRowObject* obj =
		dynamic_cast<const ibDataViewDiffRowObject*>(item.GetID());
	if (obj == nullptr)
		return -1;
	return obj->GetRecordIndex();
}

ibDataViewItem ibDataViewMetaDiffModel::ItemFromIndex(int index) const
{
	if (index < 0)
		return ibDataViewItem();
	ibDataViewDiffRowObject* obj = RowObjectAt(index);
	if (obj == nullptr)
		return ibDataViewItem();
	return ibDataViewItem(obj);
}

bool ibDataViewMetaDiffModel::RecordHasChildren(int recordIndex) const
{
	if (recordIndex < 0 || recordIndex >= static_cast<int>(m_records.size()))
		return false;
	const size_t bucket = static_cast<size_t>(recordIndex + 1);
	if (bucket >= m_childrenByParent.size())
		return false;
	// Hide the expander when every child is filtered out — otherwise
	// the user would click an empty triangle.
	for (int childIdx : m_childrenByParent[bucket]) {
		if (IsRecordVisible(childIdx))
			return true;
	}
	return false;
}

ibDataViewItem ibDataViewMetaDiffModel::GetParentItemForRecord(int recordIndex) const
{
	if (recordIndex < 0 || recordIndex >= static_cast<int>(m_records.size()))
		return ibDataViewItem();
	return ItemFromIndex(m_records[recordIndex].m_parentIndex);
}

// static
wxString ibDataViewMetaDiffModel::StatusLabel(ibMetaDiffStatus status)
{
	switch (status) {
	case ibMetaDiffStatus::Same:        return _("Same");
	case ibMetaDiffStatus::Reordered:   return _("Reordered");
	case ibMetaDiffStatus::Changed:     return _("Changed");
	case ibMetaDiffStatus::OnlyInLeft:  return _("Only in current");
	case ibMetaDiffStatus::OnlyInRight: return _("Only in other");
	}
	return wxEmptyString;
}

// static
wxString ibDataViewMetaDiffModel::SideSummary(const ibValueMetaObject* obj)
{
	// Explicit "Missing" placeholder when one side has no object —
	// reads better than a bare dash because the user immediately sees
	// which side the row is absent from.
	if (obj == nullptr)
		return _("Missing");
	wxString name;
	obj->GetObjectNameAsString(name);
	if (name.IsEmpty())
		name = _("(unnamed)");
	return name;
}

// --- ibDataViewModel overrides ----------------------------------------

void ibDataViewMetaDiffModel::GetValue(wxVariant& variant,
	const ibDataViewItem& item, unsigned int col) const
{
	const int idx = IndexFromItem(item);
	if (idx < 0 || idx >= static_cast<int>(m_records.size())) {
		variant.MakeNull();
		return;
	}
	const ibMetaDiffRecord& rec = m_records[idx];

	switch (col) {
	case kColSelect: {
		// Containers (groups, paired objects with descendants) show
		// the aggregate state of their merge-candidate descendants —
		// true only when every candidate underneath is selected.
		// Mixed states render as unchecked for now (tri-state would
		// need a custom toggle renderer).
		if (rec.IsMergeCandidate())
			// static_cast, because m_selected is a std::vector<bool>: indexing it yields a
			// PROXY (std::vector<bool>::reference), not a bool, and libc++ offers several
			// equally good conversions from it — wxVariant(bool) / (int) / (wxAny) — so the
			// call is ambiguous there while it compiles on MSVC and libstdc++.
			variant = wxVariant(static_cast<bool>(m_selected[idx]));
		else if (HasMergeCandidateDescendant(idx))
			variant = wxVariant(IsAllDescendantsSelected(idx));
		else
			variant = wxVariant(false);
		break;
	}
	case kColName: {
		// Three row shapes: property row (m_propertyName set), synthetic
		// group (m_groupClsid set), or paired/single object.
		wxString name;
		ibClassID iconClsid = 0;
		if (rec.IsProperty()) {
			name = rec.m_propertyName;
			// Properties show without an icon — the row sits inside the
			// Properties umbrella, which carries the visual cue.
		}
		else if (rec.IsGroup()) {
			name = ibMetaDiffWalker::GroupLabelFor(rec.m_groupClsid);
			iconClsid = rec.m_groupClsid;
		}
		else {
			const ibValueMetaObject* obj = rec.GetAnyObject();
			if (obj != nullptr) {
				name = obj->GetName();
				iconClsid = obj->GetClassType();
			}
		}
		wxBitmapBundle bitmap;
		if (iconClsid == g_diffCommonUmbrellaClsid
			|| iconClsid == g_diffPropertiesGroupClsid) {
			// Generic folder icon for synthetic umbrellas (Common /
			// Properties) — same source treeConfiguration_impl.cpp:1472
			// uses for the configuration-tree Common node.
			bitmap = wxArtProvider::GetBitmapBundle(
				wxART_COMMON_FOLDER, wxART_METATREE);
		}
		else if (iconClsid != 0) {
			bitmap = wxBitmapBundle(
				ibBackendPicture::GetPicture(iconClsid));
		}
		variant << ibDataViewIconText(name, bitmap);
		break;
	}
	case kColStatus: {
		variant = wxVariant(StatusLabel(rec.m_status));
		break;
	}
	case kColLeft: {
		if (rec.IsProperty())
			variant = wxVariant(rec.m_leftValue);
		else if (rec.IsGroup())
			variant = wxVariant(wxString());
		else
			variant = wxVariant(SideSummary(rec.m_left));
		break;
	}
	case kColRight: {
		if (rec.IsProperty())
			variant = wxVariant(rec.m_rightValue);
		else if (rec.IsGroup())
			variant = wxVariant(wxString());
		else
			variant = wxVariant(SideSummary(rec.m_right));
		break;
	}
	default:
		variant.MakeNull();
	}
}

bool ibDataViewMetaDiffModel::SetValue(const wxVariant& variant,
	const ibDataViewItem& item, unsigned int col)
{
	if (col != kColSelect)
		return false;

	const int idx = IndexFromItem(item);
	if (idx < 0 || idx >= static_cast<int>(m_records.size()))
		return false;

	const bool wanted = variant.GetBool();

	if (m_records[idx].IsMergeCandidate()) {
		m_selected[idx] = wanted;
		// Also flip every merge-candidate descendant — selecting a
		// "Changed" parent like a Catalog implicitly selects its
		// child attributes / forms for merge.
		CascadeSelection(idx, wanted);
		return true;
	}

	// Container rows (groups / paired-Same objects) don't merge
	// themselves but can drive their subtree.
	if (HasMergeCandidateDescendant(idx)) {
		CascadeSelection(idx, wanted);
		ValueChanged(ItemFromIndex(idx), kColSelect);
		return true;
	}

	return false;
}

bool ibDataViewMetaDiffModel::HasValue(const ibDataViewItem& item,
	unsigned col) const
{
	const int idx = IndexFromItem(item);
	if (idx < 0 || idx >= static_cast<int>(m_records.size()))
		return false;

	if (col == kColSelect) {
		// Merge candidate (leaf row that can be picked individually)
		// gets a checkbox. Container rows get one too if there's at
		// least one selectable descendant inside — clicking it
		// cascades down. Same / property rows in a pure-Same subtree
		// stay blank so the user doesn't see inert checkboxes.
		if (m_records[idx].IsMergeCandidate())
			return true;
		return HasMergeCandidateDescendant(idx);
	}

	return true;
}

ibDataViewItem ibDataViewMetaDiffModel::GetParent(const ibDataViewItem& item) const
{
	const int idx = IndexFromItem(item);
	if (idx < 0 || idx >= static_cast<int>(m_records.size()))
		return ibDataViewItem();
	return ItemFromIndex(m_records[idx].m_parentIndex);
}

unsigned int ibDataViewMetaDiffModel::GetFirstFetch(const ibDataViewItem& parent,
	const ibDataViewItem& /*anchor*/, int /*count*/,
	ibDataViewItemArray& out) const
{
	const int parentIdx = IndexFromItem(parent);
	const size_t bucket = (parentIdx < 0)
		? 0
		: static_cast<size_t>(parentIdx + 1);

	if (bucket >= m_childrenByParent.size())
		return 0;

	unsigned int count = 0;
	for (int childIdx : m_childrenByParent[bucket]) {
		if (!IsRecordVisible(childIdx))
			continue;
		out.Add(ItemFromIndex(childIdx));
		++count;
	}
	return count;
}

bool ibDataViewMetaDiffModel::GetAttr(const ibDataViewItem& item,
	unsigned int /*col*/, ibDataViewItemAttr& attr) const
{
	const int idx = IndexFromItem(item);
	if (idx < 0 || idx >= static_cast<int>(m_records.size()))
		return false;

	switch (m_records[idx].m_status) {
	case ibMetaDiffStatus::Same:
		return false;
	case ibMetaDiffStatus::Reordered:
		attr.SetBackgroundColour(wxColour(0xE6, 0xEE, 0xF5));
		return true;
	case ibMetaDiffStatus::Changed:
		attr.SetBackgroundColour(wxColour(0xF8, 0xE3, 0xD5));
		return true;
	case ibMetaDiffStatus::OnlyInLeft:
		attr.SetBackgroundColour(wxColour(0xF5, 0xE8, 0xD5));
		return true;
	case ibMetaDiffStatus::OnlyInRight:
		attr.SetBackgroundColour(wxColour(0xD8, 0xE2, 0xEB));
		return true;
	}
	return false;
}

bool ibDataViewMetaDiffModel::IsEnabled(const ibDataViewItem& item,
	unsigned int col) const
{
	const int idx = IndexFromItem(item);
	if (col == kColSelect && idx >= 0
		&& idx < static_cast<int>(m_records.size())
		&& !m_records[idx].IsMergeCandidate()
		&& !HasMergeCandidateDescendant(idx)) {
		return false;
	}
	return true;
}

// --- selection accessors ----------------------------------------------

bool ibDataViewMetaDiffModel::IsSelected(int recordIndex) const
{
	if (recordIndex < 0 || recordIndex >= static_cast<int>(m_selected.size()))
		return false;
	return m_selected[recordIndex];
}

void ibDataViewMetaDiffModel::SetSelected(int recordIndex, bool selected)
{
	if (recordIndex < 0 || recordIndex >= static_cast<int>(m_selected.size()))
		return;
	m_selected[recordIndex] = selected;
	ValueChanged(ItemFromIndex(recordIndex), kColSelect);
}

void ibDataViewMetaDiffModel::CascadeSelection(int recordIndex, bool selected)
{
	if (recordIndex < 0 || recordIndex >= static_cast<int>(m_records.size()))
		return;

	// Walk every record in the subtree rooted at recordIndex (records
	// are emitted in pre-order, so descendants are contiguous until
	// depth drops back to recordIndex's depth or below). For each:
	// - merge candidate → flip its m_selected to the new state
	// - group / Same / property container → keep state untouched but
	//   still emit ValueChanged so the aggregate checkbox re-renders
	//   under the new descendant states.
	const int rootDepth = m_records[recordIndex].m_depth;
	for (size_t i = recordIndex; i < m_records.size(); ++i) {
		if (i != static_cast<size_t>(recordIndex)
			&& m_records[i].m_depth <= rootDepth)
			break;
		if (m_records[i].IsMergeCandidate()) {
			m_selected[i] = selected;
		}
		ValueChanged(ItemFromIndex(static_cast<int>(i)), kColSelect);
	}

	// Climb back up and refresh ancestor checkbox aggregates so the
	// parent rows light up / clear in step with the cascade.
	int p = m_records[recordIndex].m_parentIndex;
	while (p >= 0) {
		ValueChanged(ItemFromIndex(p), kColSelect);
		p = m_records[p].m_parentIndex;
	}
}

bool ibDataViewMetaDiffModel::IsAllDescendantsSelected(int recordIndex) const
{
	if (recordIndex < 0 || recordIndex >= static_cast<int>(m_records.size()))
		return false;
	const int rootDepth = m_records[recordIndex].m_depth;
	bool anyCandidate = false;
	for (size_t i = recordIndex + 1; i < m_records.size(); ++i) {
		if (m_records[i].m_depth <= rootDepth)
			break;
		if (!m_records[i].IsMergeCandidate())
			continue;
		anyCandidate = true;
		if (!m_selected[i])
			return false;
	}
	return anyCandidate;
}

bool ibDataViewMetaDiffModel::HasMergeCandidateDescendant(int recordIndex) const
{
	if (recordIndex < 0 || recordIndex >= static_cast<int>(m_records.size()))
		return false;
	const int rootDepth = m_records[recordIndex].m_depth;
	for (size_t i = recordIndex + 1; i < m_records.size(); ++i) {
		if (m_records[i].m_depth <= rootDepth)
			break;
		if (m_records[i].IsMergeCandidate())
			return true;
	}
	return false;
}

void ibDataViewMetaDiffModel::SelectAllDifferences()
{
	for (size_t i = 0; i < m_records.size(); ++i) {
		if (m_records[i].IsMergeCandidate()) {
			m_selected[i] = true;
			ValueChanged(ItemFromIndex(static_cast<int>(i)), kColSelect);
		}
	}
}

void ibDataViewMetaDiffModel::ClearAllSelection()
{
	for (size_t i = 0; i < m_selected.size(); ++i) {
		if (m_selected[i]) {
			m_selected[i] = false;
			ValueChanged(ItemFromIndex(static_cast<int>(i)), kColSelect);
		}
	}
}

bool ibDataViewMetaDiffModel::HasSelection() const
{
	for (bool flag : m_selected) {
		if (flag) return true;
	}
	return false;
}

ibDataViewItem ibDataViewMetaDiffModel::GetRootItem() const
{
	if (m_records.empty())
		return ibDataViewItem();
	return ItemFromIndex(0);
}

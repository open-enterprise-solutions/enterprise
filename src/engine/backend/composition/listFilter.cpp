#include "backend/composition/listFilter.h"
#include "backend/composition/dataComposer.h"
#include "backend/model.h"             // ibValueModel::GetModelComposer — the FACADE resolves it lazily
#include "backend/compiler/typeCtor.h"

// ===========================================================================
//  ibValueFilterItem
// ===========================================================================

ibValueFilterItem::ibValueFilterItem()
	: ibValueDynamicMembers(ibValueTypes::TYPE_VALUE, false),
	  m_use(true), m_comparison(ibComparisonKind_Equal) {
	m_members.Bind(this, &ibValueFilterItem::FillMembers);
}

ibValueFilterItem::ibValueFilterItem(const wxString& field, ibComparisonKind comparison, const ibValue& value, bool use)
	: ibValueDynamicMembers(ibValueTypes::TYPE_VALUE, false),
	  m_use(use), m_field(field), m_comparison(comparison), m_value(value) {
	m_members.Bind(this, &ibValueFilterItem::FillMembers);
}

void ibValueFilterItem::FillMembers(ibMemberTable& helper) const {
	helper.AppendProp(wxT("Use"));
	helper.AppendProp(wxT("Field"));
	helper.AppendProp(wxT("Comparison"));
	helper.AppendProp(wxT("Value"));
}

bool ibValueFilterItem::Init(ibValue** paParams, const long lSizeArray) {
	// New FilterItem(field, comparison, value [, use])
	if (lSizeArray >= 1 && paParams[0] != nullptr)
		m_field = paParams[0]->GetString();
	if (lSizeArray >= 2 && paParams[1] != nullptr)
		m_comparison = paParams[1]->ConvertToEnumValue<ibComparisonKind>();
	if (lSizeArray >= 3 && paParams[2] != nullptr)
		m_value = *paParams[2];
	if (lSizeArray >= 4 && paParams[3] != nullptr)
		m_use = paParams[3]->GetBoolean();
	return true;
}

bool ibValueFilterItem::GetPropVal(const long lPropNum, ibValue& pvarPropVal) {
	switch (lPropNum) {
	case enUse:        pvarPropVal = m_use; return true;
	case enField:      pvarPropVal = m_field; return true;
	case enComparison: pvarPropVal = ibValue::CreateAndConvertEnumObjectRef<ibValueEnumComparisonKind>(m_comparison); return true;
	case enValue:      pvarPropVal = m_value; return true;
	}
	return false;
}

bool ibValueFilterItem::SetPropVal(const long lPropNum, const ibValue& varPropVal) {
	switch (lPropNum) {
	case enUse:        m_use = varPropVal.GetBoolean(); return true;
	case enField:      m_field = varPropVal.GetString(); return true;
	case enComparison: m_comparison = varPropVal.ConvertToEnumValue<ibComparisonKind>(); return true;
	case enValue:      m_value = varPropVal; return true;
	}
	return false;
}

wxString ibValueFilterItem::GetString() const {
	return m_field + wxT(" ") + ComparisonKindToOp(m_comparison);
}

// ===========================================================================
//  ibValueFilterList
// ===========================================================================

ibValueFilterList::ibValueFilterList()
	: ibValueDynamicMembers(ibValueTypes::TYPE_VALUE, false) {
	m_members.Bind(this, &ibValueFilterList::FillMembers);
}

ibValueFilterList::ibValueFilterList(ibValueModel& model, std::function<void()> onChange)
	: ibValueDynamicMembers(ibValueTypes::TYPE_VALUE, false), m_model(&model), m_onChange(std::move(onChange)) {
	m_members.Bind(this, &ibValueFilterList::FillMembers);
}

// FACADE: the model's composer is polymorphic + lazily created — resolve it on demand (by first USE the model
// is fully constructed, so GetModelComposer picks the right realisation). null model = BUFFER mode.
ibDataComposer* ibValueFilterList::Composer() const { return m_model != nullptr ? &m_model->GetModelComposer() : nullptr; }

void ibValueFilterList::FillMembers(ibMemberTable& helper) const {
	helper.AppendFunc(wxT("Add"),   3, wxT("Add(field, comparison, value)"));
	helper.AppendFunc(wxT("Count"),    wxT("Count()"));
	helper.AppendFunc(wxT("Get"),   1, wxT("Get(index)"));
	helper.AppendProc(wxT("Clear"));
}

bool   ibValueFilterList::IsEmpty() const { return Count() == 0; }
size_t ibValueFilterList::Count() const { const ibDataComposer* c = Composer(); return c != nullptr ? c->FilterCount() : m_items.size(); }

void ibValueFilterList::Clear() {
	if (ibDataComposer* c = Composer()) { c->ClearFilters(); if (m_onChange) m_onChange(); }
	else                                     m_items.clear();
}

ibValueFilterItem* ibValueFilterList::Add(const wxString& field, ibComparisonKind comparison, const ibValue& value, bool use) {
	// FACADE: write the composer (the store) IMMEDIATELY + fire the model refresh; hand the script a value item.
	if (ibDataComposer* c = Composer()) {
		if (use && !field.IsEmpty())
			c->Filter(field, ComparisonKindToOp(comparison), value);
		if (m_onChange) m_onChange();
		return new ibValueFilterItem(field, comparison, value, use);
	}
	// BUFFER: own storage (the dialog's transactional copy).
	ibValueFilterItem* item = new ibValueFilterItem(field, comparison, value, use);
	m_items.push_back(ibValuePtr<ibValueFilterItem>(item));
	return item;
}

ibValueFilterItem* ibValueFilterList::GetItem(size_t idx) const {
	// FACADE: build a transient item reflecting the composer's filter (script-owned). BUFFER: the stored item.
	if (const ibDataComposer* c = Composer()) {
		if (idx >= c->FilterCount()) return nullptr;
		wxString path, op; ibValue value;
		if (!c->GetFilterAt(idx, path, op, value)) return nullptr;
		return new ibValueFilterItem(path, OpToComparisonKind(op), value, true);
	}
	return idx < m_items.size() ? static_cast<ibValueFilterItem*>(m_items[idx]) : nullptr;
}

bool ibValueFilterList::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray) {
	switch (lMethodNum) {
	case enAdd: {
		const wxString field = (lSizeArray >= 1 && paParams[0] != nullptr) ? paParams[0]->GetString() : wxString();
		const ibComparisonKind cmp = (lSizeArray >= 2 && paParams[1] != nullptr)
			? paParams[1]->ConvertToEnumValue<ibComparisonKind>() : ibComparisonKind_Equal;
		const ibValue val = (lSizeArray >= 3 && paParams[2] != nullptr) ? *paParams[2] : ibValue();
		pvarRetValue = Add(field, cmp, val, true);
		return true;
	}
	case enCount:
		pvarRetValue = static_cast<signed int>(Count());
		return true;
	case enGet: {
		const size_t idx = (lSizeArray >= 1 && paParams[0] != nullptr) ? static_cast<size_t>(paParams[0]->GetInteger()) : 0;
		ibValueFilterItem* item = GetItem(idx);
		if (item == nullptr) return false;
		pvarRetValue = item;
		return true;
	}
	}
	return false;
}

bool ibValueFilterList::CallAsProc(const long lMethodNum, ibValue** paParams, const long lSizeArray) {
	switch (lMethodNum) {
	case enClear: Clear(); return true;
	}
	return false;
}

wxString ibValueFilterList::GetString() const {
	return wxString::Format(wxT("Filter(%u)"), (unsigned)Count());
}

// ===========================================================================
//  ibValueSortItem
// ===========================================================================

ibValueSortItem::ibValueSortItem()
	: ibValueDynamicMembers(ibValueTypes::TYPE_VALUE, false),
	  m_direction(ibSortDirection_Ascending) {
	m_members.Bind(this, &ibValueSortItem::FillMembers);
}

ibValueSortItem::ibValueSortItem(const wxString& field, ibSortDirection direction)
	: ibValueDynamicMembers(ibValueTypes::TYPE_VALUE, false),
	  m_field(field), m_direction(direction) {
	m_members.Bind(this, &ibValueSortItem::FillMembers);
}

void ibValueSortItem::FillMembers(ibMemberTable& helper) const {
	helper.AppendProp(wxT("Field"));
	helper.AppendProp(wxT("Direction"));
}

bool ibValueSortItem::Init(ibValue** paParams, const long lSizeArray) {
	// New SortItem(field [, direction])
	if (lSizeArray >= 1 && paParams[0] != nullptr)
		m_field = paParams[0]->GetString();
	if (lSizeArray >= 2 && paParams[1] != nullptr)
		m_direction = paParams[1]->ConvertToEnumValue<ibSortDirection>();
	return true;
}

bool ibValueSortItem::GetPropVal(const long lPropNum, ibValue& pvarPropVal) {
	switch (lPropNum) {
	case enField:     pvarPropVal = m_field; return true;
	case enDirection: pvarPropVal = ibValue::CreateAndConvertEnumObjectRef<ibValueEnumSortDirection>(m_direction); return true;
	}
	return false;
}

bool ibValueSortItem::SetPropVal(const long lPropNum, const ibValue& varPropVal) {
	switch (lPropNum) {
	case enField:     m_field = varPropVal.GetString(); return true;
	case enDirection: m_direction = varPropVal.ConvertToEnumValue<ibSortDirection>(); return true;
	}
	return false;
}

wxString ibValueSortItem::GetString() const {
	return m_field + (m_direction == ibSortDirection_Descending ? wxT(" DESC") : wxT(" ASC"));
}

// ===========================================================================
//  ibValueSortList
// ===========================================================================

ibValueSortList::ibValueSortList()
	: ibValueDynamicMembers(ibValueTypes::TYPE_VALUE, false) {
	m_members.Bind(this, &ibValueSortList::FillMembers);
}

ibValueSortList::ibValueSortList(ibValueModel& model, std::function<void()> onChange)
	: ibValueDynamicMembers(ibValueTypes::TYPE_VALUE, false), m_model(&model), m_onChange(std::move(onChange)) {
	m_members.Bind(this, &ibValueSortList::FillMembers);
}

ibDataComposer* ibValueSortList::Composer() const { return m_model != nullptr ? &m_model->GetModelComposer() : nullptr; }

void ibValueSortList::FillMembers(ibMemberTable& helper) const {
	helper.AppendFunc(wxT("Add"),   2, wxT("Add(field, direction)"));
	helper.AppendFunc(wxT("Count"),    wxT("Count()"));
	helper.AppendFunc(wxT("Get"),   1, wxT("Get(index)"));
	helper.AppendProc(wxT("Clear"));
}

bool   ibValueSortList::IsEmpty() const { return Count() == 0; }
size_t ibValueSortList::Count() const { const ibDataComposer* c = Composer(); return c != nullptr ? c->SortCount() : m_items.size(); }

void ibValueSortList::Clear() {
	if (ibDataComposer* c = Composer()) { c->ClearSorts(); if (m_onChange) m_onChange(); }
	else                                     m_items.clear();
}

ibValueSortItem* ibValueSortList::Add(const wxString& field, ibSortDirection direction) {
	// FACADE: write the composer (the store) IMMEDIATELY + fire the model refresh; hand the script a value item.
	if (ibDataComposer* c = Composer()) {
		if (!field.IsEmpty())
			c->Sort(field, direction == ibSortDirection_Ascending);
		if (m_onChange) m_onChange();
		return new ibValueSortItem(field, direction);
	}
	// BUFFER: own storage (the dialog's transactional copy).
	ibValueSortItem* item = new ibValueSortItem(field, direction);
	m_items.push_back(ibValuePtr<ibValueSortItem>(item));
	return item;
}

ibValueSortItem* ibValueSortList::GetItem(size_t idx) const {
	// FACADE: build a transient item reflecting the composer's sort (script-owned). BUFFER: the stored item.
	if (const ibDataComposer* c = Composer()) {
		if (idx >= c->SortCount()) return nullptr;
		wxString path; bool ascending = true;
		if (!c->GetSortAt(idx, path, ascending)) return nullptr;
		return new ibValueSortItem(path, ascending ? ibSortDirection_Ascending : ibSortDirection_Descending);
	}
	return idx < m_items.size() ? static_cast<ibValueSortItem*>(m_items[idx]) : nullptr;
}

bool ibValueSortList::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray) {
	switch (lMethodNum) {
	case enAdd: {
		const wxString field = (lSizeArray >= 1 && paParams[0] != nullptr) ? paParams[0]->GetString() : wxString();
		const ibSortDirection dir = (lSizeArray >= 2 && paParams[1] != nullptr)
			? paParams[1]->ConvertToEnumValue<ibSortDirection>() : ibSortDirection_Ascending;
		pvarRetValue = Add(field, dir);
		return true;
	}
	case enCount:
		pvarRetValue = static_cast<signed int>(Count());
		return true;
	case enGet: {
		const size_t idx = (lSizeArray >= 1 && paParams[0] != nullptr) ? static_cast<size_t>(paParams[0]->GetInteger()) : 0;
		ibValueSortItem* item = GetItem(idx);
		if (item == nullptr) return false;
		pvarRetValue = item;
		return true;
	}
	}
	return false;
}

bool ibValueSortList::CallAsProc(const long lMethodNum, ibValue** paParams, const long lSizeArray) {
	switch (lMethodNum) {
	case enClear: Clear(); return true;
	}
	return false;
}

wxString ibValueSortList::GetString() const {
	return wxString::Format(wxT("Order(%u)"), (unsigned)Count());
}

// ===========================================================================
//  ibValueGroupList
// ===========================================================================

ibValueGroupList::ibValueGroupList()
	: ibValueDynamicMembers(ibValueTypes::TYPE_VALUE, false) {
	m_members.Bind(this, &ibValueGroupList::FillMembers);
}

ibValueGroupList::ibValueGroupList(ibValueModel& model, std::function<void()> onChange)
	: ibValueDynamicMembers(ibValueTypes::TYPE_VALUE, false), m_model(&model), m_onChange(std::move(onChange)) {
	m_members.Bind(this, &ibValueGroupList::FillMembers);
}

ibDataComposer* ibValueGroupList::Composer() const { return m_model != nullptr ? &m_model->GetModelComposer() : nullptr; }

void ibValueGroupList::FillMembers(ibMemberTable& helper) const {
	helper.AppendFunc(wxT("Add"),   1, wxT("Add(field)"));
	helper.AppendFunc(wxT("Count"),    wxT("Count()"));
	helper.AppendFunc(wxT("Get"),   1, wxT("Get(index)"));
	helper.AppendProc(wxT("Clear"));
}

bool   ibValueGroupList::IsEmpty() const { return Count() == 0; }
size_t ibValueGroupList::Count() const { const ibDataComposer* c = Composer(); return c != nullptr ? c->GroupCount() : m_items.size(); }

wxString ibValueGroupList::GetField(size_t idx) const {
	if (const ibDataComposer* c = Composer()) {
		wxString f; ibQueryDimUnfold k = ibQueryDimUnfold::Elements;
		return c->GetGroupAt(idx, f, k) ? f : wxString();
	}
	return idx < m_items.size() ? m_items[idx].m_field : wxString();
}

ibQueryDimUnfold ibValueGroupList::GetKind(size_t idx) const {
	if (const ibDataComposer* c = Composer()) {
		wxString f; ibQueryDimUnfold k = ibQueryDimUnfold::Elements;
		return c->GetGroupAt(idx, f, k) ? k : ibQueryDimUnfold::Elements;
	}
	return idx < m_items.size() ? m_items[idx].m_kind : ibQueryDimUnfold::Elements;
}

void ibValueGroupList::Add(const wxString& field, ibQueryDimUnfold kind) {
	// FACADE: write the composer's grouping (the store) IMMEDIATELY + fire the model refresh. BUFFER: own storage.
	if (ibDataComposer* c = Composer()) {
		if (!field.IsEmpty())
			c->TotalBy(field, kind);
		if (m_onChange) m_onChange();
	}
	else
		m_items.push_back({ field, kind });
}

void ibValueGroupList::Clear() {
	if (ibDataComposer* c = Composer()) { c->ClearGroups(); if (m_onChange) m_onChange(); }
	else                                     m_items.clear();
}

bool ibValueGroupList::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray) {
	switch (lMethodNum) {
	case enAdd: {
		const wxString field = (lSizeArray >= 1 && paParams[0] != nullptr) ? paParams[0]->GetString() : wxString();
		Add(field);
		pvarRetValue = field;
		return true;
	}
	case enCount:
		pvarRetValue = static_cast<signed int>(Count());
		return true;
	case enGet: {
		const size_t idx = (lSizeArray >= 1 && paParams[0] != nullptr) ? static_cast<size_t>(paParams[0]->GetInteger()) : 0;
		if (idx >= Count()) return false;
		pvarRetValue = GetField(idx);
		return true;
	}
	}
	return false;
}

bool ibValueGroupList::CallAsProc(const long lMethodNum, ibValue** paParams, const long lSizeArray) {
	switch (lMethodNum) {
	case enClear: Clear(); return true;
	}
	return false;
}

wxString ibValueGroupList::GetString() const {
	return wxString::Format(wxT("Group(%u)"), (unsigned)Count());
}

// ===========================================================================
//  ibValueListSettings
// ===========================================================================

ibValueListSettings::ibValueListSettings()
	: ibValueDynamicMembers(ibValueTypes::TYPE_VALUE, false) {
	m_members.Bind(this, &ibValueListSettings::FillMembers);
	m_filter = new ibValueFilterList();
	m_order  = new ibValueSortList();
	m_group  = new ibValueGroupList();
}

// FACADE container — Filter / Order / Group are thin live wrappers over `composer` (the store); each mutation
// fires `onChange` (the owning model's refresh). The model owns ONE of these as its runtime/script settings.
ibValueListSettings::ibValueListSettings(ibValueModel& model, std::function<void()> onChange)
	: ibValueDynamicMembers(ibValueTypes::TYPE_VALUE, false) {
	m_members.Bind(this, &ibValueListSettings::FillMembers);
	m_filter = new ibValueFilterList(model, onChange);
	m_order  = new ibValueSortList(model, onChange);
	m_group  = new ibValueGroupList(model, onChange);
}

void ibValueListSettings::FillMembers(ibMemberTable& helper) const {
	helper.AppendProp(wxT("Filter"), true, false, wxNOT_FOUND);   // read-only collections
	helper.AppendProp(wxT("Order"),  true, false, wxNOT_FOUND);
	helper.AppendProp(wxT("Group"),  true, false, wxNOT_FOUND);
}

bool ibValueListSettings::GetPropVal(const long lPropNum, ibValue& pvarPropVal) {
	switch (lPropNum) {
	case enFilter: pvarPropVal = GetFilter(); return true;
	case enOrder:  pvarPropVal = GetOrder();  return true;
	case enGroup:  pvarPropVal = GetGroup();  return true;
	}
	return false;
}

// Object-level node save/load — called by the dynamic list's ReadData/WriteData so the
// settings persist on the form. TODO(spike): (de)serialize Filter/Order/Group items as
// child nodes — the seam is here; item-level round-trip is the remaining harden.
bool ibValueListSettings::ReadData(const ibDataNode& /*node*/)
{
	return true;
}

bool ibValueListSettings::WriteData(ibDataNode& /*node*/) const
{
	return true;
}

// ===========================================================================
//  Transactional bridge — composer (committed STORE) <-> ListSettings (edit BUFFER)
// ===========================================================================

// LOAD (dialog open): composer → buffer. Mirror the committed Filter / Sort / Group into the dialog's
// ListSettings so it shows the current state for editing. The buffer's value items carry the comparison KIND;
// the composer stores the op spelling, so reverse-map it (OpToComparisonKind).
void ibLoadSettingsFromComposer(ibValueListSettings* settings, const ibDataComposer& composer)
{
	if (settings == nullptr)
		return;
	if (ibValueFilterList* filter = settings->GetFilter()) {
		filter->Clear();
		for (size_t i = 0; i < composer.FilterCount(); ++i) {
			wxString path, op; ibValue value;
			if (composer.GetFilterAt(i, path, op, value))
				filter->Add(path, OpToComparisonKind(op), value, true);
		}
	}
	if (ibValueSortList* order = settings->GetOrder()) {
		order->Clear();
		for (size_t i = 0; i < composer.SortCount(); ++i) {
			wxString path; bool ascending = true;
			if (composer.GetSortAt(i, path, ascending))
				order->Add(path, ascending ? ibSortDirection_Ascending : ibSortDirection_Descending);
		}
	}
	if (ibValueGroupList* group = settings->GetGroup()) {
		group->Clear();
		for (size_t i = 0; i < composer.GroupCount(); ++i) {
			wxString path; ibQueryDimUnfold kind = ibQueryDimUnfold::Elements;
			if (composer.GetGroupAt(i, path, kind))
				group->Add(path, kind);
		}
	}
}

// COMMIT (dialog OK): buffer → composer. CLEAR the committed Filter / Sort / Group, then re-apply the buffer's
// items — the whole transaction lands atomically. Disabled (Use=false) filter lines are dropped (the composer
// has no use flag; an off line simply does not filter). Dot-walk fields → auto-JOIN on the door at lowering.
void ibCommitSettingsToComposer(ibDataComposer& composer, const ibValueListSettings* settings)
{
	if (settings == nullptr)
		return;
	composer.ClearFilters();
	composer.ClearSorts();
	composer.ClearGroups();
	if (const ibValueFilterList* filter = settings->GetFilter()) {
		for (size_t i = 0; i < filter->Count(); ++i) {
			const ibValueFilterItem* item = filter->GetItem(i);
			if (item == nullptr || !item->GetUse() || item->GetField().IsEmpty())
				continue;
			composer.Filter(item->GetField(), ComparisonKindToOp(item->GetComparison()), item->GetFilterValue());
		}
	}
	if (const ibValueSortList* order = settings->GetOrder()) {
		for (size_t i = 0; i < order->Count(); ++i) {
			const ibValueSortItem* item = order->GetItem(i);
			if (item == nullptr || item->GetField().IsEmpty())
				continue;
			composer.Sort(item->GetField(), item->IsAscending());
		}
	}
	if (const ibValueGroupList* group = settings->GetGroup()) {
		for (size_t i = 0; i < group->Count(); ++i) {
			if (group->GetField(i).IsEmpty())
				continue;
			// The grouping KIND drives it: HIERARCHY / HIERARCHYONLY unfolds the reference field's parent tree
			// (a hierarchical catalog); ELEMENTS folds rows by value. The ONE switch between a hierarchical and a flat view.
			composer.TotalBy(group->GetField(i), group->GetKind(i));
		}
	}
}

// ===========================================================================
//  Registration
// ===========================================================================

ENUM_TYPE_REGISTER(ibValueEnumComparisonKind, "ComparisonKind", enum_to_clsid("EN_CMPK"));
ENUM_TYPE_REGISTER(ibValueEnumSortDirection,  "SortDirection",  enum_to_clsid("EN_SDIR"));

VALUE_TYPE_REGISTER(ibValueFilterItem,    "FilterItem",   value_to_clsid("VL_FILTI"));
VALUE_TYPE_REGISTER(ibValueFilterList,    "FilterList",   value_to_clsid("VL_FILTL"));
VALUE_TYPE_REGISTER(ibValueSortItem,      "SortItem",     value_to_clsid("VL_SORTI"));
VALUE_TYPE_REGISTER(ibValueSortList,      "SortList",     value_to_clsid("VL_SORTL"));
VALUE_TYPE_REGISTER(ibValueGroupList,     "GroupList",    value_to_clsid("VL_GRPL"));
VALUE_TYPE_REGISTER(ibValueListSettings,  "ListSettings", value_to_clsid("VL_LSET"));

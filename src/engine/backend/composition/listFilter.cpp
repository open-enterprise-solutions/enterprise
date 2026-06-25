#include "backend/composition/listFilter.h"
#include "backend/composition/dataComposer.h"
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

void ibValueFilterList::FillMembers(ibMemberTable& helper) const {
	helper.AppendFunc(wxT("Add"),   3, wxT("Add(field, comparison, value)"));
	helper.AppendFunc(wxT("Count"),    wxT("Count()"));
	helper.AppendFunc(wxT("Get"),   1, wxT("Get(index)"));
	helper.AppendProc(wxT("Clear"));
}

ibValueFilterItem* ibValueFilterList::Add(const wxString& field, ibComparisonKind comparison, const ibValue& value, bool use) {
	ibValueFilterItem* item = new ibValueFilterItem(field, comparison, value, use);
	m_items.push_back(ibValuePtr<ibValueFilterItem>(item));
#ifdef DEBUG
	wxLogDebug(wxT("[ListSettings] Filter.Add: %s %s (count=%u)"),
		field, ComparisonKindToOp(comparison), (unsigned)m_items.size());
#endif
	return item;
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
	return wxString::Format(wxT("Filter(%u)"), (unsigned)m_items.size());
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

void ibValueSortList::FillMembers(ibMemberTable& helper) const {
	helper.AppendFunc(wxT("Add"),   2, wxT("Add(field, direction)"));
	helper.AppendFunc(wxT("Count"),    wxT("Count()"));
	helper.AppendFunc(wxT("Get"),   1, wxT("Get(index)"));
	helper.AppendProc(wxT("Clear"));
}

ibValueSortItem* ibValueSortList::Add(const wxString& field, ibSortDirection direction) {
	ibValueSortItem* item = new ibValueSortItem(field, direction);
	m_items.push_back(ibValuePtr<ibValueSortItem>(item));
	return item;
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
	return wxString::Format(wxT("Order(%u)"), (unsigned)m_items.size());
}

// ===========================================================================
//  ibValueGroupList
// ===========================================================================

ibValueGroupList::ibValueGroupList()
	: ibValueDynamicMembers(ibValueTypes::TYPE_VALUE, false) {
	m_members.Bind(this, &ibValueGroupList::FillMembers);
}

void ibValueGroupList::FillMembers(ibMemberTable& helper) const {
	helper.AppendFunc(wxT("Add"),   1, wxT("Add(field)"));
	helper.AppendFunc(wxT("Count"),    wxT("Count()"));
	helper.AppendFunc(wxT("Get"),   1, wxT("Get(index)"));
	helper.AppendProc(wxT("Clear"));
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
	return wxString::Format(wxT("Group(%u)"), (unsigned)m_fields.size());
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
//  ibApplyDynamicSettings — settings → composer (one source of truth)
// ===========================================================================

void ibApplyDynamicSettings(ibDataComposer& composer, const ibValueListSettings* settings)
{
	if (settings == nullptr)
		return;

	// Отбор → WHERE (value travels as an auto-&parameter; dot-walk → auto-JOIN).
	if (const ibValueFilterList* filter = settings->GetFilter()) {
		for (size_t i = 0; i < filter->Count(); ++i) {
			const ibValueFilterItem* item = filter->GetItem(i);
			if (item == nullptr || !item->GetUse() || item->GetField().IsEmpty())
				continue;
			composer.Filter(item->GetField(),
				ComparisonKindToOp(item->GetComparison()),
				item->GetFilterValue());
		}
	}

	// Сортировка → ORDER BY (call order).
	if (const ibValueSortList* order = settings->GetOrder()) {
		for (size_t i = 0; i < order->Count(); ++i) {
			const ibValueSortItem* item = order->GetItem(i);
			if (item == nullptr || item->GetField().IsEmpty())
				continue;
			composer.Sort(item->GetField(), item->IsAscending());
		}
	}

	// Группировка → TOTALS BY (row groups; full grouped display needs the tree model).
	if (const ibValueGroupList* group = settings->GetGroup()) {
		for (size_t i = 0; i < group->Count(); ++i) {
			const wxString field = group->GetField(i);
			if (field.IsEmpty())
				continue;
			composer.TotalBy(field);
		}
	}
}

// ===========================================================================
//  Registration
// ===========================================================================

ENUM_TYPE_REGISTER(ibValueEnumComparisonKind, "ComparisonKind", string_to_clsid("EN_CMPK"));
ENUM_TYPE_REGISTER(ibValueEnumSortDirection,  "SortDirection",  string_to_clsid("EN_SDIR"));

VALUE_TYPE_REGISTER(ibValueFilterItem,    "FilterItem",   string_to_clsid("VL_FILTI"));
VALUE_TYPE_REGISTER(ibValueFilterList,    "FilterList",   string_to_clsid("VL_FILTL"));
VALUE_TYPE_REGISTER(ibValueSortItem,      "SortItem",     string_to_clsid("VL_SORTI"));
VALUE_TYPE_REGISTER(ibValueSortList,      "SortList",     string_to_clsid("VL_SORTL"));
VALUE_TYPE_REGISTER(ibValueGroupList,     "GroupList",    string_to_clsid("VL_GRPL"));
VALUE_TYPE_REGISTER(ibValueListSettings,  "ListSettings", string_to_clsid("VL_LSET"));

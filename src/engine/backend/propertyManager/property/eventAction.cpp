#include "eventAction.h"
#include "backend/propertyManager/property/variant/variantAction.h"
#include "backend/serialize/dataBuilder.h"   // ibDataValue — node value (Binary, transitional)

// get property for grid
wxObject* (*ibEventAction::ms_propertyEventAction)(const wxString&, const wxString&, const wxPGChoices&, const wxVariant&) = nullptr;

//////////////////////////////////////////////////////////////////

wxVariantData* ibEventAction::CreateVariantData(const ibPropertyObject* property, const ibActionDescription& act) const
{
	return new ibVariantDataAction(act);
}

////////////////////////////////////////////////////////////////////////

ibActionID ibEventAction::GetValueAsInteger() const {
	const ibActionID sel = GetValueAsActionDesc().GetSystemAction();
	if (m_functor->Invoke(const_cast<ibEventAction*>(this))) {
		for (unsigned int idx = 0; idx < m_listPropValue.GetItemCount(); idx++) {
			if (m_listPropValue.GetItemId(idx) == sel) {
				return sel;
			}
		}
	}
	return wxNOT_FOUND;
}

wxString ibEventAction::GetValueAsString() const
{
	return GetValueAsActionDesc().GetCustomAction();
}

ibActionDescription& ibEventAction::GetValueAsActionDesc() const
{
	return get_cell_variant<ibVariantDataAction>()->GetValueAsActionDesc();
}

void ibEventAction::SetValue(const ibActionDescription& val)
{
	m_propValue = CreateVariantData(m_owner, val);
}

////////////////////////////////////////////////////////////////////////

//base property for "action"
bool ibEventAction::SetDataValue(const ibValue& varPropVal)
{
	if (!m_functor->Invoke(this))
		return false;
	for (unsigned int idx = 0; idx < m_listPropValue.GetItemCount(); idx++) {
		if (m_listPropValue.GetItemValue(idx) == varPropVal) {
			SetValue(m_listPropValue.GetItemId(idx));
			return true;
		}
	}
	return false;
}

bool ibEventAction::GetDataValue(ibValue& pvarPropVal) const
{
	if (!m_functor->Invoke(const_cast<ibEventAction*>(this)))
		return false;
	for (unsigned int idx = 0; idx < m_listPropValue.GetItemCount(); idx++) {
		if (m_listPropValue.GetItemId(idx) == GetValueAsInteger()) {
			pvarPropVal = m_listPropValue.GetItemValue(idx);
			return true;
		}
	}
	return false;
}

// node form: a Child { Action: <system id>, Name: <custom handler> }.
bool ibActionDescriptionMemory::ReadNode(const ibDataValue& value, ibActionDescription& actionDesc)
{
	const std::shared_ptr<ibDataNode>& root = value.AsChild();
	if (!root)
		return false;
	actionDesc.m_lAction = root->GetValue<s32>(wxT("Action"));
	actionDesc.m_strAction = root->GetValue<wxString>(wxT("Name"));
	return true;
}

bool ibActionDescriptionMemory::WriteNode(ibDataValue& value, const ibActionDescription& actionDesc)
{
	auto root = std::make_shared<ibDataNode>();
	root->SetValue(wxT("Action"), (s32)actionDesc.m_lAction);
	root->SetValue(wxT("Name"),   actionDesc.m_strAction);
	value = ibDataValue::Child(root);
	return true;
}

bool ibEventAction::ReadNodeValue(const ibDataValue& value)
{
	return ibActionDescriptionMemory::ReadNode(value, GetValueAsActionDesc());
}

bool ibEventAction::WriteNodeValue(ibDataValue& value) const
{
	return ibActionDescriptionMemory::WriteNode(value, GetValueAsActionDesc());
}

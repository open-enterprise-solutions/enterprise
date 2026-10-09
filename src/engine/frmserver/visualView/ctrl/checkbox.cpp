#include "widgets.h"
#include "core/serialize/dataBuilder.h"   // ibDataNode (control -> node)


//****************************************************************************

#include "form.h"
#include "backend/metaData.h"

ibSourceObject* ibValueCheckbox::GetSourceObject() const
{
	return m_formOwner != nullptr ?
		m_formOwner->GetSourceObject() : nullptr;
}

bool ibValueCheckbox::GetSourceList(std::vector<ibBackendFormAttributeValue*>& out) const
{
	return m_formOwner != nullptr ? m_formOwner->GetSourceList(GetFilterSourceDataType(), out) : false;
}

//****************************************************************************
//*                              Checkbox                                    *
//****************************************************************************

enum prop {
	eControlValue,
};

ibValueCheckbox::ibValueCheckbox() : ibValueWindow(), ibTypeControlFactory()//(ibValueTypes::TYPE_BOOLEAN)
{
	m_members.Bind(this, &ibValueCheckbox::FillControlMembers);
}

const ibMetaData* ibValueCheckbox::GetMetaData() const
{
	return m_formOwner != nullptr ?
		m_formOwner->GetMetaData() : nullptr;
}

void ibValueCheckbox::FillControlMembers(ibMemberTable& helper) const
{
	helper.AppendProp(wxT("Value"), eControlValue, eControl);
}

bool ibValueCheckbox::SetPropVal(const long lPropNum, const ibValue& varPropVal)
{
	const long lPropAlias = m_members.GetPropAlias(lPropNum);
	if (lPropAlias == eControl) {
		const long lPropData = m_members.GetPropData(lPropNum);
		if (lPropData == eControlValue) {
			SetControlValue(varPropVal);
		}
	}

	return ibValueFrame::SetPropVal(lPropNum, varPropVal);
}

bool ibValueCheckbox::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	const long lPropAlias = m_members.GetPropAlias(lPropNum);
	if (lPropAlias == eControl) {
		const long lPropData = m_members.GetPropData(lPropNum);
		if (lPropData == eControlValue) {
			return GetControlValue(pvarPropVal);
		}
	}
	return ibValueFrame::GetPropVal(lPropNum, pvarPropVal);
}

wxString ibValueCheckbox::GetControlTitle() const
{
	if (!m_propertyTitle->IsEmptyProperty()) {
		return m_propertyTitle->GetValueAsTranslateString();
	}
	else if (!m_propertySource->IsEmptyProperty()) {
		const ibBackendAbstractColumn* column = GetSourceAbstractColumn();
		if (column != nullptr)   // null when the bound field is gone / whole-attribute binding
			return column->GetSynonym();
	}
	return wxEmptyString;
}

bool ibValueCheckbox::IsReadOnly() const
{
	// A dotted reference, a view-only form or a read-only source cannot be written. Unbound = writable.
	const bool writableBinding = m_propertySource->IsEmptyProperty()
		|| (m_formOwner != nullptr && m_formOwner->IsWritableBinding(m_propertySource->GetValueAsSourceDesc()));
	return !writableBinding;
}

#include "backend/appData.h"

void ibValueCheckbox::OnUpdate(ibDataNode& state, ibVisualHost* host)
{
	ibValueWindow::OnUpdate(state, host);

	state.SetValue(wxT("Caption"), GetControlTitle());

	// The value from the source, through the binding path (the source walks it). Not in the designer:
	// there the box shows no value.
	if (!appData->DesignerMode()) {
		ibValue value;
		GetControlValue(value);
		state.SetValue(wxT("Checked"), value.GetBoolean());
	}

	// A read-only binding makes the box READ-ONLY: value shown and focusable, but a click / space can't
	// toggle it (OnClickedCheckbox refuses the change) — NOT Enabled off, which would grey it out
	// (availability, for action buttons, not data controls).
	state.SetValue(wxT("ReadOnly"), IsReadOnly());
}

//*******************************************************************
//*							 Control value	                        *
//*******************************************************************

bool ibValueCheckbox::GetControlValue(ibValue& pvarControlVal) const
{
	if (!m_propertySource->IsEmptyProperty() && m_formOwner != nullptr &&
		m_formOwner->GetValueByAttributePath(m_propertySource->GetValueAsSourceDesc(), pvarControlVal)) {
		return true;   // attribute-table / dotted path -> read-only walk
	}

	pvarControlVal = ibTypeControlFactory::AdjustValue(m_selValue);
	return true;
}

#include "backend/system/value/valueType.h"

bool ibValueCheckbox::SetControlValue(const ibValue& varControlVal)
{
	if (!m_propertySource->IsEmptyProperty() && m_formOwner != nullptr) {
		// Form writes only a direct-field binding (head selects the attribute); a
		// dotted reference path is read-only → no-op.
		m_formOwner->SetValueByAttributePath(m_propertySource->GetValueAsSourceDesc(), varControlVal);
	}

	// The next OnUpdate reads the value back and writes it as Checked — nothing is pushed.
	m_selValue = varControlVal.GetBoolean();

	return true;
}

//*******************************************************************
//*							 Data	                                *
//*******************************************************************

bool ibValueCheckbox::ReadData(const ibDataNode& node)
{
	m_propertyTitle->SetNodeValue(node.GetProperty(m_propertyTitle->GetName()));
	m_propertyTitleLocation->SetNodeValue(node.GetProperty(m_propertyTitleLocation->GetName()));
	m_propertySource->SetNodeValue(node.GetProperty(m_propertySource->GetName()));

	//events
	m_onCheckboxClicked->SetNodeValue(node.GetProperty(m_onCheckboxClicked->GetName()));
	return ibValueWindow::ReadData(node);
}

bool ibValueCheckbox::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertyTitle->GetName(), m_propertyTitle->GetNodeValue());
	node.SetProperty(m_propertyTitleLocation->GetName(), m_propertyTitleLocation->GetNodeValue());
	node.SetProperty(m_propertySource->GetName(), m_propertySource->GetNodeValue());

	//events
	node.SetProperty(m_onCheckboxClicked->GetName(), m_onCheckboxClicked->GetNodeValue());
	return ibValueWindow::WriteData(node);
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

CONTROL_TYPE_REGISTER(ibValueCheckbox, "Checkbox", "Widget", g_controlCheckboxCLSID);

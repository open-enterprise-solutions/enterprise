#include "widgets.h"
#include "form.h"                         // ibValueForm — the bound read and write go through the owning form
#include "backend/srcDataObject.h"        // ibSourceDataObject IS-A ibSourceObject (the upcast below)
#include "backend/appData.h"
#include "core/serialize/dataBuilder.h"   // ibDataNode (control -> node)
#include "backend/compiler/procUnit.h"
#include "backend/system/systemManager.h"   // ibValueSystemFunction::Message — a value not read, said


//****************************************************************************
//*                             Radiobutton                                  *
//****************************************************************************

ibValueRadioButton::ibValueRadioButton() : ibValueWindow(), ibTypeControlFactory()
{
}

//****************************************************************************
//*                      the optional source behind it                       *
//****************************************************************************

ibSourceObject* ibValueRadioButton::GetSourceObject() const
{
	return m_formOwner != nullptr ? m_formOwner->GetSourceObject() : nullptr;
}

bool ibValueRadioButton::GetSourceList(std::vector<ibBackendFormAttributeValue*>& out) const
{
	return m_formOwner != nullptr ? m_formOwner->GetSourceList(GetFilterSourceDataType(), out) : false;
}

const ibMetaData* ibValueRadioButton::GetMetaData() const
{
	return ibValueControl::GetMetaData();
}

ibString ibValueRadioButton::GetControlTitle() const
{
	return GetCaption();
}

bool ibValueRadioButton::GetControlValue(ibValue& pvarControlVal) const
{
	if (m_propertySource->IsEmptyProperty() || m_formOwner == nullptr)
		return false;
	return m_formOwner->GetValueByAttributePath(m_propertySource->GetValueAsSourceDesc(), pvarControlVal);
}

bool ibValueRadioButton::IsReadOnly() const
{
	// A dotted reference, a view-only form or a read-only source cannot be written. Unbound = writable.
	const bool writableBinding = m_propertySource->IsEmptyProperty()
		|| (m_formOwner != nullptr && m_formOwner->IsWritableBinding(m_propertySource->GetValueAsSourceDesc()));
	return !writableBinding;
}

bool ibValueRadioButton::IsSelected() const
{
	if (m_propertySource->IsEmptyProperty())
		return m_propertySelected->GetValueAsBoolean();

	// The source holds the value it stands for. A button with no value stands for none, and an empty source is not
	// its value: nothing is selected until something is chosen.
	ibValue value;
	if (m_propertyChoiceValue->IsEmptyProperty() || !GetControlValue(value))
		return false;
	return value == GetChoiceValue();
}

void ibValueRadioButton::OnUpdate(ibDataNode& state, ibVisualHost* host)
{
	ibValueWindow::OnUpdate(state, host);
	state.SetValue(ibProtocol::ibForm::ibState::Caption, GetCaption());
	// Not in the designer for a bound one: there it shows no value, as a bound checkbox does.
	state.SetValue(ibProtocol::ibForm::ibState::Selected,
		m_propertySource->IsEmptyProperty() || !appData->DesignerMode() ? IsSelected() : false);
	state.SetValue(ibProtocol::ibForm::ibState::ReadOnly, IsReadOnly());
}

//*******************************************************************
//*                             Property                            *
//*******************************************************************

void ibValueRadioButton::ReadChoiceValue(const ibDataValue& value)
{
	// ⭐ A VALUE THAT IS NOT READ IS SAID, with the button it belonged to. The form opens all the same — one button
	// of a switch must not take the form with it — but a button standing for no value can never be selected, and
	// without this line the person sees a switch that stopped working and nothing to say why.
	ibString reason;
	try {
		if (value.IsEmpty() || m_propertyChoiceValue->SetNodeValue(value))
			return;
	}
	catch (const ibCoreException& err) {
		reason = err.GetErrorDescription();
	}
	m_propertyChoiceValue->SetValue(ibValue());
	ibValueSystemFunction::Message(reason.IsEmpty()
		? ibString::Format(_("The radio button \"%s\" stands for a value that could not be read: it stands for none now."), GetControlName())
		: ibString::Format(_("The radio button \"%s\" stands for a value that could not be read (%s): it stands for none now."), GetControlName(), reason),
		ibStatusMessage::ibStatusMessage_Warning);
}

bool ibValueRadioButton::ReadData(const ibDataNode& node)
{
	// The caption and the starting selection are the radio button's own, so they travel with it - without
	// them every saved radio button came back as "Radio button", unselected.
	m_propertyTitle->SetNodeValue(node.GetProperty(m_propertyTitle->GetName()));
	m_propertySelected->SetNodeValue(node.GetProperty(m_propertySelected->GetName()));
	// The source first: the value it stands for is read as a value of the source's type.
	m_propertySource->SetNodeValue(node.GetProperty(m_propertySource->GetName()));
	ReadChoiceValue(node.GetProperty(m_propertyChoiceValue->GetName()));

	//events
	m_onChange->SetNodeValue(node.GetProperty(m_onChange->GetName()));
	return ibValueWindow::ReadData(node);
}

bool ibValueRadioButton::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertyTitle->GetName(), m_propertyTitle->GetNodeValue());
	node.SetProperty(m_propertySelected->GetName(), m_propertySelected->GetNodeValue());
	node.SetProperty(m_propertySource->GetName(), m_propertySource->GetNodeValue());
	node.SetProperty(m_propertyChoiceValue->GetName(), m_propertyChoiceValue->GetNodeValue());

	//events
	node.SetProperty(m_onChange->GetName(), m_onChange->GetNodeValue());
	return ibValueWindow::WriteData(node);
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

CONTROL_TYPE_REGISTER(ibValueRadioButton, "Radiobutton", "Widget", control_to_clsid("CT_RDBT"));

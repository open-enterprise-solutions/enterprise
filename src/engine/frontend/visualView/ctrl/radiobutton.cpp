
#include "widgets.h"
#include "core/serialize/dataBuilder.h"   // ibDataNode (control -> node)
#include "backend/compiler/procUnit.h"
#include "form.h"
#include "backend/metaData.h"


//****************************************************************************
//*                             Radiobutton                                  *
//****************************************************************************

ibValueRadioButton::ibValueRadioButton() : ibValueWindow(), ibTypeControlFactory()
{
}

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
	return m_formOwner != nullptr ? m_formOwner->GetMetaData() : nullptr;
}

wxObject* ibValueRadioButton::Create(wxWindow* wxparent, ibVisualHost *visualHost) 
{
	wxRadioButton *radioButton = new wxRadioButton(wxparent, wxID_ANY,
		m_propertyTitle->GetValueAsTranslateString(),
		wxDefaultPosition,
		wxDefaultSize);
	radioButton->Bind(wxEVT_RADIOBUTTON, &ibValueRadioButton::OnRadioSelected, this);

	return radioButton;
}

void ibValueRadioButton::OnCreated(wxObject* wxobject, wxWindow* wxparent, ibVisualHost *visualHost, bool firstCreated)
{
}

void ibValueRadioButton::Update(wxObject* wxobject, ibVisualHost *visualHost)
{
	wxRadioButton *radioButton = dynamic_cast<wxRadioButton *>(wxobject);

	if (radioButton != nullptr) {
		bool selected = m_propertySelected->GetValueAsBoolean() != false;
		if (!m_propertySource->IsEmptyProperty() && m_formOwner != nullptr
			&& !m_propertyChoiceValue->GetValueAsString().IsEmpty()) {
			ibValue current;
			if (m_formOwner->GetValueByAttributePath(m_propertySource->GetValueAsSourceDesc(), current))
				selected = current.GetString() == m_propertyChoiceValue->GetValueAsString();
		}
		radioButton->SetLabel(m_propertyTitle->GetValueAsTranslateString());
		radioButton->SetValue(selected);
	}

	UpdateWindow(radioButton);
}

void ibValueRadioButton::Cleanup(wxObject* obj, ibVisualHost *visualHost)
{
}

//*******************************************************************
//*                             Property                            *
//*******************************************************************

void ibValueRadioButton::OnRadioSelected(wxCommandEvent& event)
{
	m_propertySelected->SetValue(true);
	if (!m_propertySource->IsEmptyProperty() && m_formOwner != nullptr) {
		m_formOwner->SetValueByAttributePath(m_propertySource->GetValueAsSourceDesc(),
			ibValue(m_propertyChoiceValue->GetValueAsString()));
		m_formOwner->RefreshForm();
	}
	event.Skip();
}

bool ibValueRadioButton::ReadData(const ibDataNode& node)
{
	const bool ok = ibValueWindow::ReadData(node);
	m_propertySource->SetNodeValue(node.GetProperty(m_propertySource->GetName()));
	m_propertyChoiceValue->SetNodeValue(node.GetProperty(m_propertyChoiceValue->GetName()));
	return ok;
}

bool ibValueRadioButton::WriteData(ibDataNode& node) const
{
	const bool ok = ibValueWindow::WriteData(node);
	node.SetProperty(m_propertySource->GetName(), m_propertySource->GetNodeValue());
	node.SetProperty(m_propertyChoiceValue->GetName(), m_propertyChoiceValue->GetNodeValue());
	return ok;
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

CONTROL_TYPE_REGISTER(ibValueRadioButton, "Radiobutton", "Widget", control_to_clsid("CT_RDBT"));

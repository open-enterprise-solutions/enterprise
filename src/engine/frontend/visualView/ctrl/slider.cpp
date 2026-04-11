
#include "widgets.h"
#include "backend/compiler/procUnit.h"

wxIMPLEMENT_DYNAMIC_CLASS(ibValueSlider, ibValueWindow)

//****************************************************************************
//*                             Slider                                       *
//****************************************************************************

ibValueSlider::ibValueSlider() : ibValueWindow()
{
}

wxObject* ibValueSlider::Create(wxWindow* wxparent, ibVisualHost* visualHost)
{
	wxSlider* slider = new wxSlider(wxparent, wxID_ANY,
		m_propertyValue->GetValueAsInteger(),
		m_propertyMinValue->GetValueAsInteger(),
		m_propertyMaxValue->GetValueAsInteger(),
		wxDefaultPosition,
		wxDefaultSize,
		m_propertyOrient->GetValueAsInteger()
	);

	return slider;
}

void ibValueSlider::OnCreated(wxObject* wxobject, wxWindow* wxparent, ibVisualHost* visualHost, bool firstСreated)
{
}

void ibValueSlider::Update(wxObject* wxobject, ibVisualHost* visualHost)
{
	wxSlider* slider = dynamic_cast<wxSlider*>(wxobject);

	if (slider != nullptr) {

		wxWindow* winParent = slider->GetParent();
		bool isShown = slider->IsShown();
		if (isShown) slider->Hide();
		slider->SetParent(nullptr); winParent->RemoveChild(slider);
		slider->DissociateHandle();
		slider->Create(winParent, wxID_ANY,
			m_propertyValue->GetValueAsInteger(),
			m_propertyMinValue->GetValueAsInteger(),
			m_propertyMaxValue->GetValueAsInteger(),
			wxDefaultPosition,
			wxDefaultSize,
			m_propertyOrient->GetValueAsInteger()
		);
		slider->Show(isShown);	
	}

	UpdateWindow(slider);
}

void ibValueSlider::OnUpdated(wxObject* wxobject, wxWindow* wxparent, ibVisualHost* visualHost)
{
}

void ibValueSlider::Cleanup(wxObject* obj, ibVisualHost* visualHost)
{
}

//*******************************************************************
//*                           Property                              *
//*******************************************************************

bool ibValueSlider::LoadData(ibReaderMemory& reader)
{
	m_propertyMinValue->SetValue(reader.r_s32());
	m_propertyMaxValue->SetValue(reader.r_s32());
	m_propertyValue->SetValue(reader.r_s32());
	m_propertyOrient->SetValue(reader.r_s32());
	return ibValueWindow::LoadData(reader);
}

bool ibValueSlider::SaveData(ibWriterMemory writer)
{
	writer.w_s32(m_propertyMinValue->GetValueAsInteger());
	writer.w_s32(m_propertyMaxValue->GetValueAsInteger());
	writer.w_s32(m_propertyValue->GetValueAsInteger());
	writer.w_s32(m_propertyOrient->GetValueAsInteger());
	return ibValueWindow::SaveData(writer);
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

CONTROL_TYPE_REGISTER(ibValueSlider, "Slider", "Widget", string_to_clsid("CT_SLID"));
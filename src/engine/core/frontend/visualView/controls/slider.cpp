
#include "widgets.h"
#include "core/compiler/procUnit.h"

wxIMPLEMENT_DYNAMIC_CLASS(CValueSlider, IValueWindow)

//****************************************************************************
//*                             Slider                                       *
//****************************************************************************

CValueSlider::CValueSlider() : IValueWindow()
{
}

wxObject* CValueSlider::Create(wxWindow* wxparent, IVisualHost* visualHost)
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

void CValueSlider::OnCreated(wxObject* wxobject, wxWindow* wxparent, IVisualHost* visualHost, bool first—reated)
{
}

void CValueSlider::Update(wxObject* wxobject, IVisualHost* visualHost)
{
	wxSlider* slider = dynamic_cast<wxSlider*>(wxobject);

	if (slider != NULL) {

		wxWindow* winParent = slider->GetParent();
		bool isShown = slider->IsShown();
		if (isShown) slider->Hide();
		slider->SetParent(NULL); winParent->RemoveChild(slider);
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

void CValueSlider::OnUpdated(wxObject* wxobject, wxWindow* wxparent, IVisualHost* visualHost)
{
}

void CValueSlider::Cleanup(wxObject* obj, IVisualHost* visualHost)
{
}

//*******************************************************************
//*                           Property                              *
//*******************************************************************

bool CValueSlider::LoadData(CMemoryReader& reader)
{
	m_propertyMinValue->SetValue(reader.r_s32());
	m_propertyMaxValue->SetValue(reader.r_s32());
	m_propertyValue->SetValue(reader.r_s32());
	m_propertyOrient->SetValue(reader.r_s32());
	return IValueWindow::LoadData(reader);
}

bool CValueSlider::SaveData(CMemoryWriter& writer)
{
	writer.w_s32(m_propertyMinValue->GetValueAsInteger());
	writer.w_s32(m_propertyMaxValue->GetValueAsInteger());
	writer.w_s32(m_propertyValue->GetValueAsInteger());
	writer.w_s32(m_propertyOrient->GetValueAsInteger());
	return IValueWindow::SaveData(writer);
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

CONTROL_VALUE_REGISTER(CValueSlider, "slider", "widget", TEXT2CLSID("CT_SLID"));
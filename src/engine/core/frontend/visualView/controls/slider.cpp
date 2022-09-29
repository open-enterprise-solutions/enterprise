
#include "widgets.h"
#include "compiler/procUnit.h"

wxIMPLEMENT_DYNAMIC_CLASS(CValueSlider, IValueWindow)

//****************************************************************************
//*                             Slider                                       *
//****************************************************************************

CValueSlider::CValueSlider() : IValueWindow()
{
}

wxObject* CValueSlider::Create(wxObject* parent, IVisualHost* visualHost)
{
	wxSlider* m_slider = new wxSlider((wxWindow*)parent, wxID_ANY,
		m_propertyValue->GetValueAsInteger(),
		m_propertyMinValue->GetValueAsInteger(),
		m_propertyMaxValue->GetValueAsInteger(),
		wxDefaultPosition,
		wxDefaultSize,
		wxSL_HORIZONTAL);

	return m_slider;
}

void CValueSlider::OnCreated(wxObject* wxobject, wxWindow* wxparent, IVisualHost* visualHost, bool first—reated)
{
}

void CValueSlider::Update(wxObject* wxobject, IVisualHost* visualHost)
{
	wxSlider* slider = dynamic_cast<wxSlider*>(wxobject);

	if (slider != NULL) {
		slider->SetMin(m_propertyMinValue->GetValueAsInteger());
		slider->SetMax(m_propertyMaxValue->GetValueAsInteger());
		slider->SetValue(m_propertyValue->GetValueAsInteger());
	}

	UpdateWindow(slider);
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
	return IValueWindow::LoadData(reader);
}

bool CValueSlider::SaveData(CMemoryWriter& writer)
{
	writer.w_s32(m_propertyMinValue->GetValueAsInteger());
	writer.w_s32(m_propertyMaxValue->GetValueAsInteger());
	writer.w_s32(m_propertyValue->GetValueAsInteger());
	return IValueWindow::SaveData(writer);
}
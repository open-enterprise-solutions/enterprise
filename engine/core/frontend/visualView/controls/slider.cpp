
#include "widgets.h"
#include "compiler/procUnit.h"

wxIMPLEMENT_DYNAMIC_CLASS(CValueSlider, IValueWindow)

//****************************************************************************
//*                             Slider                                       *
//****************************************************************************

CValueSlider::CValueSlider() : IValueWindow()
{
	PropertyContainer *categoryButton = IObjectBase::CreatePropertyContainer("Slider");

	//property
	categoryButton->AddProperty("name", PropertyType::PT_WXNAME);
	categoryButton->AddProperty("minvalue", PropertyType::PT_INT);
	categoryButton->AddProperty("maxvalue", PropertyType::PT_INT);
	categoryButton->AddProperty("value", PropertyType::PT_WXSTRING);

	//category 
	m_category->AddCategory(categoryButton);
}

wxObject* CValueSlider::Create(wxObject* parent, IVisualHost *visualHost)
{
	wxSlider *m_slider = new wxSlider((wxWindow *)parent, wxID_ANY,
		m_value,
		m_minValue,
		m_maxValue,
		m_pos,
		m_size,
		m_style | m_window_style | m_window_style);

	return m_slider;
}

void CValueSlider::OnCreated(wxObject* wxobject, wxWindow* wxparent, IVisualHost *visualHost, bool first—reated)
{
}

void CValueSlider::Update(wxObject* wxobject, IVisualHost *visualHost)
{
	wxSlider *m_slider = dynamic_cast<wxSlider *>(wxobject);

	if (m_slider)
	{
		m_slider->SetMin(m_minValue);
		m_slider->SetMax(m_maxValue);
		m_slider->SetValue(m_value);
	}

	UpdateWindow(m_slider);
}


void CValueSlider::Cleanup(wxObject* obj, IVisualHost *visualHost)
{
}

//*******************************************************************
//*                           Property                              *
//*******************************************************************

bool CValueSlider::LoadData(CMemoryReader &reader)
{
	m_minValue = reader.r_s32();
	m_maxValue = reader.r_s32();
	m_value = reader.r_s32();
	return IValueWindow::LoadData(reader);
}

bool CValueSlider::SaveData(CMemoryWriter &writer)
{
	writer.w_s32(m_minValue);
	writer.w_s32(m_maxValue);
	writer.w_s32(m_value);
	return IValueWindow::SaveData(writer);
}

//*******************************************************************
//*                           Property                              *
//*******************************************************************


void CValueSlider::ReadProperty()
{
	IValueWindow::ReadProperty();

	IObjectBase::SetPropertyValue("name", m_controlName);
	IObjectBase::SetPropertyValue("minvalue", m_minValue);
	IObjectBase::SetPropertyValue("maxvalue", m_maxValue);
	IObjectBase::SetPropertyValue("value", m_value);
}

void CValueSlider::SaveProperty()
{
	IValueWindow::SaveProperty();

	IObjectBase::GetPropertyValue("name", m_controlName);
	IObjectBase::GetPropertyValue("minvalue", m_minValue);
	IObjectBase::GetPropertyValue("maxvalue", m_maxValue);
	IObjectBase::GetPropertyValue("value", m_value);
}
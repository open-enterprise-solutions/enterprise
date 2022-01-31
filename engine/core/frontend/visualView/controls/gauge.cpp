
#include "widgets.h"
#include "compiler/procUnit.h"

wxIMPLEMENT_DYNAMIC_CLASS(CValueGauge, IValueWindow)

//****************************************************************************
//*                             Gauge                                        *
//****************************************************************************

CValueGauge::CValueGauge() : IValueWindow(), m_range(100), m_value(30)
{
	PropertyContainer *categoryButton = IObjectBase::CreatePropertyContainer("Gauge");

	//property
	categoryButton->AddProperty("name", PropertyType::PT_WXNAME);
	categoryButton->AddProperty("range", PropertyType::PT_INT);
	categoryButton->AddProperty("value", PropertyType::PT_INT);

	//category 
	m_category->AddCategory(categoryButton);
}

wxObject* CValueGauge::Create(wxObject* parent, IVisualHost *visualHost)
{
	wxGauge *m_gauge = new wxGauge((wxWindow *)parent, wxID_ANY,
		m_range,
		m_pos,
		m_size,
		m_style | m_window_style);

	return m_gauge;
}

void CValueGauge::OnCreated(wxObject* wxobject, wxWindow* wxparent, IVisualHost *visualHost, bool first—reated)
{
}

void CValueGauge::Update(wxObject* wxobject, IVisualHost *visualHost)
{
	wxGauge *m_gauge = dynamic_cast<wxGauge *>(wxobject);

	if (m_gauge) {
		m_gauge->SetRange(m_range);
		m_gauge->SetValue(m_value);
	}

	UpdateWindow(m_gauge);
}

void CValueGauge::Cleanup(wxObject* obj, IVisualHost *visualHost)
{
}

//*******************************************************************
//*								Data                                *
//*******************************************************************

bool CValueGauge::LoadData(CMemoryReader &reader)
{
	m_range = reader.r_s32();
	m_value = reader.r_s32();
	return IValueWindow::LoadData(reader);
}

bool CValueGauge::SaveData(CMemoryWriter &writer)
{
	writer.w_s32(m_range);
	writer.w_s32(m_value);
	return IValueWindow::SaveData(writer);
}

//*******************************************************************
//*							  Property                              *
//*******************************************************************

void CValueGauge::ReadProperty()
{
	IValueWindow::ReadProperty();

	IObjectBase::SetPropertyValue("name", m_controlName);
	IObjectBase::SetPropertyValue("range", m_range);
	IObjectBase::SetPropertyValue("value", m_value);
}

void CValueGauge::SaveProperty()
{
	IValueWindow::SaveProperty();

	IObjectBase::GetPropertyValue("name", m_controlName);
	IObjectBase::GetPropertyValue("range", m_range);
	IObjectBase::GetPropertyValue("value", m_value);
}
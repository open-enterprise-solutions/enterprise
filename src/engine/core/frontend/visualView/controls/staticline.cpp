
#include "widgets.h"
#include "compiler/procUnit.h"

wxIMPLEMENT_DYNAMIC_CLASS(CValueStaticLine, IValueWindow)

//****************************************************************************
//*                             StaticLine                                   *
//****************************************************************************

CValueStaticLine::CValueStaticLine() : IValueWindow()
{
	PropertyContainer *categoryButton = IObjectBase::CreatePropertyContainer("StaticLine");
	categoryButton->AddProperty("name", PropertyType::PT_WXNAME);

	//category 
	m_category->AddCategory(categoryButton);

}

wxObject* CValueStaticLine::Create(wxObject* parent, IVisualHost *visualHost)
{
	wxStaticLine *m_staticline = new wxStaticLine((wxWindow *)parent, wxID_ANY,
		m_pos,
		m_size,
		m_style | m_window_style);

	return m_staticline;
}

void CValueStaticLine::OnCreated(wxObject* wxobject, wxWindow* wxparent, IVisualHost *visualHost, bool first—reated)
{
}

void CValueStaticLine::Update(wxObject* wxobject, IVisualHost *visualHost)
{
	wxStaticLine *m_staticline = dynamic_cast<wxStaticLine *>(wxobject);

	if (m_staticline)
	{
	}

	UpdateWindow(m_staticline);
}

void CValueStaticLine::Cleanup(wxObject* obj, IVisualHost *visualHost)
{
}

//*******************************************************************
//*                             Property                            *
//*******************************************************************

bool CValueStaticLine::LoadData(CMemoryReader &reader)
{
	return IValueWindow::LoadData(reader);
}

bool CValueStaticLine::SaveData(CMemoryWriter &writer)
{
	return IValueWindow::SaveData(writer);
}

//*******************************************************************
//*                             Property                            *
//*******************************************************************

void CValueStaticLine::ReadProperty()
{
	IValueWindow::ReadProperty();

	IObjectBase::SetPropertyValue("name", m_controlName);
}

void CValueStaticLine::SaveProperty()
{
	IValueWindow::SaveProperty();

	IObjectBase::GetPropertyValue("name", m_controlName);
}
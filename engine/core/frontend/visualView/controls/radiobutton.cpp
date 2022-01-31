
#include "widgets.h"
#include "compiler/procUnit.h"

wxIMPLEMENT_DYNAMIC_CLASS(CValueRadioButton, IValueWindow)

//****************************************************************************
//*                             Radiobutton                                  *
//****************************************************************************

CValueRadioButton::CValueRadioButton() : IValueWindow()
{
	PropertyContainer *categoryButton = IObjectBase::CreatePropertyContainer("Radiobutton");

	//property
	categoryButton->AddProperty("name", PropertyType::PT_WXNAME);
	categoryButton->AddProperty("selected", PropertyType::PT_BOOL);
	categoryButton->AddProperty("caption", PropertyType::PT_WXSTRING);

	//category 
	m_category->AddCategory(categoryButton);
}

wxObject* CValueRadioButton::Create(wxObject* parent, IVisualHost *visualHost) 
{
	wxRadioButton *m_radiobutton = new wxRadioButton((wxWindow *)parent, wxID_ANY,
		m_caption,
		m_pos,
		m_size,
		m_style | m_window_style);

	return m_radiobutton;
}

void CValueRadioButton::OnCreated(wxObject* wxobject, wxWindow* wxparent, IVisualHost *visualHost, bool first—reated)
{
}

void CValueRadioButton::Update(wxObject* wxobject, IVisualHost *visualHost)
{
	wxRadioButton *m_radiobutton = dynamic_cast<wxRadioButton *>(wxobject);

	if (m_radiobutton)
	{
		m_radiobutton->SetLabel(m_caption);
		m_radiobutton->SetValue(m_selected != false);
	}

	UpdateWindow(m_radiobutton);
}

void CValueRadioButton::Cleanup(wxObject* obj, IVisualHost *visualHost)
{
}

//*******************************************************************
//*                             Property                            *
//*******************************************************************

bool CValueRadioButton::LoadData(CMemoryReader &reader)
{
	return IValueWindow::LoadData(reader);
}

bool CValueRadioButton::SaveData(CMemoryWriter &writer)
{
	return IValueWindow::SaveData(writer);
}

//*******************************************************************
//*                             Data								*
//*******************************************************************

void CValueRadioButton::ReadProperty()
{
	IValueWindow::ReadProperty();

	IObjectBase::SetPropertyValue("name", m_controlName);
	IObjectBase::SetPropertyValue("selected", m_selected);
	IObjectBase::SetPropertyValue("caption", m_caption);
}

void CValueRadioButton::SaveProperty()
{
	IValueWindow::SaveProperty();

	IObjectBase::GetPropertyValue("name", m_controlName);
	IObjectBase::GetPropertyValue("selected", m_selected);
	IObjectBase::GetPropertyValue("caption", m_caption);
}
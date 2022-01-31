
#include "widgets.h"
#include "compiler/procUnit.h"

wxIMPLEMENT_DYNAMIC_CLASS(CValueListBox, IValueWindow)

//****************************************************************************
//*                             Listbox                                      *
//****************************************************************************

CValueListBox::CValueListBox() : IValueWindow()
{
	PropertyContainer *categoryButton = IObjectBase::CreatePropertyContainer("Listbox");
	categoryButton->AddProperty("name", PropertyType::PT_WXNAME);
	categoryButton->AddProperty("choices", PropertyType::PT_STRINGLIST);

	//category 
	m_category->AddCategory(categoryButton);
}

wxObject* CValueListBox::Create(wxObject* parent, IVisualHost *visualHost)
{
	wxListBox *m_listbox = new wxListBox((wxWindow*)parent, wxID_ANY,
		m_pos,
		m_size,
		0,
		NULL,
		m_style | m_window_style);

	// choices
	for (unsigned int i = 0; i < m_choices.Count(); i++)
		m_listbox->Append(m_choices[i]);

	return m_listbox;
}

void CValueListBox::OnCreated(wxObject* wxobject, wxWindow* wxparent, IVisualHost *visualHost, bool first—reated)
{
}

void CValueListBox::Update(wxObject* wxobject, IVisualHost *visualHost)
{
	wxListBox *m_listbox = dynamic_cast<wxListBox*>(wxobject);

	if (m_listbox)
	{
	}

	UpdateWindow(m_listbox);
}

void CValueListBox::Cleanup(wxObject* obj, IVisualHost *visualHost)
{
}

//*******************************************************************
//*								Data	                            *
//*******************************************************************

bool CValueListBox::LoadData(CMemoryReader &reader)
{
	return IValueWindow::LoadData(reader);
}

bool CValueListBox::SaveData(CMemoryWriter &writer)
{
	return IValueWindow::SaveData(writer);
}

//*******************************************************************
//*							 Property                               *
//*******************************************************************

void CValueListBox::ReadProperty()
{
	IValueWindow::ReadProperty();

	IObjectBase::SetPropertyValue("name", m_controlName);
	IObjectBase::SetPropertyValue("choices", m_choices);
}

void CValueListBox::SaveProperty()
{
	IValueWindow::SaveProperty();

	IObjectBase::GetPropertyValue("name", m_controlName);
	IObjectBase::GetPropertyValue("choices", m_choices);
}

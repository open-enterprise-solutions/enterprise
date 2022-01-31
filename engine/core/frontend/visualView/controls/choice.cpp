
#include "widgets.h"
#include "compiler/procUnit.h"

wxIMPLEMENT_DYNAMIC_CLASS(CValueChoice, IValueWindow)

//****************************************************************************
//*                             Choice                                       *
//****************************************************************************

CValueChoice::CValueChoice() : IValueWindow()
{
	PropertyContainer *categoryButton = IObjectBase::CreatePropertyContainer("Choice");

	//property
	categoryButton->AddProperty("name", PropertyType::PT_WXNAME);
	categoryButton->AddProperty("choices", PropertyType::PT_STRINGLIST);

	//category 
	m_category->AddCategory(categoryButton);
}

wxObject* CValueChoice::Create(wxObject* parent, IVisualHost *visualHost)
{
	wxString *strings = new wxString[m_choices.GetCount()];
	for (unsigned int i = 0; i < m_choices.GetCount(); i++)
		strings[i] = m_choices[i];

	wxChoice *m_choice = new wxChoice((wxWindow*)parent, wxID_ANY,
		m_pos,
		m_size,
		(int)m_choices.Count(),
		strings,
		m_window_style);

	int sel = m_selection;
	if (sel < (int)m_choices.GetCount()) m_choice->SetSelection(sel);

	delete[]strings;
	return m_choice;
}

void CValueChoice::OnCreated(wxObject* wxobject, wxWindow* wxparent, IVisualHost *visualHost, bool first—reated)
{
}

void CValueChoice::Update(wxObject* wxobject, IVisualHost *visualHost)
{
	wxChoice *m_choice = dynamic_cast<wxChoice *>(wxobject);

	if (m_choice)
	{
	}

	UpdateWindow(m_choice);
}

void CValueChoice::Cleanup(wxObject* obj, IVisualHost *visualHost)
{
}

//*******************************************************************
//*								Data	                            *
//*******************************************************************

bool CValueChoice::LoadData(CMemoryReader &reader)
{
	return IValueWindow::LoadData(reader);
}

bool CValueChoice::SaveData(CMemoryWriter &writer)
{
	return IValueWindow::SaveData(writer);
}

//*******************************************************************
//*								Property                            *
//*******************************************************************

void CValueChoice::ReadProperty()
{
	IValueWindow::ReadProperty();

	IObjectBase::SetPropertyValue("name", m_controlName);
	IObjectBase::SetPropertyValue("choices", m_choices);
}

void CValueChoice::SaveProperty()
{
	IValueWindow::SaveProperty();

	IObjectBase::GetPropertyValue("name", m_controlName);
	IObjectBase::GetPropertyValue("choices", m_choices);
}


#include "widgets.h"
#include "compiler/procUnit.h"

wxIMPLEMENT_DYNAMIC_CLASS(CValueComboBox, IValueWindow)

//****************************************************************************
//*                             ComboBox                                     *
//****************************************************************************

CValueComboBox::CValueComboBox() : IValueWindow()//, m_combobox(NULL)
{
	PropertyContainer *categoryButton = IObjectBase::CreatePropertyContainer("Combobox");

	//property
	categoryButton->AddProperty("name", PropertyType::PT_WXNAME);
	categoryButton->AddProperty("choices", PropertyType::PT_STRINGLIST);
	categoryButton->AddProperty("value", PropertyType::PT_WXSTRING);

	//category 
	m_category->AddCategory(categoryButton);
}

wxObject* CValueComboBox::Create(wxObject* parent, IVisualHost *visualHost)
{
	wxComboBox *m_combobox = new wxComboBox((wxWindow *)parent, wxID_ANY,
		m_value,
		m_pos,
		m_size,
		0,
		NULL,
		m_style | m_window_style);

	// choices
	for (unsigned int i = 0; i < m_choices.GetCount(); i++)
		m_combobox->Append(m_choices[i]);

	int sel = m_selection;
	if (sel > -1 && sel < (int)m_choices.GetCount()) m_combobox->SetSelection(sel);

	return m_combobox;
}

void CValueComboBox::OnCreated(wxObject* wxobject, wxWindow* wxparent, IVisualHost *visualHost, bool first—reated)
{
}

void CValueComboBox::Update(wxObject* wxobject, IVisualHost *visualHost)
{
	wxComboBox *m_combobox = dynamic_cast<wxComboBox *>(wxobject);

	if (m_combobox)
	{
		m_combobox->SetValue(m_value);
	}

	UpdateWindow(m_combobox);
}

void CValueComboBox::Cleanup(wxObject* obj, IVisualHost *visualHost)
{
}

//*******************************************************************
//*								 Data		                        *
//*******************************************************************

bool CValueComboBox::LoadData(CMemoryReader &reader)
{
	return IValueWindow::LoadData(reader);
}

bool CValueComboBox::SaveData(CMemoryWriter &writer)
{
	return IValueWindow::SaveData(writer);
}

//*******************************************************************
//*								 Property                           *
//*******************************************************************

void CValueComboBox::ReadProperty()
{
	IValueWindow::ReadProperty();

	IObjectBase::SetPropertyValue("name", m_controlName);
	IObjectBase::SetPropertyValue("choices", m_choices);
	IObjectBase::SetPropertyValue("value", m_value);
}

void CValueComboBox::SaveProperty()
{
	IValueWindow::SaveProperty();

	IObjectBase::GetPropertyValue("name", m_controlName);
	IObjectBase::GetPropertyValue("choices", m_choices);
	IObjectBase::GetPropertyValue("value", m_value);
}


#include "widgets.h"
#include "compiler/procUnit.h"

wxIMPLEMENT_DYNAMIC_CLASS(CValueStaticText, IValueWindow)

//****************************************************************************
//*                              StaticText                                  *
//****************************************************************************

CValueStaticText::CValueStaticText() : IValueWindow()
{
	PropertyContainer *categoryButton = IObjectBase::CreatePropertyContainer("Statictext");

	categoryButton->AddProperty("name", PropertyType::PT_WXNAME);
	categoryButton->AddProperty("markup", PropertyType::PT_BOOL);
	categoryButton->AddProperty("wrap", PropertyType::PT_INT);
	categoryButton->AddProperty("label", PropertyType::PT_WXSTRING);

	//category 
	m_category->AddCategory(categoryButton);
}

wxObject* CValueStaticText::Create(wxObject* parent, IVisualHost *visualHost)
{
	wxStaticText *m_static_text = new wxStaticText((wxWindow *)parent, wxID_ANY,
		m_label,
		m_pos,
		m_size,
		m_style | m_window_style);

	return m_static_text;
}

void CValueStaticText::OnCreated(wxObject* wxobject, wxWindow* wxparent, IVisualHost *visualHost, bool first—reated)
{
}

void CValueStaticText::Update(wxObject* wxobject, IVisualHost *visualHost)
{
	wxStaticText *m_static_text = dynamic_cast<wxStaticText *>(wxobject);

	if (m_static_text)
	{
		m_static_text->SetLabel(m_label);
		m_static_text->Wrap(m_wrap);

		if (m_markup != false) {
			m_static_text->SetLabelMarkup(m_label);
		}
	}

	UpdateWindow(m_static_text);
}

void CValueStaticText::Cleanup(wxObject* obj, IVisualHost *visualHost)
{
}

//*******************************************************************
//*                              Data	                            *
//*******************************************************************

bool CValueStaticText::LoadData(CMemoryReader &reader)
{
	m_markup = reader.r_u8();
	m_wrap = reader.r_u32();
	reader.r_stringZ(m_label);

	return IValueWindow::LoadData(reader);
}

bool CValueStaticText::SaveData(CMemoryWriter &writer)
{
	writer.w_u8(m_markup);
	writer.w_u32(m_wrap);
	writer.w_stringZ(m_label);

	return IValueWindow::SaveData(writer);
}

//*******************************************************************
//*                              Property                           *
//*******************************************************************

void CValueStaticText::ReadProperty()
{
	IValueWindow::ReadProperty();

	IObjectBase::SetPropertyValue("name", m_controlName);
	IObjectBase::SetPropertyValue("markup", m_markup);
	IObjectBase::SetPropertyValue("wrap", m_wrap);
	IObjectBase::SetPropertyValue("label", m_label);
}

void CValueStaticText::SaveProperty()
{
	IValueWindow::SaveProperty();

	IObjectBase::GetPropertyValue("name", m_controlName);
	IObjectBase::GetPropertyValue("markup", m_markup);
	IObjectBase::GetPropertyValue("wrap", m_wrap);
	IObjectBase::GetPropertyValue("label", m_label);
}

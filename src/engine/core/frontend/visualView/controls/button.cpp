#include "widgets.h"
#include "compiler/procUnit.h"

wxIMPLEMENT_DYNAMIC_CLASS(CValueButton, IValueWindow)

//****************************************************************************
//*                              Button                                      *
//****************************************************************************

CValueButton::CValueButton() : IValueWindow()
{
	//category 
	PropertyContainer *categoryButton = IObjectBase::CreatePropertyContainer("Button");
	categoryButton->AddProperty("name", PropertyType::PT_WXNAME);
	categoryButton->AddProperty("markup", PropertyType::PT_BOOL);
	categoryButton->AddProperty("default", PropertyType::PT_BOOL);
	categoryButton->AddProperty("caption", PropertyType::PT_WXSTRING);
	categoryButton->AddProperty("bitmap", PropertyType::PT_BITMAP);

	m_category->AddCategory(categoryButton);

	//event
	PropertyContainer *categoryEvent = IObjectBase::CreatePropertyContainer("Events");
	categoryEvent->AddEvent("onButtonPressed", { {"control"} }, _("On button pressed"));
	m_category->AddCategory(categoryEvent);
}

wxObject* CValueButton::Create(wxObject* parent, IVisualHost *visualHost)
{
	wxButton *m_button = new wxButton((wxWindow*)parent, wxID_ANY,
		m_caption,
		m_pos,
		m_size,
		m_style | m_window_style);

	//setup event 
	m_button->Bind(wxEVT_BUTTON, &CValueButton::OnButtonPressed, this);

	return m_button;
}

void CValueButton::OnCreated(wxObject* wxobject, wxWindow* wxparent, IVisualHost *visualHost, bool first—reated)
{
}

void CValueButton::Update(wxObject* wxobject, IVisualHost *visualHost)
{
	wxButton *m_button = dynamic_cast<wxButton *>(wxobject);

	if (m_button)
	{
		m_button->SetLabel(m_caption);

		if (m_markup != false) m_button->SetLabelMarkup(m_caption);

		if (m_default) m_button->SetDefault();

		if (!IsNull(_("bitmap"))) {
			m_button->SetBitmap(GetPropertyAsBitmap(_("bitmap")));
		}

		if (!IsNull(_("disabled"))) {
			m_button->SetBitmapDisabled(GetPropertyAsBitmap(_("disabled")));
		}

		if (!IsNull(_("pressed"))) {
			m_button->SetBitmapPressed(GetPropertyAsBitmap(_("pressed")));
		}

		if (!IsNull(_("focus"))) {
			m_button->SetBitmapFocus(GetPropertyAsBitmap(_("focus")));
		}

		if (!IsNull(_("current"))) {
			m_button->SetBitmapCurrent(GetPropertyAsBitmap(_("current")));
		}

		if (!IsNull(_("position"))) {
			m_button->SetBitmapPosition(
				static_cast<wxDirection>(GetPropertyAsInteger(_("position"))));
		}

		if (!IsNull(_("margins"))) {
			m_button->SetBitmapMargins(m_margins);
		}
	}

	UpdateWindow(m_button);
}

void CValueButton::Cleanup(wxObject* obj, IVisualHost *visualHost)
{
}

//*******************************************************************
//*                           Data									*
//*******************************************************************

bool CValueButton::LoadData(CMemoryReader &reader)
{
	m_markup = reader.r_u8();
	m_default = reader.r_u8();
	reader.r_stringZ(m_caption);

	return IValueWindow::LoadData(reader);
}

bool CValueButton::SaveData(CMemoryWriter &writer)
{
	writer.w_u8(m_markup);
	writer.w_u8(m_default);
	writer.w_stringZ(m_caption);

	return IValueWindow::SaveData(writer);
}

//*******************************************************************
//*                           Property                              *
//*******************************************************************

void CValueButton::ReadProperty()
{
	IValueWindow::ReadProperty();

	IObjectBase::SetPropertyValue("name", m_controlName);
	IObjectBase::SetPropertyValue("markup", m_markup);
	IObjectBase::SetPropertyValue("default", m_default);
	IObjectBase::SetPropertyValue("caption", m_caption);
	IObjectBase::SetPropertyValue("bitmap", m_bitmap);
}

void CValueButton::SaveProperty()
{
	IValueWindow::SaveProperty();

	IObjectBase::GetPropertyValue("name", m_controlName);
	IObjectBase::GetPropertyValue("markup", m_markup);
	IObjectBase::GetPropertyValue("default", m_default);
	IObjectBase::GetPropertyValue("caption", m_caption);
	IObjectBase::GetPropertyValue("bitmap", m_bitmap);
}

#include "sizers.h"

wxIMPLEMENT_DYNAMIC_CLASS(CValueStaticBoxSizer, IValueSizer)

//****************************************************************************
//*                             StaticBoxSizer                               *
//****************************************************************************

CValueStaticBoxSizer::CValueStaticBoxSizer() : IValueSizer()
{
	PropertyContainer *categoryStaticBox = IObjectBase::CreatePropertyContainer("StaticBox");

	categoryStaticBox->AddProperty("name", PropertyType::PT_WXNAME);
	categoryStaticBox->AddProperty("orient", PropertyType::PT_OPTION, &CValueStaticBoxSizer::GetOrient);
	categoryStaticBox->AddProperty("label", PropertyType::PT_WXSTRING);
	categoryStaticBox->AddProperty("font", PropertyType::PT_WXFONT);

	categoryStaticBox->AddProperty("fg", PropertyType::PT_WXCOLOUR);
	categoryStaticBox->AddProperty("bg", PropertyType::PT_WXCOLOUR);

	categoryStaticBox->AddProperty("tooltip", PropertyType::PT_WXSTRING);
	categoryStaticBox->AddProperty("context_menu", PropertyType::PT_BOOL);
	categoryStaticBox->AddProperty("context_help", PropertyType::PT_WXSTRING);
	categoryStaticBox->AddProperty("enabled", PropertyType::PT_BOOL);
	categoryStaticBox->AddProperty("visible", PropertyType::PT_BOOL);

	//category 
	m_category->AddCategory(categoryStaticBox);
}

wxObject* CValueStaticBoxSizer::Create(wxObject* parent, IVisualHost *visualHost)
{
	wxStaticBox* m_staticBox = new wxStaticBox((wxWindow *)parent, wxID_ANY, m_label);
	return new wxStaticBoxSizer(m_staticBox, m_orient);
}

#include "frontend/visualView/visualEditor.h"

void CValueStaticBoxSizer::OnCreated(wxObject* wxobject, wxWindow* wxparent, IVisualHost *visualHost, bool first—reated)
{
	wxStaticBoxSizer *m_staticboxsizer = dynamic_cast<wxStaticBoxSizer *>(wxobject);
	wxStaticBox* m_wndBox = m_staticboxsizer->GetStaticBox();
	wxASSERT(m_wndBox);
	if (visualHost->IsDesignerHost()) m_wndBox->PushEventHandler(new CDesignerWindow::HighlightPaintHandler(m_wndBox));
}

void CValueStaticBoxSizer::Update(wxObject* wxobject, IVisualHost *visualHost)
{
	wxStaticBoxSizer *m_staticboxsizer = dynamic_cast<wxStaticBoxSizer *>(wxobject);
	wxStaticBox* m_wndBox = m_staticboxsizer->GetStaticBox();
	wxASSERT(m_wndBox);
	if (m_staticboxsizer)
	{
		m_wndBox->SetLabel(m_label);
		m_wndBox->SetMinSize(m_minimum_size);

		m_wndBox->SetFont(m_font);
		m_wndBox->SetForegroundColour(m_fg);
		m_wndBox->SetBackgroundColour(m_bg);
		m_wndBox->Enable(m_enabled);
		m_wndBox->Show(m_visible);
		m_wndBox->SetToolTip(m_tooltip);

		m_staticboxsizer->SetOrientation(m_orient);
		m_staticboxsizer->SetMinSize(m_minimum_size);

		//after lay out 
		if (m_minimum_size != wxDefaultSize) {
			m_wndBox->Layout();
		}
	}

	UpdateSizer(m_staticboxsizer);
}

void CValueStaticBoxSizer::Cleanup(wxObject* wxobject, IVisualHost *visualHost)
{
	wxStaticBoxSizer *m_staticboxsizer = dynamic_cast<wxStaticBoxSizer *>(wxobject);
	wxStaticBox* m_wndBox = m_staticboxsizer->GetStaticBox();
	wxASSERT(m_wndBox);
	if (visualHost->IsDesignerHost()) {
		m_wndBox->PopEventHandler(true);
	}
}

//**********************************************************************************
//*                                    Data										   *
//**********************************************************************************

#include "utils/typeconv.h"

bool CValueStaticBoxSizer::LoadData(CMemoryReader &reader)
{
	m_orient = (wxOrientation)reader.r_u16();
	reader.r_stringZ(m_label);

	wxString propValue = wxEmptyString;
	reader.r_stringZ(propValue);
	m_minimum_size = TypeConv::StringToSize(propValue);
	reader.r_stringZ(propValue);
	m_font = TypeConv::StringToFont(propValue);
	reader.r_stringZ(propValue);
	m_fg = TypeConv::StringToColour(propValue);
	reader.r_stringZ(propValue);
	m_bg = TypeConv::StringToColour(propValue);

	reader.r_stringZ(m_tooltip);
	reader.r_stringZ(m_context_help);

	m_context_menu = reader.r_u8();
	m_enabled = reader.r_u8();
	m_visible = reader.r_u8();

	return IValueSizer::LoadData(reader);
}

bool CValueStaticBoxSizer::SaveData(CMemoryWriter &writer)
{
	writer.w_u16(m_orient);
	writer.w_stringZ(m_label);

	writer.w_stringZ(
		TypeConv::SizeToString(m_minimum_size)
	);
	writer.w_stringZ(
		TypeConv::FontToString(m_font)
	);
	writer.w_stringZ(
		TypeConv::ColourToString(m_fg)
	);
	writer.w_stringZ(
		TypeConv::ColourToString(m_bg)
	);
	writer.w_stringZ(
		m_tooltip
	);
	writer.w_stringZ(
		m_context_help
	);
	writer.w_u8(
		m_context_menu
	);
	writer.w_u8(
		m_enabled
	);
	writer.w_u8(
		m_visible
	);

	return IValueSizer::SaveData(writer);
}

//**********************************************************************************
//*                           Property                                             *
//**********************************************************************************

void CValueStaticBoxSizer::ReadProperty()
{
	IValueSizer::ReadProperty();

	IObjectBase::SetPropertyValue("name", m_controlName);
	IObjectBase::SetPropertyValue("orient", m_orient, true);
	IObjectBase::SetPropertyValue("label", m_label);

	IObjectBase::SetPropertyValue("font", m_font);

	IObjectBase::SetPropertyValue("fg", m_fg);
	IObjectBase::SetPropertyValue("bg", m_bg);

	IObjectBase::SetPropertyValue("tooltip", m_tooltip);
	IObjectBase::SetPropertyValue("context_menu", m_context_menu);
	IObjectBase::SetPropertyValue("context_help", m_context_help);
	IObjectBase::SetPropertyValue("enabled", m_enabled);
	IObjectBase::SetPropertyValue("visible", m_visible);
}

void CValueStaticBoxSizer::SaveProperty()
{
	IValueSizer::SaveProperty();

	IObjectBase::GetPropertyValue("name", m_controlName);
	IObjectBase::GetPropertyValue("orient", m_orient, true);
	IObjectBase::GetPropertyValue("label", m_label);

	IObjectBase::GetPropertyValue("font", m_font);

	IObjectBase::GetPropertyValue("fg", m_fg);
	IObjectBase::GetPropertyValue("bg", m_bg);

	IObjectBase::GetPropertyValue("tooltip", m_tooltip);
	IObjectBase::GetPropertyValue("context_menu", m_context_menu);
	IObjectBase::GetPropertyValue("context_help", m_context_help);
	IObjectBase::GetPropertyValue("enabled", m_enabled);
	IObjectBase::GetPropertyValue("visible", m_visible);
}
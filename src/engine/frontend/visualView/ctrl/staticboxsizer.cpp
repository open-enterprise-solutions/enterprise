
#include "sizer.h"

wxIMPLEMENT_DYNAMIC_CLASS(ibValueStaticBoxSizer, ibValueSizer)

//****************************************************************************
//*                             StaticBoxSizer                               *
//****************************************************************************

ibValueStaticBoxSizer::ibValueStaticBoxSizer() : ibValueSizer()
{
}

wxObject* ibValueStaticBoxSizer::Create(wxWindow* wxparent, ibVisualHost* visualHost)
{
	wxStaticBox* staticBox = new wxStaticBox(wxparent, wxID_ANY, m_propertyTitle->GetValueAsTranslateString());
	return new wxStaticBoxSizer(staticBox, m_propertyOrient->GetValueAsInteger());
}

void ibValueStaticBoxSizer::OnCreated(wxObject* wxobject, wxWindow* wxparent, ibVisualHost* visualHost, bool firstСreated)
{
	wxStaticBoxSizer* staticboxsizer = dynamic_cast<wxStaticBoxSizer*>(wxobject);
	wxStaticBox* staticBox = staticboxsizer->GetStaticBox();
	wxASSERT(staticBox);
	
	if (visualHost->IsDesignerHost()) {
		staticBox->PushEventHandler(g_visualHostContext->GetHighlightPaintHandler(staticBox));
	}
}

void ibValueStaticBoxSizer::Update(wxObject* wxobject, ibVisualHost* visualHost)
{
	wxStaticBoxSizer* staticboxsizer = dynamic_cast<wxStaticBoxSizer*>(wxobject);
	wxStaticBox* staticBox = staticboxsizer->GetStaticBox();
	wxASSERT(staticBox);
	if (staticboxsizer != nullptr) {
		staticBox->SetLabel(m_propertyTitle->GetValueAsTranslateString());
		staticBox->SetMinSize(m_propertyMinSize->GetValueAsSize());

		staticBox->SetFont(m_propertyFont->GetValueAsFont());
		staticBox->SetForegroundColour(m_propertyFG->GetValueAsColour());
		staticBox->SetBackgroundColour(m_propertyBG->GetValueAsColour());
		staticBox->Enable(m_propertyEnabled->GetValueAsBoolean());
		staticBox->Show(m_propertyVisible->GetValueAsBoolean());
		staticBox->SetToolTip(m_propertyTooltip->GetValueAsString());

		staticboxsizer->SetOrientation(m_propertyOrient->GetValueAsInteger());
		staticboxsizer->SetMinSize(m_propertyMinSize->GetValueAsSize());

		//after lay out 
		if (m_propertyMinSize->GetValueAsSize() != wxDefaultSize) {
			staticBox->Layout();
		}
	}

	UpdateSizer(staticboxsizer);
}

void ibValueStaticBoxSizer::Cleanup(wxObject* wxobject, ibVisualHost* visualHost)
{
	wxStaticBoxSizer* staticboxsizer = dynamic_cast<wxStaticBoxSizer*>(wxobject);
	wxStaticBox* staticBox = staticboxsizer->GetStaticBox();
	wxASSERT(staticBox);
	if (visualHost->IsDesignerHost()) {
		staticBox->PopEventHandler(true);
	}
}

//**********************************************************************************
//*                                    Data										   *
//**********************************************************************************



bool ibValueStaticBoxSizer::LoadData(ibReaderMemory& reader)
{
	m_propertyOrient->SetValue(reader.r_u16());	
	wxString propValue = wxEmptyString;
	reader.r_stringZ(propValue);
	m_propertyTitle->SetValue(propValue);
	reader.r_stringZ(propValue);
	m_propertyFont->SetValue(typeConv::StringToFont(propValue));
	reader.r_stringZ(propValue);
	m_propertyFG->SetValue(typeConv::StringToColour(propValue));
	reader.r_stringZ(propValue);
	m_propertyBG->SetValue(typeConv::StringToColour(propValue));

	reader.r_stringZ(propValue);
	m_propertyTooltip->SetValue(propValue);
	reader.r_stringZ(propValue);
	m_propertyContextHelp->SetValue(propValue);

	m_propertyContextMenu->SetValue(reader.r_u8());
	m_propertyEnabled->SetValue(reader.r_u8());
	m_propertyVisible->SetValue(reader.r_u8());

	return ibValueSizer::LoadData(reader);
}

bool ibValueStaticBoxSizer::SaveData(ibWriterMemory writer)
{
	writer.w_u16(m_propertyOrient->GetValueAsInteger());
	writer.w_stringZ(m_propertyTitle->GetValueAsString());
	writer.w_stringZ(m_propertyFont->GetValueAsString());
	writer.w_stringZ(m_propertyFG->GetValueAsString());
	writer.w_stringZ(m_propertyBG->GetValueAsString());
	writer.w_stringZ(m_propertyTooltip->GetValueAsString());
	writer.w_stringZ(m_propertyContextHelp->GetValueAsString());
	writer.w_u8(m_propertyContextMenu->GetValueAsBoolean());
	writer.w_u8(m_propertyEnabled->GetValueAsBoolean());
	writer.w_u8(m_propertyVisible->GetValueAsBoolean());

	return ibValueSizer::SaveData(writer);
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

CONTROL_TYPE_REGISTER(ibValueStaticBoxSizer, "Staticboxsizer", "Sizer", string_to_clsid("CT_SSZER"));
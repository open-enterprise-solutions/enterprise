
#include "sizer.h"
#ifdef OES_USE_WEB
#include "frontend/web/webSizer.h"
#endif


//****************************************************************************
//*                             StaticBoxSizer                               *
//****************************************************************************

ibValueStaticBoxSizer::ibValueStaticBoxSizer() : ibValueSizer()
{
}

wxObject* ibValueStaticBoxSizer::Create(ibFrontendWindow* wxparent, ibVisualHost* visualHost)
{
#ifdef OES_USE_WEB
	(void)wxparent;
	(void)visualHost;
	return new ibWebStaticBoxSizer(
		m_propertyOrient->GetValueAsInteger(),
		m_propertyTitle->GetValueAsTranslateString());
#else
	wxStaticBox* staticBox = new wxStaticBox(wxparent, wxID_ANY, m_propertyTitle->GetValueAsTranslateString());
	return new wxStaticBoxSizer(staticBox, m_propertyOrient->GetValueAsInteger());
#endif
}

void ibValueStaticBoxSizer::OnCreated(wxObject* wxobject, ibFrontendWindow* wxparent, ibVisualHost* visualHost, bool firstСreated)
{
#ifndef OES_USE_WEB
	wxStaticBoxSizer* staticboxsizer = dynamic_cast<wxStaticBoxSizer*>(wxobject);
	wxStaticBox* staticBox = staticboxsizer->GetStaticBox();
	wxASSERT(staticBox);

	if (visualHost->IsDesignerHost()) {
		staticBox->PushEventHandler(g_visualHostContext->GetHighlightPaintHandler(staticBox));
	}
#endif
}

void ibValueStaticBoxSizer::Update(wxObject* wxobject, ibVisualHost* visualHost)
{
	if (wxobject == nullptr) return;

	// Only the cast + `target` resolution differs per build; the
	// property-push body below is identical thanks to ibWebStaticBoxSizer
	// mirroring wxStaticBox's setter names (SetLabel / SetFont /
	// Enable / Show / SetToolTip / …). Desktop's "target" is the child
	// wxStaticBox that draws the caption; web's "target" is the sizer
	// itself (composite sizer+window), same object as staticboxsizer.
#ifdef OES_USE_WEB
	ibWebStaticBoxSizer* staticboxsizer = static_cast<ibWebStaticBoxSizer*>(wxobject);
	ibWebStaticBoxSizer* target         = staticboxsizer;
#else
	wxStaticBoxSizer* staticboxsizer = static_cast<wxStaticBoxSizer*>(wxobject);
	wxStaticBox*      target         = staticboxsizer->GetStaticBox();
	wxASSERT(target);
#endif

	staticboxsizer->SetOrientation(m_propertyOrient->GetValueAsInteger());
	staticboxsizer->SetMinSize(m_propertyMinSize->GetValueAsSize());

	target->SetLabel(m_propertyTitle->GetValueAsTranslateString());
	target->SetMinSize(m_propertyMinSize->GetValueAsSize());
	target->SetFont(m_propertyFont->GetValueAsFont());
	target->SetForegroundColour(m_propertyFG->GetValueAsColour());
	target->SetBackgroundColour(m_propertyBG->GetValueAsColour());
	target->Enable(m_propertyEnabled->GetValueAsBoolean());
	target->Show(m_propertyVisible->GetValueAsBoolean());
	target->SetToolTip(m_propertyTooltip->GetValueAsString());

	if (m_propertyMinSize->GetValueAsSize() != wxDefaultSize)
		target->Layout();

	UpdateSizer(staticboxsizer);
}

void ibValueStaticBoxSizer::Cleanup(wxObject* wxobject, ibVisualHost* visualHost)
{
#ifndef OES_USE_WEB
	wxStaticBoxSizer* staticboxsizer = dynamic_cast<wxStaticBoxSizer*>(wxobject);
	wxStaticBox* staticBox = staticboxsizer->GetStaticBox();
	wxASSERT(staticBox);
	if (visualHost->IsDesignerHost()) {
		staticBox->PopEventHandler(true);
	}
#endif
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

bool ibValueStaticBoxSizer::SaveData(ibWriterMemory& writer)
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

#include "sizer.h"
#include "backend/serialize/dataBuilder.h"   // ibDataNode (control -> node)
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



bool ibValueStaticBoxSizer::ReadData(const ibDataNode& node)
{
	m_propertyOrient->ReadNodeValue(node.GetProperty(m_propertyOrient->GetName()));	
	m_propertyTitle->ReadNodeValue(node.GetProperty(m_propertyTitle->GetName()));
	m_propertyFont->ReadNodeValue(node.GetProperty(m_propertyFont->GetName()));
	m_propertyFG->ReadNodeValue(node.GetProperty(m_propertyFG->GetName()));
	m_propertyBG->ReadNodeValue(node.GetProperty(m_propertyBG->GetName()));

	m_propertyTooltip->ReadNodeValue(node.GetProperty(m_propertyTooltip->GetName()));
	m_propertyContextHelp->ReadNodeValue(node.GetProperty(m_propertyContextHelp->GetName()));

	m_propertyContextMenu->ReadNodeValue(node.GetProperty(m_propertyContextMenu->GetName()));
	m_propertyEnabled->ReadNodeValue(node.GetProperty(m_propertyEnabled->GetName()));
	m_propertyVisible->ReadNodeValue(node.GetProperty(m_propertyVisible->GetName()));

	return ibValueSizer::ReadData(node);
}

bool ibValueStaticBoxSizer::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertyOrient->GetName(), m_propertyOrient->GetNodeValue());
	node.SetProperty(m_propertyTitle->GetName(), m_propertyTitle->GetNodeValue());
	node.SetProperty(m_propertyFont->GetName(), m_propertyFont->GetNodeValue());
	node.SetProperty(m_propertyFG->GetName(), m_propertyFG->GetNodeValue());
	node.SetProperty(m_propertyBG->GetName(), m_propertyBG->GetNodeValue());
	node.SetProperty(m_propertyTooltip->GetName(), m_propertyTooltip->GetNodeValue());
	node.SetProperty(m_propertyContextHelp->GetName(), m_propertyContextHelp->GetNodeValue());
	node.SetProperty(m_propertyContextMenu->GetName(), m_propertyContextMenu->GetNodeValue());
	node.SetProperty(m_propertyEnabled->GetName(), m_propertyEnabled->GetNodeValue());
	node.SetProperty(m_propertyVisible->GetName(), m_propertyVisible->GetNodeValue());

	return ibValueSizer::WriteData(node);
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

CONTROL_TYPE_REGISTER(ibValueStaticBoxSizer, "Staticboxsizer", "Sizer", control_to_clsid("CT_SSZER"));

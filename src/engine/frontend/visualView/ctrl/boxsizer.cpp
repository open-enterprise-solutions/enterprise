#include "sizer.h"
#include "core/serialize/dataBuilder.h"   // ibDataNode (control -> node)
#ifdef OES_USE_WEB
#include "frontend/web/webSizer.h"
#endif


//*******************************************************************
//*                             BoxSizer                            *
//*******************************************************************

ibValueBoxSizer::ibValueBoxSizer() : ibValueSizer()
{
}

wxObject* ibValueBoxSizer::Create(ibFrontendWindow* /*parent*/, ibVisualHost* /*visualHost*/)
{
#ifdef OES_USE_WEB
	// Sizers have no ctor parent on wx either — the owner (wxWindow or
	// another sizer) adopts them via SetSizer / Add. Walker does the
	// analogous SetParent on web.
	return new ibWebBoxSizer(m_propertyOrient->GetValueAsInteger());
#else
	return new wxBoxSizer(m_propertyOrient->GetValueAsInteger());
#endif
}

void ibValueBoxSizer::OnCreated(wxObject* wxobject, ibFrontendWindow* wxparent, ibVisualHost *visualHost, bool firstCreated)
{
}

void ibValueBoxSizer::Update(wxObject* wxobject, ibVisualHost *visualHost)
{
	// static_cast: Create() guarantees the type (wxBoxSizer desktop,
	// ibWebBoxSizer web); walker returns the same pointer unchanged.
	// Web's SetMinSize is a no-op (CSS layout), see ibWebSizer.
	if (wxobject == nullptr) return;
#ifdef OES_USE_WEB
	ibWebBoxSizer* boxSizer = static_cast<ibWebBoxSizer*>(wxobject);
#else
	wxBoxSizer*    boxSizer = static_cast<wxBoxSizer*>(wxobject);
#endif
	boxSizer->SetOrientation(m_propertyOrient->GetValueAsInteger());
	boxSizer->SetMinSize(m_propertyMinSize->GetValueAsSize());
	boxSizer->Show(m_propertyVisible->GetValueAsBoolean() && IsAvailable());
#ifndef OES_USE_WEB
	if (wxWindow* owner = boxSizer->GetContainingWindow())
		owner->Layout();
#endif
	UpdateSizer(boxSizer);
}

void ibValueBoxSizer::Cleanup(wxObject* obj, ibVisualHost *visualHost)
{
}

//*******************************************************************
//*                            Data									*
//*******************************************************************

bool ibValueBoxSizer::ReadData(const ibDataNode& node)
{
	m_propertyOrient->SetNodeValue(node.GetProperty(m_propertyOrient->GetName()));
	m_propertyVisible->SetNodeValue(node.GetProperty(m_propertyVisible->GetName()));
	return ibValueSizer::ReadData(node);
}

bool ibValueBoxSizer::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertyOrient->GetName(), m_propertyOrient->GetNodeValue());
	node.SetProperty(m_propertyVisible->GetName(), m_propertyVisible->GetNodeValue());
	return ibValueSizer::WriteData(node);
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

CONTROL_TYPE_REGISTER(ibValueBoxSizer, "Boxsizer", "Sizer", control_to_clsid("CT_BSZR"));

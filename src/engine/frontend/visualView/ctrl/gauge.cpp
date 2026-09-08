
#include "widgets.h"
#include "backend/serialize/dataBuilder.h"   // ibDataNode (control -> node)
#include "backend/compiler/procUnit.h"
#ifdef OES_USE_WEB
#include "frontend/web/webWindow.h"
#endif

//****************************************************************************
//*                             Gauge                                        *
//****************************************************************************

ibValueGauge::ibValueGauge() : ibValueWindow()
{
}

wxObject* ibValueGauge::Create(ibFrontendWindow* wxparent, ibVisualHost* visualHost)
{
#ifdef OES_USE_WEB
	(void)wxparent;
	(void)visualHost;
	return new ibWebGauge(GetControlID());
#else
	return new wxGauge(wxparent, wxID_ANY,
		m_propertyRange->GetValueAsInteger(),
		wxDefaultPosition,
		wxDefaultSize,
		m_propertyOrient->GetValueAsInteger()
	);
#endif
}

void ibValueGauge::OnCreated(wxObject* wxobject, ibFrontendWindow* wxparent, ibVisualHost* visualHost, bool firstCreated)
{
}

void ibValueGauge::Update(wxObject* wxobject, ibVisualHost* visualHost)
{
#ifdef OES_USE_WEB
	(void)visualHost;
	auto* gauge = static_cast<ibWebGauge*>(wxobject);
	if (gauge != nullptr) {
		gauge->SetRange(m_propertyRange->GetValueAsInteger());
		gauge->SetValue(m_propertyValue->GetValueAsInteger());
		gauge->SetOrientation(m_propertyOrient->GetValueAsInteger());
	}
#else
	wxGauge* gauge = dynamic_cast<wxGauge*>(wxobject);
	if (gauge != nullptr) {
		wxWindow *winParent = gauge->GetParent(); 
		bool isShown = gauge->IsShown();
		if (isShown) gauge->Hide();
		gauge->SetValue(0);
		gauge->SetParent(nullptr); winParent->RemoveChild(gauge);
		gauge->DissociateHandle();
		gauge->Create(winParent, wxID_ANY,
			m_propertyRange->GetValueAsInteger(),
			wxDefaultPosition,
			wxDefaultSize,
			m_propertyOrient->GetValueAsInteger() 
		);
		gauge->SetValue(m_propertyValue->GetValueAsInteger());
		gauge->Show(isShown);
	}
#endif

	UpdateWindow(gauge);
}

void ibValueGauge::OnUpdated(wxObject* wxobject, ibFrontendWindow* wxparent, ibVisualHost* visualHost)
{
}

void ibValueGauge::Cleanup(wxObject* obj, ibVisualHost* visualHost)
{
}

//*******************************************************************
//*								Data                                *
//*******************************************************************

bool ibValueGauge::ReadData(const ibDataNode& node)
{
	m_propertyRange->SetNodeValue(node.GetProperty(m_propertyRange->GetName()));
	m_propertyValue->SetNodeValue(node.GetProperty(m_propertyValue->GetName()));
	m_propertyOrient->SetNodeValue(node.GetProperty(m_propertyOrient->GetName()));
	return ibValueWindow::ReadData(node);
}

bool ibValueGauge::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertyRange->GetName(), m_propertyRange->GetNodeValue());
	node.SetProperty(m_propertyValue->GetName(), m_propertyValue->GetNodeValue());
	node.SetProperty(m_propertyOrient->GetName(), m_propertyOrient->GetNodeValue());
	return ibValueWindow::WriteData(node);
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

CONTROL_TYPE_REGISTER(ibValueGauge, "Gauge", "Widget", control_to_clsid("CT_GAUG"));

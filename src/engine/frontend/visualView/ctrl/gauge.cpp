
#include "widgets.h"
#include "backend/serialize/dataBuilder.h"   // ibDataNode (control -> node)
#include "backend/compiler/procUnit.h"


//****************************************************************************
//*                             Gauge                                        *
//****************************************************************************

ibValueGauge::ibValueGauge() : ibValueWindow()
{
}

wxObject* ibValueGauge::Create(wxWindow* wxparent, ibVisualHost* visualHost)
{
	return new wxGauge(wxparent, wxID_ANY,
		m_propertyRange->GetValueAsInteger(),
		wxDefaultPosition,
		wxDefaultSize,
		m_propertyOrient->GetValueAsInteger()
	);
}

void ibValueGauge::OnCreated(wxObject* wxobject, wxWindow* wxparent, ibVisualHost* visualHost, bool firstСreated)
{
}

void ibValueGauge::Update(wxObject* wxobject, ibVisualHost* visualHost)
{
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

	UpdateWindow(gauge);
}

void ibValueGauge::OnUpdated(wxObject* wxobject, wxWindow* wxparent, ibVisualHost* visualHost)
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
	m_propertyRange->ReadNodeValue(node.GetProperty(m_propertyRange->GetName()));
	m_propertyValue->ReadNodeValue(node.GetProperty(m_propertyValue->GetName()));
	m_propertyOrient->ReadNodeValue(node.GetProperty(m_propertyOrient->GetName()));
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

CONTROL_TYPE_REGISTER(ibValueGauge, "Gauge", "Widget", string_to_clsid("CT_GAUG"));
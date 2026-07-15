
#include "widgets.h"
#include "backend/serialize/dataBuilder.h"   // ibDataNode (control -> node)
#include "backend/compiler/procUnit.h"


//****************************************************************************
//*                             Slider                                       *
//****************************************************************************

ibValueSlider::ibValueSlider() : ibValueWindow()
{
}

wxObject* ibValueSlider::Create(wxWindow* wxparent, ibVisualHost* visualHost)
{
	wxSlider* slider = new wxSlider(wxparent, wxID_ANY,
		m_propertyValue->GetValueAsInteger(),
		m_propertyMinValue->GetValueAsInteger(),
		m_propertyMaxValue->GetValueAsInteger(),
		wxDefaultPosition,
		wxDefaultSize,
		m_propertyOrient->GetValueAsInteger()
	);

	return slider;
}

void ibValueSlider::OnCreated(wxObject* wxobject, wxWindow* wxparent, ibVisualHost* visualHost, bool firstCreated)
{
}

void ibValueSlider::Update(wxObject* wxobject, ibVisualHost* visualHost)
{
	wxSlider* slider = dynamic_cast<wxSlider*>(wxobject);

	if (slider != nullptr) {

		wxWindow* winParent = slider->GetParent();
		bool isShown = slider->IsShown();
		if (isShown) slider->Hide();
		slider->SetParent(nullptr); winParent->RemoveChild(slider);
		slider->DissociateHandle();
		slider->Create(winParent, wxID_ANY,
			m_propertyValue->GetValueAsInteger(),
			m_propertyMinValue->GetValueAsInteger(),
			m_propertyMaxValue->GetValueAsInteger(),
			wxDefaultPosition,
			wxDefaultSize,
			m_propertyOrient->GetValueAsInteger()
		);
		slider->Show(isShown);	
	}

	UpdateWindow(slider);
}

void ibValueSlider::OnUpdated(wxObject* wxobject, wxWindow* wxparent, ibVisualHost* visualHost)
{
}

void ibValueSlider::Cleanup(wxObject* obj, ibVisualHost* visualHost)
{
}

//*******************************************************************
//*                           Property                              *
//*******************************************************************

bool ibValueSlider::ReadData(const ibDataNode& node)
{
	m_propertyMinValue->ReadNodeValue(node.GetProperty(m_propertyMinValue->GetName()));
	m_propertyMaxValue->ReadNodeValue(node.GetProperty(m_propertyMaxValue->GetName()));
	m_propertyValue->ReadNodeValue(node.GetProperty(m_propertyValue->GetName()));
	m_propertyOrient->ReadNodeValue(node.GetProperty(m_propertyOrient->GetName()));
	return ibValueWindow::ReadData(node);
}

bool ibValueSlider::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertyMinValue->GetName(), m_propertyMinValue->GetNodeValue());
	node.SetProperty(m_propertyMaxValue->GetName(), m_propertyMaxValue->GetNodeValue());
	node.SetProperty(m_propertyValue->GetName(), m_propertyValue->GetNodeValue());
	node.SetProperty(m_propertyOrient->GetName(), m_propertyOrient->GetNodeValue());
	return ibValueWindow::WriteData(node);
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

CONTROL_TYPE_REGISTER(ibValueSlider, "Slider", "Widget", control_to_clsid("CT_SLID"));

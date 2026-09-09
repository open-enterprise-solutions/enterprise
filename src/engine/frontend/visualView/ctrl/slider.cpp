
#include "widgets.h"
#include "backend/serialize/dataBuilder.h"   // ibDataNode (control -> node)
#include "backend/compiler/procUnit.h"
#include "form.h"
#ifdef OES_USE_WEB
#include "frontend/web/webWindow.h"
#endif


//****************************************************************************
//*                             Slider                                       *
//****************************************************************************

ibValueSlider::ibValueSlider() : ibValueWindow()
{
}

wxObject* ibValueSlider::Create(ibFrontendWindow* wxparent, ibVisualHost* visualHost)
{
#ifdef OES_USE_WEB
	(void)wxparent;
	(void)visualHost;
	auto* slider = new ibWebSlider(GetControlID());
	slider->Bind(wxEVT_SLIDER, &ibValueSlider::OnWebSliderChanged, this);
	return slider;
#else
	wxSlider* slider = new wxSlider(wxparent, wxID_ANY,
		m_propertyValue->GetValueAsInteger(),
		m_propertyMinValue->GetValueAsInteger(),
		m_propertyMaxValue->GetValueAsInteger(),
		wxDefaultPosition,
		wxDefaultSize,
		m_propertyOrient->GetValueAsInteger()
	);

	return slider;
#endif
}

void ibValueSlider::OnCreated(wxObject* wxobject, ibFrontendWindow* wxparent, ibVisualHost* visualHost, bool firstCreated)
{
}

void ibValueSlider::Update(wxObject* wxobject, ibVisualHost* visualHost)
{
#ifdef OES_USE_WEB
	(void)visualHost;
	auto* slider = static_cast<ibWebSlider*>(wxobject);

	if (slider != nullptr) {
		slider->SetRange(m_propertyMinValue->GetValueAsInteger(),
			m_propertyMaxValue->GetValueAsInteger());
		slider->SetValue(m_propertyValue->GetValueAsInteger());
		slider->SetOrientation(m_propertyOrient->GetValueAsInteger());
	}
#else
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
#endif

	UpdateWindow(slider);
}

void ibValueSlider::OnUpdated(wxObject* wxobject, ibFrontendWindow* wxparent, ibVisualHost* visualHost)
{
}

void ibValueSlider::Cleanup(wxObject* obj, ibVisualHost* visualHost)
{
#ifdef OES_USE_WEB
	(void)visualHost;
	if (auto* slider = static_cast<ibWebSlider*>(obj))
		slider->Unbind(wxEVT_SLIDER, &ibValueSlider::OnWebSliderChanged, this);
#endif
}

#ifdef OES_USE_WEB
//*******************************************************************
//*                             Events                              *
//*******************************************************************

void ibValueSlider::OnWebSliderChanged(wxCommandEvent& event)
{
	// Nothing else holds a slider's position -- no source binding, no script
	// member -- so the property is the only place it can be kept, and a
	// position not kept anywhere would snap back on the next refresh.
	m_propertyValue->SetValue(event.GetInt());
	event.Skip();
}
#endif

//*******************************************************************
//*                           Property                              *
//*******************************************************************

bool ibValueSlider::ReadData(const ibDataNode& node)
{
	m_propertyMinValue->SetNodeValue(node.GetProperty(m_propertyMinValue->GetName()));
	m_propertyMaxValue->SetNodeValue(node.GetProperty(m_propertyMaxValue->GetName()));
	m_propertyValue->SetNodeValue(node.GetProperty(m_propertyValue->GetName()));
	m_propertyOrient->SetNodeValue(node.GetProperty(m_propertyOrient->GetName()));
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

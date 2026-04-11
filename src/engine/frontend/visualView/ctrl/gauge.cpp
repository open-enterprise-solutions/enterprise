
#include "widgets.h"
#include "backend/compiler/procUnit.h"

wxIMPLEMENT_DYNAMIC_CLASS(ibValueGauge, ibValueWindow)

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

bool ibValueGauge::LoadData(ibReaderMemory& reader)
{
	m_propertyRange->SetValue(reader.r_s32());
	m_propertyValue->SetValue(reader.r_s32());
	m_propertyOrient->SetValue(reader.r_s32());
	return ibValueWindow::LoadData(reader);
}

bool ibValueGauge::SaveData(ibWriterMemory writer)
{
	writer.w_s32(m_propertyRange->GetValueAsInteger());
	writer.w_s32(m_propertyValue->GetValueAsInteger());
	writer.w_s32(m_propertyOrient->GetValueAsInteger());
	return ibValueWindow::SaveData(writer);
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

CONTROL_TYPE_REGISTER(ibValueGauge, "Gauge", "Widget", string_to_clsid("CT_GAUG"));

#include "widgets.h"
#include "core/compiler/procUnit.h"

wxIMPLEMENT_DYNAMIC_CLASS(CValueGauge, IValueWindow)

//****************************************************************************
//*                             Gauge                                        *
//****************************************************************************

CValueGauge::CValueGauge() : IValueWindow()
{
}

wxObject* CValueGauge::Create(wxWindow* wxparent, IVisualHost* visualHost)
{
	return new wxGauge(wxparent, wxID_ANY,
		m_propertyRange->GetValueAsInteger(),
		wxDefaultPosition,
		wxDefaultSize,
		m_propertyOrient->GetValueAsInteger()
	);
}

void CValueGauge::OnCreated(wxObject* wxobject, wxWindow* wxparent, IVisualHost* visualHost, bool first—reated)
{
}

void CValueGauge::Update(wxObject* wxobject, IVisualHost* visualHost)
{
	wxGauge* gauge = dynamic_cast<wxGauge*>(wxobject);
	if (gauge != NULL) {
		wxWindow *winParent = gauge->GetParent(); 
		bool isShown = gauge->IsShown();
		if (isShown) gauge->Hide();
		gauge->SetValue(0);
		gauge->SetParent(NULL); winParent->RemoveChild(gauge);
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

void CValueGauge::OnUpdated(wxObject* wxobject, wxWindow* wxparent, IVisualHost* visualHost)
{
}

void CValueGauge::Cleanup(wxObject* obj, IVisualHost* visualHost)
{
}

//*******************************************************************
//*								Data                                *
//*******************************************************************

bool CValueGauge::LoadData(CMemoryReader& reader)
{
	m_propertyRange->SetValue(reader.r_s32());
	m_propertyValue->SetValue(reader.r_s32());
	m_propertyOrient->SetValue(reader.r_s32());
	return IValueWindow::LoadData(reader);
}

bool CValueGauge::SaveData(CMemoryWriter& writer)
{
	writer.w_s32(m_propertyRange->GetValueAsInteger());
	writer.w_s32(m_propertyValue->GetValueAsInteger());
	writer.w_s32(m_propertyOrient->GetValueAsInteger());
	return IValueWindow::SaveData(writer);
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

CONTROL_VALUE_REGISTER(CValueGauge, "gauge", "widget", TEXT2CLSID("CT_GAUG"));
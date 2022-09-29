
#include "widgets.h"
#include "compiler/procUnit.h"

wxIMPLEMENT_DYNAMIC_CLASS(CValueGauge, IValueWindow)

//****************************************************************************
//*                             Gauge                                        *
//****************************************************************************

CValueGauge::CValueGauge() : IValueWindow()
{
}

wxObject* CValueGauge::Create(wxObject* parent, IVisualHost* visualHost)
{
	return new wxGauge((wxWindow*)parent, wxID_ANY,
		m_propertyRange->GetValueAsInteger(),
		wxDefaultPosition,
		wxDefaultSize,
		m_propertyOrient->GetValueAsInteger());
}

void CValueGauge::OnCreated(wxObject* wxobject, wxWindow* wxparent, IVisualHost* visualHost, bool first—reated)
{
}

void CValueGauge::Update(wxObject* wxobject, IVisualHost* visualHost)
{
	wxGauge* gauge = dynamic_cast<wxGauge*>(wxobject);

	if (gauge != NULL) {
		gauge->SetRange(m_propertyRange->GetValueAsInteger());
		gauge->SetValue(m_propertyValue->GetValueAsInteger());
	}

	UpdateWindow(gauge);
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
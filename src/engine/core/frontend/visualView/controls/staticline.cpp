
#include "widgets.h"
#include "compiler/procUnit.h"

wxIMPLEMENT_DYNAMIC_CLASS(CValueStaticLine, IValueWindow)

//****************************************************************************
//*                             StaticLine                                   *
//****************************************************************************

CValueStaticLine::CValueStaticLine() : IValueWindow()
{
}

wxObject* CValueStaticLine::Create(wxObject* parent, IVisualHost *visualHost)
{
	wxStaticLine *staticline = new wxStaticLine((wxWindow *)parent, wxID_ANY,
		wxDefaultPosition,
		wxDefaultSize,
		m_propertyOrient->GetValueAsInteger());

	return staticline;
}

void CValueStaticLine::OnCreated(wxObject* wxobject, wxWindow* wxparent, IVisualHost *visualHost, bool first—reated)
{
}

void CValueStaticLine::Update(wxObject* wxobject, IVisualHost *visualHost)
{
	wxStaticLine * staticline = dynamic_cast<wxStaticLine *>(wxobject);

	if (staticline != NULL) {
		staticline->SetWindowStyle(m_propertyOrient->GetValueAsInteger());
	}

	UpdateWindow(staticline);
}

void CValueStaticLine::Cleanup(wxObject* obj, IVisualHost *visualHost)
{
}

//*******************************************************************
//*                             Property                            *
//*******************************************************************

bool CValueStaticLine::LoadData(CMemoryReader &reader)
{
	m_propertyOrient->SetValue(reader.r_s32());
	return IValueWindow::LoadData(reader);
}

bool CValueStaticLine::SaveData(CMemoryWriter &writer)
{
	writer.w_s32(m_propertyOrient->GetValueAsInteger());
	return IValueWindow::SaveData(writer);
}
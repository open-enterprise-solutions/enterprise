
#include "widgets.h"
#include "backend/compiler/procUnit.h"

wxIMPLEMENT_DYNAMIC_CLASS(ibValueStaticLine, ibValueWindow)

//****************************************************************************
//*                             StaticLine                                   *
//****************************************************************************

ibValueStaticLine::ibValueStaticLine() : ibValueWindow()
{
}

wxObject* ibValueStaticLine::Create(wxWindow* wxparent, ibVisualHost* visualHost)
{
	wxStaticLine* staticline = new wxStaticLine(wxparent, wxID_ANY,
		wxDefaultPosition,
		wxDefaultSize,
		m_propertyOrient->GetValueAsEnum()
	);

	return staticline;
}

void ibValueStaticLine::OnCreated(wxObject* wxobject, wxWindow* wxparent, ibVisualHost* visualHost, bool firstСreated)
{
}

void ibValueStaticLine::Update(wxObject* wxobject, ibVisualHost* visualHost)
{
	wxStaticLine* staticline = dynamic_cast<wxStaticLine*>(wxobject);

	if (staticline != nullptr) {
		wxWindow* winParent = staticline->GetParent();
		bool isShown = staticline->IsShown();
		if (isShown) staticline->Hide();
		staticline->SetParent(nullptr); winParent->RemoveChild(staticline);
		staticline->DissociateHandle();
		staticline->Create(winParent, wxID_ANY,
			wxDefaultPosition,
			wxDefaultSize,
			m_propertyOrient->GetValueAsEnum()
		);
		staticline->Show(isShown);
	}

	UpdateWindow(staticline);
}

void ibValueStaticLine::OnUpdated(wxObject* wxobject, wxWindow* wxparent, ibVisualHost* visualHost)
{
}

void ibValueStaticLine::Cleanup(wxObject* obj, ibVisualHost* visualHost)
{
}

//*******************************************************************
//*                             Property                            *
//*******************************************************************

bool ibValueStaticLine::LoadData(ibReaderMemory& reader)
{
	m_propertyOrient->SetValue(reader.r_s32());
	return ibValueWindow::LoadData(reader);
}

bool ibValueStaticLine::SaveData(ibWriterMemory writer)
{
	writer.w_s32(m_propertyOrient->GetValueAsInteger());
	return ibValueWindow::SaveData(writer);
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

CONTROL_TYPE_REGISTER(ibValueStaticLine, "Staticline", "Widget", string_to_clsid("CT_STLI"));
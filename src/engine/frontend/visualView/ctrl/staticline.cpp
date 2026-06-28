
#include "widgets.h"
#include "backend/serialize/dataBuilder.h"   // ibDataNode (control -> node)
#include "backend/compiler/procUnit.h"


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

bool ibValueStaticLine::ReadData(const ibDataNode& node)
{
	m_propertyOrient->ReadNodeValue(node.GetProperty(m_propertyOrient->GetName()));
	return ibValueWindow::ReadData(node);
}

bool ibValueStaticLine::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertyOrient->GetName(), m_propertyOrient->GetNodeValue());
	return ibValueWindow::WriteData(node);
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

CONTROL_TYPE_REGISTER(ibValueStaticLine, "Staticline", "Widget", control_to_clsid("CT_STLI"));

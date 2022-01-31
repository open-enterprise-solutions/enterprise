////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxwidgets community
//	Description : main child window
////////////////////////////////////////////////////////////////////////////

#include "mainFrameChild.h"

// default ctor, use Create after it
CDocChildFrame::CDocChildFrame() { }

// ctor for a valueForm showing the given view of the specified document
CDocChildFrame::CDocChildFrame(wxDocument *doc,
	wxView *view,
	wxAuiMDIParentFrame *parent,
	wxWindowID id,
	const wxString& title,
	const wxPoint& pos,
	const wxSize& size,
	long style,
	const wxString& name)
{
	Create(doc, view, parent, id, title, pos, size, style, name);
}

bool CDocChildFrame::Create(wxDocument *doc,
	wxView *view,
	wxAuiMDIParentFrame *parent,
	wxWindowID id,
	const wxString& title,
	const wxPoint& pos,
	const wxSize& size,
	long style,
	const wxString& name)
{
	if (!wxDocChildFrameAnyBase::Create(doc, view, this))
		return false;

	if (!wxAuiMDIChildFrame::Create(parent, id, title, pos, size, style, name))
		return false;

	this->Bind(wxEVT_ACTIVATE, &CDocChildFrame::OnActivate, this);
	this->Bind(wxEVT_CLOSE_WINDOW, &CDocChildFrame::OnCloseWindow, this);
	
	return true;
}

#include "mainFrame.h"

CDocChildFrame::~CDocChildFrame()
{
	wxAuiMDIParentFrame* pParentFrame = GetMDIParentFrame();

	if (pParentFrame && CMainFrame::Get())
	{
		if (pParentFrame->GetActiveChild() == this)
		{
			pParentFrame->SetActiveChild(NULL);
			pParentFrame->SetChildMenuBar(NULL);
		}
		wxAuiMDIClientWindow* pClientWindow = pParentFrame->GetClientWindow();
		wxASSERT(pClientWindow);
		int idx = pClientWindow->GetPageIndex(this);
		if (idx != wxNOT_FOUND)
		{
			pClientWindow->RemovePage(idx);
		}
	}

	this->Unbind(wxEVT_ACTIVATE, &CDocChildFrame::OnActivate, this);
	this->Unbind(wxEVT_CLOSE_WINDOW, &CDocChildFrame::OnCloseWindow, this);

	m_pMDIParentFrame = NULL;
}

void CDocChildFrame::SetLabel(const wxString& label)
{
	wxAuiMDIChildFrame::SetLabel(label);
	wxAuiMDIChildFrame::SetTitle(label);
}

bool CDocChildFrame::TryBefore(wxEvent& event)
{
	return wxDocChildFrameAnyBase::TryProcessEvent(event) || wxAuiMDIChildFrame::TryBefore(event);
}

//************************************************************************************************************
//*                                                Events                                                    *   
//************************************************************************************************************

void CDocChildFrame::OnActivate(wxActivateEvent& event)
{
	wxAuiMDIChildFrame::OnActivate(event);

	if (m_childView) {
		m_childView->Activate(event.GetActive());
	}
}

#include "common/reportManager.h"

void CDocChildFrame::OnCloseWindow(wxCloseEvent& event)
{
	if (CloseView(event)) {
		this->Destroy();
	}
}

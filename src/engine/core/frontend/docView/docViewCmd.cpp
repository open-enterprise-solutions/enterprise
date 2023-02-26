////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko 
//	Description : document/view implementation for metaObject 
////////////////////////////////////////////////////////////////////////////

#include "docView.h"
#include "frontend/mainFrame.h"

static wxMenu* s_viewMenu = NULL;

void CView::OnActivateView(bool activate, wxView* activeView, wxView* deactiveView)
{
	mainFrame->OnActivateView(activate, activeView, deactiveView);
}

#define menuPos 2

void CView::Activate(bool activate)
{
	wxDocManager* docManager = m_viewDocument != NULL ?
		m_viewDocument->GetDocumentManager() : NULL;

#if wxUSE_MENUS		
	if (activate) {
		if (s_viewMenu != NULL) {
			s_viewMenu->Unbind(wxEVT_MENU, &CView::OnViewMenuClicked, this);
			mainFrame->GetMenuBar()->Remove(menuPos);
			s_viewMenu = NULL;
		}
		s_viewMenu = CreateViewMenu();
		if (s_viewMenu != NULL) {
			const wxString &titleBar = (wxString)s_viewMenu->GetTitle(); 
			mainFrame->GetMenuBar()->Insert(menuPos, s_viewMenu, titleBar);
			s_viewMenu->SetTitle(wxEmptyString);
			s_viewMenu->Bind(wxEVT_MENU, &CView::OnViewMenuClicked, this);
		}
	}
	else if (s_viewMenu != NULL) {
		s_viewMenu->Unbind(wxEVT_MENU, &CView::OnViewMenuClicked, this);
		mainFrame->GetMenuBar()->Remove(menuPos);
		s_viewMenu = NULL;
	}
#endif

	if (docManager != NULL && wxAuiDocMDIFrame::GetFrame()) {
		OnActivateView(activate, this, docManager->GetCurrentView());
		docManager->ActivateView(this, activate);
	}
}

void CView::OnViewMenuClicked(wxCommandEvent& event) {
	OnMenuItemClicked(event.GetId());
	event.Skip();
}
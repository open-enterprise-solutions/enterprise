////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko 
//	Description : document/view implementation for metaObject 
////////////////////////////////////////////////////////////////////////////

#include "docView.h"
#include "frontend/mainFrame/mainFrame.h"

static wxMenu* s_viewMenu = nullptr;

void CMetaView::OnActivateView(bool activate, wxView* activeView, wxView* deactiveView)
{
	mainFrame->OnActivateView(activate, activeView, deactiveView);
}

#define menuPos 2

void CMetaView::Activate(bool activate)
{
	wxDocManager* docManager = m_viewDocument != nullptr ?
		m_viewDocument->GetDocumentManager() : nullptr;

#if wxUSE_MENUS		
	if (activate) {
		if (s_viewMenu != nullptr) {
			s_viewMenu->Unbind(wxEVT_MENU, &CMetaView::OnViewMenuClicked, this);
			mainFrame->GetMenuBar()->Remove(menuPos);
			s_viewMenu = nullptr;
		}
		s_viewMenu = CreateViewMenu();
		if (s_viewMenu != nullptr) {
			const wxString &titleBar = (wxString)s_viewMenu->GetTitle(); 
			mainFrame->GetMenuBar()->Insert(menuPos, s_viewMenu, titleBar);
			s_viewMenu->SetTitle(wxEmptyString);
			s_viewMenu->Bind(wxEVT_MENU, &CMetaView::OnViewMenuClicked, this);
		}
	}
	else if (s_viewMenu != nullptr) {
		s_viewMenu->Unbind(wxEVT_MENU, &CMetaView::OnViewMenuClicked, this);
		mainFrame->GetMenuBar()->Remove(menuPos);
		s_viewMenu = nullptr;
	}
#endif

	if (docManager != nullptr && CDocMDIFrame::GetFrame()) {
		OnActivateView(activate, this, docManager->GetCurrentView());
		docManager->ActivateView(this, activate);
	}
}

void CMetaView::OnViewMenuClicked(wxCommandEvent& event) {
	OnMenuItemClicked(event.GetId());
	event.Skip();
}
////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko 
//	Description : document/view implementation for metaObject 
////////////////////////////////////////////////////////////////////////////

#include "docInfo.h"
#include "frontend/mainFrame.h"

// Called by valueFramework if created automatically by the default document
// manager class: gives view a chance to initialise
bool CView::OnCreate(CDocument *WXUNUSED(doc), long flags) 
{
	return true; 
}

void CView::OnCreateToolbar(wxAuiToolBar *toolbar) {}

void CView::OnRemoveToolbar(wxAuiToolBar *toolbar) {}

void CView::OnActivateView(bool activate, wxView *activeView, wxView *deactiveView)
{
	mainFrame->OnActivateView(activate, activeView, deactiveView);
}

void CView::Activate(bool activate)
{
	wxDocManager *docManager = NULL; 
	
	if (m_viewDocument) {
		docManager = m_viewDocument->GetDocumentManager();
	}

	if (docManager && CMainFrame::Get()){
		OnActivateView(activate, this, docManager->GetCurrentView());
		docManager->ActivateView(this, activate);
	}
}

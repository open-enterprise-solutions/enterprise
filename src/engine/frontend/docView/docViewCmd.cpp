////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko 
//	Description : document/view implementation for metaObject 
////////////////////////////////////////////////////////////////////////////

#include "docView.h"
#ifndef OES_USE_WEB
#include "frontend/mainFrame/mainFrame.h"
#endif
#include "backend/moduleManager/moduleManager.h"

void ibMetaView::OnActivateView(bool activate, wxView* activeView, wxView* deactiveView)
{
#ifndef OES_USE_WEB
	// objectInspector is a designer-side panel tracking the "currently
	// selected" meta object. Web has no equivalent panel; the session's
	// active form is already tracked on ibWebFrame::m_activeTab.
	if (activate)
		objectInspector->SelectObject(GetDocument() ? GetDocument()->GetMetaObject() : nullptr);
#else
	(void)activate; (void)activeView; (void)deactiveView;
#endif
}

void ibMetaView::Activate(bool activate)
{
#ifndef OES_USE_WEB
	// Guard against infinite recursion on macOS: Activate → SetFocus →
	// resignFirstResponder → OnSetFocus → Activate → ...
	static bool s_inActivate = false;
	if (s_inActivate) return;
	s_inActivate = true;

	wxDocManager* docManager = m_viewDocument != nullptr ?
		m_viewDocument->GetDocumentManager() : wxDocManager::GetDocumentManager();

	if (docManager != nullptr && ibFrontendDocMDIFrame::GetFrame()) {
		mainFrame->ActivateView(this, activate);
		OnActivateView(activate, this, docManager->GetCurrentView());
		docManager->ActivateView(this, activate);
	}

	s_inActivate = false;
#else
	// Web: activation is driven from ibWebFrame::SetActiveTab directly
	// on the session's tab list — no docManager round-trip needed.
	(void)activate;
#endif
}
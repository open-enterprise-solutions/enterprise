////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxwidgets community
//	Description : main frame window
////////////////////////////////////////////////////////////////////////////

#include "mainFrame.h"
#include "frontend/objinspect/objinspect.h"

void CMainFrame::ShowProperty()
{
	wxAuiPaneInfo& infoToolBar = m_mgr.GetPane(objectInspector);
	
	if (!infoToolBar.IsOk())
		return;

	if (!infoToolBar.IsShown()) {
		
		infoToolBar.Show();
		objectInspector->SetFocus();
		objectInspector->Raise();
		
		m_mgr.Update();
	}
}

#include "common/docInfo.h"

void CMainFrame::OnActivateView(bool activate, wxView* activeView, wxView* deactiveView)
{
	if (m_toolbarAdditional) {

		wxAuiPaneInfo& infoToolBar = m_mgr.GetPane(m_toolbarAdditional);

		wxDocument* activeDoc = activeView ? activeView->GetDocument() : NULL;
		wxDocument* deactiveDoc = deactiveView ? deactiveView->GetDocument() : NULL;

		wxDocTemplate* deactiveDocTemplate = deactiveDoc ?
			deactiveDoc->GetDocumentTemplate() : NULL;

		CView* actView = dynamic_cast<CView*>(activeView);
		CView* deactView = dynamic_cast<CView*>(deactiveView);

		if (deactView != NULL) {
			deactView->OnRemoveToolbar(m_toolbarAdditional);
			m_toolbarAdditional->ClearTools();
		}

		if (deactiveDocTemplate != activeDoc->GetDocumentTemplate()) {

			if (actView) {
				actView->OnCreateToolbar(m_toolbarAdditional);
			}

			if (m_toolbarAdditional->GetToolCount() > 0) {
				infoToolBar.Show();
			}
			else {
				infoToolBar.Hide();
			}

			m_toolbarAdditional->Freeze();
			m_toolbarAdditional->Realize();
			m_toolbarAdditional->Thaw();
		}
		else {

			if (activate) {
				actView->OnCreateToolbar(m_toolbarAdditional);
			}

			if (m_toolbarAdditional->GetToolCount() > 0) {
				infoToolBar.Show();
			}
			else {
				infoToolBar.Hide();
			}

			m_toolbarAdditional->Freeze();
			m_toolbarAdditional->Realize();
			m_toolbarAdditional->Thaw();
		}

		infoToolBar.BestSize(m_toolbarAdditional->GetSize());

		infoToolBar.FloatingSize(
			m_toolbarAdditional->GetSize().x,
			m_toolbarAdditional->GetSize().y + 25
		);

		if (activate) {
			m_mgr.Update();
		}
	}
}
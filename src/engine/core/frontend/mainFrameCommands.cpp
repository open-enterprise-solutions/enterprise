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

		if (deactiveView != NULL) {
			CView* deactView = dynamic_cast<CView*>(deactiveView);
			if (deactView != NULL) {
				deactView->OnRemoveToolbar(m_toolbarAdditional);
			}
			m_toolbarAdditional->ClearTools();
		}

		wxAuiPaneInfo& infoToolBar = m_mgr.GetPane(m_toolbarAdditional);

		if (activate) {
			CView* actView = dynamic_cast<CView*>(activeView);
			if (actView != NULL) {
				actView->OnCreateToolbar(m_toolbarAdditional);
			}
		}

		m_toolbarAdditional->Freeze();
		m_toolbarAdditional->Realize();
		m_toolbarAdditional->Thaw();

		infoToolBar.Show(m_toolbarAdditional->GetToolCount() > 0);

		infoToolBar.BestSize(m_toolbarAdditional->GetSize());
		infoToolBar.FloatingSize(
			m_toolbarAdditional->GetSize().x,
			m_toolbarAdditional->GetSize().y + 25
		);

		m_mgr.Update();
	}
}
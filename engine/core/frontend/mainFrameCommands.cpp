////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxwidgets community
//	Description : main frame window
////////////////////////////////////////////////////////////////////////////

#include "mainFrame.h"
#include "frontend/objinspect/objinspect.h"

void CMainFrame::ShowProperty()
{
	wxAuiPaneInfo &m_info = m_mgr.GetPane(objectInspector);
	if (!m_info.IsOk()) 
		return;

	if (!m_info.IsShown()) {
		m_info.Show();
		objectInspector->SetFocus();
		objectInspector->Raise();
		m_mgr.Update();
	}
}

#include "common/docInfo.h"

void CMainFrame::OnActivateView(bool activate, wxView *activeView, wxView *deactiveView)
{
	if (m_toolbarAdditional)
	{
		wxAuiPaneInfo &m_infoToolBar = m_mgr.GetPane(m_toolbarAdditional);

		wxDocument *m_activeDoc = activeView ? activeView->GetDocument() : NULL;
		wxDocument *m_deactiveDoc = deactiveView ? deactiveView->GetDocument() : NULL;

		wxDocTemplate *m_deactiveDocTemplate = m_deactiveDoc ? m_deactiveDoc->GetDocumentTemplate() : NULL;

		CView *m_activeView = dynamic_cast<CView *>(activeView);
		CView *m_deactiveView = dynamic_cast<CView *>(deactiveView);

		if (m_deactiveView) { 
			m_deactiveView->OnRemoveToolbar(m_toolbarAdditional); 
			m_toolbarAdditional->ClearTools();
		}

		if (m_deactiveDocTemplate != m_activeDoc->GetDocumentTemplate()) {
			if (activate)
				m_activeView->OnCreateToolbar(m_toolbarAdditional);

			if (m_toolbarAdditional->GetToolCount() > 0)
				m_infoToolBar.Show();
			else m_infoToolBar.Hide();

			m_toolbarAdditional->Freeze();
			m_toolbarAdditional->Realize();
			m_toolbarAdditional->Thaw();
		}
		else {

			if (activate) 
				m_activeView->OnCreateToolbar(m_toolbarAdditional);

			if (m_toolbarAdditional->GetToolCount() > 0) 
				m_infoToolBar.Show();
			else m_infoToolBar.Hide();

			m_toolbarAdditional->Freeze();
			m_toolbarAdditional->Realize();
			m_toolbarAdditional->Thaw();
		}

		m_infoToolBar.BestSize(m_toolbarAdditional->GetSize());
		m_infoToolBar.FloatingSize(m_toolbarAdditional->GetSize().x, m_toolbarAdditional->GetSize().y + 25);

		m_mgr.Update();
	}
}
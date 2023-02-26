////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxwidgets community
//	Description : main frame window
////////////////////////////////////////////////////////////////////////////

#include "mainFrame.h"
#include "frontend/objinspect/objinspect.h"

void wxAuiDocMDIFrame::CreatePropertyPane()
{
	if (m_mgr.GetPane(wxAUI_PANE_PROPERTY).IsOk())
		return;

	m_objectInspector = new CObjectInspector(this, wxID_ANY);

	wxAuiPaneInfo paneInfo;
	paneInfo.Name(wxAUI_PANE_PROPERTY);
	paneInfo.CloseButton(true);
	paneInfo.MinimizeButton(false);
	paneInfo.MaximizeButton(false);
	paneInfo.DestroyOnClose(false);
	paneInfo.Caption(_("Property"));
	paneInfo.MinSize(300, 0);
	paneInfo.Right();
	paneInfo.Show(false);

	m_mgr.AddPane(m_objectInspector, paneInfo);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void wxAuiDocMDIFrame::ShowProperty()
{
	wxAuiPaneInfo& propertyPane = m_mgr.GetPane(wxAUI_PANE_PROPERTY);
	if (!propertyPane.IsOk())
		return;
	if (!propertyPane.IsShown()) {
		propertyPane.Show();
		m_objectInspector->SetFocus();
		m_objectInspector->Raise();
		m_mgr.Update();
	}
}

#include "frontend/docView/docView.h"

void wxAuiDocMDIFrame::OnActivateView(bool activate, wxView* activeView, wxView* deactiveView)
{
	if (m_docToolbar != NULL) {

		if (deactiveView != NULL) {
			CView* deactView = dynamic_cast<CView*>(deactiveView);
			if (deactView != NULL) {
				deactView->OnRemoveToolbar(m_docToolbar);
			}
			m_docToolbar->ClearTools();
		}

		wxAuiPaneInfo& infoToolBar = m_mgr.GetPane(m_docToolbar);

		if (activate) {
			CView* actView = dynamic_cast<CView*>(activeView);
			if (actView != NULL) {
				actView->OnCreateToolbar(m_docToolbar);
			}
		}

		m_docToolbar->Freeze();
		m_docToolbar->Realize();
		m_docToolbar->Thaw();

		infoToolBar.Show(m_docToolbar->GetToolCount() > 0);

		infoToolBar.BestSize(m_docToolbar->GetSize());
		infoToolBar.FloatingSize(
			m_docToolbar->GetSize().x,
			m_docToolbar->GetSize().y + 25
		);

		m_mgr.Update();
	}
}
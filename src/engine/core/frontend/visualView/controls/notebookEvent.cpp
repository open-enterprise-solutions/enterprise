#include "notebook.h"
#include "frontend/visualView/visualEditor.h"
#include "appData.h"

void CValueNotebook::OnPageChanged(wxAuiNotebookEvent& event)
{
	CValueNotebookPage* activePage = NULL;
	if (event.GetOldSelection() != wxNOT_FOUND) {
		wxAuiNotebook* notebook = dynamic_cast<wxAuiNotebook*>(GetWxObject());
		wxASSERT(notebook);
		wxWindow* page = notebook->GetPage(event.GetSelection());
		wxASSERT(page);
		for (unsigned int i = 0; i < GetChildCount(); i++) {
			CValueNotebookPage* child = dynamic_cast<CValueNotebookPage*>(GetChild(i));
			wxASSERT(child);
			if (page == child->GetWxObject())
				activePage = child;
		}
	}

	if (m_activePage == activePage)
		return;

	m_activePage = activePage;

	if (g_visualHostContext != NULL && m_activePage != NULL) {
		g_visualHostContext->SelectControl(m_activePage);
		event.Skip();
	}
	else if (m_activePage != NULL) {
		event.Skip(
			CallEvent(
				m_eventOnPageChanged, CValue(m_activePage)
			)
		);
	}
}

void CValueNotebook::OnBGDClick(wxAuiNotebookEvent& event)
{
	if (g_visualHostContext != NULL)
		g_visualHostContext->SelectControl(this);
	event.Skip();
}

void CValueNotebook::OnEndDrag(wxAuiNotebookEvent& event)
{
	wxAuiNotebook* notebook = dynamic_cast<wxAuiNotebook*>(GetWxObject());
	wxASSERT(notebook);
	wxWindow* page = notebook->GetPage(notebook->GetSelection());
	wxASSERT(page);
	if (g_visualHostContext != NULL) {
		if (ChangeChildPosition(m_activePage, event.GetSelection())) {
			if (g_visualHostContext != NULL)
				g_visualHostContext->RefreshTree();
		}
	}
	else if (!appData->DesignerMode()) {
		ChangeChildPosition(m_activePage, event.GetSelection());
	}
	event.Skip();
}

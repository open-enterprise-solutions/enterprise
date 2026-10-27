#include "notebook.h"
#include "form.h"
#include "backend/appData.h"

void ibValueNotebook::OnPageChanged(wxAuiNotebookEvent& event)
{
	// No page when the last one went: wx 3.3 says so with wxNOT_FOUND. The notebook is the one that sent the event —
	// asked through the form instead, it is not there while the form closes and takes its pages down.
	ibValueNotebookPage* activePage = nullptr;
	if (event.GetOldSelection() != wxNOT_FOUND && event.GetSelection() != wxNOT_FOUND) {
		wxAuiNotebook* notebook = dynamic_cast<wxAuiNotebook*>(event.GetEventObject());
		wxASSERT(notebook);
		wxWindow* page = notebook->GetPage(event.GetSelection());
		wxASSERT(page);
		for (unsigned int i = 0; i < GetChildCount(); i++) {
			ibValueNotebookPage* child = dynamic_cast<ibValueNotebookPage*>(GetChild(i));
			wxASSERT(child);
			if (page == child->GetWxObject())
				activePage = child;
		}
	}

	if (m_activePage == activePage)
		return;

	m_activePage = activePage;

	if (g_visualHostContext != nullptr && m_activePage != nullptr) {
		g_visualHostContext->SelectControl(m_activePage);
		event.Skip();
	}
	else if (m_activePage != nullptr) {
		event.Skip(
			CallAsEvent(
				m_eventOnPageChanged, ibValue(m_activePage)
			)
		);
	}

	m_formOwner->RefreshForm();
}

void ibValueNotebook::OnBGDClick(wxAuiNotebookEvent& event)
{
	if (g_visualHostContext != nullptr) {
		g_visualHostContext->SelectControl(this);
	}
	event.Skip();
}

void ibValueNotebook::OnEndDrag(wxAuiNotebookEvent& event)
{
	wxAuiNotebook* notebook = dynamic_cast<wxAuiNotebook*>(GetWxObject());
	wxASSERT(notebook);
	wxWindow* page = notebook->GetPage(notebook->GetSelection());
	wxASSERT(page);
	if (g_visualHostContext != nullptr) {
		if (ChangeChildPosition(m_activePage, event.GetSelection())) {
			if (g_visualHostContext != nullptr) {
				g_visualHostContext->RefreshEditor();
			}
		}
	}
	else if (!appData->DesignerMode()) {
		ChangeChildPosition(m_activePage, event.GetSelection());
	}
	event.Skip();
}

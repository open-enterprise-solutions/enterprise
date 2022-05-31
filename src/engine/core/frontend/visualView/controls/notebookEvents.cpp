#include "notebook.h"
#include "frontend/visualView/visualEditor.h"

void CValueNotebook::OnEnablePage(wxCommandEvent &event)
{
	m_bInitialized = true; 
	event.Skip(); 
}

void CValueNotebook::OnChangedPage(wxAuiNotebookEvent &event)
{
	m_activePage = NULL;

	wxAuiNotebook *m_notebookCtrl = dynamic_cast<wxAuiNotebook *>(event.GetEventObject());
	wxASSERT(m_notebookCtrl);
	wxWindow *m_wndPage = m_notebookCtrl->GetPage(m_notebookCtrl->GetSelection());
	wxASSERT(m_wndPage);
	for (unsigned int i = 0; i < GetChildCount(); i++)
	{
		CValueNotebookPage *child = dynamic_cast<CValueNotebookPage *>(GetChild(i));
		wxASSERT(child);
		if (m_wndPage == child->GetWxObject()) 
			m_activePage = child;
	}

	if (m_visualHostContext && m_activePage) {
		m_visualHostContext->SelectObject(m_activePage); 
		event.Skip(); 
	}
	else {
		if (!m_bInitialized)
			return;
		event.Skip(
			CallEvent("onPageChanged", CValue(m_activePage))
		);
	}
}
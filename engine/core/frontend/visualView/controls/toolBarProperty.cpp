#include "toolBar.h"
#include "frontend/visualView/visualEditor.h"
#include "frontend/visualView/visualEditorView.h"

void CValueToolbar::OnPropertyCreated(Property *property)
{
}

void CValueToolbar::OnPropertySelected(Property *property)
{
}

void CValueToolbar::OnPropertyChanged(Property *property)
{
	if (property->GetName() == wxT("action_source")) {
		if (
			wxMessageBox(_("The data source has been changed. Refill Tools?"), _("Toolbar"),
				wxYES_NO) == wxYES) {

			while (GetChildCount() != 0) {
				m_visualHostContext->CutObject(GetChild(0), true);
			}

			IValueFrame *sourceElement = m_actionSource != wxNOT_FOUND ?
				FindControlByID(m_actionSource) : NULL;

			if (sourceElement) {
				actionData_t actions = 
					sourceElement->GetActions(sourceElement->GetTypeForm());
				for (unsigned int i = 0; i < actions.GetCount(); i++) {
					action_identifier_t action_id = actions.GetID(i);

					CValueToolBarItem *toolItem = dynamic_cast<CValueToolBarItem *>(
						m_formOwner->CreateControl("tool", this)
					);
					wxASSERT(toolItem);
					toolItem->m_controlName = wxT("tool_")+ actions.GetNameByID(action_id);
					toolItem->m_caption = actions.GetCaptionByID(action_id);
					toolItem->m_action = wxString::Format("%i", action_id);
					toolItem->ReadProperty();

					m_visualHostContext->InsertObject(toolItem, this);
					toolItem->SaveProperty();
				}
			}

			if (GetChildCount() == 0) {
				CValueToolbar::AddToolItem(); 
			}

			m_visualHostContext->RefreshEditor();
		}
	}
}
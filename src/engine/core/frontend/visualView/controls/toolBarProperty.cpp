#include "toolBar.h"
#include "frontend/visualView/visualEditor.h"
#include "frontend/visualView/visualHost.h"

void CValueToolbar::OnPropertyCreated(Property* property)
{
}

void CValueToolbar::OnPropertySelected(Property* property)
{
}

void CValueToolbar::OnPropertyChanged(Property* property)
{
	if (property->GetName() == wxT("action_source")) {
		
		int answer = wxMessageBox(
			_("The data source has been changed. Refill Tools?"), 
			_("Toolbar"), wxYES_NO
		);

		if (answer == wxYES) {

			while (GetChildCount() != 0) {
				m_visualHostContext->CutObject(GetChild(0), true);
			}

			IValueFrame* sourceElement = m_actionSource != wxNOT_FOUND ?
				FindControlByID(m_actionSource) : NULL;

			if (sourceElement) {
				actionData_t actions =
					sourceElement->GetActions(sourceElement->GetTypeForm());
				for (unsigned int i = 0; i < actions.GetCount(); i++) {
					action_identifier_t id = actions.GetID(i);

					CValueToolBarItem* toolItem = dynamic_cast<CValueToolBarItem*>(
						m_formOwner->CreateControl("tool", this)
						);
					wxASSERT(toolItem);
					toolItem->m_controlName = m_controlName + wxT("_") + actions.GetNameByID(id);
					toolItem->m_caption = actions.GetCaptionByID(id);
					toolItem->m_action = wxString::Format("%i", id);
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
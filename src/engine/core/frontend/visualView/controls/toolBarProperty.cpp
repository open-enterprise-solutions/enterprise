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
	if (m_actSource == property) {

		int answer = wxMessageBox(
			_("The data source has been changed. Refill Tools?"),
			_("Toolbar"), wxYES_NO
		);

		if (answer == wxYES) {

			while (GetChildCount() != 0) {
				g_visualHostContext->CutObject(GetChild(0), true);
			}

			IValueFrame* sourceElement = property->GetValueAsInteger() != wxNOT_FOUND ?
				FindControlByID(property->GetValueAsInteger()) : NULL;

			if (sourceElement) {
				actionData_t actions =
					sourceElement->GetActions(sourceElement->GetTypeForm());
				for (unsigned int i = 0; i < actions.GetCount(); i++) {
					const action_identifier_t &id = actions.GetID(i);
					if (id != wxNOT_FOUND) {
						CValueToolBarItem* toolItem = dynamic_cast<CValueToolBarItem*>(
							m_formOwner->CreateControl("tool", this)
						);
						wxASSERT(toolItem);
						toolItem->SetControlName(GetControlName() + wxT("_") + actions.GetNameByID(id));
						toolItem->SetCaption(actions.GetCaptionByID(id));
						toolItem->SetAction(wxString::Format("%i", id));
						g_visualHostContext->InsertObject(toolItem, this);
					}
					else {
						CValueToolBarSeparator* toolItemSeparator = dynamic_cast<CValueToolBarSeparator*>(
							m_formOwner->CreateControl("toolSeparator", this)
						);
						g_visualHostContext->InsertObject(toolItemSeparator, this);
					}
				}
			}

			if (GetChildCount() == 0) {
				CValueToolbar::AddToolItem();
			}

			g_visualHostContext->RefreshEditor();
		}
	}
}
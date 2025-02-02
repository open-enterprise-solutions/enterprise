#include "toolBar.h"
#include "frontend/visualView/visualHost.h"

void CValueToolbar::OnPropertyCreated(Property* property)
{
}

void CValueToolbar::OnPropertySelected(Property* property)
{
}

void CValueToolbar::OnPropertyChanged(Property* property, const wxVariant& oldValue, const wxVariant& newValue)
{
	if (m_actSource == property) {

		int answer = wxMessageBox(
			_("The data source has been changed. Refill Tools?"),
			_("Toolbar"), wxYES_NO
		);

		if (answer == wxYES) {

			while (GetChildCount() != 0) {
				g_visualHostContext->CutControl(GetChild(0), true);
			}

			IValueFrame* sourceElement = property->GetValueAsInteger() != wxNOT_FOUND ?
				FindControlByID(property->GetValueAsInteger()) : nullptr;

			if (sourceElement != nullptr) {
				actionData_t actionData =
					sourceElement->GetActions(sourceElement->GetTypeForm());
				for (unsigned int i = 0; i < actionData.GetCount(); i++) {
					const action_identifier_t &id = actionData.GetID(i);
					if (id != wxNOT_FOUND) {
						CValueToolBarItem* toolItem = dynamic_cast<CValueToolBarItem*>(
							m_formOwner->CreateControl("tool", this)
						);
						wxASSERT(toolItem);
						toolItem->SetControlName(GetControlName() + wxT("_") + actionData.GetNameByID(id));
						toolItem->SetCaption(actionData.GetCaptionByID(id));
						toolItem->SetAction(wxString::Format("%i", id));
						g_visualHostContext->InsertControl(toolItem, this);
					}
					else {
						CValueToolBarSeparator* toolItemSeparator = dynamic_cast<CValueToolBarSeparator*>(
							m_formOwner->CreateControl("toolSeparator", this)
						);
						g_visualHostContext->InsertControl(toolItemSeparator, this);
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
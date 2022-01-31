#include "toolbar.h"
#include "frontend/visualView/visualEditor.h"
#include "metadata/objects/baseObject.h"

//**********************************************************************************
//*                              Events                                            *
//**********************************************************************************

void CValueToolbar::OnTool(wxCommandEvent &event)
{
	IValueFrame *foundedControl = FindControlByID(event.GetId());

	if (foundedControl) {

		CAuiToolBar *toolBar = wxDynamicCast(GetWxObject(),
			CAuiToolBar);

		if (toolBar) {
			toolBar->SetFocus();
		}

		action_identifier_t actionId = foundedControl->GetPropertyAsInteger("action");
		IValueFrame *sourceElement = m_actionSource != wxNOT_FOUND ?
			FindControlByID(m_actionSource) : NULL;
		if (sourceElement && actionId > 0) {

			try {
				sourceElement->ExecuteAction(actionId,
					foundedControl->GetOwnerForm()
				);
			}
			catch (const CTranslateError *err) {
				wxMessageBox(err->what(), m_formOwner->m_caption);
			}
		}
		else {
			wxString eventName =
				foundedControl->GetPropertyAsString("action");
			if (eventName.Length() > 0) {
				CValueToolbar::CallFunction(eventName);
			}
		}

		if (m_visualHostContext) {
			m_visualHostContext->SelectObject(foundedControl);
		}
	}

	event.Skip();
}

void CValueToolbar::OnRightDown(wxMouseEvent &event)
{
	event.Skip();
}
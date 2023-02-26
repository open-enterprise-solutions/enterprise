#include "toolbar.h"
#include "frontend/visualView/visualEditor.h"
#include "core/metadata/metaObjects/objects/object.h"
#include "utils/stringUtils.h"
//**********************************************************************************
//*                              Events                                            *
//**********************************************************************************

void CValueToolbar::OnToolBarLeftDown(wxMouseEvent& event)
{
	CAuiToolBar* toolBar = wxDynamicCast(
		GetWxObject(), CAuiToolBar
	);
	wxASSERT(toolBar);
	if (g_visualHostContext != NULL) {
		wxAuiToolBarItem* foundedItem = toolBar->FindToolByPosition(event.GetX(), event.GetY());
		if (foundedItem == NULL)
			g_visualHostContext->SelectControl(this);
	}

	event.Skip();
}

#include "appData.h"
#include "frontend/visualView/visualHost.h"

void CValueToolbar::OnTool(wxCommandEvent& event)
{
	CVisualDocument* visualDoc = CValueToolbar::GetVisualDocument();
	if (visualDoc != NULL) {
		IVisualHost* visualHost = visualDoc->GetVisualView();
		bool isDemonstation = visualHost ? visualHost->IsDemonstration() : false;
		if (isDemonstation) {
			//event.Skip();
			//return;
		}
	}

	IValueFrame* foundedControl = FindControlByID(event.GetId());
	if (foundedControl != NULL) {
		CAuiToolBar* toolBar = wxDynamicCast(
			GetWxObject(), CAuiToolBar
		);
		wxASSERT(toolBar);
		if (toolBar != NULL)
			toolBar->SetFocus();
		CValueToolBarItem* toolItem = dynamic_cast<CValueToolBarItem*>(foundedControl);
		if (toolItem != NULL) {
			IValueFrame* sourceElement = GetActionSrc() != wxNOT_FOUND ?
				FindControlByID(GetActionSrc()) : NULL;
			const wxString &actionValue = toolItem->GetAction();
			action_identifier_t actionId = wxNOT_FOUND;
			if (sourceElement != NULL &&
				actionValue.ToInt(&actionId)) {
				try {
					sourceElement->ExecuteAction(actionId,
						toolItem->GetOwnerForm()
					);
				}
				catch (const CTranslateError* err) {
					wxMessageBox(err->what(), m_formOwner->GetCaption());
				}
			}
			else if (actionValue.Length() > 0) {
				CValueToolbar::CallFunction(actionValue, toolItem->GetValue());
			}
		}

		if (g_visualHostContext != NULL) {
			g_visualHostContext->SelectControl(foundedControl);
		}
	}

	event.Skip();
}

void CValueToolbar::OnToolDropDown(wxAuiToolBarEvent& event)
{
	CAuiToolBar* toolBar = wxDynamicCast(
		GetWxObject(), CAuiToolBar
	);

	if (event.IsDropDownClicked()) {

		wxPoint pos;
		wxRect itemRect = event.GetItemRect();
		pos.y = itemRect.height + 5;

		for (size_t idx = 0; idx < toolBar->GetToolCount(); idx++) {
			wxAuiToolBarItem* item = toolBar->FindToolByIndex(idx);
			wxASSERT(item);
			int toolid = item->GetId();
			if (toolid == event.GetId())
				break;
			wxRect rect = toolBar->GetToolRect(item->GetId());
			pos.x += rect.width;
		}

		wxMenu menuPopup;
		menuPopup.Append(wxID_OPEN, _("Test"));
		if (toolBar->PopupMenu(&menuPopup, pos)) {
			event.Skip();
		}
		else {
			event.Veto();
		}
	}
	else {
		event.Skip();
	}
}

void CValueToolbar::OnRightDown(wxMouseEvent& event)
{
	event.Skip();
}
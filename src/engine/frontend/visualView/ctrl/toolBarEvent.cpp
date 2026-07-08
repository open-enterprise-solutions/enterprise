#include "toolbar.h"
#include "backend/metaCollection/partial/commonObject.h"
#include "frontend/visualView/visualHostClient.h"

//**********************************************************************************
//*                              Events                                            *
//**********************************************************************************

void ibValueToolbar::OnToolBarLeftDown(wxMouseEvent& event)
{
#ifndef OES_USE_WEB
	// Designer-only: clicking empty space on the toolbar (not on a
	// tool) selects the toolbar control itself in the visual editor's
	// selection model. Web has no designer context — the body
	// collapses to a plain event.Skip.
	ibAuiToolBar* toolBar = wxDynamicCast(
		GetWxObject(), ibAuiToolBar
	);
	wxASSERT(toolBar);
	if (g_visualHostContext != nullptr) {
		const ibFormVisualDocument* visualDoc = ibValueToolbar::GetVisualDocument();
		if (visualDoc == nullptr || (visualDoc != nullptr && !visualDoc->IsVisualDemonstrationDoc())) {
			wxAuiToolBarItem* foundedItem = toolBar->FindToolByPosition(event.GetX(), event.GetY());
			if (foundedItem == nullptr) g_visualHostContext->SelectControl(this);
		}
	}
#endif

	event.Skip();
}

#include "backend/system/systemManager.h"

void ibValueToolbar::OnTool(wxCommandEvent& event)
{
#ifndef OES_USE_WEB
	// Designer preview mode (VisualDemonstrationDoc) swallows tool
	// clicks — the designer is previewing the form, not running it.
	// Web has no designer context; the guard collapses to "always run".
	const ibFormVisualDocument* visualDoc = ibValueToolbar::GetVisualDocument();
	if (visualDoc != nullptr && visualDoc->IsVisualDemonstrationDoc()) {
		event.Skip();
		return;
	}
#endif

	ibValueFrame* parentToolControl = FindControlByID(event.GetId());
	if (parentToolControl == nullptr) {
		event.Skip();
		return;
	}

#ifndef OES_USE_WEB
	// Desktop-only focus nudge — mouse click on a tool occasionally
	// leaves the toolbar itself unfocused on MSW, blocking subsequent
	// keyboard accel routing.
	if (auto* toolBar = wxDynamicCast(GetWxObject(), ibAuiToolBar))
		toolBar->SetFocus();
#endif

	ibValueToolBarItem* foundedToolControl =
		dynamic_cast<ibValueToolBarItem*>(parentToolControl);
	if (foundedToolControl == nullptr) {
		event.Skip();
		return;
	}

#ifndef OES_USE_WEB
	// Snapshot the visual-editor pointer BEFORE the action. Both
	// CallAsAction (eDefActionAndClose → CloseForm) and CallAsEvent
	// (arbitrary user script) can close the owner form, which destroys
	// every child control including this toolbar — `this` becomes
	// dangling. The old post-action `g_visualHostContext` check
	// dereferenced `this` via FindVisualEditor() and crashed.
	ibFrontendVisualEditorNotebook* const visualEditor = g_visualHostContext;
#endif

	const ibActionDescription& actionDesc = foundedToolControl->GetAction();
	const wxString& strAction = actionDesc.GetCustomAction();

	// Source resolution: explicit m_actSource wins; otherwise fall back
	// to the owner form, matching how ibValueToolbar::Update picks a
	// source. Desktop's historical `nullptr` fallback meant system
	// actions without an explicit source silently no-op'd — unified
	// with web's behaviour so both platforms dispatch identically.
	ibValueFrame* sourceElement = GetActionSrc() != wxNOT_FOUND
		? FindControlByID(GetActionSrc())
		: GetOwnerForm();

	if (sourceElement != nullptr && actionDesc.GetSystemAction() != wxNOT_FOUND) {
		try {
			sourceElement->CallAsAction(
				actionDesc.GetSystemAction(),
				GetOwnerForm()
			);
		}
		catch (const ibBackendAccessException& err) {
#ifndef OES_USE_WEB
			// Desktop surfaces access-denied as a modal Alert; web has
			// no modal channel yet, so it silently swallows.
			ibValueSystemFunction::Alert(err.GetErrorDescription());
#else
			(void)err;
#endif
		}
		catch (const ibBackendLockException& err) {
#ifndef OES_USE_WEB
			// Same pattern as access-denied — version-conflict and
			// row-lock-timeout both need user-visible "reload and retry"
			// feedback. Web routes the same exception through wfrontend's
			// ExceptionToJson → OES.handleBackendError toast/alert, so
			// here we silent-swallow on web to avoid double-surfacing.
			ibValueSystemFunction::Alert(err.GetErrorDescription());
#else
			(void)err;
#endif
		}
		catch (const ibBackendException&) {
		}
	}
	else if (strAction.Length() > 0) {
		CallAsEvent(strAction, parentToolControl->GetValue());
	}

#ifndef OES_USE_WEB
	// Use only the snapshot — never touch `this` after the action above.
	if (visualEditor != nullptr)
		visualEditor->SelectControl(parentToolControl);
#endif

	event.Skip();
}

#ifndef OES_USE_WEB
// Desktop-only: dropdown on wxAuiToolBar tool + right-click on the
// toolbar. Web's ibValueToolbar header excludes these methods (no
// wxAuiToolBarEvent in the web build), so the whole body is guarded.
void ibValueToolbar::OnToolDropDown(wxAuiToolBarEvent& event)
{
	ibAuiToolBar* toolBar = wxDynamicCast(
		GetWxObject(), ibAuiToolBar
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

void ibValueToolbar::OnRightDown(wxMouseEvent& event)
{
	event.Skip();
}
#endif
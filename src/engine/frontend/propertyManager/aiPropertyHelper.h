#ifndef __AI_PROPERTY_HELPER_H__
#define __AI_PROPERTY_HELPER_H__

// ------------------------------------------------------------------------
// AI-assisted property fill — Workmate parity for the metadata Property
// Inspector.
//
// The property grid wires `wxEVT_PG_RIGHT_CLICK` to
// `ibAIPropertyHelper::ShowContextMenu`, which surfaces a single
// "Сгенерировать через AI" command for the synonym / comment / tooltip /
// title family of text properties. On selection the helper builds a
// prompt from the owning object's kind + name + sibling attributes and
// dispatches `ibPluginManager::CompleteCodeAsync`; the callback updates
// the property and any peers in the JSON envelope (so one click on
// "Synonym" can also fill "Comment" / "Tooltip" if the model returned
// them).
//
// The helper holds no per-object state — a static atomic generation
// counter debounces re-clicks while a request is in flight.
// ------------------------------------------------------------------------

#include <wx/string.h>

#include "backend/propertyManager/propertyManager.h"
#include "frontend/frontend.h"

class wxPropertyGridManager;
class wxPGProperty;
class wxWindow;

class FRONTEND_API ibAIPropertyHelper {
public:
	// True for the properties that this helper knows how to fill —
	// "Synonym", "Comment", "Tooltip", "Title", "Caption" (case-sensitive
	// match against `ibProperty::GetName()`). Used by the object
	// inspector to decide whether to bother with the context menu.
	static bool IsAIEligible(const wxString& propertyName);

	// Right-click handler: builds + pops a context menu next to the
	// supplied property. `parent` is the property grid (or any wxWindow
	// that should be the popup's owner). Returns immediately — modal
	// menu handling and async dispatch live inside.
	static void ShowContextMenu(wxWindow* parent,
	                            wxPropertyGridManager* pgManager,
	                            wxPGProperty* pgProperty,
	                            ibProperty* property,
	                            ibPropertyObject* owner);

	// Fire the AI generation request immediately, without showing the
	// menu first. Public so a future toolbar button can drive the same
	// flow.
	static void RunGenerate(wxWindow* parent,
	                        wxPropertyGridManager* pgManager,
	                        wxPGProperty* pgProperty,
	                        ibProperty* property,
	                        ibPropertyObject* owner);

	// True when a request initiated by this helper is currently in
	// flight. The object inspector uses this to ignore re-entrant
	// right-clicks (Workmate's "AI button is grey until the request
	// returns" UX).
	static bool IsBusy();
};

#endif // __AI_PROPERTY_HELPER_H__

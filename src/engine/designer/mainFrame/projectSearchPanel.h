/////////////////////////////////////////////////////////////////////////////
// ibProjectSearchPanel — 1С:Workmate-style "Project Search by metadata".
//
// A dockable wxPanel that lets the user query the live configuration's
// metadata tree by name / synonym / comment. Single-click on a result
// opens the corresponding metadata object via the metadata-tree
// OpenFormMDI path, the same entry point used by double-clicking the
// Configuration tree.
//
// Scope (Phase 1):
//   - Top-level objects only (Catalog.<Name>, Document.<Name>, etc.).
//     Deeper introspection (attributes, tabular sections, forms) lands
//     in a follow-up commit; we already have metaBridge::HostMetaQuery
//     ready for that walk.
//   - Case-insensitive substring match across:
//       * GetName()      — the canonical identifier (English)
//       * GetSynonym()   — current-locale display name (ru-RU / uk-UA / en-US)
//       * GetComment()   — free-form developer-facing notes
//   - Empty query clears the list. No partial-match scoring — alphabetic
//     ordering by fullName keeps the results stable across keystrokes.
//
// Russian UI throughout — Workmate parity ships in Russian by design.
//
// The panel lives next to the Configuration tree on the left side of the
// AUI manager; it is created once at Designer startup (cheap — no work
// until the user types) and toggled via Ctrl+Shift+F.
/////////////////////////////////////////////////////////////////////////////

#ifndef _IB_PROJECT_SEARCH_PANEL_H_
#define _IB_PROJECT_SEARCH_PANEL_H_

#include <wx/panel.h>
#include <wx/string.h>

#include <vector>

class wxTextCtrl;
class wxListView;
class wxListEvent;
class wxCommandEvent;
class wxButton;
class wxStaticText;
class ibValueMetaObject;

class ibProjectSearchPanel : public wxPanel {
public:
	ibProjectSearchPanel(wxWindow* parent, int id = wxID_ANY);
	~ibProjectSearchPanel() override = default;

	// Move keyboard focus into the query input. Called by the Designer's
	// global Ctrl+Shift+F handler so the user can start typing immediately
	// from anywhere in the app.
	void FocusQueryInput();

private:
	struct Hit {
		ibValueMetaObject* metaObject = nullptr;  // owning ptr lives in activeMetaData
		wxString           kindLabel;             // "Catalog", "Document", …
		wxString           fullName;              // "Catalog.Products"
		wxString           synonym;               // current-locale display, may be empty
	};

	void OnQueryChanged(wxCommandEvent& event);
	void OnClearClick(wxCommandEvent& event);
	void OnResultActivated(wxListEvent& event);
	void OnResultSelected(wxListEvent& event);

	// Walk activeMetaData->GetCommonMetaObject() once per query and
	// populate m_hits with every top-level object that matches `needle`.
	// Returns the number of hits so the caller can update the status line
	// without re-iterating the vector.
	std::size_t RunSearch(const wxString& needle);

	// Refresh the wxListView from m_hits. No-op when the panel is hidden;
	// the user can't see stale state and the next query will re-render.
	void RebuildList();

	// Navigate the Configuration tree pane to `hit` and pop its form via
	// the standard OpenFormMDI path. Falls back to a status-line message
	// when the tree isn't reachable (configuration not loaded).
	void NavigateTo(const Hit& hit);

	wxTextCtrl*   m_queryInput  = nullptr;
	wxButton*     m_clearButton = nullptr;
	wxListView*   m_resultList  = nullptr;
	wxStaticText* m_statusLine  = nullptr;

	std::vector<Hit> m_hits;
};

#endif // _IB_PROJECT_SEARCH_PANEL_H_

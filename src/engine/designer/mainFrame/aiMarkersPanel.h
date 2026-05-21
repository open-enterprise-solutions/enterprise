/////////////////////////////////////////////////////////////////////////////
// ibAiMarkersPanel — aggregated marker list for the Designer AI Assistant.
//
// Workmate parity: "Маркеры: Установка и удаление маркеров в редакторе".
// The panel aggregates editor markers (errors, warnings, AI-suggested
// fixes) across the project into a single sortable list. Clicking a row
// jumps the editor to the file:line of the marker; the "Применить все
// автофиксы" button walks rows whose AI fix is populated and applies them
// in sequence.
//
// Source of markers (Phase 1):
//   - ibOutputWindow's accumulated error / warning lines (real source). The
//     output window already maps lineNumber → (file, docPath, srcLine);
//     we read that map plus a derived severity from the marker overlay
//     state. No new subsystem to feed messages — we surface what's there.
//
// Future sources (Phase 2):
//   - Σ-Check + EDT marker subsystem hook when it lands. The panel uses
//     a polymorphic MarkerSource so adding a new source is a single class
//     subclassing MarkerSource — no panel changes required.
//
// Russian UI throughout.
/////////////////////////////////////////////////////////////////////////////

#ifndef _IB_AI_MARKERS_PANEL_H_
#define _IB_AI_MARKERS_PANEL_H_

#include <wx/panel.h>
#include <wx/string.h>

#include <vector>

class wxListView;
class wxListEvent;
class wxButton;
class wxStaticText;
class wxCommandEvent;

class ibAiMarkersPanel : public wxPanel {
public:
	ibAiMarkersPanel(wxWindow* parent, int id = wxID_ANY);
	~ibAiMarkersPanel() override = default;

	// One marker row. Severity is the raw string "error" / "warning" /
	// "info" / "fix" so future severities flow through without a schema
	// change. fix is the AI-suggested replacement (empty when the marker
	// has no automatic fix attached).
	struct Marker {
		wxString severity;
		wxString file;
		wxString docPath;   // metadata path inside the configuration, may be empty
		int      line     = 0;
		wxString message;
		wxString fix;
	};

	// Replace the current marker list and re-render. Called by external
	// code that injects markers from a new source (Σ-Check or an AI
	// review run). Idempotent if the list is unchanged — wxListView
	// rebuild is cheap at the expected scale (O(100) markers).
	void SetMarkers(std::vector<Marker> markers);

	// Append one marker and re-render. Faster than SetMarkers when a
	// source streams markers one at a time.
	void AddMarker(const Marker& m);

	// Walk the output window's existing error/warning lines and ingest
	// them into m_markers. Called once at construction; can be invoked
	// again from outside to refresh after a long build/check run.
	void RefreshFromOutputWindow();

private:
	void OnRowActivated(wxListEvent& event);
	void OnApplyAllFixes(wxCommandEvent& event);
	void OnRefreshClick(wxCommandEvent& event);

	void Rebuild();

	std::vector<Marker> m_markers;

	wxListView*   m_list           = nullptr;
	wxButton*     m_applyAllButton = nullptr;
	wxButton*     m_refreshButton  = nullptr;
	wxStaticText* m_statusLine     = nullptr;
};

#endif // _IB_AI_MARKERS_PANEL_H_

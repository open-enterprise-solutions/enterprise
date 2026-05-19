/////////////////////////////////////////////////////////////////////////////
// Detail pane — wxHtmlWindow rendering a single ibHelpEntry as
// structured HTML with Description / Syntax / Parameters / Example /
// Availability sections.
/////////////////////////////////////////////////////////////////////////////

#ifndef _IB_HELP_DETAIL_VIEW_H_
#define _IB_HELP_DETAIL_VIEW_H_

#include <wx/panel.h>
#include <wx/html/htmlwin.h>

struct ibHelpEntry;
class ibHelpPaneView;

class ibHelpDetailView : public wxPanel {
public:
	ibHelpDetailView(wxWindow* parent, ibHelpPaneView* pane);

	// Replace the rendered content with the entry's HTML. Pass
	// nullptr to display the empty / "select an entry" state.
	void ShowEntry(const ibHelpEntry* entry);

	// Font-size adjustment. wxHtmlWindow's SetFonts accepts a size
	// array indexed by the <font size="N"> attribute (1..7); we shift
	// the array up / down by one notch per click and re-render.
	void AdjustFontSize(int delta);

private:
	ibHelpPaneView* m_pane = nullptr;
	wxHtmlWindow*   m_html = nullptr;

	// Base font size offset applied on top of the default Helvetica
	// scale. Clamped to [-2, +4] to keep the layout sane on Retina
	// and 96-DPI both. Persisted via the host frame's options.xml in
	// a follow-up iteration.
	int             m_fontSizeBoost = 0;

	void ApplyFontScale();
	wxString        m_lastEntryId;

	// Click on a link in the HTML body (e.g. see_also entries) routes
	// through the parent pane's ShowEntry so the history stack stays
	// coherent.
	void OnLinkClicked(wxHtmlLinkEvent& event);

	// Right-click on the HTML pane — opens a Copy / Select All menu
	// and copies the current selection into the system clipboard. Bound
	// from the ctor; needs access to m_pane / m_html so it's an
	// instance method.
	void OnContextMenu(wxContextMenuEvent& event);

	// Non-static because RenderHtml resolves see_also ids to human
	// labels via m_pane->GetCorpus().
	wxString        RenderHtml(const ibHelpEntry& entry) const;
	static wxString EscapeHtml(const wxString& raw);
	static wxString FormatCodeBlock(const wxString& code);
};

#endif // _IB_HELP_DETAIL_VIEW_H_

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

private:
	ibHelpPaneView* m_pane = nullptr;
	wxHtmlWindow*   m_html = nullptr;

	// Click on a link in the HTML body (e.g. see_also entries) routes
	// through the parent pane's ShowEntry so the history stack stays
	// coherent.
	void OnLinkClicked(wxHtmlLinkEvent& event);

	static wxString RenderHtml(const ibHelpEntry& entry);
	static wxString EscapeHtml(const wxString& raw);
	static wxString FormatCodeBlock(const wxString& code);
};

#endif // _IB_HELP_DETAIL_VIEW_H_

/////////////////////////////////////////////////////////////////////////////
// ibHelpDetailView — wxHtmlWindow rendering structured entry HTML.
//
// wxHtmlWindow understands a deliberately small HTML subset (a near-HTML 3.2
// renderer with limited attribute support). The styling here is built on
// table + cellspacing tricks that work across wxHtmlWindow's actual
// capabilities rather than CSS that it would silently ignore.
/////////////////////////////////////////////////////////////////////////////

#include "frontend/help/helpDetailView.h"

#include "frontend/help/helpPaneView.h"

#include "backend/help/helpCorpus.h"
#include "backend/help/helpEntry.h"

#include <wx/sizer.h>
#include <wx/uri.h>

ibHelpDetailView::ibHelpDetailView(wxWindow* parent, ibHelpPaneView* pane)
    : wxPanel(parent, wxID_ANY), m_pane(pane) {
	m_html = new wxHtmlWindow(this, wxID_ANY);

	// wxHtmlWindow's default font is small on Retina. Bump one notch and
	// pin the serif/sans/mono family so monospace blocks render distinct
	// from prose.
	static const int fontSizes[] = { 10, 11, 12, 14, 16, 20, 28 };
	m_html->SetFonts(wxT("Helvetica"), wxT("Menlo"), fontSizes);

	auto* sizer = new wxBoxSizer(wxVERTICAL);
	sizer->Add(m_html, 1, wxEXPAND);
	SetSizer(sizer);

	Bind(wxEVT_HTML_LINK_CLICKED,
	     &ibHelpDetailView::OnLinkClicked, this);
}

void ibHelpDetailView::ShowEntry(const ibHelpEntry* entry) {
	if (entry == nullptr) {
		m_html->SetPage(
		    wxT("<html><body bgcolor=\"#fafbfc\">"
		         "<table border=\"0\" cellpadding=\"20\" width=\"100%\"><tr><td>"
		         "<font color=\"#888888\" size=\"3\"><i>")
		    + wxString(_("Select an entry in the tree on the left, or "
		                  "press Ctrl+F1 on an identifier in the editor."))
		    + wxT("</i></font>"
		           "</td></tr></table></body></html>"));
		return;
	}
	m_html->SetPage(RenderHtml(*entry));
}

wxString ibHelpDetailView::EscapeHtml(const wxString& raw) {
	wxString out;
	out.reserve(raw.size());
	for (auto it = raw.begin(); it != raw.end(); ++it) {
		const wxUniChar c = *it;
		if      (c == wxT('&'))  out += wxT("&amp;");
		else if (c == wxT('<'))  out += wxT("&lt;");
		else if (c == wxT('>'))  out += wxT("&gt;");
		else if (c == wxT('"'))  out += wxT("&quot;");
		else if (c == wxT('\n')) out += wxT("<br>");
		else                     out += c;
	}
	return out;
}

wxString ibHelpDetailView::FormatCodeBlock(const wxString& code) {
	wxString escaped;
	escaped.reserve(code.size());
	for (auto it = code.begin(); it != code.end(); ++it) {
		const wxUniChar c = *it;
		if      (c == wxT('&')) escaped += wxT("&amp;");
		else if (c == wxT('<')) escaped += wxT("&lt;");
		else if (c == wxT('>')) escaped += wxT("&gt;");
		else                    escaped += c;
	}
	// Outer table provides the tinted background + border that wxHtmlWindow
	// honours; nested <tt> + <font> keeps the monospace family bound to the
	// code text. cellpadding=8 gives breathing room around the block.
	return wxT("<table border=\"0\" cellspacing=\"0\" cellpadding=\"8\" "
	             "bgcolor=\"#f3f4f6\" width=\"100%\"><tr><td>"
	             "<font face=\"Menlo\" size=\"2\" color=\"#1a1a1a\"><tt>")
	       + escaped
	       + wxT("</tt></font></td></tr></table>");
}

wxString ibHelpDetailView::RenderHtml(const ibHelpEntry& entry) {
	wxString html;
	html.reserve(4096);

	// Outer container: light page background, inner table holding content
	// with consistent left-margin via cellpadding.
	html += wxT("<html><body bgcolor=\"#ffffff\">"
	             "<table border=\"0\" cellpadding=\"12\" cellspacing=\"0\" "
	             "width=\"100%\"><tr><td>");

	// === Title block ========================================================
	// Local name as h2 with a coloured underline (table-based since
	// wxHtmlWindow ignores CSS borders). English alias appears below as
	// muted subtitle when distinct from local name.
	html += wxT("<font size=\"5\" color=\"#1f2937\"><b>");
	html += EscapeHtml(entry.nameLocal.IsEmpty() ? entry.nameEn
	                                                : entry.nameLocal);
	html += wxT("</b></font>");
	if (!entry.nameEn.IsEmpty() && entry.nameEn != entry.nameLocal) {
		html += wxT("<br><font size=\"3\" color=\"#6b7280\">");
		html += EscapeHtml(entry.nameEn);
		html += wxT("</font>");
	}
	// Coloured rule under the title.
	html += wxT("<table border=\"0\" cellspacing=\"0\" cellpadding=\"0\" "
	             "width=\"100%\" bgcolor=\"#2563eb\" height=\"2\">"
	             "<tr><td></td></tr></table>");
	html += wxT("<br>");

	// Inline one-liner signature directly under the title.
	if (!entry.signature.IsEmpty()) {
		html += wxT("<font face=\"Menlo\" size=\"3\" color=\"#0f172a\">");
		html += EscapeHtml(entry.signature);
		html += wxT("</font><br><br>");
	}

	auto section = [&](const wxString& title, const wxString& body,
	                    bool monospace) {
		if (body.IsEmpty()) return;
		html += wxT("<font size=\"3\" color=\"#2563eb\"><b>");
		html += EscapeHtml(title);
		html += wxT("</b></font><br>");
		if (monospace) html += FormatCodeBlock(body);
		else {
			html += wxT("<font color=\"#1f2937\">");
			html += EscapeHtml(body);
			html += wxT("</font>");
		}
		html += wxT("<br><br>");
	};

	section(_("Description"),  entry.description, false);
	section(_("Syntax"),        entry.syntaxBlock, true);
	section(_("Parameters"),    entry.parameters,  false);
	section(_("Returns"),       entry.returnDescr, false);
	section(_("Example"),       entry.example,     true);
	section(_("Availability"),  entry.availability, false);

	// === See also ===========================================================
	if (!entry.seeAlso.empty()) {
		html += wxT("<font size=\"3\" color=\"#2563eb\"><b>");
		html += EscapeHtml(_("See also"));
		html += wxT("</b></font><br>");
		bool first = true;
		for (const wxString& ref : entry.seeAlso) {
			if (!first) html += wxT(" &middot; ");
			first = false;
			html += wxT("<a href=\"oeshelp://");
			html += EscapeHtml(ref);
			html += wxT("\"><font color=\"#2563eb\">");
			html += EscapeHtml(ref);
			html += wxT("</font></a>");
		}
		html += wxT("<br><br>");
	}

	// === Draft badge ========================================================
	if (!entry.reviewed) {
		html += wxT("<table border=\"0\" cellspacing=\"0\" cellpadding=\"6\" "
		             "bgcolor=\"#fef3c7\" width=\"100%\"><tr><td>"
		             "<font color=\"#92400e\" size=\"2\"><i>");
		html += EscapeHtml(_("Draft — this entry awaits editorial review."));
		html += wxT("</i></font></td></tr></table>");
	}

	html += wxT("</td></tr></table></body></html>");
	return html;
}

void ibHelpDetailView::OnLinkClicked(wxHtmlLinkEvent& event) {
	const wxString href = event.GetLinkInfo().GetHref();
	const wxString prefix = wxT("oeshelp://");
	if (!href.StartsWith(prefix)) return;
	const wxString id = href.Mid(prefix.length());
	if (m_pane) m_pane->ShowEntry(id);
}

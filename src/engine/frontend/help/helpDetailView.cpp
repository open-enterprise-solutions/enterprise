/////////////////////////////////////////////////////////////////////////////
// ibHelpDetailView — wxHtmlWindow rendering structured entry HTML.
//
// The renderer is intentionally minimal: bold section headers, code
// blocks via <pre>, inline links via <a href="oeshelp://<id>">. Full
// markdown→HTML translation is Phase 6 once description fields land
// with richer content; Phase 3 ships plaintext-with-paragraph-breaks.
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

	auto* sizer = new wxBoxSizer(wxVERTICAL);
	sizer->Add(m_html, 1, wxEXPAND);
	SetSizer(sizer);

	Bind(wxEVT_HTML_LINK_CLICKED,
	     &ibHelpDetailView::OnLinkClicked, this);
}

void ibHelpDetailView::ShowEntry(const ibHelpEntry* entry) {
	if (entry == nullptr) {
		m_html->SetPage(
		    wxT("<html><body><p><i>")
		    + wxString(_("Виберіть запис у дереві ліворуч або через "
		                  "Ctrl+F1 на ідентифікаторі в редакторі."))
		    + wxT("</i></p></body></html>"));
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
	// wxHtmlWindow supports <pre> but not syntax highlighting in Phase 3.
	// Plain monospace block with HTML-escaped contents.
	wxString escaped;
	escaped.reserve(code.size());
	for (auto it = code.begin(); it != code.end(); ++it) {
		const wxUniChar c = *it;
		if      (c == wxT('&')) escaped += wxT("&amp;");
		else if (c == wxT('<')) escaped += wxT("&lt;");
		else if (c == wxT('>')) escaped += wxT("&gt;");
		else                    escaped += c;
	}
	return wxT("<pre>") + escaped + wxT("</pre>");
}

wxString ibHelpDetailView::RenderHtml(const ibHelpEntry& entry) {
	wxString html;
	html.reserve(2048);
	html += wxT("<html><body>");

	// Title — local name plus English alias parenthesised, matching
	// 1C convention "Дата (Date)".
	html += wxT("<h2>");
	html += EscapeHtml(entry.nameLocal.IsEmpty() ? entry.nameEn
	                                                : entry.nameLocal);
	if (!entry.nameEn.IsEmpty() && entry.nameEn != entry.nameLocal) {
		html += wxT(" <font color=\"#666666\">(");
		html += EscapeHtml(entry.nameEn);
		html += wxT(")</font>");
	}
	html += wxT("</h2>");

	auto section = [&](const wxString& title, const wxString& body,
	                    bool monospace) {
		if (body.IsEmpty()) return;
		html += wxT("<p><b>");
		html += EscapeHtml(title);
		html += wxT(":</b><br>");
		if (monospace) html += FormatCodeBlock(body);
		else           html += EscapeHtml(body);
		html += wxT("</p>");
	};

	section(_("Опис"),         entry.description, false);
	section(_("Синтаксис"),    entry.syntaxBlock, true);
	section(_("Параметри"),    entry.parameters,  false);
	section(_("Повертає"),     entry.returnDescr, false);
	section(_("Приклад"),      entry.example,     true);
	section(_("Доступність"),  entry.availability, false);

	// "Див. також" — clickable links into the same pane.
	if (!entry.seeAlso.empty()) {
		html += wxT("<p><b>");
		html += EscapeHtml(_("Див. також"));
		html += wxT(":</b><br>");
		bool first = true;
		for (const wxString& ref : entry.seeAlso) {
			if (!first) html += wxT(", ");
			first = false;
			html += wxT("<a href=\"oeshelp://");
			html += EscapeHtml(ref);
			html += wxT("\">");
			html += EscapeHtml(ref);
			html += wxT("</a>");
		}
		html += wxT("</p>");
	}

	if (!entry.reviewed) {
		html += wxT("<hr><p><i><font color=\"#aa6600\">");
		html += EscapeHtml(_("Чернетка: запис очікує редакторської перевірки."));
		html += wxT("</font></i></p>");
	}

	html += wxT("</body></html>");
	return html;
}

void ibHelpDetailView::OnLinkClicked(wxHtmlLinkEvent& event) {
	const wxString href = event.GetLinkInfo().GetHref();
	const wxString prefix = wxT("oeshelp://");
	if (!href.StartsWith(prefix)) return;
	const wxString id = href.Mid(prefix.length());
	if (m_pane) m_pane->ShowEntry(id);
}

/////////////////////////////////////////////////////////////////////////////
// ibHelpDetailView — wxHtmlWindow rendering structured entry HTML.
//
// wxHtmlWindow understands a deliberately small HTML subset (a near-HTML 3.2
// renderer with limited attribute support). The styling here is built on
// table + cellspacing tricks that work across wxHtmlWindow's actual
// capabilities rather than CSS that it would silently ignore.
//
// `<pre>` is used for code blocks so newlines and indentation survive
// rendering; the surrounding table cell paints the tinted background
// because `<pre bgcolor=...>` is unreliable across wxHtmlWindow versions.
/////////////////////////////////////////////////////////////////////////////

#include "frontend/help/helpDetailView.h"

#include "frontend/help/helpPaneView.h"

#include "backend/help/helpCorpus.h"
#include "backend/help/helpEntry.h"
#include "backend/compiler/compileCode.h"

#include <wx/clipbrd.h>
#include <wx/menu.h>
#include <wx/sizer.h>
#include <wx/uri.h>

ibHelpDetailView::ibHelpDetailView(wxWindow* parent, ibHelpPaneView* pane)
    : wxPanel(parent, wxID_ANY), m_pane(pane) {
	m_html = new wxHtmlWindow(this, wxID_ANY);

	auto* sizer = new wxBoxSizer(wxVERTICAL);
	sizer->Add(m_html, 1, wxEXPAND);
	SetSizer(sizer);

	ApplyFontScale();

	Bind(wxEVT_HTML_LINK_CLICKED,
	     &ibHelpDetailView::OnLinkClicked, this);

	// Right-click context menu on the HTML pane — wxHtmlWindow's
	// built-in handling is "select with mouse drag, Ctrl+C copies"; this
	// adds an explicit "Copy" item so the discoverable workflow exists
	// and so we can also offer "Copy all" (entire entry as plain text).
	m_html->Bind(wxEVT_CONTEXT_MENU,
	             &ibHelpDetailView::OnContextMenu, this);
}

void ibHelpDetailView::ApplyFontScale() {
	// Base + boost shifts the whole 7-step ladder uniformly so <font
	// size="N"> tags keep their relative weighting.
	const int base = 11 + m_fontSizeBoost;
	const int sizes[] = {
	    std::max(6, base - 2), std::max(6, base - 1),
	    base,                  std::max(6, base + 2),
	    std::max(6, base + 4), std::max(6, base + 7),
	    std::max(6, base + 12)
	};
	m_html->SetFonts(wxT("Helvetica"), wxT("Menlo"), sizes);
}

void ibHelpDetailView::AdjustFontSize(int delta) {
	const int next = m_fontSizeBoost + delta;
	if (next < -2 || next > 6) return;  // keep in sane range
	m_fontSizeBoost = next;
	ApplyFontScale();
	// Re-render whatever was last shown so the new sizes take effect
	// immediately — wxHtmlWindow doesn't auto-relayout on SetFonts.
	if (!m_lastEntryId.IsEmpty() && m_pane) {
		if (auto corpus = m_pane->GetCorpus()) {
			if (const ibHelpEntry* e = corpus->FindById(m_lastEntryId))
				m_html->SetPage(RenderHtml(*e));
		}
	}
}

void ibHelpDetailView::ShowEntry(const ibHelpEntry* entry) {
	m_lastEntryId = entry ? entry->id : wxString();
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

namespace {

wxString EscapeForPre(const wxString& code) {
	// Same as EscapeHtml but newlines stay as real `\n` inside <pre>
	// so the layout is preserved by the parser.
	wxString out;
	out.reserve(code.size());
	for (auto it = code.begin(); it != code.end(); ++it) {
		const wxUniChar c = *it;
		if      (c == wxT('&')) out += wxT("&amp;");
		else if (c == wxT('<')) out += wxT("&lt;");
		else if (c == wxT('>')) out += wxT("&gt;");
		else                    out += c;
	}
	return out;
}

} // namespace

wxString ibHelpDetailView::FormatCodeBlock(const wxString& code) {
	// Tinted background via outer table; <pre> preserves indentation
	// and newlines verbatim. The inner <font face="Menlo"> binds the
	// monospace family to the block content (wxHtmlWindow respects
	// face= on font inside pre even though it would ignore CSS).
	return wxT("<table border=\"0\" cellspacing=\"0\" cellpadding=\"8\" "
	             "bgcolor=\"#f3f4f6\" width=\"100%\"><tr><td>"
	             "<font face=\"Menlo\" size=\"2\" color=\"#1a1a1a\"><pre>")
	       + EscapeForPre(code)
	       + wxT("</pre></font></td></tr></table>");
}

wxString ibHelpDetailView::RenderHtml(const ibHelpEntry& entry) const {
	wxString html;
	html.reserve(4096);

	// Outer container with consistent left-margin via cellpadding.
	html += wxT("<html><body bgcolor=\"#ffffff\">"
	             "<table border=\"0\" cellpadding=\"12\" cellspacing=\"0\" "
	             "width=\"100%\"><tr><td>");

	// === Title block ========================================================
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

	// Show only the syntax form matching the active compile style. The
	// alternative form lives in the corpus but distracts when a CES
	// project doesn't need to see VES braces and vice versa. Falls back
	// to whichever form is non-empty when the preferred one is missing.
	const bool prefersVes = (ibCompileCode::GetCodeStyle() == 0);
	const wxString& activeSyntax =
	    prefersVes && !entry.syntaxBlockVes.IsEmpty() ? entry.syntaxBlockVes
	  : !entry.syntaxBlock.IsEmpty()                    ? entry.syntaxBlock
	  : entry.syntaxBlockVes;
	section(_("Syntax"), activeSyntax, true);

	section(_("Parameters"),    entry.parameters,  false);
	section(_("Returns"),       entry.returnDescr, false);

	const wxString& activeExample =
	    prefersVes && !entry.exampleVes.IsEmpty() ? entry.exampleVes
	  : !entry.example.IsEmpty()                    ? entry.example
	  : entry.exampleVes;
	section(_("Example"), activeExample, true);

	section(_("Availability"),  entry.availability, false);

	// === See also ===========================================================
	// Resolve each id to a human-readable label via the corpus snapshot
	// held by the pane. Falls back to the raw id when the referenced
	// entry is missing (a loader warning would have fired at corpus
	// build time; here we just stay readable).
	if (!entry.seeAlso.empty()) {
		auto corpus = m_pane ? m_pane->GetCorpus()
		                       : std::shared_ptr<const ibHelpCorpus>();
		html += wxT("<font size=\"3\" color=\"#2563eb\"><b>");
		html += EscapeHtml(_("See also"));
		html += wxT("</b></font><br>");
		bool first = true;
		for (const wxString& ref : entry.seeAlso) {
			if (!first) html += wxT(" &middot; ");
			first = false;
			wxString label = ref;
			if (corpus) {
				if (const ibHelpEntry* target = corpus->FindById(ref)) {
					if (!target->nameLocal.IsEmpty())
						label = target->nameLocal;
					else if (!target->nameEn.IsEmpty())
						label = target->nameEn;
				}
			}
			html += wxT("<a href=\"oeshelp://");
			html += EscapeHtml(ref);
			html += wxT("\"><font color=\"#2563eb\">");
			html += EscapeHtml(label);
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

void ibHelpDetailView::OnContextMenu(wxContextMenuEvent& event) {
	wxMenu menu;
	menu.Append(wxID_COPY,      _("Copy"));
	menu.Append(wxID_SELECTALL, _("Select All"));

	const wxString selected = m_html->SelectionToText();
	menu.Enable(wxID_COPY, !selected.IsEmpty());

	menu.Bind(wxEVT_MENU, [this](wxCommandEvent&) {
		const wxString text = m_html->SelectionToText();
		if (text.IsEmpty()) return;
		if (wxTheClipboard->Open()) {
			wxTheClipboard->SetData(new wxTextDataObject(text));
			wxTheClipboard->Close();
		}
	}, wxID_COPY);

	menu.Bind(wxEVT_MENU, [this](wxCommandEvent&) {
		m_html->SelectAll();
	}, wxID_SELECTALL);

	m_html->PopupMenu(&menu);
	event.Skip(false);
}

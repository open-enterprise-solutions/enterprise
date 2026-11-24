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

#include "appstudio/syntaxHelper/helpDetailView.h"

#include "appstudio/syntaxHelper/helpPaneView.h"

#include "core/syntaxHelper/helpCorpus.h"
#include "core/syntaxHelper/helpEntry.h"

#include <wx/clipbrd.h>
#include <wx/menu.h>
#include <wx/sizer.h>
#include <wx/uri.h>

#include "deskgui/univ/theme.h"          // ibThemeEngine — the page's inks are the theme's
#include "deskgui/univ/colourScheme.h"   // wxTHEME_COLOUR

namespace {

// A colour of the theme in force, as the page's markup names it. wxHtmlWindow paints what the page says, so the page
// is written in the look's colours - written in light ones, it stayed white in the dark look (2026-11-24, Max).
wxString Ink(ibColourScheme::StdColour colour)
{
	return ibThemeEngine::Get()->GetColourScheme()->Get(colour).GetAsString(wxC2S_HTML_SYNTAX);
}

} // namespace

ibHelpDetailView::ibHelpDetailView(wxWindow* parent, ibHelpPaneView* pane)
    : ibPanel(parent, wxID_ANY), m_pane(pane) {
	m_html = new wxHtmlWindow(this, wxID_ANY);

	auto* sizer = new wxBoxSizer(wxVERTICAL);
	sizer->Add(m_html, 1, wxEXPAND);
	SetSizer(sizer);

	ApplyFontScale();

	Bind(wxEVT_HTML_LINK_CLICKED,
	     &ibHelpDetailView::OnLinkClicked, this);
	Bind(wxEVT_SYS_COLOUR_CHANGED, &ibHelpDetailView::OnSysColourChanged, this);

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
	// the text in the system's face, as wx's own SetStandardFonts takes it (wxNORMAL_FONT) - "Helvetica" stood here
	// until 2026-11-12, a face Windows has not and maps to Arial
	m_html->SetFonts(wxNORMAL_FONT->GetFaceName(), wxT("Menlo"), sizes);
}

void ibHelpDetailView::SetFontSizeBoost(int boost) {
	if (boost < -2) boost = -2;
	if (boost >  6) boost =  6;
	if (boost == m_fontSizeBoost) return;
	m_fontSizeBoost = boost;
	ApplyFontScale();
	if (!m_lastEntryId.IsEmpty() && m_pane) {
		if (const ibHelpEntry* e = m_pane->Entry(m_lastEntryId))
			m_html->SetPage(RenderHtml(*e));
	}
}

void ibHelpDetailView::AdjustFontSize(int delta) {
	const int next = m_fontSizeBoost + delta;
	if (next < -2 || next > 6) return;  // keep in sane range
	m_fontSizeBoost = next;
	ApplyFontScale();
	// Re-render whatever was last shown so the new sizes take effect
	// immediately — wxHtmlWindow doesn't auto-relayout on SetFonts.
	if (!m_lastEntryId.IsEmpty() && m_pane) {
		if (const ibHelpEntry* e = m_pane->Entry(m_lastEntryId))
			m_html->SetPage(RenderHtml(*e));
	}
}

void ibHelpDetailView::ShowEntry(const ibHelpEntry* entry) {
	m_lastEntryId = entry ? entry->id : ibString();
	if (entry == nullptr) {
		m_html->SetPage(
		    wxT("<html><body bgcolor=\"") + Ink(ibColourScheme::WINDOW) + wxT("\">"
		         "<table border=\"0\" cellpadding=\"20\" width=\"100%\"><tr><td>"
		         "<font color=\"") + Ink(ibColourScheme::CONTROL_TEXT_DISABLED) + wxT("\" size=\"3\"><i>")
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

// Apply a tiny markdown subset to the description / parameters /
// returns / availability fields: `**bold**` -> <b>bold</b> and
// `inline code` -> <tt>inline code</tt>. Runs AFTER EscapeHtml so we
// don't have to worry about HTML injection from the corpus.
// Two-pass parser so inline `code` spans are recognised first and
// excluded from bold processing — keeps `**` verbatim inside code.
// Unmatched delimiters degrade gracefully: a stray `**` or single
// backtick is emitted literally instead of corrupting the rest of
// the output.
wxString ApplyInlineMarkdown(const wxString& escaped)
{
	struct Span { size_t begin; size_t end; }; // half-open, content only
	std::vector<Span> codeSpans;

	// Pass 1: locate balanced backtick pairs.
	for (size_t i = 0; i + 1 < escaped.size(); ++i) {
		if (escaped[i] != wxT('`')) continue;
		// Find matching closing backtick. Bail out on newline so
		// unterminated code on a single line doesn't swallow the
		// whole paragraph.
		size_t j = i + 1;
		while (j < escaped.size() && escaped[j] != wxT('`')) {
			if (escaped[j] == wxT('\n')) { j = escaped.size(); break; }
			++j;
		}
		if (j >= escaped.size()) break; // no closing tick
		codeSpans.push_back({ i + 1, j });
		i = j;
	}

	// Pass 2: locate balanced ** pairs OUTSIDE code spans. Same
	// graceful-degrade rule.
	std::vector<Span> boldSpans;
	auto inAnyCode = [&codeSpans](size_t i) {
		for (const auto& s : codeSpans)
			if (i >= s.begin - 1 && i < s.end + 1) return true; // include delimiters
		return false;
	};
	for (size_t i = 0; i + 1 < escaped.size(); ++i) {
		if (escaped[i] != wxT('*') || escaped[i + 1] != wxT('*')) continue;
		if (inAnyCode(i)) { ++i; continue; }
		size_t j = i + 2;
		while (j + 1 < escaped.size()) {
			if (escaped[j] == wxT('*') && escaped[j + 1] == wxT('*') && !inAnyCode(j)) break;
			++j;
		}
		if (j + 1 >= escaped.size() || escaped[j] != wxT('*')) break;
		boldSpans.push_back({ i + 2, j });
		i = j + 1;
	}

	// Pass 3: emit output, wrapping recognised spans with HTML tags.
	wxString out;
	out.reserve(escaped.size() + 64);
	auto codeIt = codeSpans.begin();
	auto boldIt = boldSpans.begin();
	for (size_t i = 0; i < escaped.size(); ++i) {
		// Skip the opening delimiter and emit the tag.
		if (codeIt != codeSpans.end() && i == codeIt->begin - 1) {
			out += wxT("<font face=\"Menlo\" color=\"") + Ink(ibColourScheme::CONTROL_TEXT) + wxT("\"><tt>");
			continue;
		}
		if (codeIt != codeSpans.end() && i == codeIt->end) {
			out += wxT("</tt></font>");
			++codeIt;
			continue; // skip closing backtick
		}
		if (boldIt != boldSpans.end() && i == boldIt->begin - 2) {
			out += wxT("<b>");
			++i; // skip the second '*'
			continue;
		}
		if (boldIt != boldSpans.end() && i == boldIt->end) {
			out += wxT("</b>");
			++i; // skip the second '*'
			++boldIt;
			continue;
		}
		out += escaped[i];
	}
	return out;
}

} // namespace

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
	// the control's tier under the text's ink, as the assistant's code block
	return wxT("<table border=\"0\" cellspacing=\"0\" cellpadding=\"8\" "
	             "bgcolor=\"") + Ink(ibColourScheme::CONTROL) + wxT("\" width=\"100%\"><tr><td>"
	             "<font face=\"Menlo\" size=\"2\" color=\"") + Ink(ibColourScheme::CONTROL_TEXT) + wxT("\"><pre>")
	       + EscapeForPre(code)
	       + wxT("</pre></font></td></tr></table>");
}

wxString ibHelpDetailView::RenderHtml(const ibHelpEntry& entry) const {
	wxString html;
	html.reserve(4096);

	// THE PAGE IN THE THEME'S INKS (Ink): the content tier under the text's ink, as the tree beside it; the headings,
	// the rule and the links in the alternate ink, as the assistant's headings; what is said less, the muted one.
	const wxString paper = Ink(ibColourScheme::WINDOW), text = Ink(ibColourScheme::CONTROL_TEXT),
		muted = Ink(ibColourScheme::CONTROL_TEXT_DISABLED), heading = Ink(ibColourScheme::CONTROL_TEXT_ALTERNATE);

	// Outer container with consistent left-margin via cellpadding.
	html += wxT("<html><body bgcolor=\"") + paper + wxT("\">"
	             "<table border=\"0\" cellpadding=\"12\" cellspacing=\"0\" "
	             "width=\"100%\"><tr><td>");

	// === Title block ========================================================
	html += wxT("<font size=\"5\" color=\"") + text + wxT("\"><b>");
	html += EscapeHtml(entry.nameLocal.IsEmpty() ? entry.nameEn
	                                                : entry.nameLocal);
	html += wxT("</b></font>");
	if (!entry.nameEn.IsEmpty() && entry.nameEn != entry.nameLocal) {
		html += wxT("<br><font size=\"3\" color=\"") + muted + wxT("\">");
		html += EscapeHtml(entry.nameEn);
		html += wxT("</font>");
	}
	// Coloured rule under the title.
	html += wxT("<table border=\"0\" cellspacing=\"0\" cellpadding=\"0\" "
	             "width=\"100%\" bgcolor=\"") + heading + wxT("\" height=\"2\">"
	             "<tr><td></td></tr></table>");
	html += wxT("<br>");

	if (!entry.signature.IsEmpty()) {
		html += wxT("<font face=\"Menlo\" size=\"3\" color=\"") + text + wxT("\">");
		html += EscapeHtml(entry.signature);
		html += wxT("</font><br><br>");
	}

	auto section = [&](const wxString& title, const wxString& body,
	                    bool monospace) {
		if (body.IsEmpty()) return;
		html += wxT("<font size=\"3\" color=\"") + heading + wxT("\"><b>");
		html += EscapeHtml(title);
		html += wxT("</b></font><br>");
		if (monospace) html += FormatCodeBlock(body);
		else {
			html += wxT("<font color=\"") + text + wxT("\">");
			html += ApplyInlineMarkdown(EscapeHtml(body));
			html += wxT("</font>");
		}
		html += wxT("<br><br>");
	};

	section(_("Description"),  entry.description, false);

	// Show only the syntax form matching the active compile style. The
	// alternative form lives in the corpus but distracts when a CES
	// project doesn't need to see VES braces and vice versa. Falls back
	// to whichever form is non-empty when the preferred one is missing.
	const bool prefersVes = (m_pane->GetCodeStyle() == 0);
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
		html += wxT("<font size=\"3\" color=\"") + heading + wxT("\"><b>");
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
			html += wxT("\"><font color=\"") + heading + wxT("\">");
			html += EscapeHtml(label);
			html += wxT("</font></a>");
		}
		html += wxT("<br><br>");
	}

	// === Draft badge ========================================================
	if (!entry.reviewed) {
		// the action's ink on the control's tier - an amber band glared in the dark look
		html += wxT("<table border=\"0\" cellspacing=\"0\" cellpadding=\"6\" "
		             "bgcolor=\"") + Ink(ibColourScheme::CONTROL) + wxT("\" width=\"100%\"><tr><td>"
		             "<font color=\"") + Ink(ibColourScheme::HYPERLINK) + wxT("\" size=\"2\"><i>");
		html += EscapeHtml(_("Draft - this entry awaits editorial review."));
		html += wxT("</i></font></td></tr></table>");
	}

	html += wxT("</td></tr></table></body></html>");
	return html;
}

void ibHelpDetailView::OnSysColourChanged(wxSysColourChangedEvent& event) {
	// The page is written in the look's colours (Ink): the look changed, the entry shown is written again.
	event.Skip();
	ShowEntry(!m_lastEntryId.IsEmpty() && m_pane ? m_pane->Entry(m_lastEntryId) : nullptr);
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

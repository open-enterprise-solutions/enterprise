/////////////////////////////////////////////////////////////////////////////
// ibPluginWebPane — native implementation.
//
// Pane composition (top → bottom):
//   wxBoxSizer
//     m_history     wxHtmlWindow   — rendered transcript (HTML 3.2 subset)
//     m_input       wxTextCtrl     — multiline input (Enter sends, Shift+Enter newline)
//     m_sendBtn     wxButton       — explicit send
//     m_statusBar   wxStaticText   — model / latency / token meter
//
// Envelope dispatch lives in DispatchEnvelope; the transcript is held as a
// list of Entry records and re-rendered to one HTML document via
// RenderTranscript. Markdown bodies are converted through md4c-html so the
// output is CommonMark-compliant; wxHtmlWindow renders the resulting HTML
// directly. No JavaScript runs in this pane.
/////////////////////////////////////////////////////////////////////////////

#include "frontend/pluginWebPane/pluginWebPane.h"
#include "frontend/pluginWebPane/slashCommandPopup.h"
#include "frontend/pluginWebPane/chatContextPopup.h"
#include "frontend/pluginWebPane/chatHistory.h"
#include "frontend/pluginWebPane/chatContext.h"

// SEC-P1-9: ScopedPluginIdGuard so onMessage dispatches scope caller pid.
#include "backend/appData.h"
#include "backend/plugin/pluginManager.h"

#include "3rdparty/md4c/md4c-html.h"
#include "3rdparty/nlohmann/json.hpp"

#include <wx/sizer.h>
#include <wx/button.h>
#include <wx/choice.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/html/htmlwin.h>
#include <wx/log.h>
#include <wx/clipbrd.h>
#include <wx/dataobj.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>
#include <wx/stc/stc.h>
// COMMIT-MSG: wxExecute / wxArrayString for spawning `git commit -F`
// when the user clicks "Использовать как коммит" in a commit-suggestion
// reply. wxFileName::CreateTempFileName + wxFile write the message
// body to disk so multi-line commit text survives shell quoting on
// every platform.
#include <wx/utils.h>
#include <wx/arrstr.h>
#include <wx/filename.h>
#include <wx/file.h>
#include <wx/filefn.h>

#include <functional>
#include <unordered_map>

namespace {

// SEC-P1-9: resolve the registering plugin's id for a paneId and push it
// into the host-side tl_currentPluginId for the duration of an onMessage
// call. Without this scope, any host trampoline the plugin fires
// synchronously (Meta*, ReadPluginEnv) sees an empty pluginId and fails
// the policy gate / env-isolation check. Falls back to an empty guard
// when appData / pluginManager aren't available (tests).
std::unique_ptr<ScopedPluginIdGuard> MakePaneScopeGuard(const wxString& paneId)
{
	if (appData == nullptr) return nullptr;
	auto* pm = appData->GetPluginManager();
	if (pm == nullptr) return nullptr;
	const std::string pid = pm->GetPluginIdForPane(paneId);
	if (pid.empty()) return nullptr;
	return std::make_unique<ScopedPluginIdGuard>(pid);
}

// Event ID for the cross-thread push channel. Off-UI callers cannot touch
// wxHtmlWindow directly; they post a queued event that this pane drains on
// the main thread.
wxDEFINE_EVENT(IB_PLUGIN_WEB_PANE_PUSH_FROM_THREAD, wxThreadEvent);

// md4c output sink callback. Appends each chunk to the std::string in
// userdata. Buffer comes from md_html caller — md4c never owns it.
void MdSink(const MD_CHAR* text, MD_SIZE size, void* userdata)
{
	auto* out = static_cast<std::string*>(userdata);
	out->append(text, size);
}

// Convert a CommonMark markdown body to safe HTML using md4c. Returns the
// rendered HTML as a wxString. On md4c failure (rare; only OOM) we fall
// back to an HTML-escaped pre-formatted block so the user still sees the
// raw text rather than an empty entry.
wxString MarkdownToHtml(const wxString& markdown)
{
	const wxScopedCharBuffer utf8 = markdown.utf8_str();
	std::string out;
	const unsigned md_flags    = MD_FLAG_TABLES | MD_FLAG_STRIKETHROUGH
	                           | MD_FLAG_PERMISSIVEAUTOLINKS
	                           | MD_FLAG_TASKLISTS
	                           | MD_FLAG_NOHTMLBLOCKS    // strip raw HTML for safety
	                           | MD_FLAG_NOHTMLSPANS;
	const unsigned html_flags  = MD_HTML_FLAG_VERBATIM_ENTITIES;
	const int rc = md_html(utf8.data(),
	                        static_cast<MD_SIZE>(std::strlen(utf8.data())),
	                        &MdSink,
	                        &out,
	                        md_flags,
	                        html_flags);
	if (rc != 0) {
		wxString safe;
		for (auto it = markdown.begin(); it != markdown.end(); ++it) {
			const wxUniChar c = *it;
			if      (c == wxT('&')) safe += wxT("&amp;");
			else if (c == wxT('<')) safe += wxT("&lt;");
			else if (c == wxT('>')) safe += wxT("&gt;");
			else                     safe += c;
		}
		return wxT("<pre>") + safe + wxT("</pre>");
	}
	return wxString::FromUTF8(out.c_str());
}

// HTML-escape a plain string. Used for role labels and any text we drop
// directly into the transcript outside of markdown conversion.
wxString HtmlEscape(const wxString& s)
{
	wxString out;
	out.reserve(s.size() + 8);
	for (auto it = s.begin(); it != s.end(); ++it) {
		const wxUniChar c = *it;
		if      (c == wxT('&')) out += wxT("&amp;");
		else if (c == wxT('<')) out += wxT("&lt;");
		else if (c == wxT('>')) out += wxT("&gt;");
		else if (c == wxT('"')) out += wxT("&quot;");
		else                     out += c;
	}
	return out;
}

// CES keyword set — kept in sync with backend/compiler/translateCode.cpp's
// s_listKeyWord. Used to colorize ```ces / ```bsl / ```oes code blocks in
// the transcript the same way the in-editor Scintilla lexer paints them.
// Stored as a sorted const array so binary-search keeps the highlighter
// linear in the input size.
const wxString* CesKeywordTable(size_t& outCount)
{
	static const wxString kKeywords[] = {
		wxT("And"),         wxT("Ascending"),    wxT("Break"),
		wxT("By"),          wxT("Continue"),     wxT("Descending"),
		wxT("Distinct"),    wxT("Do"),           wxT("Else"),
		wxT("Elseif"),      wxT("EndDo"),        wxT("EndFunction"),
		wxT("EndProcedure"),wxT("Endif"),        wxT("Endtry"),
		wxT("Equals"),      wxT("Except"),       wxT("Export"),
		wxT("False"),       wxT("For"),          wxT("Foreach"),
		wxT("From"),        wxT("Function"),     wxT("GoTo"),
		wxT("Group"),       wxT("If"),           wxT("In"),
		wxT("Into"),        wxT("Join"),         wxT("New"),
		wxT("Not"),         wxT("Null"),         wxT("On"),
		wxT("Or"),          wxT("OrderBy"),      wxT("Procedure"),
		wxT("Raise"),       wxT("Return"),       wxT("Select"),
		wxT("Skip"),        wxT("Take"),         wxT("Then"),
		wxT("To"),          wxT("True"),         wxT("Try"),
		wxT("Undefined"),   wxT("Val"),          wxT("Var"),
		wxT("Where"),       wxT("While"),
		// Preprocessor — leading '#' included so the highlighter
		// matches "#Region" as a single token.
		wxT("#Define"),     wxT("#Else"),        wxT("#Endif"),
		wxT("#EndRegion"),  wxT("#Ifdef"),       wxT("#Ifndef"),
		wxT("#Region"),     wxT("#Undef"),
	};
	outCount = sizeof(kKeywords) / sizeof(kKeywords[0]);
	return kKeywords;
}

// VES keyword set — Russian / 1C-БСЛ flavour. OES supports BOTH syntax
// modes (CES braces, VES Если/Тогда/КонецЕсли) and they compile to the
// same bytecode. The highlighter accepts ```ves or ```bsl fences and
// also recognises Cyrillic tokens inside ```ces blocks when present (a
// developer who pastes a 1C snippet into a CES editor still wants to see
// it coloured).
const wxString* VesKeywordTable(size_t& outCount)
{
	static const wxString kKeywords[] = {
		// Control flow
		wxT("Если"),       wxT("Тогда"),         wxT("Иначе"),
		wxT("ИначеЕсли"),  wxT("КонецЕсли"),
		wxT("Для"),        wxT("Каждого"),       wxT("Из"),
		wxT("По"),          wxT("Цикл"),          wxT("КонецЦикла"),
		wxT("Пока"),       wxT("Прервать"),     wxT("Продолжить"),
		wxT("Перейти"),
		// Operators
		wxT("Не"),          wxT("И"),              wxT("Или"),
		// Definitions
		wxT("Процедура"), wxT("КонецПроцедуры"),
		wxT("Функция"),   wxT("КонецФункции"),
		wxT("Экспорт"),  wxT("Знач"),         wxT("Возврат"),
		wxT("Попытка"),  wxT("Исключение"),  wxT("КонецПопытки"),
		wxT("ВызватьИсключение"),
		wxT("Перем"),     wxT("Новый"),
		// Constants
		wxT("Неопределено"), wxT("Null"),         wxT("NULL"),
		wxT("Истина"),    wxT("Ложь"),
		// Preprocessor / 1C-style directives
		wxT("#Если"),     wxT("#Иначе"),        wxT("#КонецЕсли"),
		wxT("#Область"), wxT("#КонецОбласти"),
	};
	outCount = sizeof(kKeywords) / sizeof(kKeywords[0]);
	return kKeywords;
}

bool IsCesKeyword(const wxString& token)
{
	if (token.IsEmpty()) return false;
	{
		size_t n = 0;
		const wxString* tbl = CesKeywordTable(n);
		// Case-insensitive compare — CES is keyword-case-insensitive in
		// the lexer, even though canonical style is CamelCase.
		for (size_t i = 0; i < n; ++i) {
			if (token.CmpNoCase(tbl[i]) == 0) return true;
		}
	}
	// Also check the Cyrillic VES table so highlighter covers both
	// syntax flavours from a single entry point.
	{
		size_t n = 0;
		const wxString* tbl = VesKeywordTable(n);
		for (size_t i = 0; i < n; ++i) {
			if (token.CmpNoCase(tbl[i]) == 0) return true;
		}
	}
	return false;
}

// Render a single CES/OES source snippet as syntax-coloured HTML for
// wxHtmlWindow. Colour palette mirrors the in-editor lexer roughly:
//   keyword       → #af00db   bold (purple)
//   string  "..." → #008000           (green)
//   comment //... → #6a737d  italic   (slate)
//   number        → #0451a5           (blue)
//   identifier    → default text colour
//
// wxHtmlWindow understands <font color="#hex"> and <i>/<b>; we do NOT
// rely on CSS classes (its CSS support is unreliable).
wxString HighlightCes(const wxString& code)
{
	auto isIdentStart = [](wxUniChar c) {
		const auto v = static_cast<unsigned>(c.GetValue());
		return (v >= 'A' && v <= 'Z') ||
		       (v >= 'a' && v <= 'z') ||
		       // Cyrillic block U+0400..U+04FF — needed for VES keywords
		       // (Если, Процедура, Цикл, etc) and identifiers in
		       // 1C-style modules. Highlighter must treat Cyrillic
		       // letters as part of identifier tokens, otherwise it
		       // breaks "Если" into "Е" + "сли" and the keyword match
		       // never fires.
		       (v >= 0x0400 && v <= 0x04FF) ||
		       v == '_' || v == '#';
	};
	auto isIdentCont = [](wxUniChar c) {
		const auto v = static_cast<unsigned>(c.GetValue());
		return (v >= 'A' && v <= 'Z') ||
		       (v >= 'a' && v <= 'z') ||
		       (v >= '0' && v <= '9') ||
		       (v >= 0x0400 && v <= 0x04FF) ||  // Cyrillic — see above
		       v == '_';
	};
	auto isDigit = [](wxUniChar c) {
		const auto v = static_cast<unsigned>(c.GetValue());
		return v >= '0' && v <= '9';
	};

	wxString out;
	out.reserve(code.size() * 2);

	const size_t n = code.size();
	size_t i = 0;
	while (i < n) {
		const wxUniChar c = code[i];

		// Line comment: // ... end-of-line
		if (c == wxT('/') && i + 1 < n && code[i + 1] == wxT('/')) {
			size_t j = i;
			while (j < n && code[j] != wxT('\n')) ++j;
			const wxString segment = code.SubString(i, j - 1);
			out += wxT("<font color=\"#6a737d\"><i>");
			out += HtmlEscape(segment);
			out += wxT("</i></font>");
			i = j;
			continue;
		}

		// String literal — accept both " and ' as bookends.
		if (c == wxT('"') || c == wxT('\'')) {
			const wxUniChar quote = c;
			size_t j = i + 1;
			while (j < n && code[j] != quote && code[j] != wxT('\n')) {
				if (code[j] == wxT('\\') && j + 1 < n) j += 2;
				else                                    ++j;
			}
			if (j < n) ++j; // include closing quote if present
			const wxString segment = code.SubString(i, j - 1);
			out += wxT("<font color=\"#008000\">");
			out += HtmlEscape(segment);
			out += wxT("</font>");
			i = j;
			continue;
		}

		// Number literal — runs of digits with optional decimal point.
		if (isDigit(c)) {
			size_t j = i;
			while (j < n && (isDigit(code[j]) || code[j] == wxT('.'))) ++j;
			out += wxT("<font color=\"#0451a5\">");
			out += HtmlEscape(code.SubString(i, j - 1));
			out += wxT("</font>");
			i = j;
			continue;
		}

		// Identifier or keyword.
		if (isIdentStart(c)) {
			size_t j = i + 1;
			while (j < n && isIdentCont(code[j])) ++j;
			const wxString tok = code.SubString(i, j - 1);
			if (IsCesKeyword(tok)) {
				out += wxT("<font color=\"#af00db\"><b>");
				out += HtmlEscape(tok);
				out += wxT("</b></font>");
			} else {
				out += HtmlEscape(tok);
			}
			i = j;
			continue;
		}

		// Everything else: punctuation / whitespace / non-source bytes
		// passes through as-is (after escape).
		out += HtmlEscape(wxString(c));
		++i;
	}
	return out;
}

// Wrap a code block in a styled table with a Copy / Apply link header.
// blockIdx is stored as part of the URL so OnLinkClicked can recover the
// original raw text via Entry::codeBlocks lookup.
//
// Visual: a thin two-row table — top row has the language tag on the
// left and action links on the right (light grey background); bottom
// row is the coloured code body inside a <pre>.
wxString RenderCesCodeBlock(size_t entryIdx, size_t blockIdx,
                              const wxString& lang, const wxString& code,
                              bool commitSuggestion)
{
	wxString html;
	html += wxT("<table width=\"100%\" cellspacing=\"0\" cellpadding=\"6\" border=\"0\">");
	// Header row: language tag (left) + action links (right). Commit
	// suggestions append a third link "Использовать как коммит" that
	// HandleCommitLink picks up and runs `git commit -m` after
	// confirmation. Render the extra link inline so wxHtmlWindow keeps
	// the right-align flow in one cell.
	wxString rightLinks;
	rightLinks += wxString::Format(
	    wxT("<a href=\"oes-copy:%zu:%zu\">%s</a>"),
	    entryIdx, blockIdx, HtmlEscape(_("Копировать")));
	rightLinks += wxT("&nbsp;&nbsp;");
	rightLinks += wxString::Format(
	    wxT("<a href=\"oes-apply:%zu:%zu\">%s</a>"),
	    entryIdx, blockIdx, HtmlEscape(_("Применить")));
	if (commitSuggestion) {
		rightLinks += wxT("&nbsp;&nbsp;");
		rightLinks += wxString::Format(
		    wxT("<a href=\"oes-commit:%zu:%zu\">%s</a>"),
		    entryIdx, blockIdx,
		    HtmlEscape(_("Использовать как коммит")));
	}
	// Single-column layout: language tag on top-left, action links on
	// the NEXT line (not a right-aligned cell) so they don't clip when
	// the pane is narrow. wxHtmlWindow doesn't reflow right-aligned
	// table cells into the visible width — they just walk off-screen
	// (observed at <420px pane width on macOS).
	html += wxString::Format(
	    wxT("<tr><td bgcolor=\"#eef0f3\">"
	        "<font size=\"-1\" color=\"#6b7280\">%s</font><br>"
	        "<font size=\"-1\">%s</font>"
	        "</td></tr>"),
	    HtmlEscape(lang.IsEmpty() ? wxT("ces") : lang),
	    rightLinks);
	// Code body in its own row. wxHtmlWindow <pre> does NOT wrap long
	// lines — we accept horizontal scroll inside the pane for code
	// blocks. Wrapping CES would break indent and obscure the syntax.
	html += wxT("<tr><td bgcolor=\"#f8f9fa\"><pre>");
	html += HighlightCes(code);
	html += wxT("</pre></td></tr>");
	html += wxT("</table>");
	return html;
}

// Extract fenced code blocks from a markdown body. Returns the body with
// each block replaced by a unique placeholder token, and fills `out`
// with (lang, code) pairs in the order they appeared. Placeholders are
// substituted with the rendered HTML after MarkdownToHtml runs.
wxString ExtractCodeBlocks(const wxString& markdown,
                            std::vector<std::pair<wxString, wxString>>& out)
{
	wxString result;
	result.reserve(markdown.size());

	const size_t n = markdown.size();
	size_t i = 0;
	while (i < n) {
		// Look for triple-backtick fence at start of a line.
		const bool atLineStart = (i == 0) ||
		                          markdown[i - 1] == wxT('\n');
		if (atLineStart && i + 2 < n &&
		    markdown[i]     == wxT('`') &&
		    markdown[i + 1] == wxT('`') &&
		    markdown[i + 2] == wxT('`')) {
			// Lang tag runs from after ``` to end-of-line.
			size_t headerEnd = i + 3;
			while (headerEnd < n && markdown[headerEnd] != wxT('\n')) ++headerEnd;
			wxString lang = (headerEnd > i + 3)
			    ? markdown.SubString(i + 3, headerEnd - 1)
			    : wxString();
			lang.Trim(false).Trim(true);
			// Body runs from after the newline until the closing fence.
			size_t bodyStart = (headerEnd < n) ? headerEnd + 1 : headerEnd;
			size_t bodyEnd = bodyStart;
			while (bodyEnd + 2 < n) {
				const bool fenceLineStart = (bodyEnd == 0) ||
				                              markdown[bodyEnd - 1] == wxT('\n');
				if (fenceLineStart &&
				    markdown[bodyEnd]     == wxT('`') &&
				    markdown[bodyEnd + 1] == wxT('`') &&
				    markdown[bodyEnd + 2] == wxT('`')) {
					break;
				}
				++bodyEnd;
			}
			if (bodyEnd + 2 >= n) {
				// Unterminated fence — pass through as-is so the body
				// still renders, even if without highlighting.
				result += markdown[i];
				++i;
				continue;
			}
			const wxString body = markdown.SubString(bodyStart, bodyEnd - 1);
			out.push_back({lang, body});
			result += wxString::Format(wxT("\n\nOESBLOCK_%zu_PLACEHOLDER\n\n"),
			                              out.size() - 1);
			i = bodyEnd + 3;
			// Skip the trailing newline of the closing fence so md4c
			// doesn't emit a stray paragraph.
			if (i < n && markdown[i] == wxT('\n')) ++i;
			continue;
		}
		result += markdown[i];
		++i;
	}
	return result;
}

// User-visible role labels. Russian by default — the Designer ships UI in
// ru/uk; English flavour is the catalog fallback so wxGetTranslation
// surfaces it for en builds without a string table change.
wxString RoleLabel(ibPluginWebPane::Entry::Role role)
{
	using R = ibPluginWebPane::Entry::Role;
	switch (role) {
	case R::User:         return _("Вы");
	case R::Assistant:    return _("Ассистент");
	case R::System:       return _("Система");
	case R::Error:        return _("Ошибка");
	case R::Plan:         return _("План");
	case R::TripleReview: return _("Triple-review");
	case R::Subtasks:     return _("Подзадачи");
	}
	return wxEmptyString;
}

// Map a verdict string to its (bgcolor, fgcolor) badge palette. PASS = green,
// WARN = amber, BLOCK = red. Unknown verdicts fall back to a neutral grey so
// a forward-compatible payload still renders something legible.
void VerdictBadgeColors(const wxString& verdict,
                          const char*& outBg, const char*& outFg)
{
	if      (verdict == wxT("PASS"))  { outBg = "#16a34a"; outFg = "#ffffff"; }
	else if (verdict == wxT("WARN"))  { outBg = "#d97706"; outFg = "#ffffff"; }
	else if (verdict == wxT("BLOCK")) { outBg = "#b91c1c"; outFg = "#ffffff"; }
	else                               { outBg = "#6b7280"; outFg = "#ffffff"; }
}

// Map a severity tag to its cell background. Mirrors the badge palette but
// at a lower saturation since count cells sit inside the transcript row and
// shouldn't outshine the verdict badge above them.
const char* SeverityBgColor(const wxString& severity)
{
	if      (severity == wxT("P0")) return "#fecaca";
	else if (severity == wxT("P1")) return "#fed7aa";
	else if (severity == wxT("P2")) return "#fde68a";
	else if (severity == wxT("P3")) return "#e5e7eb";
	return "#f3f4f6";
}

} // namespace

ibPluginWebPane::ibPluginWebPane(wxWindow* parent,
                                  const wxString& paneId,
                                  const wxString& title,
                                  const wxString& /*htmlBundlePath*/,
                                  ibPluginWebMsgFn onMessage,
                                  void* userData)
    : wxPanel(parent, wxID_ANY)
    , m_paneId(paneId)
    , m_title(title)
    , m_onMessage(onMessage)
    , m_userData(userData)
{
	auto* root = new wxBoxSizer(wxVERTICAL);

	auto* policyRow = new wxBoxSizer(wxHORIZONTAL);
	policyRow->Add(new wxStaticText(this, wxID_ANY, _("Профиль")),
	               0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT,
	               FromDIP(4));
	m_modelProfile = new wxChoice(this, wxID_ANY);
	m_modelProfile->Append(_("По умолчанию"));
	m_modelProfile->Append(_("Pugi"));
	m_modelProfile->Append(_("OpenAI fast"));
	m_modelProfile->Append(_("OpenAI quality"));
	m_modelProfile->SetSelection(0);
	policyRow->Add(m_modelProfile, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT,
	               FromDIP(12));

	policyRow->Add(new wxStaticText(this, wxID_ANY, _("Режим агента")),
	               0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT,
	               FromDIP(4));
	m_permissionMode = new wxChoice(this, wxID_ANY);
	m_permissionMode->Append(_("Только чтение"));
	m_permissionMode->Append(_("Подтверждать всё"));
	m_permissionMode->Append(_("Подтверждать изменения"));
	m_permissionMode->Append(_("Авто"));
	m_permissionMode->SetSelection(static_cast<int>(m_permissionModeValue));
	policyRow->Add(m_permissionMode, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT,
	               FromDIP(4));
	root->Add(policyRow, 0, wxEXPAND | wxTOP, FromDIP(4));

	// Drop wxHW_NO_SELECTION so the user can drag-select transcript text
	// and copy with Cmd+C (system menu Edit→Copy is still available).
	// Default behaviour with selection enabled mirrors any other native
	// rich-text viewer in the Designer.
	m_history = new wxHtmlWindow(this, wxID_ANY, wxDefaultPosition,
	                              wxDefaultSize,
	                              wxHW_SCROLLBAR_AUTO);
	root->Add(m_history, 1, wxEXPAND | wxALL, FromDIP(4));

	m_input = new wxTextCtrl(this, wxID_ANY, wxEmptyString,
	                          wxDefaultPosition,
	                          wxSize(-1, FromDIP(72)),
	                          wxTE_MULTILINE | wxTE_PROCESS_ENTER);
	root->Add(m_input, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(4));

	auto* btnRow = new wxBoxSizer(wxHORIZONTAL);
	m_statusBar = new wxStaticText(this, wxID_ANY, wxEmptyString);
	m_statusBar->SetForegroundColour(wxColour(120, 120, 130));
	btnRow->Add(m_statusBar, 1, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(4));

	// "Новый чат" sits to the left of "Отправить" — same row, smaller
	// visual weight. Order matters: btnRow grows from the status bar's
	// stretchable spacer leftward, so adding the new-chat button first
	// would push it under the spacer. Add it AFTER the spacer + BEFORE
	// the send button so the layout reads:  [status…]  [New]  [Send].
	// "Скопировать всё" — dumps the entire transcript as plain markdown
	// to the system clipboard. wxHtmlWindow's selection-to-clipboard
	// (Cmd+C / Ctrl+C) is unreliable across roles + code blocks, so
	// expose an explicit button. Equivalent to Workmate's "Copy chat".
	auto* copyAllBtn = new wxButton(this, wxID_ANY, _("Скопировать всё"));
	btnRow->Add(copyAllBtn, 0, wxALL, FromDIP(4));

	m_newChatBtn = new wxButton(this, wxID_ANY, _("Новый чат"));
	btnRow->Add(m_newChatBtn, 0, wxALL, FromDIP(4));

	m_sendBtn = new wxButton(this, wxID_ANY, _("Отправить"));
	btnRow->Add(m_sendBtn, 0, wxALL, FromDIP(4));
	root->Add(btnRow, 0, wxEXPAND);

	SetSizer(root);

	m_sendBtn   ->Bind(wxEVT_BUTTON, &ibPluginWebPane::OnSendClicked,    this);
	m_newChatBtn->Bind(wxEVT_BUTTON, &ibPluginWebPane::OnNewChatClicked, this);
	m_modelProfile->Bind(wxEVT_CHOICE,
	                      &ibPluginWebPane::OnModelProfileChanged,
	                      this);
	m_permissionMode->Bind(wxEVT_CHOICE,
	                        &ibPluginWebPane::OnPermissionModeChanged,
	                        this);
	copyAllBtn  ->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
		wxString out;
		for (const auto& e : m_entries) {
			wxString label;
			switch (e.role) {
			case Entry::Role::User:         label = wxT("## Вы\n");        break;
			case Entry::Role::Assistant:    label = wxT("## Ассистент\n"); break;
			case Entry::Role::System:       label = wxT("## Система\n");   break;
			case Entry::Role::Error:        label = wxT("## Ошибка\n");    break;
			case Entry::Role::Plan:         label = wxT("## План\n");      break;
			case Entry::Role::TripleReview: label = wxT("## Triple-review\n"); break;
			case Entry::Role::Subtasks:     label = wxT("## Подзадачи\n"); break;
			}
			out += label;
			out += e.markdown;
			out += wxT("\n\n---\n\n");
		}
		if (wxTheClipboard->Open()) {
			wxTheClipboard->SetData(new wxTextDataObject(out));
			wxTheClipboard->Close();
			if (m_statusBar) m_statusBar->SetLabel(_("Весь чат скопирован"));
		}
	});

	// Cmd+C / Ctrl+C on the history widget — fall back to "copy whole
	// transcript" when wxHtmlWindow returns an empty selection. Solves
	// "Cmd+A then Cmd+C copies nothing" on macOS.
	m_history->Bind(wxEVT_CHAR_HOOK, [this](wxKeyEvent& evt) {
		const bool isCopy = (evt.GetKeyCode() == 'C') &&
		                     (evt.CmdDown() || evt.ControlDown());
		if (!isCopy) { evt.Skip(); return; }
		const wxString sel = m_history->SelectionToText();
		wxString out = sel;
		if (out.IsEmpty()) {
			for (const auto& e : m_entries) { out += e.markdown; out += wxT("\n\n"); }
		}
		if (!out.IsEmpty() && wxTheClipboard->Open()) {
			wxTheClipboard->SetData(new wxTextDataObject(out));
			wxTheClipboard->Close();
			if (m_statusBar) m_statusBar->SetLabel(_("Скопировано"));
		}
		// Don't Skip — we owned this combo.
	});
	m_history->Bind(wxEVT_CONTEXT_MENU, [this](wxContextMenuEvent& evt) {
		wxMenu menu;
		const int idCopy = wxWindow::NewControlId();
		const int idCopyAll = wxWindow::NewControlId();
		menu.Append(idCopy, _("Копировать"));
		menu.Append(idCopyAll, _("Скопировать всё"));

		auto copyText = [this](bool all) {
			wxString out;
			if (!all) out = m_history->SelectionToText();
			if (out.IsEmpty()) {
				for (const auto& e : m_entries) {
					out += e.markdown;
					out += wxT("\n\n");
				}
			}
			if (!out.IsEmpty() && wxTheClipboard->Open()) {
				wxTheClipboard->SetData(new wxTextDataObject(out));
				wxTheClipboard->Close();
				if (m_statusBar) {
					m_statusBar->SetLabel(all ? _("Весь чат скопирован")
					                          : _("Скопировано"));
				}
			}
		};
		menu.Bind(wxEVT_MENU, [copyText](wxCommandEvent&) { copyText(false); },
		          idCopy);
		menu.Bind(wxEVT_MENU, [copyText](wxCommandEvent&) { copyText(true); },
		          idCopyAll);
		wxPoint pos = evt.GetPosition();
		pos = (pos == wxDefaultPosition)
		        ? wxPoint(FromDIP(8), FromDIP(8))
		        : m_history->ScreenToClient(pos);
		m_history->PopupMenu(&menu, pos);
	});
	m_input  ->Bind(wxEVT_TEXT_ENTER, &ibPluginWebPane::OnInputEnter, this);
	m_input  ->Bind(wxEVT_TEXT,       &ibPluginWebPane::OnInputText,  this);
	// EVT_CHAR_HOOK fires before native key dispatch, which lets us
	// intercept Up/Down/Enter/Esc while the popup is open without
	// fighting the wxTextCtrl's own arrow-key handling.
	m_input  ->Bind(wxEVT_CHAR_HOOK,  &ibPluginWebPane::OnInputKeyDown, this);
	m_input  ->Bind(wxEVT_KILL_FOCUS, &ibPluginWebPane::OnInputKillFocus, this);
	m_history->Bind(wxEVT_HTML_LINK_CLICKED, &ibPluginWebPane::OnLinkClicked, this);

	Bind(IB_PLUGIN_WEB_PANE_PUSH_FROM_THREAD,
	     &ibPluginWebPane::OnPushFromOtherThread, this);

	// Debounced save timer. Owner = this so wxEVT_TIMER lands here; we
	// hand-bind it to OnSaveTimer rather than overriding Notify() so the
	// handler keeps the same shape as the rest of the pane's event sinks.
	m_saveTimer.SetOwner(this);
	Bind(wxEVT_TIMER, &ibPluginWebPane::OnSaveTimer, this);

	// Resolve the per-configuration bucket and rehydrate the prior
	// transcript if one exists. Failures here are non-fatal: an empty
	// transcript is a perfectly valid state for first-time use.
	m_configHash = ibChatHistory::ComputeConfigHash();
	if (m_persistEnabled) {
		std::vector<Entry> restored;
		if (ibChatHistory::Load(m_configHash, restored)) {
			m_entries = std::move(restored);
		}
	}

	// Render initial empty state (transcript with no entries shows a
	// short hint line so the pane does not look broken on first load).
	RenderTranscript();
}

ibPluginWebPane::~ibPluginWebPane()
{
	// Flush any pending debounced save synchronously. The timer fires
	// once-shot; if it was running, the user's last edits would otherwise
	// be lost on quit (common case: hit Cmd+Q within 500ms of typing).
	if (m_saveTimer.IsRunning()) {
		m_saveTimer.Stop();
		SaveNow();
	}
}

wxString ibPluginWebPane::CurrentModelProfileId() const
{
	const int sel = m_modelProfile ? m_modelProfile->GetSelection() : 0;
	switch (sel) {
	case 1: return wxT("pugi");
	case 2: return wxT("openai-fast");
	case 3: return wxT("openai-quality");
	default: return wxT("env");
	}
}

wxString ibPluginWebPane::CurrentModelProfileLabel() const
{
	const int sel = m_modelProfile ? m_modelProfile->GetSelection() : 0;
	if (m_modelProfile != nullptr && sel >= 0) {
		return m_modelProfile->GetString(sel);
	}
	return _("По умолчанию");
}

void ibPluginWebPane::OnModelProfileChanged(wxCommandEvent& /*event*/)
{
	if (m_statusBar) {
		m_statusBar->SetLabel(_("Профиль: ") + CurrentModelProfileLabel());
	}
}

void ibPluginWebPane::OnPermissionModeChanged(wxCommandEvent& /*event*/)
{
	const int sel = m_permissionMode ? m_permissionMode->GetSelection() : -1;
	if (sel >= static_cast<int>(PermissionMode::ReadOnly) &&
	    sel <= static_cast<int>(PermissionMode::Auto)) {
		m_permissionModeValue = static_cast<PermissionMode>(sel);
	}
	RestorePlanPolicyAfterApply();
	ApplyPermissionModePolicy();
	if (m_statusBar == nullptr) return;
	switch (m_permissionModeValue) {
	case PermissionMode::ReadOnly:
		m_statusBar->SetLabel(_("Режим агента: только чтение"));
		break;
	case PermissionMode::ConfirmAll:
		m_statusBar->SetLabel(_("Режим агента: подтверждать всё"));
		break;
	case PermissionMode::ConfirmWrites:
		m_statusBar->SetLabel(_("Режим агента: подтверждать изменения"));
		break;
	case PermissionMode::Auto:
		m_statusBar->SetLabel(_("Режим агента: авто"));
		break;
	}
}

void ibPluginWebPane::ApplyPermissionModePolicy()
{
	if (appData == nullptr) return;
	auto* pm = appData->GetPluginManager();
	if (pm == nullptr) return;

	const std::string pid = pm->GetPluginIdForPane(m_paneId);
	if (pid.empty()) return;
	const wxString pluginId = wxString::FromUTF8(pid.c_str());

	using Policy = ibPluginManager::MutationPolicy;
	pm->SetMutationPolicy(pluginId, wxT("meta.query"), Policy::AllowAlways);

	Policy mutationPolicy = Policy::Ask;
	switch (m_permissionModeValue) {
	case PermissionMode::ReadOnly:
		mutationPolicy = Policy::Deny;
		break;
	case PermissionMode::ConfirmAll:
	case PermissionMode::ConfirmWrites:
		mutationPolicy = Policy::Ask;
		break;
	case PermissionMode::Auto:
		mutationPolicy = Policy::AllowSession;
		break;
	}

	pm->SetMutationPolicy(pluginId, wxT("meta.create"), mutationPolicy);
	pm->SetMutationPolicy(pluginId, wxT("meta.edit"),   mutationPolicy);
	pm->SetMutationPolicy(pluginId, wxT("meta.delete"), mutationPolicy);
}

void ibPluginWebPane::ElevatePlanPolicyForApply()
{
	if (appData == nullptr) return;
	auto* pm = appData->GetPluginManager();
	if (pm == nullptr) return;

	const std::string pid = pm->GetPluginIdForPane(m_paneId);
	if (pid.empty()) return;
	const wxString pluginId = wxString::FromUTF8(pid.c_str());

	using Policy = ibPluginManager::MutationPolicy;
	pm->SetMutationPolicy(pluginId, wxT("meta.create"), Policy::AllowSession);
	pm->SetMutationPolicy(pluginId, wxT("meta.edit"),   Policy::AllowSession);
	pm->SetMutationPolicy(pluginId, wxT("meta.delete"), Policy::AllowSession);
	m_planPolicyElevated = true;
}

void ibPluginWebPane::RestorePlanPolicyAfterApply()
{
	if (!m_planPolicyElevated) return;
	m_planPolicyElevated = false;
	ApplyPermissionModePolicy();
}

bool ibPluginWebPane::ConfirmAgentAction(const wxString& actionLabel) const
{
	return wxMessageBox(
	           wxString::Format(_("Разрешить действие: %s?"), actionLabel),
	           _("Подтверждение"),
	           wxYES_NO | wxNO_DEFAULT | wxICON_WARNING,
	           const_cast<ibPluginWebPane*>(this)) == wxYES;
}

void ibPluginWebPane::PushMessage(const wxString& jsonInline)
{
	if (wxThread::IsMain()) {
		DispatchEnvelope(jsonInline);
		return;
	}
	auto* evt = new wxThreadEvent(IB_PLUGIN_WEB_PANE_PUSH_FROM_THREAD);
	evt->SetString(jsonInline);
	wxQueueEvent(this, evt);
}

void ibPluginWebPane::OnPushFromOtherThread(wxThreadEvent& event)
{
	const wxString payload = event.GetString();
	DispatchEnvelope(payload);
}

void ibPluginWebPane::DispatchEnvelope(const wxString& jsonInline)
{
	const wxScopedCharBuffer utf8 = jsonInline.utf8_str();
	auto env = nlohmann::json::parse(utf8.data(), nullptr, /*allow_exceptions=*/false);
	if (env.is_discarded() || !env.is_object()) {
		Entry e;
		e.role     = Entry::Role::System;
		e.markdown = wxString::Format(_("Получен некорректный ответ: %s"),
		                                jsonInline);
		AppendEntry(std::move(e));
		return;
	}

	const std::string kind = env.value("kind", "");

	if (kind == "chat.delta") {
		const std::string rid   = env.value("requestId", "");
		const std::string delta = env.value("delta",     "");
		AppendStreamingDelta(wxString::FromUTF8(rid.c_str()),
		                      wxString::FromUTF8(delta.c_str()));
		return;
	}
	if (kind == "chat.end") {
		const std::string rid = env.value("requestId", "");
		std::string metaText;
		if (env.contains("meta") && env["meta"].is_object()) {
			const auto& meta = env["meta"];
			const std::string model = meta.value("model", "");
			if (!model.empty()) metaText = "model: " + model;
			if (meta.contains("tokens") && meta["tokens"].is_object()) {
				const auto& tok = meta["tokens"];
				const int in  = tok.value("in",  0);
				const int out = tok.value("out", 0);
				if (in + out > 0) {
					if (!metaText.empty()) metaText += " · ";
					metaText += std::to_string(in + out) + " tokens";
				}
			}
		}
		CompleteStreaming(wxString::FromUTF8(rid.c_str()),
		                   wxString::FromUTF8(metaText.c_str()));
		return;
	}
	if (kind == "error") {
		RestorePlanPolicyAfterApply();
		std::string code, detail;
		if (env.contains("error") && env["error"].is_object()) {
			const auto& err = env["error"];
			code   = err.value("code",   "");
			detail = err.value("detail", "");
		} else if (env.contains("error") && env["error"].is_string()) {
			detail = env["error"].get<std::string>();
		}
		Entry e;
		e.role      = Entry::Role::Error;
		e.requestId = wxString::FromUTF8(env.value("requestId", "").c_str());
		if (!detail.empty() && !code.empty()) {
			e.markdown = wxString::Format(wxT("%s — %s"),
			                                wxString::FromUTF8(code.c_str()),
			                                wxString::FromUTF8(detail.c_str()));
		} else if (!detail.empty()) {
			e.markdown = wxString::FromUTF8(detail.c_str());
		} else if (!code.empty()) {
			e.markdown = wxString::FromUTF8(code.c_str());
		} else {
			e.markdown = _("Неизвестная ошибка");
		}
		// If we were streaming, close out the streaming entry first so
		// the error doesn't shadow a half-rendered assistant reply.
		if (m_pending) { m_pending = false; m_pendingRequestId.Clear(); }
		UpdateSendButtonLabel();
		AppendEntry(std::move(e));
		return;
	}
	if (kind == "editor.skill") {
		// Rendered inbound — show as user message with the op + code,
		// then forward as chat.send so the plugin handles it. Mirrors
		// the previous JS-side bounce in sample.html.
		const std::string op       = env.value("op",       "send");
		const std::string language = env.value("language", "ces");
		const std::string code     = env.value("code",     "");
		wxString opLabel;
		if      (op == "explain")       opLabel = _("Объясни следующий код");
		else if (op == "review")        opLabel = _("Проверь следующий код");
		else if (op == "fix")           opLabel = _("Исправь следующий код");
		else if (op == "doc")           opLabel = _("Сгенерируй документирующий комментарий для");
		else if (op == "send")          opLabel = _("Обсудим этот код");
		else if (op == "triple-review") opLabel = _("Triple-review модуля");
		else if (op == "agent")         opLabel = _("Создать через агента");
		else if (op == "commit")        opLabel = _("Сгенерируй сообщение коммита для следующего diff");
		else                             opLabel = _("Обработай код") + wxString(wxT(" (")) +
		                                            wxString::FromUTF8(op.c_str()) +
		                                            wxString(wxT(")"));
		const wxString lang = wxString::FromUTF8(language.c_str());
		const wxString src  = wxString::FromUTF8(code.c_str());

		// COMMIT-MSG: remember the original envelope requestId so the
		// assistant reply for this turn renders the extra
		// "Использовать как коммит" link. DispatchUserPrompt below
		// builds a fresh requestId for the outbound chat.send (older
		// worker contracts), so we also register that one once we
		// know the value. Easiest path: capture rid from the
		// envelope here, and an additional sentinel matched by
		// AppendStreamingDelta heuristically. We use a separate flag
		// instead: every assistant entry whose user-row immediately
		// preceded it with the commit opLabel gets tagged at chat.end
		// time (CompleteStreaming) via a back-fill pass.
		const bool isCommitTurn = (op == "commit");
		if (isCommitTurn) {
			// Record the envelope's own rid (if the worker happens to
			// echo it back unchanged) AND set the one-shot sentinel so
			// DispatchUserPrompt below registers its newly-minted
			// chat-<ts> rid as well. Belt-and-braces: either path tags
			// the assistant reply.
			const std::string ridUtf8 = env.value("requestId", "");
			if (!ridUtf8.empty()) {
				m_commitRequestIds.insert(wxString::FromUTF8(ridUtf8.c_str()));
			}
			m_nextPromptIsCommit = true;
		}

		// SEC-CODEX-P1: triple-review uses a DIFFERENT MCP tool on the
		// server (name="triple_review"), not the chat llm_query path.
		// Bouncing through DispatchUserPrompt would re-emit as chat.send
		// and drop the op + raw code — aiBridge's special-case only
		// fires for editor.skill envelopes. Forward the original
		// envelope verbatim so aiBridge sees op="triple-review" and
		// routes to RunTripleReview. Render a user-row HERE because
		// the direct-dispatch branch below does NOT call
		// DispatchUserPrompt (which is what appends the user row in
		// the chat-skill branch). Non-triple-review skills append via
		// DispatchUserPrompt only — appending here too would duplicate
		// the row.
		// AGENT-MODE: op="agent" follows the same direct-dispatch path —
		// aiBridge routes it to RunOesAgent which emits agent.plan, not
		// chat.delta, so DispatchUserPrompt's chat.send wrapper would lose
		// the op + context fields.
		if (op == "triple-review" || op == "agent") {
			if (op == "agent" &&
			    m_permissionModeValue == PermissionMode::ConfirmAll &&
			    !ConfirmAgentAction(_("запросить план изменений"))) {
				return;
			}
			Entry e;
			e.role     = Entry::Role::User;
			if (op == "agent" && code.empty()) {
				// AGENT-MODE: agent skill carries the natural-language
				// request in `prompt` when invoked from /agent slash; show
				// it raw rather than as a (empty) code block.
				const std::string promptStr = env.value("prompt", std::string());
				e.markdown = opLabel + wxT(": ") + wxString::FromUTF8(promptStr.c_str());
			} else {
				e.markdown = opLabel + wxT(":\n\n```") + lang + wxT("\n") + src + wxT("\n```");
			}
			AppendEntry(std::move(e));
			if (m_onMessage != nullptr) {
				const std::string paneIdUtf8(m_paneId.utf8_str().data());
				const std::string envUtf8(jsonInline.utf8_str().data());
				try {
					m_onMessage(paneIdUtf8.c_str(), envUtf8.c_str(), m_userData);
				} catch (...) {
					Entry err;
					err.role     = Entry::Role::Error;
					err.markdown = (op == "agent")
					    ? _("Не удалось отправить запрос агенту.")
					    : _("Не удалось отправить запрос на triple-review.");
					AppendEntry(std::move(err));
				}
			}
			return;
		}

		// Other skills (explain/review/fix/doc/send) bounce through
		// chat.send because the legacy llm_query tool handles them
		// uniformly as natural-language prompts.
		DispatchUserPrompt(opLabel + wxT(":\n\n```") + lang + wxT("\n") + src + wxT("\n```"));
		return;
	}
	if (kind == "agent.plan") {
		Entry e;
		e.role           = Entry::Role::Plan;
		e.planId         = wxString::FromUTF8(env.value("planId",         "").c_str());
		e.conversationId = wxString::FromUTF8(env.value("conversationId", "").c_str());
		e.rationale      = wxString::FromUTF8(env.value("rationale",      "").c_str());
		if (env.contains("mutations") && env["mutations"].is_array()) {
			wxString lines;
			for (const auto& m : env["mutations"]) {
				if (!m.is_object()) continue;
				const std::string mop   = m.value("op",       "?");
				const std::string mkind = m.value("kind",     "");
				const std::string mname = m.value("fullName", "");
				lines += wxT("  - ") + wxString::FromUTF8(mop.c_str())
				      + wxT(" ") + wxString::FromUTF8(mkind.c_str())
				      + wxT(" ") + wxString::FromUTF8(mname.c_str())
				      + wxT("\n");
			}
			e.mutations = lines;
		}
		AppendEntry(std::move(e));
		return;
	}
	if (kind == "agent.applied") {
		const wxString planId = wxString::FromUTF8(env.value("planId", "").c_str());
		for (auto& e : m_entries) {
			if (e.role == Entry::Role::Plan && e.planId == planId) {
				e.applied = true;
			}
		}
		RestorePlanPolicyAfterApply();
		RenderTranscript();
		return;
	}
	if (kind == "agent.subtask") {
		// Workmate-parity subtask decomposition. Pugi (or any future agent
		// emitter) sends one envelope per parent plan; the children come
		// pre-populated with their initial status ("pending" by default).
		// dependsOn carries other subtask ids that block this row.
		Entry e;
		e.role         = Entry::Role::Subtasks;
		e.parentPlanId = wxString::FromUTF8(env.value("parentPlanId", "").c_str());
		if (env.contains("subtasks") && env["subtasks"].is_array()) {
			for (const auto& st : env["subtasks"]) {
				if (!st.is_object()) continue;
				Entry::Subtask s;
				s.id     = wxString::FromUTF8(st.value("id",     "").c_str());
				s.title  = wxString::FromUTF8(st.value("title",  "").c_str());
				s.status = wxString::FromUTF8(st.value("status", "pending").c_str());
				if (st.contains("dependsOn") && st["dependsOn"].is_array()) {
					for (const auto& d : st["dependsOn"]) {
						if (d.is_string()) {
							s.dependsOn.push_back(
							    wxString::FromUTF8(d.get<std::string>().c_str()));
						}
					}
				}
				e.subtasks.push_back(std::move(s));
			}
		}
		AppendEntry(std::move(e));
		return;
	}
	if (kind == "agent.subtaskStatus") {
		// Single-row update — match by parentPlanId then by subtask id.
		// Missing parent / missing id silently no-op (delivery may race a
		// "New chat" clear; surfacing an Error row would be more noise
		// than signal for a status ping).
		const wxString parentPlanId = wxString::FromUTF8(
		    env.value("parentPlanId", "").c_str());
		const wxString subtaskId    = wxString::FromUTF8(
		    env.value("id", "").c_str());
		const wxString newStatus    = wxString::FromUTF8(
		    env.value("status", "").c_str());
		if (parentPlanId.IsEmpty() || subtaskId.IsEmpty()) return;
		bool changed = false;
		for (auto& ent : m_entries) {
			if (ent.role != Entry::Role::Subtasks) continue;
			if (ent.parentPlanId != parentPlanId)  continue;
			for (auto& s : ent.subtasks) {
				if (s.id == subtaskId) {
					if (!newStatus.IsEmpty()) s.status = newStatus;
					changed = true;
				}
			}
		}
		if (changed) {
			RenderTranscript();
			ScheduleSave();
		}
		return;
	}
	if (kind == "agent.tripleReview") {
		// aiBridge wraps the Anvil triple_review tool response into this
		// envelope; the `result` object carries the verdict + reviewers
		// + findings. Fill an Entry record without trying to also produce
		// a flat markdown body — the dedicated render path below has full
		// access to the structured data and never falls back to markdown.
		Entry e;
		e.role      = Entry::Role::TripleReview;
		e.requestId = wxString::FromUTF8(env.value("requestId", "").c_str());

		if (env.contains("result") && env["result"].is_object()) {
			const auto& res = env["result"];
			e.verdict       = wxString::FromUTF8(res.value("verdict", "").c_str());
			e.verdictReason = wxString::FromUTF8(res.value("reason",  "").c_str());

			if (res.contains("counts") && res["counts"].is_object()) {
				const auto& c = res["counts"];
				e.countP0 = c.value("P0", 0);
				e.countP1 = c.value("P1", 0);
				e.countP2 = c.value("P2", 0);
				e.countP3 = c.value("P3", 0);
			}
			if (res.contains("reviewers") && res["reviewers"].is_array()) {
				for (const auto& r : res["reviewers"]) {
					if (!r.is_object()) continue;
					Entry::ReviewerSummary rs;
					rs.model     = wxString::FromUTF8(r.value("model",   "").c_str());
					rs.content   = wxString::FromUTF8(r.value("content", "").c_str());
					rs.p0        = r.value("p0", 0);
					rs.p1        = r.value("p1", 0);
					rs.p2        = r.value("p2", 0);
					rs.p3        = r.value("p3", 0);
					rs.latencyMs = r.value("latencyMs", 0L);
					e.reviewers.push_back(std::move(rs));
				}
			}
			if (res.contains("findings") && res["findings"].is_array()) {
				for (const auto& f : res["findings"]) {
					if (!f.is_object()) continue;
					Entry::Finding fd;
					fd.severity = wxString::FromUTF8(f.value("severity", "").c_str());
					fd.reviewer = wxString::FromUTF8(f.value("reviewer", "").c_str());
					fd.line     = f.value("line", 0);
					fd.message  = wxString::FromUTF8(f.value("message",  "").c_str());
					fd.fix      = wxString::FromUTF8(f.value("fix",      "").c_str());
					e.findings.push_back(std::move(fd));
				}
			}
		}
		AppendEntry(std::move(e));
		return;
	}

	// Unknown envelope kind — render as a system note rather than swallow
	// silently. Helps when a plugin ships an envelope shape that postdates
	// this pane build.
	Entry e;
	e.role     = Entry::Role::System;
	e.markdown = wxString::Format(_("Неизвестное сообщение: kind=%s"),
	                                wxString::FromUTF8(kind.c_str()));
	AppendEntry(std::move(e));
}

void ibPluginWebPane::AppendEntry(Entry entry)
{
	m_entries.push_back(std::move(entry));
	// Enforce hard cap to keep both RAM and disk usage bounded — same
	// limit chatHistory::Save enforces on disk, applied here so the
	// in-memory transcript can't grow unbounded between saves either.
	if (m_entries.size() > ibChatHistory::kMaxEntries) {
		m_entries.erase(
		    m_entries.begin(),
		    m_entries.begin() +
		        (m_entries.size() - ibChatHistory::kMaxEntries));
	}
	RenderTranscript();
	ScheduleSave();
}

void ibPluginWebPane::AppendStreamingDelta(const wxString& requestId,
                                            const wxString& delta)
{
	// Locate an existing assistant entry for this request — could be
	// either a real streaming entry from a prior delta, OR the "Думаю…"
	// placeholder we inserted in DispatchUserPrompt so the user sees
	// immediate feedback. The placeholder shares the same requestId; on
	// first real delta we OVERWRITE its markdown instead of appending,
	// otherwise the placeholder text leaks into the rendered assistant
	// reply and corrupts md4c fence parsing.
	Entry* target = nullptr;
	for (auto& ent : m_entries) {
		if (ent.role == Entry::Role::Assistant &&
		    ent.requestId == requestId) {
			target = &ent;
			break;
		}
	}
	if (target == nullptr) {
		Entry e;
		e.role      = Entry::Role::Assistant;
		e.markdown  = delta;
		e.requestId = requestId;
		// COMMIT-MSG: tag the entry when its rid was registered as a
		// commit-message turn. The flag drives RenderCesCodeBlock to
		// emit the extra "Использовать как коммит" link on every
		// fenced block inside this entry.
		if (m_commitRequestIds.find(requestId) != m_commitRequestIds.end()) {
			e.commitSuggestion = true;
		}
		m_entries.push_back(std::move(e));
	} else if (target->markdown == _("Думаю…")) {
		// First real delta replacing the placeholder.
		target->markdown = delta;
		// Placeholder was created in DispatchUserPrompt before the
		// rid was registered with m_commitRequestIds (the order is:
		// DispatchUserPrompt mints rid → registers it → creates the
		// "Думаю…" placeholder Entry). The Entry was therefore created
		// with commitSuggestion=false; back-fill it here on the first
		// real delta so the rendered links appear.
		if (!target->commitSuggestion &&
		    m_commitRequestIds.find(requestId) != m_commitRequestIds.end()) {
			target->commitSuggestion = true;
		}
	} else {
		target->markdown += delta;
	}
	m_pending          = true;
	m_pendingRequestId = requestId;
	UpdateSendButtonLabel();
	RenderTranscript();
	// Stream deltas only schedule the debounced save; the timer collapses
	// many chunks into one disk write 500ms after the final delta.
	ScheduleSave();
}

void ibPluginWebPane::CompleteStreaming(const wxString& /*requestId*/,
                                         const wxString& meta)
{
	m_pending = false;
	m_pendingRequestId.Clear();
	if (m_statusBar) m_statusBar->SetLabel(meta);
	UpdateSendButtonLabel();
	// End-of-stream is a good time to flush: the response is complete,
	// the user's next interaction is likely a few seconds away.
	ScheduleSave();
}

void ibPluginWebPane::RenderTranscript()
{
	// Build full HTML document. wxHtmlWindow parses an HTML 3.2-ish
	// subset; we lean on TABLE + FONT + simple inline tags rather than
	// modern CSS because CSS support in wxHtmlWindow is very limited.
	wxString doc;
	doc.reserve(8192);

	// Body bg uses the assistant-row color so an assistant row blends into
	// the canvas — the user / error / plan rows stand out as the framed
	// boxes. Mirrors the Cursor / GitHub Copilot "assistant text on the
	// page surface, user input in a bubble" convention.
	doc += wxT("<html><body bgcolor=\"#fafbfc\" text=\"#1f2937\">");
	doc += wxT("<table width=\"100%\" cellspacing=\"6\" cellpadding=\"8\" border=\"0\">");

	if (m_entries.empty()) {
		doc += wxT("<tr><td><font color=\"#6b7280\" size=\"-1\">");
		doc += HtmlEscape(_("Введите запрос или используйте контекстное меню в редакторе."));
		doc += wxT("</font></td></tr>");
	}

	for (const auto& e : m_entries) {
		// Pick a row background that visually separates entries. User
		// rows get a subtle blue tint so the question reads as the
		// boundary; assistant rows get pure white so the markdown body
		// has the highest contrast against the muted canvas around it.
		const char* bg = "#ffffff";
		const char* roleColor = "#374151";
		switch (e.role) {
		case Entry::Role::User:         bg = "#dbeafe"; roleColor = "#1d4ed8"; break;
		case Entry::Role::Assistant:    bg = "#ffffff"; roleColor = "#1f2937"; break;
		case Entry::Role::System:       bg = "#f1f5f9"; roleColor = "#475569"; break;
		case Entry::Role::Error:        bg = "#fee2e2"; roleColor = "#b91c1c"; break;
		case Entry::Role::Plan:         bg = "#ffedd5"; roleColor = "#9a3412"; break;
		case Entry::Role::TripleReview: bg = "#ecfeff"; roleColor = "#0e7490"; break;
		case Entry::Role::Subtasks:     bg = "#f5f3ff"; roleColor = "#6d28d9"; break;
		}

		doc += wxString::Format(
		    wxT("<tr><td bgcolor=\"%s\">"),
		    wxString::FromUTF8(bg));

		// Role label + per-entry actions. Assistant rows get a
		// "Скопировать ответ" link that copies the whole markdown body
		// to the clipboard (separate from per-code-block Copy buttons).
		doc += wxString::Format(
		    wxT("<font size=\"-1\" color=\"%s\"><b>%s</b></font>"),
		    wxString::FromUTF8(roleColor),
		    HtmlEscape(RoleLabel(e.role)));
		if (e.role == Entry::Role::Assistant && !e.markdown.IsEmpty() &&
		    e.markdown != _("Думаю…")) {
			const size_t entryIdx = static_cast<size_t>(&e - &m_entries[0]);
			doc += wxString::Format(
			    wxT("&nbsp;&nbsp;<font size=\"-1\">"
			        "<a href=\"oes-copyreply:%zu\">%s</a></font>"),
			    entryIdx, HtmlEscape(_("Скопировать ответ")));
		}
		doc += wxT("<br>");

		if (e.role == Entry::Role::Plan) {
			if (!e.rationale.IsEmpty()) {
				doc += wxT("<p>") + HtmlEscape(e.rationale) + wxT("</p>");
			}
			if (!e.mutations.IsEmpty()) {
				doc += wxT("<pre>") + HtmlEscape(e.mutations) + wxT("</pre>");
			}
			if (e.applied) {
				doc += wxT("<font color=\"#059669\" size=\"-1\"><b>");
				doc += HtmlEscape(_("Применено"));
				doc += wxT("</b></font>");
			} else if (!e.planId.IsEmpty()) {
				// Plan-action links — wxHtmlWindow fires
				// wxEVT_HTML_LINK_CLICKED which we route in
				// OnLinkClicked. URL scheme oes-plan: + action.
				doc += wxString::Format(
				    wxT("<a href=\"oes-plan:approve/%s/%s\">[%s]</a>&nbsp;"
				        "<a href=\"oes-plan:reject/%s/%s\">[%s]</a>"),
				    HtmlEscape(e.planId),
				    HtmlEscape(e.conversationId),
				    HtmlEscape(_("Одобрить")),
				    HtmlEscape(e.planId),
				    HtmlEscape(e.conversationId),
				    HtmlEscape(_("Отклонить")));
			}
		} else if (e.role == Entry::Role::TripleReview) {
			// Verdict badge + reason line. Single-row table with a coloured
			// left cell — wxHtmlWindow renders <table bgcolor> reliably and
			// CSS backgrounds are unsupported here.
			const char* badgeBg = "#6b7280";
			const char* badgeFg = "#ffffff";
			VerdictBadgeColors(e.verdict, badgeBg, badgeFg);
			doc += wxT("<table cellspacing=\"0\" cellpadding=\"6\" border=\"0\"><tr>");
			doc += wxString::Format(
			    wxT("<td bgcolor=\"%s\"><font color=\"%s\"><b>&nbsp;%s — %s&nbsp;</b></font></td>"),
			    wxString::FromUTF8(badgeBg),
			    wxString::FromUTF8(badgeFg),
			    HtmlEscape(_("Вердикт")),
			    HtmlEscape(e.verdict.IsEmpty() ? wxString(wxT("—")) : e.verdict));
			doc += wxT("<td>&nbsp;");
			if (!e.verdictReason.IsEmpty()) {
				doc += HtmlEscape(e.verdictReason);
			}
			doc += wxT("</td></tr></table>");

			// Counts row — one cell per severity with its own bg colour.
			doc += wxT("<p><b>");
			doc += HtmlEscape(_("Резюме"));
			doc += wxT("</b>: ");
			const wxString sevTags[4] = {
			    wxT("P0"), wxT("P1"), wxT("P2"), wxT("P3")
			};
			const int sevCounts[4] = {
			    e.countP0, e.countP1, e.countP2, e.countP3
			};
			for (int si = 0; si < 4; ++si) {
				doc += wxString::Format(
				    wxT("<font size=\"-1\"><table cellspacing=\"0\" cellpadding=\"3\" border=\"0\" "
				        "bgcolor=\"%s\"><tr><td>&nbsp;%s: %d&nbsp;</td></tr></table></font>&nbsp;"),
				    wxString::FromUTF8(SeverityBgColor(sevTags[si])),
				    HtmlEscape(sevTags[si]),
				    sevCounts[si]);
			}
			doc += wxT("</p>");

			// Reviewers section — one table per model. Heading cell carries
			// the model name + latency; body cell holds the model's content
			// (rendered as a pre block to preserve the bullet-style output
			// reviewers usually emit).
			if (!e.reviewers.empty()) {
				doc += wxT("<p><b>");
				doc += HtmlEscape(_("Рецензенты"));
				doc += wxT("</b></p>");
				for (const auto& r : e.reviewers) {
					doc += wxT("<table width=\"100%\" cellspacing=\"0\" cellpadding=\"6\" border=\"0\">");
					doc += wxString::Format(
					    wxT("<tr><td bgcolor=\"#e5e7eb\"><b>%s</b>"
					        "&nbsp;&nbsp;<font size=\"-1\" color=\"#6b7280\">"
					        "P0:%d&nbsp;P1:%d&nbsp;P2:%d&nbsp;P3:%d&nbsp;·&nbsp;%ld ms"
					        "</font></td></tr>"),
					    HtmlEscape(r.model),
					    r.p0, r.p1, r.p2, r.p3, r.latencyMs);
					doc += wxT("<tr><td bgcolor=\"#f9fafb\"><pre>");
					doc += HtmlEscape(r.content);
					doc += wxT("</pre></td></tr></table>");
				}
			}

			// Findings table — severity / line / reviewer / message / fix.
			// Five-column layout; severity cell carries its palette colour
			// so a quick scan reveals where the P0s sit.
			if (!e.findings.empty()) {
				doc += wxT("<p><b>");
				doc += HtmlEscape(_("Замечания"));
				doc += wxT("</b></p>");
				doc += wxT("<table width=\"100%\" cellspacing=\"0\" cellpadding=\"4\" border=\"1\">");
				doc += wxT("<tr bgcolor=\"#e5e7eb\">");
				doc += wxT("<td><b>") + HtmlEscape(_("Серьёзность")) + wxT("</b></td>");
				doc += wxT("<td><b>") + HtmlEscape(_("Строка"))       + wxT("</b></td>");
				doc += wxT("<td><b>") + HtmlEscape(_("Рецензент"))    + wxT("</b></td>");
				doc += wxT("<td><b>") + HtmlEscape(_("Описание"))     + wxT("</b></td>");
				doc += wxT("<td><b>") + HtmlEscape(_("Исправление")) + wxT("</b></td>");
				doc += wxT("</tr>");
				for (const auto& f : e.findings) {
					doc += wxT("<tr>");
					doc += wxString::Format(
					    wxT("<td bgcolor=\"%s\"><b>%s</b></td>"),
					    wxString::FromUTF8(SeverityBgColor(f.severity)),
					    HtmlEscape(f.severity));
					doc += wxString::Format(wxT("<td>%d</td>"), f.line);
					doc += wxT("<td>") + HtmlEscape(f.reviewer) + wxT("</td>");
					doc += wxT("<td>") + HtmlEscape(f.message)  + wxT("</td>");
					doc += wxT("<td>") + HtmlEscape(f.fix)      + wxT("</td>");
					doc += wxT("</tr>");
				}
				doc += wxT("</table>");
			}
		} else if (e.role == Entry::Role::Subtasks) {
			// Workmate-parity tree view. wxHtmlWindow does NOT render the
			// nested-list bullet styles modern browsers offer, so we build a
			// flat two-column table with an indentation prefix per row.
			// Column 1 = status icon (emoji-free per repo policy; ASCII
			// symbol inside a coloured table cell). Column 2 = subtask
			// title; dependsOn rows shift right by one level so the
			// parent/children relationship reads at a glance.
			if (!e.parentPlanId.IsEmpty()) {
				doc += wxT("<font size=\"-1\" color=\"#6b7280\">");
				doc += HtmlEscape(_("План"));
				doc += wxT(": ");
				doc += HtmlEscape(e.parentPlanId);
				doc += wxT("</font><br>");
			}
			if (e.subtasks.empty()) {
				doc += wxT("<i>");
				doc += HtmlEscape(_("Список подзадач пуст."));
				doc += wxT("</i>");
			} else {
				// Build an id-set so dependsOn entries whose target sits
				// in the SAME subtasks block render as children (indented).
				// dependsOn ids that point outside the block render at the
				// root level — we can't safely guess their parent without
				// more context.
				std::unordered_map<wxString, size_t> idIndex;
				for (size_t si = 0; si < e.subtasks.size(); ++si) {
					if (!e.subtasks[si].id.IsEmpty()) {
						idIndex[e.subtasks[si].id] = si;
					}
				}
				doc += wxT("<table cellspacing=\"0\" cellpadding=\"4\" border=\"0\" width=\"100%\">");
				for (const auto& s : e.subtasks) {
					// Status palette — keep grey for unknown so a forward-rev
					// envelope doesn't paint the row red.
					const char* statusBg = "#e5e7eb";
					const char* statusFg = "#374151";
					wxString statusSym = wxT("?");
					if      (s.status == wxT("pending")) { statusBg = "#e5e7eb"; statusFg = "#4b5563"; statusSym = wxT("o"); }
					else if (s.status == wxT("running")) { statusBg = "#dbeafe"; statusFg = "#1d4ed8"; statusSym = wxT(">"); }
					else if (s.status == wxT("done"))    { statusBg = "#dcfce7"; statusFg = "#15803d"; statusSym = wxT("v"); }
					else if (s.status == wxT("failed"))  { statusBg = "#fee2e2"; statusFg = "#b91c1c"; statusSym = wxT("x"); }

					// Indent rows that depend on another row in THIS block.
					// Single level — Workmate parity, not a generic DAG.
					bool isChild = false;
					for (const auto& dep : s.dependsOn) {
						if (idIndex.find(dep) != idIndex.end()) { isChild = true; break; }
					}
					const wxString indent = isChild ? wxT("&nbsp;&nbsp;&nbsp;&nbsp;") : wxString();

					doc += wxT("<tr>");
					doc += wxString::Format(
					    wxT("<td width=\"24\" bgcolor=\"%s\" align=\"center\"><font color=\"%s\"><b>%s</b></font></td>"),
					    wxString::FromUTF8(statusBg),
					    wxString::FromUTF8(statusFg),
					    HtmlEscape(statusSym));
					doc += wxT("<td>");
					doc += indent;
					doc += HtmlEscape(s.title.IsEmpty() ? s.id : s.title);
					if (!s.id.IsEmpty()) {
						doc += wxT("&nbsp;&nbsp;<font size=\"-1\" color=\"#9ca3af\">");
						doc += HtmlEscape(s.id);
						doc += wxT("</font>");
					}
					if (!s.dependsOn.empty()) {
						wxString deps;
						for (size_t di = 0; di < s.dependsOn.size(); ++di) {
							if (di > 0) deps += wxT(", ");
							deps += s.dependsOn[di];
						}
						doc += wxT("<br><font size=\"-1\" color=\"#9ca3af\">");
						doc += HtmlEscape(_("зависит от"));
						doc += wxT(": ");
						doc += HtmlEscape(deps);
						doc += wxT("</font>");
					}
					doc += wxT("</td></tr>");
				}
				doc += wxT("</table>");
			}
		} else {
			// Extract code fences from the markdown body BEFORE md4c
			// sees them — replace each with a unique placeholder, run
			// md4c on the rest, then substitute the placeholders with
			// our CES-highlighted block (table header + Copy / Apply
			// links + coloured <pre> body). This route keeps inline
			// code (`like this`) handled by md4c as monospace text
			// while fenced blocks get the full IDE-style treatment.
			std::vector<std::pair<wxString, wxString>> blocks;
			wxString stripped = ExtractCodeBlocks(e.markdown, blocks);
			wxString body     = MarkdownToHtml(stripped);
			for (size_t bi = 0; bi < blocks.size(); ++bi) {
				const wxString placeholder = wxString::Format(
				    wxT("OESBLOCK_%zu_PLACEHOLDER"), bi);
				const wxString html = RenderCesCodeBlock(
				    /*entryIdx*/ static_cast<size_t>(&e - &m_entries[0]),
				    /*blockIdx*/ bi,
				    blocks[bi].first,
				    blocks[bi].second,
				    /*commitSuggestion*/ e.commitSuggestion);
				body.Replace(placeholder, html);
			}
			// Persist code blocks on the entry so link handlers can
			// resolve them later. Index reflects emission order, same
			// as the URL scheme produced above.
			const_cast<Entry&>(e).codeBlocks = std::move(blocks);
			doc += body;
		}

		doc += wxT("</td></tr>");
	}

	doc += wxT("</table></body></html>");

	if (m_history != nullptr) {
		m_history->SetPage(doc);
		// Keep scrolled to bottom — newest entries appear at the end.
		m_history->Scroll(0, m_history->GetVirtualSize().y);
	}
}

void ibPluginWebPane::OnSendClicked(wxCommandEvent& /*event*/)
{
	// Stop-while-pending takes precedence over text input. Clicking the
	// button (or pressing Enter) during streaming fires agent.cancel and
	// flips the label back to "Отправить"; the typed text remains intact
	// so the user can resend without retyping.
	if (m_pending) {
		DispatchCancelRequest();
		return;
	}

	const wxString text = m_input->GetValue();
	if (text.IsEmpty()) return;
	m_input->Clear();
	if (m_slashPopup && m_slashPopup->IsShown()) m_slashPopup->Dismiss();

	// Slash commands map to editor.skill envelopes; everything else
	// falls through to the standard chat.send path.
	if (TryDispatchSlashCommand(text)) return;
	DispatchUserPrompt(text);
}

void ibPluginWebPane::OnInputEnter(wxCommandEvent& event)
{
	// wxTE_PROCESS_ENTER fires this when Return is pressed inside the
	// multi-line text control. Shift+Enter still inserts a newline because
	// wxTextCtrl handles that before firing this event.
	OnSendClicked(event);
}

void ibPluginWebPane::OnInputText(wxCommandEvent& /*event*/)
{
	RefreshSlashPopup();
	RefreshAtPopup();
}

void ibPluginWebPane::OnInputKeyDown(wxKeyEvent& event)
{
	// Resolve "which popup is steering arrow keys right now". The slash
	// popup binds to text starting with '/', the @ popup binds to a
	// trailing '@<partial>' anywhere in the text — they cannot both be
	// visible at the same time, but the routing logic is symmetric.
	auto* activePopup =
	    (m_slashPopup && m_slashPopup->IsShown())
	        ? static_cast<wxPopupTransientWindow*>(m_slashPopup)
	    : (m_atPopup && m_atPopup->IsShown())
	        ? static_cast<wxPopupTransientWindow*>(m_atPopup)
	    : nullptr;

	// Shift+Enter (with no popup steering) inserts a newline rather than
	// sending. Without this the wxTE_PROCESS_ENTER style hands Shift+Enter
	// to OnInputEnter the same way plain Enter — sending mid-paragraph.
	if (activePopup == nullptr &&
	    (event.GetKeyCode() == WXK_RETURN ||
	     event.GetKeyCode() == WXK_NUMPAD_ENTER) &&
	    event.ShiftDown()) {
		m_input->WriteText(wxT("\n"));
		return;
	}
	if (activePopup == nullptr) { event.Skip(); return; }

	// Polymorphic dispatch via a tiny lambda — keeps the switch statement
	// from doubling in length per popup.
	auto move = [this](int delta) {
		if (m_slashPopup && m_slashPopup->IsShown()) m_slashPopup->MoveSelection(delta);
		else if (m_atPopup && m_atPopup->IsShown()) m_atPopup->MoveSelection(delta);
	};
	auto accept = [this]() {
		if (m_slashPopup && m_slashPopup->IsShown()) m_slashPopup->AcceptSelection();
		else if (m_atPopup && m_atPopup->IsShown()) m_atPopup->AcceptSelection();
	};
	auto dismiss = [this]() {
		if (m_slashPopup && m_slashPopup->IsShown()) m_slashPopup->Dismiss();
		else if (m_atPopup && m_atPopup->IsShown()) m_atPopup->Dismiss();
	};

	switch (event.GetKeyCode()) {
	case WXK_DOWN:        move(+1);  return;
	case WXK_UP:          move(-1);  return;
	case WXK_RETURN:
	case WXK_NUMPAD_ENTER:
	case WXK_TAB:         accept();  return;
	case WXK_ESCAPE:      dismiss(); return;
	default:              event.Skip(); return;
	}
}

void ibPluginWebPane::OnInputKillFocus(wxFocusEvent& event)
{
	// wxPopupTransientWindow already auto-dismisses on outside click,
	// but Tab-out / programmatic focus changes can leave a stale popup
	// floating. Belt-and-braces: kill it whenever the input loses focus.
	if (m_slashPopup && m_slashPopup->IsShown()) m_slashPopup->Dismiss();
	if (m_atPopup    && m_atPopup   ->IsShown()) m_atPopup   ->Dismiss();
	event.Skip();
}

void ibPluginWebPane::RefreshSlashPopup()
{
	if (m_input == nullptr) return;
	const wxString text = m_input->GetValue();

	// We only trigger the popup when the slash is the FIRST character —
	// mid-text slashes (URLs, fractions) must not flash a dropdown.
	if (text.IsEmpty() || text[0] != wxT('/')) {
		if (m_slashPopup && m_slashPopup->IsShown()) m_slashPopup->Dismiss();
		return;
	}
	// Also bail once the user typed a space — at that point the command
	// is committed and they're now writing the argument.
	if (text.Find(wxT(' ')) != wxNOT_FOUND) {
		if (m_slashPopup && m_slashPopup->IsShown()) m_slashPopup->Dismiss();
		return;
	}

	if (m_slashPopup == nullptr) {
		m_slashPopup = new ibSlashCommandPopup(this,
		    [this](const wxString& cmd) { AcceptSlashCommand(cmd); });
	}
	m_slashPopup->UpdatePrefix(text, m_input);
}

void ibPluginWebPane::AcceptSlashCommand(const wxString& command)
{
	if (m_input == nullptr) return;
	// Replace the "/<partial>" prefix with the full command + space.
	// We slice on the first space (there shouldn't be any while the
	// popup is open, but defensive in case of races) so anything the
	// user already typed after the command name is preserved.
	const wxString current = m_input->GetValue();
	int spaceAt = static_cast<int>(current.find(wxT(' ')));
	wxString tail;
	if (spaceAt != wxNOT_FOUND) {
		tail = current.Mid(spaceAt);  // includes the leading space
	}
	const wxString replaced = command + (tail.IsEmpty() ? wxString(wxT(" ")) : tail);
	m_input->ChangeValue(replaced);   // ChangeValue = no wxEVT_TEXT
	m_input->SetInsertionPointEnd();
	m_input->SetFocus();
}

// ---------------------------------------------------------------------------
// @-context popup glue
// ---------------------------------------------------------------------------

namespace {

// Find the start of a trailing "@<partial>" run in `text`, walking back from
// the insertion point. Returns -1 if the cursor is not currently inside an
// @-token. Token characters mirror ibChatContext::IsTokenChar but we
// duplicate the predicate here so the pane doesn't have to expose it.
int FindTrailingAtRun(const wxString& text, long caret)
{
	if (caret < 0 || caret > static_cast<long>(text.size())) return -1;
	auto isTokenChar = [](wxUniChar c) {
		const auto v = static_cast<unsigned>(c.GetValue());
		return (v >= 'A' && v <= 'Z') ||
		       (v >= 'a' && v <= 'z') ||
		       (v >= '0' && v <= '9') ||
		       v == '_' || v == '.';
	};
	long i = caret - 1;
	while (i >= 0 && isTokenChar(text[i])) --i;
	if (i < 0 || text[i] != wxT('@')) return -1;
	// '@' must be at start-of-text or preceded by whitespace so we don't
	// fire the popup on email-style "user@host" tokens.
	if (i > 0) {
		const wxUniChar prev = text[i - 1];
		const bool ws = (prev == wxT(' ') || prev == wxT('\n')
		              || prev == wxT('\t'));
		if (!ws) return -1;
	}
	return static_cast<int>(i);
}

} // namespace

void ibPluginWebPane::RefreshAtPopup()
{
	if (m_input == nullptr) return;
	const wxString text  = m_input->GetValue();
	const long     caret = m_input->GetInsertionPoint();
	const int      at    = FindTrailingAtRun(text, caret);
	if (at < 0) {
		if (m_atPopup && m_atPopup->IsShown()) m_atPopup->Dismiss();
		return;
	}
	const wxString prefix = text.SubString(at + 1, caret - 1);

	if (m_atPopup == nullptr) {
		m_atPopup = new ibChatContextPopup(this,
		    [this](const wxString& fullName) { AcceptAtSuggestion(fullName); });
	}
	m_atPopup->UpdatePrefix(prefix, m_input);
}

void ibPluginWebPane::AcceptAtSuggestion(const wxString& fullName)
{
	if (m_input == nullptr) return;
	const wxString text  = m_input->GetValue();
	const long     caret = m_input->GetInsertionPoint();
	const int      at    = FindTrailingAtRun(text, caret);
	if (at < 0) return;  // race — token vanished between key-up and accept

	// Replace text[at .. caret-1] (i.e. the "@<partial>") with the full
	// "@<fullName> " literal. We use ChangeValue to avoid re-triggering
	// the popup via wxEVT_TEXT.
	const wxString head = text.Mid(0, at);
	const wxString tail = text.Mid(caret);
	const wxString insert = wxT("@") + fullName + wxT(" ");
	const wxString replaced = head + insert + tail;
	m_input->ChangeValue(replaced);
	m_input->SetInsertionPoint(static_cast<long>((head + insert).size()));
	m_input->SetFocus();
}

// ---------------------------------------------------------------------------
// New-chat button + debounced save
// ---------------------------------------------------------------------------

void ibPluginWebPane::OnNewChatClicked(wxCommandEvent& /*event*/)
{
	if (m_entries.empty()) return;  // nothing to clear
	const int rc = wxMessageBox(_("Очистить историю чата?"),
	                             _("Новый чат"),
	                             wxYES_NO | wxICON_QUESTION, this);
	if (rc != wxYES) return;
	m_entries.clear();
	m_pending = false;
	m_pendingRequestId.Clear();
	UpdateSendButtonLabel();
	if (m_persistEnabled) {
		ibChatHistory::Clear(m_configHash);
	}
	if (m_saveTimer.IsRunning()) m_saveTimer.Stop();
	if (m_statusBar) m_statusBar->SetLabel(wxEmptyString);
	RenderTranscript();
}

void ibPluginWebPane::OnSaveTimer(wxTimerEvent& /*event*/)
{
	SaveNow();
}

void ibPluginWebPane::ScheduleSave()
{
	if (!m_persistEnabled) return;
	// Debounce: any new entry within the 500ms window restarts the
	// timer. wxTimer::Start with a one-shot flag does exactly that.
	m_saveTimer.Start(500, wxTIMER_ONE_SHOT);
}

void ibPluginWebPane::SaveNow()
{
	if (!m_persistEnabled) return;
	if (m_configHash.IsEmpty()) {
		m_configHash = ibChatHistory::ComputeConfigHash();
	}
	ibChatHistory::Save(m_configHash, m_entries);
}

// ---------------------------------------------------------------------------
// Test-only entry points
// ---------------------------------------------------------------------------

void ibPluginWebPane::SetConfigHashForTests(const wxString& configHash)
{
	m_configHash = configHash;
}

void ibPluginWebPane::ReloadHistoryFromDisk()
{
	std::vector<Entry> restored;
	if (ibChatHistory::Load(m_configHash, restored)) {
		m_entries = std::move(restored);
		RenderTranscript();
	}
}

void ibPluginWebPane::SetPersistEnabledForTests(bool enabled)
{
	m_persistEnabled = enabled;
}

void ibPluginWebPane::AppendEntryForTests(Entry entry)
{
	AppendEntry(std::move(entry));
}

bool ibPluginWebPane::TryDispatchSlashCommand(const wxString& text)
{
	if (text.IsEmpty() || text[0] != wxT('/')) return false;

	// Split into "/cmd" and the rest. Allow either a space or a newline
	// as the separator so multi-line prompts work ("/explain\n<code>").
	int sep = wxNOT_FOUND;
	for (size_t i = 0; i < text.size(); ++i) {
		const wxUniChar c = text[i];
		if (c == wxT(' ') || c == wxT('\n') || c == wxT('\t')) {
			sep = static_cast<int>(i);
			break;
		}
	}
	const wxString head = (sep == wxNOT_FOUND) ? text : text.Mid(0, sep);
	wxString body = (sep == wxNOT_FOUND) ? wxString() : text.Mid(sep + 1);
	body.Trim(false).Trim(true);

	const auto& cmds = ibSlashCommandPopup::AllCommands();
	const ibSlashCommandPopup::Command* match = nullptr;
	for (const auto& c : cmds) {
		if (head.CmpNoCase(c.name) == 0) { match = &c; break; }
	}
	if (match == nullptr) return false;  // unknown — let chat.send handle it

	if (match->custom) {
		wxString prompt = match->prompt;
		if (!body.IsEmpty()) {
			if (!prompt.IsEmpty()) prompt += wxT("\n\n");
			prompt += body;
		}
		DispatchUserPrompt(prompt);
		return true;
	}

	// Echo the user-facing prompt into the transcript so the chat
	// history reflects what was actually sent.
	{
		Entry e;
		e.role = Entry::Role::User;
		if (body.IsEmpty()) {
			e.markdown = match->name + wxT(" — ") + match->description;
		} else {
			e.markdown = match->name + wxT(" — ") + match->description
			           + wxT("\n\n```ces\n") + body + wxT("\n```");
		}
		AppendEntry(std::move(e));
	}

	if (m_onMessage == nullptr) return true;

	const wxString rid = wxString::Format(wxT("skill-%lld"),
	                                       static_cast<long long>(wxGetUTCTime()));
	nlohmann::json env;
	env["kind"]      = "editor.skill";
	env["requestId"] = std::string(rid.utf8_str());
	env["op"]        = std::string(match->op.utf8_str());
	env["language"]  = "ces";
	env["code"]      = std::string(body.utf8_str());
	// AGENT-MODE: the agent skill expects the body to be the user's
	// natural-language instruction, not code — surface it as `prompt` so
	// aiBridge's RunOesAgent uses it directly rather than the code fallback.
	if (match->op == wxT("agent")) {
		env["prompt"] = std::string(body.utf8_str());
	}

	wxString localeName;
	if (wxLocale* loc = wxGetLocale()) localeName = loc->GetCanonicalName();
	if (localeName.IsEmpty()) localeName = wxT("uk-UA");
	env["locale"]    = std::string(localeName.utf8_str());

	const std::string payload = env.dump();
	// SEC-P1-12: std::string owns the bytes outright — was wxScopedCharBuffer
	// which holds a refcounted handle to the wxString's internal buffer
	// that could free under us if m_paneId is mutated.
	const std::string paneIdUtf8(m_paneId.utf8_str().data());
	// SEC-P1-9: scope tl_currentPluginId for the duration of the onMessage
	// call so synchronous host trampolines see the registering plugin's id.
	auto pidGuard = MakePaneScopeGuard(m_paneId);
	try {
		m_onMessage(paneIdUtf8.c_str(), payload.c_str(), m_userData);
	} catch (...) {
		RestorePlanPolicyAfterApply();
		Entry err;
		err.role     = Entry::Role::Error;
		err.markdown = _("Не удалось отправить запрос — плагин вернул исключение.");
		AppendEntry(std::move(err));
	}
	return true;
}

void ibPluginWebPane::DispatchUserPrompt(const wxString& prompt)
{
	if (m_onMessage == nullptr) return;
	if (prompt.IsEmpty()) return;

	// Show the user prompt in the transcript immediately so the wait
	// for the assistant reply doesn't look like the click did nothing.
	// The transcript shows the prompt EXACTLY as the user typed it (with
	// the @-tokens intact); the resolved context block is only injected
	// into the outbound envelope. Mirrors 1С:Workmate UX — the chat
	// history stays readable, the LLM gets the expanded context.
	Entry e;
	e.role     = Entry::Role::User;
	e.markdown = prompt;
	AppendEntry(std::move(e));

	// Resolve @-tokens into a context block. Empty when the prompt has no
	// resolvable references; in that case we send the prompt unchanged.
	wxWindow* searchRoot = wxGetTopLevelParent(this);
	const wxString contextBlock =
	    ibChatContext::BuildContextBlock(prompt, searchRoot);
	const wxString outboundPrompt = contextBlock.IsEmpty()
	    ? prompt
	    : contextBlock + prompt;

	// Build the chat.send envelope. Plain string concat — we control
	// every field so a tiny snprintf-free path is enough.
	const wxString rid = wxString::Format(wxT("chat-%lld"),
	                                        static_cast<long long>(wxGetUTCTime()));

	// COMMIT-MSG: if the caller upstream (DispatchEnvelope for an
	// op="commit" editor.skill) flagged this prompt as a commit-message
	// generation turn, register the freshly minted rid so the assistant
	// streaming entry picks up the commitSuggestion flag.
	if (m_nextPromptIsCommit) {
		m_commitRequestIds.insert(rid);
		m_nextPromptIsCommit = false;
	}
	nlohmann::json env;
	env["kind"]      = "chat.send";
	env["requestId"] = std::string(rid.utf8_str());
	env["text"]      = std::string(outboundPrompt.utf8_str());
	env["prompt"]    = std::string(outboundPrompt.utf8_str()); // v3-shim fallback
	env["profile"]   = std::string(CurrentModelProfileId().utf8_str());
	// Locale: prefer the live wxLocale name, fall back to "uk-UA" which
	// is the OES Designer dev-mode default Pugi uses.
	wxString localeName;
	if (wxLocale* loc = wxGetLocale()) localeName = loc->GetCanonicalName();
	if (localeName.IsEmpty()) localeName = wxT("uk-UA");
	env["locale"]    = std::string(localeName.utf8_str());
	const std::string payload = env.dump();

	// Enter pending state as soon as the envelope leaves the pane. The
	// first chat.delta normally trips this too, but flipping the button
	// label up front gives the user something to click for Stop even if
	// the plugin's first chunk takes seconds to arrive.
	m_pending          = true;
	m_pendingRequestId = rid;
	UpdateSendButtonLabel();

	// Thinking indicator — visible feedback while the assistant
	// computes. wxHtmlWindow can't animate, so we use a static "…"
	// row that gets cleared by the first chat.delta (AppendStreamingDelta
	// replaces the pending entry when it matches m_pendingRequestId).
	// Status bar carries the live phase label as well.
	Entry waiting;
	waiting.role      = Entry::Role::Assistant;
	waiting.markdown  = _("Думаю…");
	waiting.requestId = rid;
	// COMMIT-MSG: tag the placeholder up front so the assistant entry
	// keeps its commitSuggestion=true once chat.delta back-fills the
	// body — RenderCesCodeBlock relies on this flag at render time.
	if (m_commitRequestIds.find(rid) != m_commitRequestIds.end()) {
		waiting.commitSuggestion = true;
	}
	AppendEntry(std::move(waiting));
	if (m_statusBar) m_statusBar->SetLabel(_("Отправлено, ждём ответа…"));

	// SEC-P1-12: std::string ownership — see DispatchEnvelope.
	const std::string paneIdUtf8(m_paneId.utf8_str().data());
	// SEC-P1-9: scope tl_currentPluginId for the onMessage dispatch.
	auto pidGuard = MakePaneScopeGuard(m_paneId);
	try {
		m_onMessage(paneIdUtf8.c_str(), payload.c_str(), m_userData);
	} catch (...) {
		Entry err;
		err.role     = Entry::Role::Error;
		err.markdown = _("Не удалось отправить запрос — плагин вернул исключение.");
		// Plugin threw before the worker spawned — never going to get a
		// chat.end, so revert state here.
		m_pending = false;
		m_pendingRequestId.Clear();
		UpdateSendButtonLabel();
		AppendEntry(std::move(err));
	}
}

void ibPluginWebPane::UpdateSendButtonLabel()
{
	if (m_sendBtn == nullptr) return;
	m_sendBtn->SetLabel(m_pending ? _("Стоп") : _("Отправить"));
}

void ibPluginWebPane::DispatchCancelRequest()
{
	if (m_onMessage == nullptr) return;
	if (m_pendingRequestId.IsEmpty()) {
		// Nothing to cancel — drop straight back to idle.
		m_pending = false;
		UpdateSendButtonLabel();
		return;
	}

	nlohmann::json env;
	env["kind"]      = "agent.cancel";
	env["requestId"] = std::string(m_pendingRequestId.utf8_str());
	const std::string payload = env.dump();

	// SEC-P1-12: std::string ownership.
	const std::string paneIdUtf8(m_paneId.utf8_str().data());
	// SEC-P1-9: scope tl_currentPluginId for the cancel envelope.
	auto pidGuard = MakePaneScopeGuard(m_paneId);
	try {
		m_onMessage(paneIdUtf8.c_str(), payload.c_str(), m_userData);
	} catch (...) {
		// Plugin bug while cancelling — keep host alive.
	}

	// Flip the button immediately for snappy feedback. The worker still
	// emits chat.end (with cancelled:true) which will idempotently
	// re-clear pending; UpdateSendButtonLabel here just shortens the
	// visible "Стоп" hold time so the UI feels responsive.
	m_pending = false;
	m_pendingRequestId.Clear();
	UpdateSendButtonLabel();
	if (m_statusBar) m_statusBar->SetLabel(_("Отменено"));
}

void ibPluginWebPane::OnLinkClicked(wxHtmlLinkEvent& event)
{
	const wxString href = event.GetLinkInfo().GetHref();

	// oes-copy:<entryIdx>:<blockIdx> — clipboard copy of the raw code.
	if (href.StartsWith(wxT("oes-copy:"))) {
		HandleCopyLink(href.Mid(wxString(wxT("oes-copy:")).length()));
		return;
	}
	// oes-copyreply:<entryIdx> — copy the whole assistant markdown body
	// (everything between the role label and the next entry, including
	// any code blocks). Mirrors Workmate's "Copy reply" button.
	if (href.StartsWith(wxT("oes-copyreply:"))) {
		const wxString idx = href.Mid(wxString(wxT("oes-copyreply:")).length());
		long ei = 0;
		if (idx.ToLong(&ei) && ei >= 0 &&
		    ei < static_cast<long>(m_entries.size())) {
			const auto& e = m_entries[ei];
			if (wxTheClipboard->Open()) {
				wxTheClipboard->SetData(new wxTextDataObject(e.markdown));
				wxTheClipboard->Close();
				if (m_statusBar) m_statusBar->SetLabel(_("Ответ скопирован"));
			}
		}
		return;
	}
	// oes-apply:<entryIdx>:<blockIdx> — insert / replace in the editor.
	if (href.StartsWith(wxT("oes-apply:"))) {
		HandleApplyLink(href.Mid(wxString(wxT("oes-apply:")).length()));
		return;
	}
	// COMMIT-MSG: oes-commit:<entryIdx>:<blockIdx> — copy the message
	// to the clipboard AND run `git commit -m "<message>"` after a
	// confirmation modal. Only ever rendered on entries the pane
	// flagged as commit-message suggestions.
	if (href.StartsWith(wxT("oes-commit:"))) {
		HandleCommitLink(href.Mid(wxString(wxT("oes-commit:")).length()));
		return;
	}

	if (m_onMessage == nullptr) { event.Skip(); return; }
	if (!href.StartsWith(wxT("oes-plan:"))) {
		event.Skip();
		return;
	}
	// Format: oes-plan:approve/<planId>/<conversationId>
	const wxString body = href.AfterFirst(wxT(':'));
	const wxString action = body.BeforeFirst(wxT('/'));
	const wxString rest   = body.AfterFirst(wxT('/'));
	const wxString planId = rest.BeforeFirst(wxT('/'));
	const wxString convId = rest.AfterFirst(wxT('/'));

	if (action == wxT("approve")) {
		if (IsReadOnlyMode()) {
			Entry err;
			err.role     = Entry::Role::Error;
			err.markdown = _("Режим \"Только чтение\" запрещает применение плана.");
			AppendEntry(std::move(err));
			return;
		}
		if ((m_permissionModeValue == PermissionMode::ConfirmAll ||
		     m_permissionModeValue == PermissionMode::ConfirmWrites) &&
		    !ConfirmAgentAction(_("применить план изменений"))) {
			return;
		}
		if (m_permissionModeValue != PermissionMode::Auto) {
			ElevatePlanPolicyForApply();
		}
	} else if (action == wxT("reject")) {
		RestorePlanPolicyAfterApply();
	}

	nlohmann::json env;
	env["kind"]           = (action == wxT("approve")) ? "agent.approve"
	                       : (action == wxT("reject"))  ? "agent.reject"
	                                                     : "agent.undo";
	env["planId"]         = std::string(planId.utf8_str());
	env["conversationId"] = std::string(convId.utf8_str());
	const std::string payload = env.dump();

	// SEC-P1-12: std::string ownership.
	const std::string paneIdUtf8(m_paneId.utf8_str().data());
	// SEC-P1-9: scope tl_currentPluginId for the plan-action envelope.
	auto pidGuard = MakePaneScopeGuard(m_paneId);
	try {
		m_onMessage(paneIdUtf8.c_str(), payload.c_str(), m_userData);
	} catch (...) {
		// Plugin bug — keep host alive.
		RestorePlanPolicyAfterApply();
	}
}

void ibPluginWebPane::HandleCopyLink(const wxString& spec)
{
	// spec = "<entryIdx>:<blockIdx>"
	const wxString eStr = spec.BeforeFirst(wxT(':'));
	const wxString bStr = spec.AfterFirst(wxT(':'));
	long ei = 0, bi = 0;
	if (!eStr.ToLong(&ei) || !bStr.ToLong(&bi)) return;
	if (ei < 0 || ei >= static_cast<long>(m_entries.size())) return;
	const auto& e = m_entries[ei];
	if (bi < 0 || bi >= static_cast<long>(e.codeBlocks.size())) return;

	if (wxTheClipboard->Open()) {
		wxTheClipboard->SetData(new wxTextDataObject(e.codeBlocks[bi].second));
		wxTheClipboard->Close();
		// Visual confirmation — flash status bar for 1s.
		if (m_statusBar) {
			m_statusBar->SetLabel(_("Скопировано"));
			m_statusBar->Update();
		}
	}
}

void ibPluginWebPane::HandleApplyLink(const wxString& spec)
{
	const wxString eStr = spec.BeforeFirst(wxT(':'));
	const wxString bStr = spec.AfterFirst(wxT(':'));
	long ei = 0, bi = 0;
	if (!eStr.ToLong(&ei) || !bStr.ToLong(&bi)) return;
	if (ei < 0 || ei >= static_cast<long>(m_entries.size())) return;
	const auto& e = m_entries[ei];
	if (bi < 0 || bi >= static_cast<long>(e.codeBlocks.size())) return;

	const wxString& code = e.codeBlocks[bi].second;

	// Find the currently focused wxStyledTextCtrl anywhere in the
	// top-level window tree. The chat pane shouldn't reach down into
	// designer-specific types, so we walk wxWindows generically and
	// dispatch by dynamic_cast — the editor's class hierarchy roots at
	// wxStyledTextCtrl whichever subclass the host installs.
	wxWindow* focused = wxWindow::FindFocus();
	wxStyledTextCtrl* stc = dynamic_cast<wxStyledTextCtrl*>(focused);
	if (stc == nullptr) {
		// Fallback: walk the top-level window's children for the first
		// styled text control that is visible.
		wxWindow* root = wxGetTopLevelParent(this);
		if (root != nullptr) {
			std::function<wxStyledTextCtrl*(wxWindow*)> walk =
			    [&walk](wxWindow* w) -> wxStyledTextCtrl* {
				if (w == nullptr) return nullptr;
				if (auto* s = dynamic_cast<wxStyledTextCtrl*>(w)) {
					if (s->IsShown()) return s;
				}
				for (wxWindow* child : w->GetChildren()) {
					if (auto* found = walk(child)) return found;
				}
				return nullptr;
			};
			stc = walk(root);
		}
	}

	if (stc == nullptr) {
		if (m_statusBar) m_statusBar->SetLabel(
		    _("Не найден активный редактор кода"));
		return;
	}

	// Replace selection if any, otherwise insert at caret. wxSTC's
	// ReplaceSelection handles both forms.
	stc->ReplaceSelection(code);
	stc->SetFocus();
	if (m_statusBar) m_statusBar->SetLabel(_("Вставлено в редактор"));
}

// COMMIT-MSG: oes-commit:<entryIdx>:<blockIdx> — copy the proposed
// message to the clipboard AND run `git commit -m "<message>"` after
// user confirmation. The link is only rendered when the assistant
// entry was flagged as a commit suggestion (commitSuggestion=true);
// outside that context, the URL scheme is never emitted.
void ibPluginWebPane::HandleCommitLink(const wxString& spec)
{
	const wxString eStr = spec.BeforeFirst(wxT(':'));
	const wxString bStr = spec.AfterFirst(wxT(':'));
	long ei = 0, bi = 0;
	if (!eStr.ToLong(&ei) || !bStr.ToLong(&bi)) return;
	if (ei < 0 || ei >= static_cast<long>(m_entries.size())) return;
	const auto& e = m_entries[ei];
	if (bi < 0 || bi >= static_cast<long>(e.codeBlocks.size())) return;

	const wxString& message = e.codeBlocks[bi].second;
	if (message.IsEmpty()) {
		if (m_statusBar) m_statusBar->SetLabel(_("Сообщение коммита пусто"));
		return;
	}

	// Clipboard copy first — even if the user cancels the commit, the
	// message stays on the clipboard for manual paste.
	if (wxTheClipboard->Open()) {
		wxTheClipboard->SetData(new wxTextDataObject(message));
		wxTheClipboard->Close();
	}

	// Build a confirmation modal showing the message body so the user
	// sees exactly what's about to land. wxYES_NO + wxICON_QUESTION is
	// the platform-standard "yes runs, no cancels" shape.
	wxString preview = message;
	// Trim to ~10 lines for the dialog preview; the full body still
	// goes to git unchanged.
	{
		int newlines = 0;
		for (size_t i = 0; i < preview.size(); ++i) {
			if (preview[i] == wxT('\n')) {
				if (++newlines == 10) {
					preview = preview.Mid(0, i);
					preview += wxT("\n…");
					break;
				}
			}
		}
	}
	const wxString question =
	    wxString::Format(_("Создать коммит с этим сообщением?\n\n%s"),
	                      preview);
	const int answer = wxMessageBox(question,
	                                  _("Использовать как коммит"),
	                                  wxYES_NO | wxICON_QUESTION);
	if (answer != wxYES) {
		if (m_statusBar) m_statusBar->SetLabel(
		    _("Коммит отменён, сообщение скопировано в буфер"));
		return;
	}

	// Verify there is something to commit BEFORE invoking git. `git
	// diff --cached --quiet` exits 1 when there ARE staged changes
	// (counter-intuitive but documented) and 0 when there are none.
	wxArrayString diffOut, diffErr;
	const long diffRc = wxExecute(
	    wxT("git diff --cached --quiet"),
	    diffOut, diffErr,
	    wxEXEC_SYNC | wxEXEC_NODISABLE);
	if (diffRc == 0) {
		// rc==0 means no staged changes.
		Entry err;
		err.role     = Entry::Role::Error;
		err.markdown = _("Нет staged-изменений (выполните `git add` перед коммитом).");
		AppendEntry(std::move(err));
		return;
	}

	// Build the git invocation. We pass the message via a temp file
	// (`git commit -F <file>`) rather than `-m` so multi-line bodies
	// survive shell quoting on all three platforms. wxExecute on
	// Windows uses CreateProcess which doesn't reinterpret \n inside
	// double-quotes; the -F path is robust.
	const wxString tmpPath = wxFileName::CreateTempFileName(wxT("oes-commit-"));
	if (tmpPath.IsEmpty()) {
		Entry err;
		err.role     = Entry::Role::Error;
		err.markdown = _("Не удалось создать временный файл сообщения.");
		AppendEntry(std::move(err));
		return;
	}
	{
		wxFile f(tmpPath, wxFile::write);
		if (!f.IsOpened()) {
			Entry err;
			err.role     = Entry::Role::Error;
			err.markdown = _("Не удалось открыть временный файл сообщения.");
			AppendEntry(std::move(err));
			return;
		}
		const wxScopedCharBuffer buf = message.utf8_str();
		f.Write(buf.data(), buf.length());
	}

	const wxString cmd = wxT("git commit -F \"") + tmpPath + wxT("\"");
	wxArrayString outLines, errLines;
	const long rc = wxExecute(cmd, outLines, errLines,
	                            wxEXEC_SYNC | wxEXEC_NODISABLE);
	// Best-effort cleanup; wxRemoveFile drops the temp file even if
	// git couldn't read it.
	wxRemoveFile(tmpPath);

	if (rc == 0) {
		Entry sys;
		sys.role     = Entry::Role::System;
		wxString body = _("Коммит создан.");
		if (!outLines.IsEmpty()) {
			body += wxT("\n\n```\n");
			for (const wxString& ln : outLines) {
				body += ln;
				body += wxT("\n");
			}
			body += wxT("```");
		}
		sys.markdown = body;
		AppendEntry(std::move(sys));
		if (m_statusBar) m_statusBar->SetLabel(_("Коммит создан"));
	} else {
		Entry err;
		err.role     = Entry::Role::Error;
		wxString body = _("Команда git вернула ошибку.");
		if (!errLines.IsEmpty()) {
			body += wxT("\n\n```\n");
			for (const wxString& ln : errLines) {
				body += ln;
				body += wxT("\n");
			}
			body += wxT("```");
		}
		err.markdown = body;
		AppendEntry(std::move(err));
	}
}

// (UpdateSendButtonLabel / DispatchCancelRequest defined earlier — this
// block was a duplicate inserted by the slash-command agent before it
// noticed the Stop-generation agent had already shipped them. Kept the
// earlier impls because they include the Cyrillic status update.)

// (OnSaveTimer / OnNewChatClicked defined earlier — duplicates removed
// during agent merge.)

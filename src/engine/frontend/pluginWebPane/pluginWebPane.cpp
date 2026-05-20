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

#include "3rdparty/md4c/md4c-html.h"
#include "3rdparty/nlohmann/json.hpp"

#include <wx/sizer.h>
#include <wx/button.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/html/htmlwin.h>
#include <wx/log.h>
#include <wx/clipbrd.h>
#include <wx/dataobj.h>
#include <wx/msgdlg.h>
#include <wx/stc/stc.h>

#include <functional>

namespace {

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
                              const wxString& lang, const wxString& code)
{
	wxString html;
	html += wxT("<table width=\"100%\" cellspacing=\"0\" cellpadding=\"6\" border=\"0\">");
	html += wxString::Format(
	    wxT("<tr><td bgcolor=\"#eef0f3\">"
	        "<font size=\"-1\" color=\"#6b7280\">%s</font></td>"
	        "<td align=\"right\" bgcolor=\"#eef0f3\">"
	        "<font size=\"-1\">"
	        "<a href=\"oes-copy:%zu:%zu\">%s</a>&nbsp;&nbsp;"
	        "<a href=\"oes-apply:%zu:%zu\">%s</a>"
	        "</font></td></tr>"),
	    HtmlEscape(lang.IsEmpty() ? wxT("ces") : lang),
	    entryIdx, blockIdx, HtmlEscape(_("Копировать")),
	    entryIdx, blockIdx, HtmlEscape(_("Применить")));
	html += wxT("<tr><td colspan=\"2\" bgcolor=\"#f8f9fa\"><pre>");
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
	case R::User:      return _("Вы");
	case R::Assistant: return _("Ассистент");
	case R::System:    return _("Система");
	case R::Error:     return _("Ошибка");
	case R::Plan:      return _("План");
	}
	return wxEmptyString;
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
	m_newChatBtn = new wxButton(this, wxID_ANY, _("Новый чат"));
	btnRow->Add(m_newChatBtn, 0, wxALL, FromDIP(4));

	m_sendBtn = new wxButton(this, wxID_ANY, _("Отправить"));
	btnRow->Add(m_sendBtn, 0, wxALL, FromDIP(4));
	root->Add(btnRow, 0, wxEXPAND);

	SetSizer(root);

	m_sendBtn   ->Bind(wxEVT_BUTTON, &ibPluginWebPane::OnSendClicked,    this);
	m_newChatBtn->Bind(wxEVT_BUTTON, &ibPluginWebPane::OnNewChatClicked, this);
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
		if      (op == "explain") opLabel = _("Объясни следующий код");
		else if (op == "review")  opLabel = _("Проверь следующий код");
		else if (op == "fix")     opLabel = _("Исправь следующий код");
		else if (op == "doc")     opLabel = _("Сгенерируй документирующий комментарий для");
		else if (op == "send")    opLabel = _("Обсудим этот код");
		else                       opLabel = _("Обработай код") + wxString(wxT(" (")) +
		                                       wxString::FromUTF8(op.c_str()) +
		                                       wxString(wxT(")"));
		const wxString lang = wxString::FromUTF8(language.c_str());
		const wxString src  = wxString::FromUTF8(code.c_str());

		Entry e;
		e.role     = Entry::Role::User;
		e.markdown = opLabel + wxT(":\n\n```") + lang + wxT("\n") + src + wxT("\n```");
		AppendEntry(std::move(e));

		// Re-emit as chat.send so the plugin can route through its
		// normal LLM path.
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
		RenderTranscript();
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
	// First chunk of a turn — create the assistant entry.
	if (!m_pending || m_pendingRequestId != requestId) {
		Entry e;
		e.role      = Entry::Role::Assistant;
		e.markdown  = delta;
		e.requestId = requestId;
		m_entries.push_back(std::move(e));
		m_pending          = true;
		m_pendingRequestId = requestId;
		UpdateSendButtonLabel();
	} else {
		// Append to existing streaming entry.
		if (!m_entries.empty()) {
			m_entries.back().markdown += delta;
		}
	}
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

	doc += wxT("<html><body bgcolor=\"#ffffff\" text=\"#1f2937\">");
	doc += wxT("<table width=\"100%\" cellspacing=\"0\" cellpadding=\"8\" border=\"0\">");

	if (m_entries.empty()) {
		doc += wxT("<tr><td><font color=\"#6b7280\" size=\"-1\">");
		doc += HtmlEscape(_("Введите запрос или используйте контекстное меню в редакторе."));
		doc += wxT("</font></td></tr>");
	}

	for (const auto& e : m_entries) {
		// Pick a row background that distinguishes the role without
		// veering into the playful side. User rows = subtle blue gray;
		// assistant rows = white; system/error rows = warning tints.
		const char* bg = "#ffffff";
		const char* roleColor = "#374151";
		switch (e.role) {
		case Entry::Role::User:      bg = "#f3f4f6"; roleColor = "#2563eb"; break;
		case Entry::Role::Assistant: bg = "#ffffff"; roleColor = "#1f2937"; break;
		case Entry::Role::System:    bg = "#f5f5f5"; roleColor = "#6b7280"; break;
		case Entry::Role::Error:     bg = "#fef2f2"; roleColor = "#b91c1c"; break;
		case Entry::Role::Plan:      bg = "#fff7ed"; roleColor = "#9a3412"; break;
		}

		doc += wxString::Format(
		    wxT("<tr><td bgcolor=\"%s\">"),
		    wxString::FromUTF8(bg));

		doc += wxString::Format(
		    wxT("<font size=\"-1\" color=\"%s\"><b>%s</b></font><br>"),
		    wxString::FromUTF8(roleColor),
		    HtmlEscape(RoleLabel(e.role)));

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
				    blocks[bi].second);
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

	wxString localeName;
	if (wxLocale* loc = wxGetLocale()) localeName = loc->GetCanonicalName();
	if (localeName.IsEmpty()) localeName = wxT("uk-UA");
	env["locale"]    = std::string(localeName.utf8_str());

	const std::string payload = env.dump();
	const wxScopedCharBuffer paneIdUtf8 = m_paneId.utf8_str();
	try {
		m_onMessage(paneIdUtf8.data(), payload.c_str(), m_userData);
	} catch (...) {
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
	nlohmann::json env;
	env["kind"]      = "chat.send";
	env["requestId"] = std::string(rid.utf8_str());
	env["text"]      = std::string(outboundPrompt.utf8_str());
	env["prompt"]    = std::string(outboundPrompt.utf8_str()); // v3-shim fallback
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

	const wxScopedCharBuffer paneIdUtf8 = m_paneId.utf8_str();
	try {
		m_onMessage(paneIdUtf8.data(), payload.c_str(), m_userData);
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

	const wxScopedCharBuffer paneIdUtf8 = m_paneId.utf8_str();
	try {
		m_onMessage(paneIdUtf8.data(), payload.c_str(), m_userData);
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
	// oes-apply:<entryIdx>:<blockIdx> — insert / replace in the editor.
	if (href.StartsWith(wxT("oes-apply:"))) {
		HandleApplyLink(href.Mid(wxString(wxT("oes-apply:")).length()));
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

	nlohmann::json env;
	env["kind"]           = (action == wxT("approve")) ? "agent.approve"
	                       : (action == wxT("reject"))  ? "agent.reject"
	                                                     : "agent.undo";
	env["planId"]         = std::string(planId.utf8_str());
	env["conversationId"] = std::string(convId.utf8_str());
	const std::string payload = env.dump();

	const wxScopedCharBuffer paneIdUtf8 = m_paneId.utf8_str();
	try {
		m_onMessage(paneIdUtf8.data(), payload.c_str(), m_userData);
	} catch (...) {
		// Plugin bug — keep host alive.
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

// (UpdateSendButtonLabel / DispatchCancelRequest defined earlier — this
// block was a duplicate inserted by the slash-command agent before it
// noticed the Stop-generation agent had already shipped them. Kept the
// earlier impls because they include the Cyrillic status update.)

// (OnSaveTimer / OnNewChatClicked defined earlier — duplicates removed
// during agent merge.)

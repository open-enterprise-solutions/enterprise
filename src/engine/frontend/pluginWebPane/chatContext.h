/////////////////////////////////////////////////////////////////////////////
// ibChatContext — @<metadata> token resolver for ibPluginWebPane.
//
// Mirrors 1С:Workmate's @Catalog2 / @selection / @file flow:
//   - User types "@" → popup lists matching metadata objects
//   - Selected row inserts "@Kind.Name" into the input box
//   - On submit, every "@<token>" in the prompt is resolved server-side
//     (via metaBridge::HostMetaQuery for metadata tokens, or via the live
//     editor for "@selection" / "@file" / "@open") and prepended to the
//     outgoing prompt as a "=== Context ===" block.
//
// Token grammar:
//   "@selection"          → focused wxStyledTextCtrl's GetSelectedText()
//   "@file"               → focused wxStyledTextCtrl's GetText()
//   "@open"               → list of open editor tab titles
//   "@<Kind>.<Name>"      → metadata object (e.g. @Catalog.Counterparties)
//   "@<Name>"             → metadata object resolved by name across all kinds
//
// Token terminator: any whitespace, end-of-string, or a second '@'.
// Token characters: ASCII letters / digits / '.' / '_' — wide enough to
// catch "Catalog.Counterparties" but narrow enough that ordinary punctuation
// in a Russian sentence breaks the token cleanly.
/////////////////////////////////////////////////////////////////////////////

#ifndef _IB_CHAT_CONTEXT_H_
#define _IB_CHAT_CONTEXT_H_

#include <wx/string.h>

#include <utility>
#include <vector>

class wxWindow;

namespace ibChatContext {

// One candidate row in the popup. `kindLabel` is the Russian
// human-readable label ("Справочник", "Документ", …) used as the column
// prefix. `fullName` is the canonical insertion text minus the leading
// '@' ("Catalog.Counterparties").
struct Suggestion {
	wxString kindLabel;
	wxString fullName;
};

// Walk the active configuration's metaobject tree and return every
// top-level business object that starts with `prefix` (case-insensitive,
// matched against the object's name or "Kind.Name" form). When `prefix`
// is empty, returns every top-level object up to `cap`.
//
// Includes synthetic entries for "@selection", "@file", "@open" when
// they start with the prefix — keeps the popup uniform across special
// and metadata tokens.
//
// Cap is a hard limit on returned rows; the UI never needs more than
// ~50 entries on screen.
std::vector<Suggestion> CollectSuggestions(const wxString& prefix,
                                           size_t cap = 50);

// Find all "@token" tokens in `prompt`. Returns ordered list of unique
// tokens (without the leading '@'). Order-preserving deduplication keeps
// the resulting context block predictable for the LLM.
std::vector<wxString> ExtractTokens(const wxString& prompt);

// Resolve one metadata token to a rendered context block. Calls
// metaBridge::HostMetaQuery under the hood. Returns wxEmptyString on
// failure (unknown token) — caller decides whether to surface that to
// the user or silently drop the unresolved token.
wxString RenderMetadataBlock(const wxString& token);

// Resolve "@selection" / "@file" / "@open" against the focused editor
// rooted at `searchRoot`. Returns wxEmptyString when no editor is in
// scope or the special token does not match.
wxString RenderEditorBlock(const wxString& specialToken,
                            wxWindow*       searchRoot);

// Build the full context preamble for a prompt. Resolves every token
// found by ExtractTokens, concatenates the rendered blocks, and wraps
// them in === Context === markers. Returns wxEmptyString when no tokens
// resolved (so the caller can keep the prompt unchanged).
wxString BuildContextBlock(const wxString& prompt, wxWindow* searchRoot);

} // namespace ibChatContext

#endif // _IB_CHAT_CONTEXT_H_

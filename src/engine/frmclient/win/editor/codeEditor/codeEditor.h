#ifndef __IB_CODE_EDITOR_H__
#define __IB_CODE_EDITOR_H__

// THE DESKTOP'S SCRIPT EDITOR (frontend/win/editor/codeEditor/codeEditor.h), copied whole: the lexer, the colouring,
// the folds, the indent, the braces, the literals and their constructors are the client's. What the desktop asked the
// configuration — the module's text, the names at the caret, a call's form, the syntax — is asked of the server's tab
// (the designer's module tab, frmserver ibModuleTextView): its document is that tab's.

#include "frmclient/frmclient.h"

#include <wx/docview.h>
#include <wx/cmdproc.h>
#include <wx/listctrl.h>
#include <wx/stc/stc.h>

#include <vector>
#include <array>
#include <map>
#include <memory>

#include "core/compiler/translateCode.h"   // the lexem stream, and what the caret stands in
#include "core/compiler/scriptFoldCode.h"  // ibFoldCode — where the text folds, the core's one copy
#include "core/compiler/codeDef.h"   // CODE_VES / CODE_CES — the syntax the stream reads in, for fold-mode dispatch

#include "protocol/protocol.h"   // ibProtocol::ibView::ibModule::ibRequest — what the module's tab is asked
#include "protocol/protocolNode.h"

#include "frmclient/mainFrame/settings/editorsettings.h"
#include "frmclient/mainFrame/settings/fontcolorsettings.h"

#include "frmclient/win/editor/codeEditor/components/autoComplete.h"
#include "frmclient/win/editor/codeEditor/components/callTip.h"

class ibTabDocument;
class ibPropertyObject;
class ibReaderMemory;

// Forwards wxCommandProcessor's undo/redo stack to the underlying
// wxStyledTextCtrl — STC owns its own undo buffer, but ibDocument
// expects a wxCommandProcessor on the document side.
class ibModuleCommandProcessor : public wxCommandProcessor {
	wxStyledTextCtrl* m_codeEditor = nullptr;
public:
	explicit ibModuleCommandProcessor(wxStyledTextCtrl* codeEditor)
		: m_codeEditor(codeEditor) {}

	bool Undo() override        { m_codeEditor->Undo(); return true; }
	bool Redo() override        { m_codeEditor->Redo(); return true; }
	bool CanUndo() const override { return m_codeEditor->CanUndo(); }
	bool CanRedo() const override { return m_codeEditor->CanRedo(); }
};

///////////////////////////////////////////////////////////////////////////

#define wxSTC_FOLDLEVELBASE_FLAG 0x400

#define wxSTC_FOLDLEVELWHITE_FLAG 0x1000
#define wxSTC_FOLDLEVELHEADER_FLAG 0x2000
#define wxSTC_FOLDLEVELELSE_FLAG 0x4000

// The core spells the fold levels without Scintilla (core/compiler/scriptFoldCode.h): the same numbers.
static_assert(ibFoldCode::FoldLevelBase == wxSTC_FOLDLEVELBASE, "the core's fold base is Scintilla's");
static_assert(ibFoldCode::FoldLevelWhite == wxSTC_FOLDLEVELWHITEFLAG, "the core's closer flag is Scintilla's");
static_assert(ibFoldCode::FoldLevelHeader == wxSTC_FOLDLEVELHEADERFLAG, "the core's opener flag is Scintilla's");
static_assert(ibFoldCode::FoldLevelElse == wxSTC_FOLDLEVELELSE_FLAG, "the core's else mark is the editor's");

///////////////////////////////////////////////////////////////////////////

class FRMCLIENT_API ibCodeEditor : public wxStyledTextCtrl {
	wxDECLARE_CLASS(ibCodeEditor);   // wxDynamicCast(focus, ibCodeEditor) — else any text control passes as one

protected:
	// Marker IDs — protected so designer's ibCodeEditorDesigner subclass
	// (which manages breakpoint markers via debugClient) can reach them.
	//
	// ⚠ THE NUMBER IS THE DRAWING ORDER: Scintilla draws a higher-numbered marker over a lower one. The dots come
	// first so the arrows land on top of them - a conditional dot numbered after the arrows hid the arrow of a
	// run stopped on it.
	enum {
		Breakpoint = 1,
		ConditionalBreakpoint,   // a breakpoint that stops only when its condition is true - the same dot, another colour
		CurrentLine,
		BreakLine,
	};

public:

	// Resolve identifier at the caret (or current selection if non-empty).
	// Used by syntax-helper Look-Up flow — host frame asks the focused
	// editor for the identifier under the cursor and feeds it to
	// ibHelpResolver. Returns wxEmptyString if the caret is not on a
	// word boundary.
	wxString GetIdentifierUnderCursor();

	// THE STRING LITERAL THE CARET SITS IN, as a span of the document. A query written in a module
	// lives in a literal — usually a multi-line one, quoted and with `|` continuation markers — so
	// anything that wants to work on that query has three jobs before it can start: find the
	// literal's bounds, take the TEXT out of its spelling, and put it back the same way.
	//
	// These three are that, and they are here rather than in the constructor because they are facts
	// about the SCRIPT's spelling of a string, which is the editor's subject, not the query
	// language's. Found() is false when the caret is not inside a literal — the same shape
	// GetIdentifierUnderCursor has, so the menu item beside it gates the same way.
	struct StringLiteralSpan
	{
		int      m_start = -1;   // document position of the opening quote
		int      m_end   = -1;   // document position just past the closing quote
		wxString m_text;         // the string's VALUE — quotes stripped, "" unescaped, `|` markers removed
		bool     Found() const { return m_start >= 0 && m_end > m_start; }
	};
	// NOT const: reading the document and the caret goes through wxStyledTextCtrl's own non-const
	// accessors, and casting that away to decorate this one with `const` would be lying about the
	// object to satisfy a keyword.
	StringLiteralSpan GetStringLiteralUnderCursor();

	// Write `text` back into `span`, spelled as the script spells a multi-line string: quoted,
	// inner quotes doubled, every line after the first opened with `|` and indented to the opening
	// quote. Replaces exactly that literal and nothing around it.
	void ReplaceStringLiteral(const StringLiteralSpan& span, const wxString& text);

	// The same spelling, written at `position` where there was no literal at all — so a tool that
	// AUTHORS a query can be reached from anywhere in a module, not only from inside an existing
	// string. Without this the constructor would only ever be able to edit a query somebody had
	// already typed by hand, which is the wrong way round.
	void InsertStringLiteral(int position, const wxString& text);

	// How the script spells `text` as one string literal, indented under `indent`. Shared by the
	// two above so the quoting rule lives in one place.
	static wxString SpellStringLiteral(const wxString& text, const wxString& indent);

	// Custom right-click menu: standard Cut/Copy/Paste/SelectAll +
	// "Look up in Syntax Helper". The Look-Up item posts
	// wxID_FRONTEND_SYNTAX_HELPER_LOOKUP up the parent chain so the
	// host frame handles it (subphase 1.3b).
	void OnContextMenu(wxContextMenuEvent& event);

	// Build a `Procedure name(args) ... End`/`{...}` template suitable for
	// inserting at a free spot in a module — CES uses brace-fenced bodies,
	// VES uses keyword fences. Mode is the editor's — the syntax its module
	// is written in (SetCodeStyle). Used by the visual editor's "create
	// handler from event" path and the procedures-and-functions dialog.
	wxString MakeProcedureTemplate(const wxString& name,
	                               const wxString& args) const;

	ibCodeEditor();
	// `document` — the tab of the module it edits (the server's module view): what it loads, hands back and asks of.
	// None — a text of the window's own (a composer's expression), in the syntax the editor starts with.
	ibCodeEditor(ibTabDocument* document, wxWindow* parent, wxWindowID id = wxID_ANY,
		const wxPoint& pos = wxDefaultPosition,
		const wxSize& size = wxDefaultSize, long style = 0,
		const wxString& strName = wxSTCNameStr);
	virtual ~ibCodeEditor();

	// hook the document manager into event handling chain here
	// ⚠ Out of line, unlike the desktop's: frmclient hides every symbol it does not export, and an editor derived outside
	// it (designer-thin's ibCodeEditorDesigner) emitted this body and could not reach ibAutoComplete / ibCallTip (CI's
	// macOS link, 2026-10-15).
	virtual bool TryBefore(wxEvent& event) override;

	// The module's text, fetched from its tab — and handed back to it ({Change {Text}}) when it differs from what the
	// server holds.
	bool LoadModule();
	// …from an answer already fetched — a form's tab opens its form and its module in one (Text).
	bool LoadModule(const ibProtocolNode& answer);
	bool SaveModule();

	// THE SYNTAX THE MODULE IS WRITTEN IN — the desktop's editor read it of the process (ibCompileCode::GetCodeStyle); here
	// it is the tab's (View.State.Syntax): the text is read again in it and coloured anew.
	void SetCodeStyle(short codeStyle);
	short GetCodeStyle() const { return m_tc.GetCodeStyle(); }

	int GetRealPosition();
	int GetRealPositionFromPoint(const wxPoint& pt);

	void RefreshEditor();
	void ActivateEditor();

	void FindText(const wxString& findString, int wxflags);

	void ShowGotoLine();
	void ShowMethods();

	// Re-indent the lines covered by the current selection (or the
	// caret line if nothing selected) using the same fold-mask logic
	// as PrepareTABs. Wrapped in BeginUndoAction / EndUndoAction so
	// the user can undo the whole reformat with one Ctrl+Z.
	void FormatSelection();

	// Selection-aware indent / unindent — mirror of Tab / Shift+Tab
	// keystroke behaviour, exposed as a menu / toolbar command for
	// users who don't recall the shortcut.
	void IncreaseIndent();
	void DecreaseIndent();

	// Prepend "//" to every line in the current selection (or the
	// caret line if nothing is selected). Wrapped in BeginUndoAction.
	void AddCommentsToSelection();

	// Strip the leading "//" comment prefix from every line in the
	// current selection (or the caret line). Lines without a comment
	// prefix are left alone.
	void RemoveCommentsFromSelection();

	void SetCurrentLine(int line, bool setLine = true);
	// The module checked where it compiles — the text handed back first, the command to its tab; what the check says
	// comes as the server's messages.
	void SyntaxControl();

	//Update breakpoints 
	void RefreshBreakpoint(bool deleteCurrentBreakline = false);

	//Editor setting 
	void SetEditorSettings(const ibEditorSettings& settings);

	//Font setting
	void SetFontColorSettings(const ibFontColorSettings& settings);

	// ⭐ THE VALUE THE RUNTIME ANSWERED WITH, for the word the mouse is on — the one door that puts it
	// up, so the editor also KEEPS it: the system pops a tooltip on its own schedule and shows whatever
	// text is in place then, so the same answer is laid back down on every movement across that word
	// (LoadToolTip). An empty value takes the hint down, which is how a step clears what it made stale.
	// Not wxWindow::SetToolTip, which is not virtual and says nothing about where the text came from.
	void SetDebugValue(const wxString& value);

	// ⚠ THE EDITOR'S OWN POSITION, NOT THE COMPILER'S. GetRealPosition counts CHARACTERS, which is
	// what a caret means to the compiler; a window is placed by wxSTC's document position, which is
	// a byte offset. Any non-ASCII above the caret pulls the two apart, and the tip is drawn that
	// much too early — measured 2026-09-07 on a module whose messages are in Russian, where it
	// landed four lines above the call it described. This is the SECOND caller that had it; the
	// other is LoadCallTip.
	void ShowCallTip(const wxString& title) { m_ct.Show(GetCurrentPos(), title); }

protected:

	// Events
	void OnStyleNeeded(wxStyledTextEvent& event);
	void OnMarginClick(wxStyledTextEvent& event);
	void OnTextChange(wxStyledTextEvent& event);

	void OnKeyDown(wxKeyEvent& event);
	void OnCharAdded(wxStyledTextEvent& event);
	void OnUpdateUI(wxStyledTextEvent& event);
	void OnMouseMove(wxMouseEvent& event);

	// ---- Debugger integration hooks ----
	// Frontend's ibCodeEditor doesn't know about backend's debugClient;
	// designer's ibCodeDesigner subclass overrides these to forward to
	// debugClient. codeRunner / other sessionless hosts get the default
	// no-op behaviour so the editor still works without any debug
	// infrastructure attached.

	// True if the debugger is parked at a breakpoint right now.
	// Autocomplete / tooltip switch to live-eval against the running
	// script when this returns true.
	virtual bool IsDebuggerEnterLoop() const { return false; }

	// Toggle (or remove) the breakpoint marker at `line`. Override
	// performs the marker update + debugClient round-trip.
	virtual void OnEditDebugPoint(int line) {}

	// Multi-line edit (paste / cut) just shifted code by linesAdded
	// at `line`. `atLineStart` = the edit happened at column 0 of `line`
	// (the whole line moved), which decides whether a breakpoint sitting
	// ON `line` shifts too. Override forwards the shift to debugClient->PatchModule.
	virtual void OnPatchModule(int line, int linesAdded, bool atLineStart) {}

	// The tooltip handler wants the debugger to evaluate an expression against the running session — the module is
	// the subclass's to name (its document's), the editor knows only its tab.
	virtual void OnEvaluateToolTip(const wxString& expression) {}

	// Repopulate breakpoint markers from the debugger's persistent
	// store. Called from RefreshBreakpoint after MarkerDeleteAll.
	virtual void RefreshBreakpointMarkers() {}

private:

	void PrepareTooTipExpression(unsigned int currPos, wxString& expression, wxString& currentWord, bool& hasPoint);

	void PrepareTABs();

	// ⭐ WHAT THE DESKTOP'S EDITOR ASKED THE CONFIGURATION — the names at the caret, a call's form — asked of the tab, with
	// the text as it is now and the caret in it (characters); false — no tab, or refused.
	bool FetchAtCaret(ibProtocol::ibView::ibModule::ibRequest kind, ibProtocolNode& answer);

	void LoadAutoComplete();
	void LoadToolTip(const wxPoint& pos);
	void LoadCallTip();

	//Support styling
	void HighlightSyntaxAndCalculateFoldLevel(const int fromPos, const int toPos);
	// The fold manager's levels handed to the control when the text changed (RecalcFoldLevel) — on styling.
	void UpdateFoldLevel();

	//Support debugger
	void EditDebugPoint(int line);

protected:
	// Subclasses (designer's ibCodeEditorDesigner) reach the document
	// + breakpoint marker enum to wire debug-client interactions.
	ibTabDocument*    m_document         = nullptr;

private:
	// The text as the server holds it — loaded, or last handed back: what is not different is not sent.
	wxString          m_moduleText;
	// What the inspector shows as it is activated — the property shown as it was made, held (ActivateEditor).
	std::shared_ptr<ibPropertyObject> m_property;

	ibAutoComplete    m_ac;
	ibCallTip         m_ct;

	// ⭐⭐ THE EDITOR KEEPS A LEXER, NOT A COMPILER — and it already had one. What it needs of the
	// text is the token stream: for syntax colouring, for folding, for brace matching, and to know
	// what the caret is standing in. Everything the old ibPrecompileCode did BEYOND that — a second
	// scope tree, a second set of variables, values computed by a second walker over a fake
	// context — is answered by the compiler now, through one door each: ibValueAtCaret for what an
	// expression arrived at, ibNamesAtCaret for what may be written.
	ibTranslateCode   m_tc;
	ibFoldCode        m_fp;

	bool m_initialized        = false;
	int  m_indentationSize    = 0;
	bool m_enableAutoComplete = false;

	int  m_lineBreakpoint     = wxNOT_FOUND;

	// The hint the breakpoint margin put up (OnMouseMove) - taken down when the mouse leaves the margin,
	// so it does not stand over the text. Empty when the margin has none up.
	wxString m_marginHint;

	// The expression the debugger was last asked the value of (LoadToolTip) - so travelling across one
	// word does not ask again and again. Empty when nothing has been asked.
	wxString m_askedExpression;

	// …and the answer, kept because the tooltip has to be laid down again on every movement for the
	// system to find it there when it decides to show one. Empty until the answer arrives.
	wxString m_askedValue;

protected:
	// The context menu's debugger part, for the line it was opened on - the designer adds the
	// breakpoint condition there; a host with no debugger adds nothing. Last in the class: a virtual
	// added between others moves every one after it in the table, and an object built before reads
	// the wrong slot.
	virtual void AppendDebugMenu(wxMenu& /*menu*/, int /*line*/) {}

	// What the breakpoint margin says about the line under the mouse - the designer answers with the
	// breakpoint's condition. False: nothing to say, `hint` untouched.
	virtual bool GetDebugPointHint(int /*line*/, wxString& /*hint*/) { return false; }
};

#endif 
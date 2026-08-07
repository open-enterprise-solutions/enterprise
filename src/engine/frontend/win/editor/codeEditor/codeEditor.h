#ifndef __IB_CODE_EDITOR_H__
#define __IB_CODE_EDITOR_H__

#include "frontend/frontend.h"

#include <wx/docview.h>
#include <wx/cmdproc.h>
#include <wx/listctrl.h>
#include <wx/stc/stc.h>

#include <vector>
#include <array>
#include <map>

#include "codeEditorInterpreter.h"

#include "frontend/mainFrame/settings/editorsettings.h"
#include "frontend/mainFrame/settings/fontcolorsettings.h"

#include "backend/compiler/compileCode.h"   // CODE_VES / CODE_CES + GetCodeStyle for fold-mode dispatch
#include "backend/debugger/debugDefs.h"

#include "frontend/win/editor/codeEditor/components/autoComplete.h"
#include "frontend/win/editor/codeEditor/components/callTip.h"

class ibMetaDocument;
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

///////////////////////////////////////////////////////////////////////////

class FRONTEND_API ibCodeEditor : public wxStyledTextCtrl {

protected:
	// Marker IDs — protected so designer's ibCodeEditorDesigner subclass
	// (which manages breakpoint markers via debugClient) can reach them.
	enum {
		Breakpoint = 1,
		CurrentLine,
		BreakLine,
	};

private:

	class ibFoldLevelParser {

		enum FoldKind : short {
			FoldProcedure = 0,
			FoldFunction,
			FoldIf,
			FoldDo,
			FoldTry,
			FoldBrace,         // CES `{ ... }` block — opens at `{`, closes at `}`
			FoldLinq,          // block-syntax LINQ — opens at `from`, closes at
			                   // the terminal `select` / `group` / `distinct`.
			                   // Per-kind counter handles unbalanced opens
			                   // (e.g. multi-from with single select); trailing
			                   // unclosed opens fall under UpdateFoldLevel's
			                   // hint_ignore_fold trim same as in-progress
			                   // keyword folds elsewhere.
			FoldKindCount
		};

		struct FoldPoint {
			int   line;   // source line m_number
			short delta;  // +1 open, 0 mark (Else/ElseIf/Except), -1 close
		};

	public:

		ibFoldLevelParser(ibCodeEditor* codeEditor)
			: m_update_fold(false), m_counts{}, m_codeEditor(codeEditor) {}

		void RecalcFoldLevel() const { m_update_fold = true; }

		void UpdateFoldLevel() {

			if (!m_update_fold)
				return;

			CalcFoldLevel();

			// Detect a tail of opens that never close (incomplete code while
			// the user is still typing) and skip them so half-finished folds
			// don't drag the whole tail of the file into a phantom region.
			auto hint_ignore_fold = m_folding_vector.end();
			if (!m_folding_vector.empty()) {
				int unclosed = 0;
				for (const auto& v : m_folding_vector) unclosed += v.delta;
				while (hint_ignore_fold != m_folding_vector.begin()) {
					if (unclosed == 0) break;
					hint_ignore_fold = std::prev(hint_ignore_fold);
					if (hint_ignore_fold->delta > 0) unclosed--;
				}
			}

			int level = 0, fold_start = 0;

			for (auto it = m_folding_vector.begin(); it != m_folding_vector.end(); ++it) {

				if (hint_ignore_fold != m_folding_vector.end() && it >= hint_ignore_fold && it->delta > 0)
					continue;

				for (int l = fold_start + 1; l <= it->line - 1; l++)
					m_codeEditor->SetFoldLevel(l, wxSTC_FOLDLEVELBASE_FLAG + std::max(level, 0));

				if (it->delta > 0)
					m_codeEditor->SetFoldLevel(it->line, (wxSTC_FOLDLEVELBASE_FLAG + std::max(level, 0)) | wxSTC_FOLDLEVELHEADER_FLAG);
				else if (it->delta < 0)
					m_codeEditor->SetFoldLevel(it->line, (wxSTC_FOLDLEVELBASE_FLAG + std::max(level, 0)) | wxSTC_FOLDLEVELWHITE_FLAG);
				else
					m_codeEditor->SetFoldLevel(it->line, wxSTC_FOLDLEVELBASE_FLAG + std::max(level, 0));

				level += it->delta;
				fold_start = it->line;
			}

			for (int l = fold_start + 1; l <= m_codeEditor->GetLineCount(); l++)
				m_codeEditor->SetFoldLevel(l, wxSTC_FOLDLEVELBASE_FLAG + std::max(level, 0));

			m_update_fold = false;
		}

		int GetFoldMask(int line) const {
			// Accumulate ALL deltas up to and including `line` for the
			// cumulative indent level, AND the per-line net for the flag.
			// The previous version had a `prev_line != v.line` guard around
			// `level += v.delta` which dropped every foldpoint past the
			// first one on a given line — so a single-line `from..select`
			// or `{ stmt; }` pair (Open +1, Close -1) contributed only +1
			// to the running level. Across a large file with many such
			// pairs the level drifted upward monotonically and indent ran
			// away. Flag is decided by the NET of deltas on the queried
			// line: net>0 = HEADER (opener), net<0 = WHITE (closer),
			// net==0 with a mark (Else/ElseIf/Except) = ELSE, else BASE.
			int level = 0;
			int netHere = 0;
			bool hasMark = false;
			for (const auto& v : m_folding_vector) {

				if (v.line > line)
					break;

				level += v.delta;

				if (v.line == line) {
					netHere += v.delta;
					if (v.delta == 0)
						hasMark = true;
				}
			}

			if (level < 0) level = 0;

			if (netHere > 0) {
				// Header line sits at the outer (parent) indent; PrepareTABs
				// adds +1 for the body that follows the header. `level`
				// already includes this line's opens, so subtract 1 to
				// match the existing decoder.
				return (wxSTC_FOLDLEVELBASE_FLAG + (level - 1)) | wxSTC_FOLDLEVELHEADER_FLAG;
			}
			if (netHere < 0) {
				// Closer line: PrepareTABs WHITE branch subtracts 1 to land
				// at parent indent. `level` already retreated past the
				// closes — add 1 so the decoder lands at the right spot.
				return (wxSTC_FOLDLEVELBASE_FLAG + (level + 1)) | wxSTC_FOLDLEVELWHITE_FLAG;
			}
			if (hasMark) {
				// Else/ElseIf/Except marker on a net-zero line. PrepareTABs
				// ELSE branch subtracts 1 to align with the parent.
				return (wxSTC_FOLDLEVELBASE_FLAG + level) | wxSTC_FOLDLEVELELSE_FLAG;
			}
			// Plain line OR balanced inline pair (Open + Close on the same
			// source line, e.g. one-liner `from x in arr select x` or
			// `{ stmt; }`). Net effect is zero, line sits at the surrounding
			// scope's indent — no fold marker.
			return wxSTC_FOLDLEVELBASE_FLAG + level;
		}

	private:

		void OpenFold(const int line, FoldKind kind) {
			m_counts[kind]++;
			m_folding_vector.push_back({ line, +1 });
		}

		void MarkFold(const int line) {
			m_folding_vector.push_back({ line, 0 });
		}

		void CloseFold(const int line, FoldKind kind) {
			if (m_counts[kind] > 0) {
				m_folding_vector.push_back({ line, -1 });
				m_counts[kind]--;
			}
		}

		void ClearFoldLevel() {
			m_counts.fill(0);
			m_folding_vector.clear();
		}

		// Map a script keyword to a fold-kind. Returns FoldKindCount if the
		// keyword does not open/close a fold region.
		static FoldKind OpenKindFor(short numData) {
			switch (numData) {
				case KEY_PROCEDURE: return FoldProcedure;
				case KEY_FUNCTION:  return FoldFunction;
				case KEY_IF:        return FoldIf;
				case KEY_FOR:
				case KEY_FOREACH:
				case KEY_WHILE:     return FoldDo;
				case KEY_TRY:       return FoldTry;
				case KEY_FROM:      return FoldLinq;
				default:            return FoldKindCount;
			}
		}

		static FoldKind CloseKindFor(short numData) {
			switch (numData) {
				case KEY_ENDPROCEDURE: return FoldProcedure;
				case KEY_ENDFUNCTION:  return FoldFunction;
				case KEY_ENDIF:        return FoldIf;
				case KEY_ENDDO:        return FoldDo;
				case KEY_ENDTRY:       return FoldTry;
				// LINQ block ends at the terminal projection — `select` (most
				// common). `group` MAY close (terminal `group X by K`), but
				// MUST NOT close on `group X by K into g <continuation>` —
				// `group` is followed by more clauses there. Two-keyword
				// peek for KEY_INTO happens in CalcFoldLevel below so the
				// classification depends on the next lex; this helper only
				// answers the "no peek needed" case. `distinct` is a
				// modifier on select, not a standalone terminator.
				// Multi-from queries open multiple times but emit a single
				// terminal — the per-kind counter (Open vs Close) absorbs
				// the imbalance: the trailing-unclosed-opens trim in
				// UpdateFoldLevel keeps the visible fold consistent.
				case KEY_SELECT:       return FoldLinq;
				default:               return FoldKindCount;
			}
		}

		static bool IsMarkKeyword(short numData) {
			return numData == KEY_ELSE
				|| numData == KEY_ELSEIF
				|| numData == KEY_EXCEPT;
		}

		void CalcFoldLevel() {

			ClearFoldLevel();

			// Fold-driver split by syntax mode:
			//   * VES — keyword-only. Procedure / Function / If / While
			//     / For / Try open; End* close. `{ }` braces are not
			//     legal in VES, so no brace tracking.
			//   * CES — brace-only for control-flow blocks. KEY_*
			//     open keywords (Procedure / Function / If / ...) have
			//     no matching close in CES — `End*` is filtered out
			//     by IsAllowedKey — so opening them stacks +1 on every
			//     body line and the Allman-style body ends up at the
			//     wrong indent. Only `{` / `}` drive fold structure;
			//     keywords are pure syntactic markers, no fold. The
			//     LINQ surface (`from .. select / group`) DOES still
			//     fold via keywords in both modes (its closers stay
			//     keywords in CES).
			// Mode is read from ibCompileCode (process-global).
			const bool isCES = (ibCompileCode::GetCodeStyle() == CODE_CES);

			const auto& lexems = m_codeEditor->m_precompileModule->GetLexems();
			for (size_t i = 0; i < lexems.size(); ++i) {
				const ibLexem& lex = lexems[i];

				if (isCES && lex.m_lexType == DELIMITER) {
					if (lex.m_numData == '{')
						OpenFold(lex.m_numLine, FoldBrace);
					else if (lex.m_numData == '}')
						CloseFold(lex.m_numLine, FoldBrace);
					continue;
				}

				if (lex.m_lexType != KEYWORD)
					continue;

				// CES: `Procedure / Function / If / While / For / Try`
				// would each open a keyword fold with no matching close
				// — `End*` keywords are filtered out by IsAllowedKey in
				// CES (`}` is the only legal closer). The orphan open
				// stacks +1 on every body line, so the Allman-style
				// `Function foo()\n{\n body \n}` body ends up at indent
				// 2 instead of 1. Braces alone drive CES folding; KEY_*
				// open keywords are pure syntactic markers, not fold
				// regions. LINQ's KEY_FROM stays — it pairs with
				// KEY_SELECT / KEY_GROUP which are still keywords in CES.
				if (isCES) {
					const bool isLinqOpenOrClose =
						lex.m_numData == KEY_FROM ||
						lex.m_numData == KEY_SELECT ||
						lex.m_numData == KEY_GROUP;
					if (!isLinqOpenOrClose) {
						if (IsMarkKeyword(lex.m_numData))
							MarkFold(lex.m_numLine);
						continue;
					}
				}

				if (FoldKind k = OpenKindFor(lex.m_numData); k != FoldKindCount) {
					// Multi-from LINQ: `from a in X from b in Y select Z` is
					// ONE logical query — one Open at the first `from`, one
					// Close at the `select`. Subsequent `from`s while a Linq
					// query is still open (count[Linq] > 0) are clause
					// continuations, not new fold regions. Without this the
					// parser emitted +N opens but only -1 close, leaving
					// N-1 unbalanced opens dangling for the rest of the
					// file — every line after a multi-from / nested-from
					// then sat at level >= 1 in GetFoldMask, and
					// FormatSelection re-indented subsequent code at the
					// wrong depth. Same logic naturally covers nested LINQ
					// in a parenthesised subexpression — inner LINQ shares
					// the outer's fold without stair-stepping.
					if (k == FoldLinq && m_counts[FoldLinq] > 0)
						continue;
					OpenFold(lex.m_numLine, k);
					continue;
				}
				if (FoldKind k = CloseKindFor(lex.m_numData); k != FoldKindCount) {
					CloseFold(lex.m_numLine, k);
					continue;
				}
				// `group` is a conditional LINQ close: terminal
				// `group X by K` closes the fold; non-terminal
				// `group X by K into <g>` does NOT (continuation
				// keeps the fold open until the final select). Peek
				// the rest of this LINQ statement for KEY_INTO; if
				// found before any other LINQ-opener / closer, treat
				// as non-terminal and skip the close.
				if (lex.m_numData == KEY_GROUP) {
					bool terminal = true;
					for (size_t j = i + 1; j < lexems.size(); ++j) {
						const ibLexem& nxt = lexems[j];
						// Stop on statement-terminator or another LINQ
						// open/close — we're past the group's clause.
						if (nxt.m_lexType == DELIMITER &&
							(nxt.m_numData == ';' || nxt.m_numData == '}'
							 || nxt.m_numData == ')'))
							break;
						if (nxt.m_lexType != KEYWORD)
							continue;
						if (nxt.m_numData == KEY_INTO) { terminal = false; break; }
						if (nxt.m_numData == KEY_FROM ||
							nxt.m_numData == KEY_SELECT)
							break;
					}
					if (terminal)
						CloseFold(lex.m_numLine, FoldLinq);
					continue;
				}
				if (IsMarkKeyword(lex.m_numData))
					MarkFold(lex.m_numLine);
			}
		}

	private:

		// Recalc-pending flag. Set by RecalcFoldLevel (cheap, lazy);
		// consumed by UpdateFoldLevel on the next paint.
		mutable bool m_update_fold;

		// Per-kind open-fold counters — protect against unbalanced
		// close-tokens emitting spurious -1 deltas.
		std::array<short, FoldKindCount> m_counts;

		// Owning editor (non-owning back-pointer).
		ibCodeEditor* m_codeEditor;

		// Sorted-by-line list of fold transitions populated by
		// CalcFoldLevel — consumed by UpdateFoldLevel and GetFoldMask.
		std::vector<FoldPoint> m_folding_vector;
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
	// VES uses keyword fences. Mode is read from ibCompileCode (process-
	// global). Used by the visual editor's "create handler from event"
	// path and the procedures-and-functions dialog.
	static wxString MakeProcedureTemplate(const wxString& name,
	                                       const wxString& args);

	ibCodeEditor();
	ibCodeEditor(ibMetaDocument* document, wxWindow* parent, wxWindowID id = wxID_ANY,
		const wxPoint& pos = wxDefaultPosition,
		const wxSize& size = wxDefaultSize, long style = 0,
		const wxString& strName = wxSTCNameStr);
	virtual ~ibCodeEditor();

	bool LoadModule();
	bool SaveModule();

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
	bool SyntaxControl(bool throwMessage = true) const;

	//Update breakpoints 
	void RefreshBreakpoint(bool deleteCurrentBreakline = false);

	//Editor setting 
	void SetEditorSettings(const ibEditorSettings& settings);

	//Font setting 
	void SetFontColorSettings(const ibFontColorSettings& settings);

	void ShowAutoComplete(
		const ibDebugAutoCompleteData& autoCompleteData);

	void ShowCallTip(const wxString& title) { m_ct.Show(GetRealPosition(), title); }

	// hook the document manager into event handling chain here
	virtual bool TryBefore(wxEvent& event) override {
		wxEventType type = event.GetEventType();
		if (type == wxEVT_PAINT ||
			type == wxEVT_NC_PAINT ||
			type == wxEVT_ERASE_BACKGROUND) {
			return wxStyledTextCtrl::TryBefore(event);
		}
		if (m_ct.Active()) {
			if (type == wxEVT_KEY_DOWN) {
				m_ct.CallEvent(event); return wxStyledTextCtrl::TryBefore(event);
			}
		}

		if (m_ac.CallEvent(event))
			return true;
		if (m_ct.CallEvent(event))
			return true;
		else return wxStyledTextCtrl::TryBefore(event);
	}

protected:

	// Events
	void OnStyleNeeded(wxStyledTextEvent& event);
	void OnMarginClick(wxStyledTextEvent& event);
	void OnTextChange(wxStyledTextEvent& event);

	void OnKeyDown(wxKeyEvent& event);
	void OnCharAdded(wxStyledTextEvent& event);
	void OnUpdateUI(wxStyledTextEvent& event);
	void OnMouseMove(wxMouseEvent& event) {
		LoadToolTip(event.GetPosition());
		event.Skip();
	}

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

	// Autocomplete / tooltip handlers want the debugger to evaluate
	// an expression against the running session.
	virtual void OnEvaluateAutocomplete(const wxString& fileName,
	                                     const wxString& docPath,
	                                     const wxString& expression,
	                                     const wxString& keyword,
	                                     int pos) {}
	virtual void OnEvaluateToolTip(const wxString& fileName,
	                                const wxString& docPath,
	                                const wxString& expression) {}

	// Repopulate breakpoint markers from the debugger's persistent
	// store. Called from RefreshBreakpoint after MarkerDeleteAll.
	virtual void RefreshBreakpointMarkers() {}

private:

	// Private func
	void AddKeywordFromObject(const ibValue& vObject);

	bool PrepareExpression(unsigned int currPos, wxString& expression, wxString& keyword, wxString& currentWord, bool& hasPoint);
	void PrepareTooTipExpression(unsigned int currPos, wxString& expression, wxString& currentWord, bool& hasPoint);

	void PrepareTABs();

	void LoadSysKeyword();
	void LoadIntelliList();
	void LoadFromKeyWord(const wxString& keyWord);

	void LoadAutoComplete();
	void LoadToolTip(const wxPoint& pos);
	void LoadCallTip();

	//Support styling
	void HighlightSyntaxAndCalculateFoldLevel(const int fromPos, const int toPos);

	//Support debugger
	void EditDebugPoint(int line);

protected:
	// Subclasses (designer's ibCodeEditorDesigner) reach the document
	// + breakpoint marker enum to wire debug-client interactions.
	ibMetaDocument*   m_document         = nullptr;

private:
	ibPrecompileCode* m_precompileModule = nullptr;

	ibAutoComplete    m_ac;
	ibCallTip         m_ct;
	ibTranslateCode   m_tc;
	ibFoldLevelParser m_fp;

	bool m_initialized        = false;
	int  m_indentationSize    = 0;
	bool m_enableAutoComplete = false;

	int  m_lineBreakpoint     = wxNOT_FOUND;
};

#endif 
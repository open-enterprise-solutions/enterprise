#ifndef __IB_SCRIPT_FOLD_CODE_H__
#define __IB_SCRIPT_FOLD_CODE_H__

////////////////////////////////////////////////////////////////////////////
// WHERE A MODULE'S TEXT FOLDS — the code editor's fold manager, out of the widget.
////////////////////////////////////////////////////////////////////////////
//
// The desktop's and the thin client's code editors each held this class nested and private, line for line the
// same but for where the syntax was read. It reads only the translator's lexems (ibTranslateCode), so it lives
// in the core beside the translator, and both editors hold the one copy under the same name (m_fp).
//
// ⭐ THE ANSWER IS SCINTILLA'S FOLD LEVEL, as a number: the core has no Scintilla, so the flags are spelled here
// with Scintilla's values (wxSTC_FOLDLEVELBASE / WHITEFLAG / HEADERFLAG, and the editors' ELSE mark), and the
// editor hands the levels to its control (SetFoldLevel) itself.
//
// ⭐ THE SYNTAX IS THE STREAM'S: the folds are read in the syntax the lexems were read in (GetCodeStyle) — the
// session's on the desktop, the tab's in the thin client, as each editor says it before lexing.
//
////////////////////////////////////////////////////////////////////////////

#include "core/core.h"   // CORE_API

#include <array>
#include <vector>

class ibTranslateCode;

class CORE_API ibFoldCode {
public:

	// Scintilla's fold-level encoding — what GetFoldMask answers and the editors' indentation decodes.
	enum : int {
		FoldLevelBase   = 0x400,    // wxSTC_FOLDLEVELBASE_FLAG
		FoldLevelWhite  = 0x1000,   // wxSTC_FOLDLEVELWHITE_FLAG: a closer line
		FoldLevelHeader = 0x2000,   // wxSTC_FOLDLEVELHEADER_FLAG: an opener line
		FoldLevelElse   = 0x4000,   // wxSTC_FOLDLEVELELSE_FLAG: Else / ElseIf / Except on a net-zero line
	};

	ibFoldCode();

	// The text changed — the folds are recomputed on the next UpdateFoldLevel (cheap, lazy).
	void RecalcFoldLevel() const { m_update_fold = true; }

	// Recompute when RecalcFoldLevel asked for it: the levels of lines [1, lineCount] are written into `levels`
	// (index = line; index 0 is not written). False — nothing was pending, `levels` is untouched.
	bool UpdateFoldLevel(const ibTranslateCode& tc, int lineCount, std::vector<int>& levels);

	// The level `line` stands at, with the flag of what it opens or closes — the editors' auto-indent reads it.
	int GetFoldMask(int line) const;

private:

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

	// Map a script keyword to a fold-kind. Returns FoldKindCount if the
	// keyword does not open/close a fold region.
	static FoldKind OpenKindFor(short numData);
	static FoldKind CloseKindFor(short numData);
	static bool IsMarkKeyword(short numData);

	void OpenFold(const int line, FoldKind kind);
	void MarkFold(const int line);
	void CloseFold(const int line, FoldKind kind);
	void ClearFoldLevel();

	void CalcFoldLevel(const ibTranslateCode& tc);

	// Recalc-pending flag. Set by RecalcFoldLevel (cheap, lazy);
	// consumed by UpdateFoldLevel on the next paint.
	mutable bool m_update_fold;

	// Per-kind open-fold counters — protect against unbalanced
	// close-tokens emitting spurious -1 deltas.
	std::array<short, FoldKindCount> m_counts;

	// Sorted-by-line list of fold transitions populated by
	// CalcFoldLevel — consumed by UpdateFoldLevel and GetFoldMask.
	std::vector<FoldPoint> m_folding_vector;
};

#endif

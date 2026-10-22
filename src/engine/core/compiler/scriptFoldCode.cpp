////////////////////////////////////////////////////////////////////////////
//	Description : where a module's text folds — the code editor's fold manager (scriptFoldCode.h)
////////////////////////////////////////////////////////////////////////////

#include "scriptFoldCode.h"

#include "core/compiler/translateCode.h"   // the lexem stream
#include "core/compiler/codeDef.h"         // KEYWORD / DELIMITER, KEY_*

#include <algorithm>
#include <iterator>

ibFoldCode::ibFoldCode()
	: m_update_fold(false), m_counts{}
{
}

bool ibFoldCode::UpdateFoldLevel(const ibTranslateCode& tc, int lineCount, std::vector<int>& levels)
{
	if (!m_update_fold)
		return false;

	CalcFoldLevel(tc);

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

	// Every line the folds name is written, as the editor wrote it line by line before the move — also a
	// fold point past the last line.
	int lastLine = std::max(lineCount, 0);
	for (const auto& v : m_folding_vector)
		lastLine = std::max(lastLine, v.line);
	levels.assign(lastLine + 1, FoldLevelBase);

	auto setFoldLevel = [&levels](int line, int level) {
		if (line >= 0 && line < static_cast<int>(levels.size()))
			levels[line] = level;
	};

	int level = 0, fold_start = 0;

	for (auto it = m_folding_vector.begin(); it != m_folding_vector.end(); ++it) {

		if (hint_ignore_fold != m_folding_vector.end() && it >= hint_ignore_fold && it->delta > 0)
			continue;

		for (int l = fold_start + 1; l <= it->line - 1; l++)
			setFoldLevel(l, FoldLevelBase + std::max(level, 0));

		if (it->delta > 0)
			setFoldLevel(it->line, (FoldLevelBase + std::max(level, 0)) | FoldLevelHeader);
		else if (it->delta < 0)
			setFoldLevel(it->line, (FoldLevelBase + std::max(level, 0)) | FoldLevelWhite);
		else
			setFoldLevel(it->line, FoldLevelBase + std::max(level, 0));

		level += it->delta;
		fold_start = it->line;
	}

	for (int l = fold_start + 1; l <= lineCount; l++)
		setFoldLevel(l, FoldLevelBase + std::max(level, 0));

	m_update_fold = false;
	return true;
}

int ibFoldCode::GetFoldMask(int line) const
{
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
		return (FoldLevelBase + (level - 1)) | FoldLevelHeader;
	}
	if (netHere < 0) {
		// Closer line: PrepareTABs WHITE branch subtracts 1 to land
		// at parent indent. `level` already retreated past the
		// closes — add 1 so the decoder lands at the right spot.
		return (FoldLevelBase + (level + 1)) | FoldLevelWhite;
	}
	if (hasMark) {
		// Else/ElseIf/Except marker on a net-zero line. PrepareTABs
		// ELSE branch subtracts 1 to align with the parent.
		return (FoldLevelBase + level) | FoldLevelElse;
	}
	// Plain line OR balanced inline pair (Open + Close on the same
	// source line, e.g. one-liner `from x in arr select x` or
	// `{ stmt; }`). Net effect is zero, line sits at the surrounding
	// scope's indent — no fold marker.
	return FoldLevelBase + level;
}

void ibFoldCode::OpenFold(const int line, FoldKind kind)
{
	m_counts[kind]++;
	m_folding_vector.push_back({ line, +1 });
}

void ibFoldCode::MarkFold(const int line)
{
	m_folding_vector.push_back({ line, 0 });
}

void ibFoldCode::CloseFold(const int line, FoldKind kind)
{
	if (m_counts[kind] > 0) {
		m_folding_vector.push_back({ line, -1 });
		m_counts[kind]--;
	}
}

void ibFoldCode::ClearFoldLevel()
{
	m_counts.fill(0);
	m_folding_vector.clear();
}

ibFoldCode::FoldKind ibFoldCode::OpenKindFor(short numData)
{
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

ibFoldCode::FoldKind ibFoldCode::CloseKindFor(short numData)
{
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

bool ibFoldCode::IsMarkKeyword(short numData)
{
	return numData == KEY_ELSE
		|| numData == KEY_ELSEIF
		|| numData == KEY_EXCEPT;
}

void ibFoldCode::CalcFoldLevel(const ibTranslateCode& tc)
{
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
	// Mode is the stream's (scriptFoldCode.h).
	const bool isCES = tc.GetCodeStyle() == CODE_CES;

	for (size_t i = 0; i < tc.GetLexCount(); ++i) {
		const ibLexem& lex = tc.GetLexAt(i);

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
			for (size_t j = i + 1; j < tc.GetLexCount(); ++j) {
				const ibLexem& nxt = tc.GetLexAt(j);
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

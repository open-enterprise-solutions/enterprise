////////////////////////////////////////////////////////////////////////////
//	Description : The query constructor - the window and everything on it
//	              (queryConstructor.h)
////////////////////////////////////////////////////////////////////////////

// THE DESKTOP'S FILE (frontend/win/dlgs/queryConstructor/queryConstructor.cpp), so far the query pane's styling
// (ibStyleQueryText, ibMarkQueryText): the keyword set is the language's own table (core/query/queryKeywords.h) and
// the colours are the engine's defaults, so it runs on the client as it is and asks the server nothing.
//
// ⚠ NOT HERE YET: the constructor window itself (ibDialogQueryConstructor) — see queryConstructor.h.

#include "queryConstructor.h"

#include "core/query/queryKeywords.h"                        // ibAllQueryKeywords — the highlighter's word set
#include "frmclient/mainFrame/settings/fontcolorsettings.h"  // the engine's own font + colours, as the code editor

#include <wx/stc/stc.h>


// THE QUERY TEXT, STYLED. The KEYWORD SET comes from the language's own table, so a word added to
// the grammar lights up the day it is added and a localized table lights up its own words.
//
// The lexer is the C one, not Scintilla's SQL one, and the reason is COMMENTS. Our query language
// takes its comment form from the shared lexer (ibTranslateCode::SkipSpaces) — `//` to end of line,
// and nothing else. Scintilla's SQL lexer knows the opposite set: it colours `--` and `/* … */`, and
// leaves `//` plain. So the SQL lexer painted as a comment what the engine would refuse, and painted
// as code what the engine ignores — a pane that teaches the wrong language. The C lexer's line
// comment IS ours.
//
// One residue, stated rather than hidden: the C lexer also colours a `/* … */` block, which the
// engine does not accept. Making the two agree exactly means either teaching the engine block
// comments — a change to the SHARED lexer, so it would land in the script language too — or forking
// the vendored SQL lexer. Neither is worth it for a form the language does not have.
//
// The font and the colours are the ENGINE'S defaults (ibFontColorSettings, default-constructed —
// the same object the code editor styles itself from), so this pane looks like every other place
// code is shown in the product rather than like a window with opinions.
void ibStyleQueryText(wxStyledTextCtrl* text)
{
	const ibFontColorSettings settings;
	wxFont font = settings.GetFont();

	text->SetLexer(wxSTC_LEX_CPP);
	text->SetKeyWords(0, ibAllQueryKeywords());

	// STYLE_DEFAULT first, then StyleClearAll — the cascade carries the font and the base colours to
	// every style index, so only the ones that DIFFER are set afterwards.
	text->StyleSetFont(wxSTC_STYLE_DEFAULT, font);
	text->StyleSetForeground(wxSTC_STYLE_DEFAULT,
		settings.GetColors(ibFontColorSettings::DisplayItem_Default).foreColor);
	text->StyleSetBackground(wxSTC_STYLE_DEFAULT,
		settings.GetColors(ibFontColorSettings::DisplayItem_Default).backColor);
	text->StyleClearAll();

	const ibFontColorSettings::Colors keyword = settings.GetColors(ibFontColorSettings::DisplayItem_Keyword);
	text->StyleSetForeground(wxSTC_C_WORD, keyword.foreColor);
	text->StyleSetBold(wxSTC_C_WORD, keyword.bold);
	text->StyleSetForeground(wxSTC_C_STRING,
		settings.GetColors(ibFontColorSettings::DisplayItem_String).foreColor);
	// A DATE literal is written in apostrophes ('20260101'), which the C lexer reads as a character
	// constant — so it takes the string colour, the way the pane's reader means it.
	text->StyleSetForeground(wxSTC_C_CHARACTER,
		settings.GetColors(ibFontColorSettings::DisplayItem_String).foreColor);
	text->StyleSetForeground(wxSTC_C_NUMBER,
		settings.GetColors(ibFontColorSettings::DisplayItem_Number).foreColor);
	// The three comment styles the C lexer can emit — `//` is the one our language has, the other two
	// cost nothing to colour and keep a pasted block from looking like broken code.
	const ibFontColorSettings::Colors comment = settings.GetColors(ibFontColorSettings::DisplayItem_Comment);
	text->StyleSetForeground(wxSTC_C_COMMENTLINE, comment.foreColor);
	text->StyleSetForeground(wxSTC_C_COMMENT, comment.foreColor);
	text->StyleSetForeground(wxSTC_C_COMMENTDOC, comment.foreColor);
	text->StyleSetForeground(wxSTC_C_OPERATOR,
		settings.GetColors(ibFontColorSettings::DisplayItem_Operator).foreColor);

	// LINE NUMBERS. The engine reports where it stopped as `line N (position M)` — that is the whole
	// reason they earn their room here: without them the verdict line names a place the reader has
	// to count to. The margin is sized to the text, so it does not take a fixed slice of a short
	// query, and the folding margin stays off (there is nothing to fold in one statement).
	text->SetMarginType(0, wxSTC_MARGIN_NUMBER);
	text->StyleSetForeground(wxSTC_STYLE_LINENUMBER,
		settings.GetColors(ibFontColorSettings::DisplayItem_Comment).foreColor);
	text->SetMarginWidth(0, text->TextWidth(wxSTC_STYLE_LINENUMBER, wxT("_9999")));
	text->SetMarginWidth(1, 0);
	text->SetTabWidth(4);
	text->SetUseHorizontalScrollBar(true);

	// A PARAMETER IS ITS OWN THING and must read as one. `&Store` is not an identifier of this query
	// — it is a value handed in from outside — and the C lexer, which is what colours the rest,
	// knows nothing about the ampersand: it paints `&` as an operator and `Store` as plain text, so
	// a parameter looked exactly like a column. Marked with an INDICATOR rather than a style,
	// because the lexer owns the styles and would repaint over anything set there; an indicator is
	// drawn on top and survives re-lexing. The colour is the preprocessor slot — the same one the
	// code editor uses for "this is substituted, not evaluated here".
	text->IndicatorSetStyle(kQueryParameterIndicator, wxSTC_INDIC_TEXTFORE);
	text->IndicatorSetForeground(kQueryParameterIndicator,
		settings.GetColors(ibFontColorSettings::DisplayItem_Preprocessor).foreColor);
	// A WORD OF THE LANGUAGE BY ITS PLACE wears the keyword's colour — it is one there, and only there.
	text->IndicatorSetStyle(kQueryPlacedWordIndicator, wxSTC_INDIC_TEXTFORE);
	text->IndicatorSetForeground(kQueryPlacedWordIndicator, keyword.foreColor);

	// AND RE-MARKED AS IT IS TYPED. Marking only where the text is set programmatically leaves a
	// parameter plain until something else happens to refill the pane — the colour arrives late,
	// which reads as a glitch rather than as a rule. The lexer repaints on every change; this
	// follows it, because an indicator is not restored by the lexer.
	text->Bind(wxEVT_STC_CHANGE, [text](wxStyledTextEvent& event) {
		event.Skip();
		ibMarkQueryText(text);
	});
}

// ⭐ THE WORDS ONLY A PLACE MAKES THE LANGUAGE'S — on the desktop read off the ENGINE'S tokens, so the pane and the
// parser agree on what a word is: `Date` after the AS of a CAST is a type, `YEAR` with a bracket after it is a call.
//
// ⚠ NOT HERE YET: those tokens are the query lexer's (backend/query/queryLexer.h), and the client does not link the
// backend. Until the lexer has a road here such words read as the C lexer colours them; the parameters below and the
// keyword set above are the language's own all the same.
static void MarkPlacedWords(wxStyledTextCtrl* text)
{
	text->SetIndicatorCurrent(kQueryPlacedWordIndicator);
	text->IndicatorClearRange(0, text->GetTextLength());
}

// MARK EVERY `&name` IN THE PANE. Called after the text is set — the indicator has to be re-applied
// then, because the text it marked is gone. Scanning here rather than asking the lexer keeps this
// independent of which lexer the pane uses; the rule is the language's own and fits in one line of
// prose: an ampersand followed by a name.
void ibMarkQueryText(wxStyledTextCtrl* text)
{
	if (text == nullptr)
		return;
	MarkPlacedWords(text);

	// WALKED IN SCINTILLA'S OWN POSITIONS, which are BYTES. Scanning a wxString instead would put
	// the mark left of the word the moment a parameter is named in Cyrillic, because two bytes go
	// to one character — and the bug would only show up for people whose names are not ASCII.
	// A byte >= 0x80 is part of a name here for the same reason: it is the tail of such a character.
	const int length = text->GetTextLength();
	text->SetIndicatorCurrent(kQueryParameterIndicator);
	text->IndicatorClearRange(0, length);

	for (int pos = 0; pos + 1 < length; ++pos) {
		if (text->GetCharAt(pos) != '&')
			continue;

		int end = pos + 1;
		for (; end < length; ++end) {
			const int ch = text->GetCharAt(end);
			const bool nameChar = (ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'Z')
				|| (ch >= 'a' && ch <= 'z') || ch == '_' || ch >= 0x80;
			if (!nameChar)
				break;
		}
		if (end == pos + 1)
			continue;   // a bare ampersand is not a parameter

		text->IndicatorFillRange(pos, end - pos);
		pos = end - 1;
	}
}

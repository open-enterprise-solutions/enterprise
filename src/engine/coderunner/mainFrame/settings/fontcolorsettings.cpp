#include "fontcolorsettings.h"

#include <variant>   // std::get — a style's value
#include <wx/settings.h>   // wxSystemSettings::GetAppearance — the look in force

#include "deskgui/univ/stc/stc.h"
#include "deskgui/typeconv.h"              // typeConv::ColourToWxColour
#include "core/style/style.h"              // ibStyle::Platform — the paper is the light look's content colour
#include "core/localization/dictionary.h"   // _() — the dialog's words

ibFontColorSettings::ibFontColorSettings()
{
#if defined(__WXOSX__) || defined(__APPLE__)
	m_font = wxFont(13, wxFONTFAMILY_MODERN, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, wxT("Menlo"));
#elif defined(__WXMSW__)
	m_font = wxFont(10, wxFONTFAMILY_MODERN, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, wxT("Consolas"));
#else
	m_font = wxFont(10, wxFONTFAMILY_MODERN, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL);
#endif

	// THE DARK LOOK'S — Visual Studio's dark editor, its paper and its inks, as the studio's (frmclient's
	// fontcolorsettings; Max, 2026-11-24: "the background for programming from Visual Studio, the same principle").
	if (wxSystemSettings::GetAppearance().IsDark()) {
		const wxColour night(0x1E, 0x1E, 0x1E);
		m_colors[DisplayItem_Default] = { wxColour(0xDC, 0xDC, 0xDC), night };
		m_colors[DisplayItem_Comment] = { wxColour(0x57, 0xA6, 0x4A), night };
		m_colors[DisplayItem_Preprocessor] = { wxColour(0x9B, 0x9B, 0x9B), night };
		m_colors[DisplayItem_Keyword] = { wxColour(0x56, 0x9C, 0xD6), night };
		m_colors[DisplayItem_Operator] = { wxColour(0xB4, 0xB4, 0xB4), night };
		m_colors[DisplayItem_String] = { wxColour(0xD6, 0x9D, 0x85), night };
		m_colors[DisplayItem_Number] = { wxColour(0xB5, 0xCE, 0xA8), night };
		m_colors[DisplayItem_Error] = { wxColour(0xF4, 0x47, 0x47), night, true };
		m_colors[DisplayItem_Warning] = { wxColour(0xF4, 0x47, 0x47), night };
		m_colors[DisplayItem_Selection] = { wxColour(0xFF, 0xFF, 0xFF), wxColour(0xB8, 0x5A, 0x38) };
		return;
	}

	// The light look's: every kind on the one paper colour, its content colour — a field's, a list's (it was a cream of
	// its own: "the cream gives a feeling of dirt", Max, 2026-11-12); what tells them apart is the ink.
	const wxColour paper = typeConv::ColourToWxColour(std::get<ibColour>(ibStyle::Platform().Get(ibStyleItem::FieldBackground, false)));
	m_colors[DisplayItem_Default] = { wxColour(0x00, 0x00, 0x00), paper };
	m_colors[DisplayItem_Comment] = { wxColour(0x00, 0x80, 0x00), paper };
	m_colors[DisplayItem_Preprocessor] = { wxColour(0x80, 0x00, 0x00), paper };
	m_colors[DisplayItem_Keyword] = { wxColour(0x00, 0x00, 0xFF), paper };
	m_colors[DisplayItem_Operator] = { wxColour(0x80, 0x80, 0x80), paper };
	m_colors[DisplayItem_String] = { wxColour(0xFF, 0x80, 0x80), paper };
	m_colors[DisplayItem_Number] = { wxColour(0xFF, 0x00, 0x00), paper };
	m_colors[DisplayItem_Error] = { wxColour(0xFF, 0x00, 0x00), paper, true };
	m_colors[DisplayItem_Warning] = { wxColour(0xFF, 0x00, 0x00), paper };
	m_colors[DisplayItem_Selection] = { wxColour(0xFF, 0xFF, 0xFF), wxColour(0xD9, 0x77, 0x57) };  // terracotta, the interior accent
}

wxString ibFontColorSettings::GetDisplayItemName(unsigned int i)
{
	switch (i) {
	case DisplayItem_Default: return _("Default");
	case DisplayItem_Comment: return _("Comment");
	case DisplayItem_Preprocessor: return _("Preprocessor");
	case DisplayItem_Keyword: return _("Keyword");
	case DisplayItem_Operator: return _("Operator");
	case DisplayItem_String: return _("String");
	case DisplayItem_Number: return _("Number");
	case DisplayItem_Error: return _("Error");
	case DisplayItem_Warning: return _("Warning");
	case DisplayItem_Selection: return _("Selection");
	}
	return wxString();
}

wxFont ibFontColorSettings::GetFont(DisplayItem displayItem) const
{
	wxFont font = m_font;
	if (m_colors[displayItem].bold)
		font.SetWeight(wxFONTWEIGHT_BOLD);
	if (m_colors[displayItem].italic)
		font.SetStyle(wxFONTSTYLE_ITALIC);
	return font;
}

void ibFontColorSettings::Apply(ibStyledTextCtrl& text) const
{
	// The default style first: StyleClearAll copies it into every style, so those not set below — annotations,
	// indent guides, call tips — get the chosen font too, and zoom grows them all from one size.
	const Colors& plain = m_colors[DisplayItem_Default];
	text.StyleSetFont(ibSTC_STYLE_DEFAULT, m_font);
	text.StyleSetForeground(ibSTC_STYLE_DEFAULT, plain.foreColor);
	text.StyleSetBackground(ibSTC_STYLE_DEFAULT, plain.backColor);
	text.StyleClearAll();

	static const struct { int style; DisplayItem kind; } s_lexerStyles[] = {
		{ ibSTC_C_DEFAULT, DisplayItem_Default },
		{ ibSTC_C_IDENTIFIER, DisplayItem_Default },
		{ ibSTC_C_COMMENT, DisplayItem_Comment },
		{ ibSTC_C_COMMENTLINE, DisplayItem_Comment },
		{ ibSTC_C_COMMENTDOC, DisplayItem_Comment },
		{ ibSTC_C_PREPROCESSOR, DisplayItem_Preprocessor },
		{ ibSTC_C_WORD, DisplayItem_Keyword },
		{ ibSTC_C_OPERATOR, DisplayItem_Operator },
		{ ibSTC_C_STRING, DisplayItem_String },
		{ ibSTC_C_STRINGEOL, DisplayItem_String },
		{ ibSTC_C_CHARACTER, DisplayItem_String },
		{ ibSTC_C_NUMBER, DisplayItem_Number },
	};
	for (const auto& [style, kind] : s_lexerStyles) {
		text.StyleSetFont(style, GetFont(kind));
		text.StyleSetForeground(style, m_colors[kind].foreColor);
		text.StyleSetBackground(style, m_colors[kind].backColor);
	}

	const Colors& selection = m_colors[DisplayItem_Selection];
	text.SetSelForeground(true, selection.foreColor);
	text.SetSelBackground(true, selection.backColor);

	// The fold boxes are the paper ruled in the ink: the white box ruled in black the editor defined stood out of
	// the dark paper (2026-11-24). The editor gives the shapes; the colours are the set's, as for the text.
	for (const int marker : { ibSTC_MARKNUM_FOLDER, ibSTC_MARKNUM_FOLDEROPEN, ibSTC_MARKNUM_FOLDERSUB,
		ibSTC_MARKNUM_FOLDEREND, ibSTC_MARKNUM_FOLDEROPENMID, ibSTC_MARKNUM_FOLDERMIDTAIL, ibSTC_MARKNUM_FOLDERTAIL }) {
		text.MarkerSetForeground(marker, plain.backColor);
		text.MarkerSetBackground(marker, plain.foreColor);
	}

	// The whole font, not its size alone: with the size only, the margin kept the default face until some later
	// style change pulled the font through.
	text.StyleSetFont(ibSTC_STYLE_LINENUMBER, GetFont(DisplayItem_Number));

	// The caret is the paper's opposite, so it shows on any paper.
	const wxColour& paper = plain.backColor;
	text.SetCaretForeground(wxColour(255 - paper.Red(), 255 - paper.Green(), 255 - paper.Blue()));
}

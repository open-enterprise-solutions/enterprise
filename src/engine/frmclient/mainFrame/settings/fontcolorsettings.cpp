#include "fontcolorsettings.h"
#include "settingsxml.h"

#include "deskgui/univ/stc/stc.h"
#include "deskgui/univ/theme.h"          // ibThemeEngine — the paper is the theme's content colour
#include "deskgui/univ/colourScheme.h"   // wxTHEME_COLOUR

// The cream the paper was until 2026-11-12, kept in the options of everyone who saved them (the dialog saves every kind,
// so nobody chose it) — read as the paper it is now (Load)
static const wxColour kFormerPaper(0xFA, 0xF7, 0xF0);

ibFontColorSettings::ibFontColorSettings()
{
#if defined(__WXOSX__) || defined(__APPLE__)
	m_font = wxFont(13, wxFONTFAMILY_MODERN, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, wxT("Menlo"));
#else
	m_font = wxFont(10, wxFONTFAMILY_MODERN, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL);
#endif

	// Every kind on the one paper colour, the theme's content colour — a field's, a list's (it was a cream of its own:
	// "the cream gives a feeling of dirt", Max, 2026-11-12); what tells them apart is the ink.
	const wxColour paper = wxTHEME_COLOUR(WINDOW);
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

	// The whole font, not its size alone: with the size only, the margin kept the default face until some later
	// style change pulled the font through.
	text.StyleSetFont(ibSTC_STYLE_LINENUMBER, GetFont(DisplayItem_Number));

	// The caret is the paper's opposite, so it shows on any paper.
	const wxColour& paper = plain.backColor;
	text.SetCaretForeground(wxColour(255 - paper.Red(), 255 - paper.Green(), 255 - paper.Blue()));
}

void ibFontColorSettings::Save(ibXmlWriter& writer, const wxString& tag) const
{
	writer.WriteStartElement(tag, ibString());
	AddTextNode(writer, wxT("font"), m_font.GetNativeFontInfoDesc());

	for (unsigned int i = 0; i < DisplayItem_Last; ++i) {
		writer.WriteStartElement(wxT("displayitem"), ibString());
		AddTextNode(writer, wxT("id"), wxString::Format(wxT("%u"), i));
		AddTextNode(writer, wxT("foreColor"), m_colors[i].foreColor.GetAsString(wxC2S_HTML_SYNTAX).Lower());
		AddTextNode(writer, wxT("backColor"), m_colors[i].backColor.GetAsString(wxC2S_HTML_SYNTAX).Lower());
		AddTextNode(writer, wxT("bold"), m_colors[i].bold);
		AddTextNode(writer, wxT("italic"), m_colors[i].italic);
		writer.WriteEndElement();
	}
	writer.WriteEndElement();
}

void ibFontColorSettings::Load(const ibXmlNode* root)
{
	for (const ibXmlNode* node = root->GetChildren(); node != nullptr; node = node->GetNext()) {
		if (node->GetName() == wxT("font")) {
			m_font.SetNativeFontInfo(node->GetNodeContent());
			continue;
		}
		if (node->GetName() != wxT("displayitem"))
			continue;

		// A kind is read whole or not at all: without its number there is no knowing whose colours these are.
		unsigned int id = DisplayItem_Last;
		Colors colors;
		for (const ibXmlNode* field = node->GetChildren(); field != nullptr; field = field->GetNext()) {
			const ibString& name = field->GetName();
			if (name == wxT("id"))
				field->GetNodeContent().ToUInt(&id);
			else if (name == wxT("foreColor"))
				colors.foreColor.Set(field->GetNodeContent());
			else if (name == wxT("backColor")) {
				colors.backColor.Set(field->GetNodeContent());
				if (colors.backColor == kFormerPaper)
					colors.backColor = wxTHEME_COLOUR(WINDOW);
			}
			else if (name == wxT("bold"))
				colors.bold = IsTrueNode(field);
			else if (name == wxT("italic"))
				colors.italic = IsTrueNode(field);
		}
		if (id < DisplayItem_Last)
			m_colors[id] = colors;
	}
}

#include "fontcolorsettings.h"
#include "settingsxml.h"

#include <variant>   // std::get — a style's value
#include <wx/settings.h>   // wxSystemSettings::GetAppearance — the look in force

#include "deskgui/univ/stc/stc.h"
#include "core/style/style.h"            // ibStyle::Platform — the light paper is the light look's content colour
#include "deskgui/typeconv.h"            // typeConv::ColourToWxColour

// The cream the paper was until 2026-11-12, kept in the options of everyone who saved them (the dialog saves every kind,
// so nobody chose it) — read as the paper it is now (Load)
static const wxColour kFormerPaper(0xFA, 0xF7, 0xF0);

// The light look's paper, every kind on it: its content colour — a field's, a list's (it was a cream of its own: "the
// cream gives a feeling of dirt", Max, 2026-11-12); what tells them apart is the ink, chosen against it.
static wxColour Paper()
{
	return typeConv::ColourToWxColour(std::get<ibColour>(ibStyle::Platform().Get(ibStyleItem::FieldBackground, false)));
}

// The set of a look: the dark one's or the light one's; for System the look in force's — the dark one's in the dark (the
// look is set before any window — one for the run).
static size_t Look(wxApp::Appearance look = wxApp::Appearance::System)
{
	if (look == wxApp::Appearance::System)
		return wxSystemSettings::GetAppearance().IsDark() ? 1 : 0;
	return look == wxApp::Appearance::Dark ? 1 : 0;
}

// A set's kinds, each by its number, as elements of the one written.
static void WriteColors(ibXmlWriter& writer, const ibFontColorSettings::Colors (&colors)[ibFontColorSettings::DisplayItem_Last])
{
	for (unsigned int i = 0; i < ibFontColorSettings::DisplayItem_Last; ++i) {
		writer.WriteStartElement(wxT("displayitem"), ibString());
		AddTextNode(writer, wxT("id"), wxString::Format(wxT("%u"), i));
		AddTextNode(writer, wxT("foreColor"), colors[i].foreColor.GetAsString(wxC2S_HTML_SYNTAX).Lower());
		AddTextNode(writer, wxT("backColor"), colors[i].backColor.GetAsString(wxC2S_HTML_SYNTAX).Lower());
		AddTextNode(writer, wxT("bold"), colors[i].bold);
		AddTextNode(writer, wxT("italic"), colors[i].italic);
		writer.WriteEndElement();
	}
}

// …and one read back: a kind the node does not name keeps its default.
static void ReadColors(const ibXmlNode* node, ibFontColorSettings::Colors (&colors)[ibFontColorSettings::DisplayItem_Last])
{
	// A kind is read whole or not at all: without its number there is no knowing whose colours these are.
	unsigned int id = ibFontColorSettings::DisplayItem_Last;
	ibFontColorSettings::Colors read;
	for (const ibXmlNode* field = node->GetChildren(); field != nullptr; field = field->GetNext()) {
		const ibString& name = field->GetName();
		if (name == wxT("id"))
			field->GetNodeContent().ToUInt(&id);
		else if (name == wxT("foreColor"))
			read.foreColor.Set(field->GetNodeContent());
		else if (name == wxT("backColor")) {
			read.backColor.Set(field->GetNodeContent());
			if (read.backColor == kFormerPaper)
				read.backColor = Paper();
		}
		else if (name == wxT("bold"))
			read.bold = IsTrueNode(field);
		else if (name == wxT("italic"))
			read.italic = IsTrueNode(field);
	}
	if (id < ibFontColorSettings::DisplayItem_Last)
		colors[id] = read;
}

ibFontColorSettings::ibFontColorSettings()
{
#if defined(__WXOSX__) || defined(__APPLE__)
	m_font = wxFont(13, wxFONTFAMILY_MODERN, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, wxT("Menlo"));
#elif defined(__WXMSW__)
	m_font = wxFont(10, wxFONTFAMILY_MODERN, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, wxT("Consolas"));
#else
	m_font = wxFont(10, wxFONTFAMILY_MODERN, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL);
#endif

	Colors (&light)[DisplayItem_Last] = m_colors[0];
	const wxColour paper = Paper();
	light[DisplayItem_Default] = { wxColour(0x00, 0x00, 0x00), paper };
	light[DisplayItem_Comment] = { wxColour(0x00, 0x80, 0x00), paper };
	light[DisplayItem_Preprocessor] = { wxColour(0x80, 0x00, 0x00), paper };
	light[DisplayItem_Keyword] = { wxColour(0x00, 0x00, 0xFF), paper };
	light[DisplayItem_Operator] = { wxColour(0x80, 0x80, 0x80), paper };
	light[DisplayItem_String] = { wxColour(0xFF, 0x80, 0x80), paper };
	light[DisplayItem_Number] = { wxColour(0xFF, 0x00, 0x00), paper };
	light[DisplayItem_Error] = { wxColour(0xFF, 0x00, 0x00), paper, true };
	light[DisplayItem_Warning] = { wxColour(0xFF, 0x00, 0x00), paper };
	light[DisplayItem_Selection] = { wxColour(0xFF, 0xFF, 0xFF), wxColour(0xD9, 0x77, 0x57) };  // terracotta, the interior accent

	// THE DARK LOOK'S — Visual Studio's dark editor (Max, 2026-11-24: "the background for programming from Visual Studio,
	// the same principle"): its paper and its inks, the selection the dark look's accent.
	Colors (&dark)[DisplayItem_Last] = m_colors[1];
	const wxColour night(0x1E, 0x1E, 0x1E);
	dark[DisplayItem_Default] = { wxColour(0xDC, 0xDC, 0xDC), night };
	dark[DisplayItem_Comment] = { wxColour(0x57, 0xA6, 0x4A), night };
	dark[DisplayItem_Preprocessor] = { wxColour(0x9B, 0x9B, 0x9B), night };
	dark[DisplayItem_Keyword] = { wxColour(0x56, 0x9C, 0xD6), night };
	dark[DisplayItem_Operator] = { wxColour(0xB4, 0xB4, 0xB4), night };
	dark[DisplayItem_String] = { wxColour(0xD6, 0x9D, 0x85), night };
	dark[DisplayItem_Number] = { wxColour(0xB5, 0xCE, 0xA8), night };
	dark[DisplayItem_Error] = { wxColour(0xF4, 0x47, 0x47), night, true };
	dark[DisplayItem_Warning] = { wxColour(0xF4, 0x47, 0x47), night };
	dark[DisplayItem_Selection] = { wxColour(0xFF, 0xFF, 0xFF), wxColour(0xB8, 0x5A, 0x38) };
}

const ibFontColorSettings::Colors& ibFontColorSettings::GetColors(DisplayItem displayItem) const
{
	return m_colors[Look()][displayItem];
}

void ibFontColorSettings::SetColors(DisplayItem displayItem, const Colors& colors)
{
	m_colors[Look()][displayItem] = colors;
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

// The font of every kind with a kind's bold and italic over it — of whichever set the kind's colours are.
static wxFont KindFont(const wxFont& font, const ibFontColorSettings::Colors& colors)
{
	wxFont kindFont = font;
	if (colors.bold)
		kindFont.SetWeight(wxFONTWEIGHT_BOLD);
	if (colors.italic)
		kindFont.SetStyle(wxFONTSTYLE_ITALIC);
	return kindFont;
}

wxFont ibFontColorSettings::GetFont(DisplayItem displayItem) const
{
	return KindFont(m_font, GetColors(displayItem));
}

void ibFontColorSettings::Apply(ibStyledTextCtrl& text, wxApp::Appearance look) const
{
	const Colors (&colors)[DisplayItem_Last] = m_colors[Look(look)];

	// The default style first: StyleClearAll copies it into every style, so those not set below — annotations,
	// indent guides, call tips — get the chosen font too, and zoom grows them all from one size.
	const Colors& plain = colors[DisplayItem_Default];
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
		text.StyleSetFont(style, KindFont(m_font, colors[kind]));
		text.StyleSetForeground(style, colors[kind].foreColor);
		text.StyleSetBackground(style, colors[kind].backColor);
	}

	const Colors& selection = colors[DisplayItem_Selection];
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
	text.StyleSetFont(ibSTC_STYLE_LINENUMBER, KindFont(m_font, colors[DisplayItem_Number]));

	// The caret is the paper's opposite, so it shows on any paper.
	const wxColour& paper = plain.backColor;
	text.SetCaretForeground(wxColour(255 - paper.Red(), 255 - paper.Green(), 255 - paper.Blue()));
}

void ibFontColorSettings::Save(ibXmlWriter& writer, const wxString& tag) const
{
	writer.WriteStartElement(tag, ibString());
	AddTextNode(writer, wxT("font"), m_font.GetNativeFontInfoDesc());
	WriteColors(writer, m_colors[0]);
	writer.WriteStartElement(wxT("dark"), ibString());
	WriteColors(writer, m_colors[1]);
	writer.WriteEndElement();
	writer.WriteEndElement();
}

void ibFontColorSettings::Load(const ibXmlNode* root)
{
	for (const ibXmlNode* node = root->GetChildren(); node != nullptr; node = node->GetNext()) {
		if (node->GetName() == wxT("font"))
			m_font.SetNativeFontInfo(node->GetNodeContent());
		else if (node->GetName() == wxT("displayitem"))
			ReadColors(node, m_colors[0]);
		else if (node->GetName() == wxT("dark")) {
			for (const ibXmlNode* item = node->GetChildren(); item != nullptr; item = item->GetNext()) {
				if (item->GetName() == wxT("displayitem"))
					ReadColors(item, m_colors[1]);
			}
		}
	}
}

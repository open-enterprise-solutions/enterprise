#ifndef FONT_COLOR_SETTINGS_H
#define FONT_COLOR_SETTINGS_H

/////////////////////////////////////////////////////////////////////////////
// The font of code, and the colours of each kind of text in it: one font for
// all, and per kind a fore and back colour with bold and italic over the
// font. Kept in the options file under "fontcolor", each kind by its number.
/////////////////////////////////////////////////////////////////////////////

#include <wx/wx.h>

#include <wx/wx.h>

class wxStyledTextCtrl;
class wxXmlNode;

class ibFontColorSettings
{
public:

	// ⚠ The numbers are saved: a new kind goes before DisplayItem_Last, never between.
	enum DisplayItem
	{
		DisplayItem_Default,
		DisplayItem_Comment,
		DisplayItem_Preprocessor,
		DisplayItem_Keyword,
		DisplayItem_Operator,
		DisplayItem_String,
		DisplayItem_Number,
		DisplayItem_Error,
		DisplayItem_Warning,
		DisplayItem_Selection,
		DisplayItem_Last,
	};

	struct Colors
	{
		wxColour foreColor;
		wxColour backColor;
		bool bold = false;
		bool italic = false;
	};

	ibFontColorSettings();

	unsigned int GetNumDisplayItems() const { return DisplayItem_Last; }

	// The kind's name as a person reads it, translated.
	static wxString GetDisplayItemName(unsigned int i);

	// The font of every kind, as chosen.
	const wxFont& GetFont() const { return m_font; }
	void SetFont(const wxFont& font) { m_font = font; }

	// The font with the kind's bold and italic over it.
	wxFont GetFont(DisplayItem displayItem) const;

	const Colors& GetColors(DisplayItem displayItem) const { return m_colors[displayItem]; }
	void SetColors(DisplayItem displayItem, const Colors& colors) { m_colors[displayItem] = colors; }

	// Paints a text control in these: each kind of text the C lexer tells apart in its font and colours, the
	// selection, the line numbers, and a caret that shows on the paper.
	void Apply(wxStyledTextCtrl& text) const;

	// The settings as a node named by the tag; Load reads one back, keeping
	// the default for a kind the node does not name.
	wxXmlNode* Save(const wxString& tag) const;
	void Load(wxXmlNode* root);

private:

	wxFont m_font;
	Colors m_colors[DisplayItem_Last];
};

#endif

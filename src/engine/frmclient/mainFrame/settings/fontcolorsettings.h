#ifndef FONT_COLOR_SETTINGS_H
#define FONT_COLOR_SETTINGS_H

/////////////////////////////////////////////////////////////////////////////
// The font of code, and the colours of each kind of text in it: one font for
// all, and per kind a fore and back colour with bold and italic over the
// font — a set per look, as a code editor's are per theme (Max, 2026-11-24:
// "as Visual Studio"). Kept in the options file under "fontcolor", each kind
// by its number, the dark look's under its "dark".
/////////////////////////////////////////////////////////////////////////////

#include <wx/wx.h>

#include "frmclient/frmclient.h"

class ibStyledTextCtrl;
class ibXmlNode;
class ibXmlWriter;

class FRMCLIENT_API ibFontColorSettings
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

	// The kind's colours in the look in force.
	const Colors& GetColors(DisplayItem displayItem) const;
	void SetColors(DisplayItem displayItem, const Colors& colors);

	// Paints a text control in these: each kind of text the C lexer tells apart in its font and colours, the
	// selection, the line numbers, and a caret that shows on the paper. In the set of the look given - the look in
	// force's by default (System, as the client's theme says it); what is printed takes the light one's, paper being
	// white in either look (2026-11-25: printed in the dark look, a module came out light grey on black).
	void Apply(ibStyledTextCtrl& text, wxApp::Appearance look = wxApp::Appearance::System) const;

	// The settings written as an element named by the tag; Load reads one back,
	// keeping the default for a kind the node does not name.
	void Save(ibXmlWriter& writer, const wxString& tag) const;
	void Load(const ibXmlNode* root);

private:

	wxFont m_font;
	// [0] the light look's, [1] the dark one's — the look in force's is the one read, set and painted with; the look is
	// set before any window, so it is one for the whole run.
	Colors m_colors[2][DisplayItem_Last];
};

#endif

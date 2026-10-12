#ifndef _CORE_FONT_H__
#define _CORE_FONT_H__

////////////////////////////////////////////////////////////////////////////
// ibFont — A FONT AS DATA: face, style, weight, size, family, underlined, struck through — the whole font,
// and nothing the screen answers.
////////////////////////////////////////////////////////////////////////////
//
// ⭐ LIGHT AND WHOLE: what a window needs to make a wxFont and what a page needs to write its CSS
// (font-family, font-style, font-weight, font-size in pt, text-decoration) are both read off these fields,
// with nothing lost on either road. wxFont is a pointer to a font the system made; there is nothing in it
// to copy. A side's converter reads these fields off a wxFont and makes one of them. The three
// words are wx's, copied with wx's numbers (wx/font.h, wx/defs.h), so the converter casts one to the other.
//
////////////////////////////////////////////////////////////////////////////

#include "core/core.h"
#include "core/fstring.h"

enum ibFontFamily {
	ibFONTFAMILY_DEFAULT = 70,
	ibFONTFAMILY_DECORATIVE,
	ibFONTFAMILY_ROMAN,
	ibFONTFAMILY_SCRIPT,
	ibFONTFAMILY_SWISS,
	ibFONTFAMILY_MODERN,
	ibFONTFAMILY_TELETYPE,
};

enum ibFontStyle {
	ibFONTSTYLE_NORMAL = 90,
	ibFONTSTYLE_ITALIC = 93,
	ibFONTSTYLE_SLANT = 94,
};

// The named numeric weights; a weight between them is a weight too.
enum ibFontWeight {
	ibFONTWEIGHT_THIN = 100,
	ibFONTWEIGHT_EXTRALIGHT = 200,
	ibFONTWEIGHT_LIGHT = 300,
	ibFONTWEIGHT_NORMAL = 400,
	ibFONTWEIGHT_MEDIUM = 500,
	ibFONTWEIGHT_SEMIBOLD = 600,
	ibFONTWEIGHT_BOLD = 700,
	ibFONTWEIGHT_EXTRABOLD = 800,
	ibFONTWEIGHT_HEAVY = 900,
	ibFONTWEIGHT_EXTRAHEAVY = 1000,
};

struct CORE_API ibFont {
	ibString     m_faceName;                            // empty = the family's default face
	ibFontStyle  m_style = ibFONTSTYLE_NORMAL;
	int          m_weight = ibFONTWEIGHT_NORMAL;        // numeric, as wxFont::GetNumericWeight: any from 1 to 1000
	double       m_pointSize = 0;                       // fractional, as wxFont::GetFractionalPointSize; 0 = no font at all
	ibFontFamily m_family = ibFONTFAMILY_DEFAULT;
	bool         m_underlined = false;
	bool         m_strikethrough = false;

	bool IsOk() const { return m_pointSize > 0; }

	// A text — the form fonts were always written in (the sides' typeConv FontToString), byte for byte:
	// `face,style,weight,pointSize,family,underlined`, e.g. "Arial,90,700,8,70,0". The size is whole points, the
	// strikethrough is not in it. The text is the whole font, read leniently, as that reader read: a piece that
	// is not a number reads as a new font's, a style, weight or family out of range as the ordinary one, and the
	// weights before wxWidgets 3.1.2 (90 normal, 91 light, 92 bold) as today's. Refused — an empty text — the
	// font is left alone.
	bool FromString(const ibString& text);

	// …and the text it reads from — empty for no font at all.
	ibString ToString() const;

	bool operator==(const ibFont& other) const {
		return m_faceName == other.m_faceName && m_style == other.m_style && m_weight == other.m_weight
			&& m_pointSize == other.m_pointSize && m_family == other.m_family
			&& m_underlined == other.m_underlined && m_strikethrough == other.m_strikethrough;
	}
	bool operator!=(const ibFont& other) const { return !(*this == other); }
};

#endif

#ifndef _CORE_COLOUR_H__
#define _CORE_COLOUR_H__

////////////////////////////////////////////////////////////////////////////
// ibColour — A COLOUR AS DATA: four channels, or none.
////////////////////////////////////////////////////////////////////////////
//
// wxColour cannot be copied byte for byte: it is a wxGDIObject (a table of virtuals, a pointer to shared
// data, and on Windows a COLORREF beside the channels). Its channels and whether it was set are copied
// here, and a side's converter moves them field by field.
//
////////////////////////////////////////////////////////////////////////////

#include "core/core.h"
#include "core/fstring.h"

struct CORE_API ibColour {
	unsigned char m_red = 0;
	unsigned char m_green = 0;
	unsigned char m_blue = 0;
	unsigned char m_alpha = 0;
	bool          m_isInit = false;   // no colour at all — wxColour's IsOk() false

	ibColour() = default;
	ibColour(unsigned char red, unsigned char green, unsigned char blue, unsigned char alpha = 255)
		: m_red(red), m_green(green), m_blue(blue), m_alpha(alpha), m_isInit(true) {}

	bool IsOk() const { return m_isInit; }

	// A text — the form colours were always written in (the sides' typeConv ColourToString): `r,g,b`, e.g.
	// "255,255,255", opaque. A channel that is not a number from 0 to 255 reads as 0, as that reader read it.
	// ⚠ A SYSTEM COLOUR BY ITS wx NAME ("wxSYS_COLOUR_WINDOW") IS REFUSED: it is a question to the desktop, and
	// the core has none to ask. Refused — that, or an empty text — the colour is left alone.
	bool FromString(const ibString& text);

	// …and the text it reads from — empty for no colour at all (an unset colour was written "0,0,0", and read
	// back black).
	ibString ToString() const;

	bool operator==(const ibColour& other) const {
		if (m_isInit != other.m_isInit)
			return false;
		return !m_isInit
			|| (m_red == other.m_red && m_green == other.m_green && m_blue == other.m_blue && m_alpha == other.m_alpha);
	}
	bool operator!=(const ibColour& other) const { return !(*this == other); }
};

#endif

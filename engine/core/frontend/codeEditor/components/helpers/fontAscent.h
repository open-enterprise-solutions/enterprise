#ifndef _FONT_ASCENT_H__
#define _FONT_ASCENT_H__

#include <wx/font.h>

// wxFont with ascent cached, a pointer to this type is stored in Font::fid.
class wxFontWithAscent : public wxFont
{
	int m_ascent;

public:

    wxFontWithAscent()
        : wxFont(), m_ascent(0)
    {
    }

	wxFontWithAscent(const wxFont &font)
		: wxFont(font), m_ascent(0)
	{
	}

    void SetAscent(int ascent) { m_ascent = ascent; }
    int GetAscent() const { return m_ascent; }
};


#endif
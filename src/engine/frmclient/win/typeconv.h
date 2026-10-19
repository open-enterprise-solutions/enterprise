#ifndef _FRMCLIENT_TYPECONV_H__
#define _FRMCLIENT_TYPECONV_H__

// WHAT A PROPERTY'S VALUE IS WRITTEN AS, READ INTO wx's AND BACK — the server writes a control's size, colour and
// font as the core's text (ibSize, ibColour, ibFont: FromString / ToString); these turn it into the widgets' and
// back, and read the system colours by name that the core leaves to the desktop.

#include <wx/colour.h>
#include <wx/font.h>
#include <wx/gdicmn.h>
#include <wx/settings.h>

#include <utility>

#include "core/graphics/colour.h"     // ibColour — ColourToWxColour / WxColourToColour
#include "core/graphics/font.h"       // ibFont — FontToWxFont / WxFontToFont
#include "core/graphics/geometry.h"   // ibSize — SizeToWxSize / WxSizeToSize

namespace typeConv
{
	// ⭐ THE CORE'S FONT AND COLOUR AS wx's, and back — field by field (core/graphics/font.h, colour.h: the
	// words are wx's, with wx's numbers). An unset one is an unset one on either side.
	inline wxFont FontToWxFont(const ibFont& font) {
		if (!font.IsOk())
			return wxNullFont;
		return wxFont(wxFontInfo(font.m_pointSize)
			.FaceName(font.m_faceName)
			.Family(static_cast<wxFontFamily>(font.m_family))
			.Style(static_cast<wxFontStyle>(font.m_style))
			.Weight(font.m_weight)
			.Underlined(font.m_underlined)
			.Strikethrough(font.m_strikethrough));
	}

	inline ibFont WxFontToFont(const wxFont& font) {
		ibFont result;
		if (!font.IsOk())
			return result;
		result.m_faceName = font.GetFaceName();
		result.m_style = static_cast<ibFontStyle>(font.GetStyle());
		result.m_weight = font.GetNumericWeight();
		result.m_pointSize = font.GetFractionalPointSize();
		const wxFontFamily family = font.GetFamily();
		result.m_family = family >= wxFONTFAMILY_DEFAULT && family <= wxFONTFAMILY_TELETYPE
			? static_cast<ibFontFamily>(family) : ibFONTFAMILY_DEFAULT;
		result.m_underlined = font.GetUnderlined();
		result.m_strikethrough = font.GetStrikethrough();
		return result;
	}

	inline wxColour ColourToWxColour(const ibColour& colour) {
		if (!colour.IsOk())
			return wxNullColour;
		return wxColour(colour.m_red, colour.m_green, colour.m_blue, colour.m_alpha);
	}

	inline ibColour WxColourToColour(const wxColour& colour) {
		if (!colour.IsOk())
			return ibColour();
		return ibColour(colour.Red(), colour.Green(), colour.Blue(), colour.Alpha());
	}

	inline wxSize SizeToWxSize(const ibSize& size) {
		return wxSize(size.m_width, size.m_height);
	}

	inline ibSize WxSizeToSize(const wxSize& size) {
		return ibSize(size.x, size.y);
	}

	// A system colour by its wx name, "wxSYS_COLOUR_WINDOW"; a name it does not know is the button face.
	inline wxSystemColour StringToSystemColour(const wxString& name) {
#define SYSTEM_COLOUR(colour) { wxT(#colour), colour }
		static const std::pair<const wxChar*, wxSystemColour> s_systemColours[] = {
			SYSTEM_COLOUR(wxSYS_COLOUR_SCROLLBAR),
			SYSTEM_COLOUR(wxSYS_COLOUR_BACKGROUND),
			SYSTEM_COLOUR(wxSYS_COLOUR_ACTIVECAPTION),
			SYSTEM_COLOUR(wxSYS_COLOUR_INACTIVECAPTION),
			SYSTEM_COLOUR(wxSYS_COLOUR_MENU),
			SYSTEM_COLOUR(wxSYS_COLOUR_WINDOW),
			SYSTEM_COLOUR(wxSYS_COLOUR_WINDOWFRAME),
			SYSTEM_COLOUR(wxSYS_COLOUR_MENUTEXT),
			SYSTEM_COLOUR(wxSYS_COLOUR_WINDOWTEXT),
			SYSTEM_COLOUR(wxSYS_COLOUR_CAPTIONTEXT),
			SYSTEM_COLOUR(wxSYS_COLOUR_ACTIVEBORDER),
			SYSTEM_COLOUR(wxSYS_COLOUR_INACTIVEBORDER),
			SYSTEM_COLOUR(wxSYS_COLOUR_APPWORKSPACE),
			SYSTEM_COLOUR(wxSYS_COLOUR_HIGHLIGHT),
			SYSTEM_COLOUR(wxSYS_COLOUR_HIGHLIGHTTEXT),
			SYSTEM_COLOUR(wxSYS_COLOUR_BTNFACE),
			SYSTEM_COLOUR(wxSYS_COLOUR_BTNSHADOW),
			SYSTEM_COLOUR(wxSYS_COLOUR_GRAYTEXT),
			SYSTEM_COLOUR(wxSYS_COLOUR_BTNTEXT),
			SYSTEM_COLOUR(wxSYS_COLOUR_INACTIVECAPTIONTEXT),
			SYSTEM_COLOUR(wxSYS_COLOUR_BTNHIGHLIGHT),
			SYSTEM_COLOUR(wxSYS_COLOUR_3DDKSHADOW),
			SYSTEM_COLOUR(wxSYS_COLOUR_3DLIGHT),
			SYSTEM_COLOUR(wxSYS_COLOUR_INFOTEXT),
			SYSTEM_COLOUR(wxSYS_COLOUR_INFOBK),
		};
#undef SYSTEM_COLOUR
		for (const auto& [text, colour] : s_systemColours) {
			if (name == text)
				return colour;
		}
		return wxSYS_COLOUR_BTNFACE;
	}

	// The core's texts, read into wx's and written back. A colour may be a system colour by name, which the core
	// refuses: it is a question to this side.
	inline wxColour StringToColour(const wxString& text) {
		if (text.StartsWith(wxT("wx")))
			return wxSystemSettings::GetColour(StringToSystemColour(text));
		ibColour colour;
		colour.FromString(text);
		return ColourToWxColour(colour);
	}

	inline wxString ColourToString(const wxColour& colour) {
		return WxColourToColour(colour).ToString();
	}

	inline wxFont StringToFont(const wxString& text) {
		ibFont font;
		font.FromString(text);
		return FontToWxFont(font);
	}

	inline wxString FontToString(const wxFont& font) {
		return WxFontToFont(font).ToString();
	}

	// A text that is not a size reads as no size said, wxDefaultSize.
	inline wxSize StringToSize(const wxString& text) {
		ibSize size = ibDefaultSize;
		size.FromString(text);
		return SizeToWxSize(size);
	}

	inline wxString SizeToString(const wxSize& size) {
		return WxSizeToSize(size).ToString();
	}
}

#endif

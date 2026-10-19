#ifndef _TYPECONV_H__
#define _TYPECONV_H__
#include "core/diagnostics/journal.h"      // ibJournal — the technology journal
#include "core/fnumber.h"
#include "core/graphics/colour.h"     // ibColour — ColourToWxColour / WxColourToColour
#include "core/graphics/font.h"       // ibFont — FontToWxFont / WxFontToFont
#include "core/graphics/geometry.h"   // ibPoint / ibSize — PointToWxPoint / SizeToWxSize

#include <wx/tokenzr.h>
#include <wx/filename.h>

// The core's colour, font, point and size as the widgets' and back. The backend holds those as data
// (core/graphics) and never needs this file; frmclient keeps its own copy beside its own widgets.
#include <wx/artprov.h>
#include <wx/gdicmn.h>
#include <wx/colour.h>
#include <wx/font.h>
#include <wx/bitmap.h>
#include <wx/dcmemory.h>
#include <wx/pen.h>
#include <wx/settings.h>

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

	inline wxPoint PointToWxPoint(const ibPoint& point) {
		return wxPoint(point.m_x, point.m_y);
	}

	inline ibPoint WxPointToPoint(const wxPoint& point) {
		return ibPoint(point.x, point.y);
	}

	inline wxSize SizeToWxSize(const ibSize& size) {
		return wxSize(size.m_width, size.m_height);
	}

	inline ibSize WxSizeToSize(const wxSize& size) {
		return ibSize(size.x, size.y);
	}
}

#endif

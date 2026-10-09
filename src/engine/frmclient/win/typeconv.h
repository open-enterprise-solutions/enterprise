#ifndef _FRMCLIENT_TYPECONV_H__
#define _FRMCLIENT_TYPECONV_H__

// WHAT A PROPERTY'S VALUE IS WRITTEN AS, READ BACK — the server writes a control's size, colour and font as text
// (its typeConv: SizeToString, ColourToString, FontToString); these are its readers, copied from backend/typeconv.h
// line for line — and the writers a sheet saves itself with.

#include <wx/colour.h>
#include <wx/font.h>
#include <wx/gdicmn.h>
#include <wx/settings.h>
#include <wx/tokenzr.h>

#include "fontcontainer.h"

#define ElseIfSystemColourConvert( NAME, value )	\
	else if ( value == wxT(#NAME) )					\
	{												\
		systemVal =	NAME;							\
	}

namespace typeConv
{
	inline bool StringToPoint(const wxString& val, wxPoint* point) {
		wxPoint result;

		bool error = false;
		wxString str_x, str_y;
		long val_x = -1, val_y = -1;

		if (val != wxT("")) {
			wxStringTokenizer tkz(val, wxT(","));
			if (tkz.HasMoreTokens()) {
				str_x = tkz.GetNextToken();
				str_x.Trim(true);
				str_x.Trim(false);
				if (tkz.HasMoreTokens())
				{
					str_y = tkz.GetNextToken();
					str_y.Trim(true);
					str_y.Trim(false);
				}
				else
					error = true;
			}
			else
				error = true;

			if (!error)
				error = !str_x.ToLong(&val_x);

			if (!error)
				error = !str_y.ToLong(&val_y);

			if (!error)
				result = wxPoint(val_x, val_y);
		}
		else
			result = wxDefaultPosition;

		if (error)
			result = wxDefaultPosition;

		point->x = result.x;
		point->y = result.y;

		return !error;
	}

	inline wxPoint StringToPoint(const wxString& val) {
		wxPoint result;
		StringToPoint(val, &result);
		return result;
	}

	inline wxSize StringToSize(const wxString& str) {
		wxPoint point = StringToPoint(str);
		return wxSize(point.x, point.y);
	}

	inline wxString SizeToString(const wxSize& size) {
		return wxString::Format(wxT("%d,%d"), size.GetWidth(), size.GetHeight());
	}

	inline wxFontContainer StringToFont(const wxString& str) {

		wxFontContainer font;

		// face name, style, weight, point size, family, underlined
		wxStringTokenizer tkz(str, wxT(","));

		if (tkz.HasMoreTokens())
		{
			wxString faceName = tkz.GetNextToken();
			faceName.Trim(true);
			faceName.Trim(false);
			font.SetFaceName(faceName);
		}

		if (tkz.HasMoreTokens()) {
			long l_style;
			wxString s_style = tkz.GetNextToken();
			if (s_style.ToLong(&l_style)) {
				if (l_style >= wxFONTSTYLE_NORMAL && l_style < wxFONTSTYLE_MAX) {
					font.SetStyle(static_cast<wxFontStyle>(l_style));
				}
				else {
					font.SetStyle(wxFONTSTYLE_NORMAL);
				}
			}
		}

		if (tkz.HasMoreTokens()) {
			long l_weight;
			wxString s_weight = tkz.GetNextToken();
			if (s_weight.ToLong(&l_weight)) {
				// Due to an ABI break in wxWidgets 3.1.2 the values of the symbols changed, the previous
				// values are distinct from the new values but are in their range, so they need to be tested first.
#if wxCHECK_VERSION(3, 1, 2)
				if (l_weight >= wxNORMAL && l_weight <= wxBOLD) {
					switch (l_weight) {
					case wxNORMAL:
						font.SetWeight(wxFONTWEIGHT_NORMAL);
						break;
					case wxLIGHT:
						font.SetWeight(wxFONTWEIGHT_LIGHT);
						break;
					case wxBOLD:
						font.SetWeight(wxFONTWEIGHT_BOLD);
						break;
					default:
						font.SetWeight(wxFONTWEIGHT_NORMAL);
						break;
					}
				}
				else if (l_weight >= wxFONTWEIGHT_NORMAL && l_weight < wxFONTWEIGHT_MAX) {
					font.SetWeight(static_cast<wxFontWeight>(l_weight));
				}
				else {
					font.SetWeight(wxFONTWEIGHT_NORMAL);
				}
#else
				if (l_weight >= wxFONTWEIGHT_NORMAL && l_weight < wxFONTWEIGHT_MAX) {
					font.SetWeight(static_cast<wxFontWeight>(l_weight));
				}
				else {
					// Either an invalid value or a value of a wxWidgets 3.1.2 symbol,
					// since these symbols are not available here, test for their values directly
					switch (l_weight) {
					case 300:
						font.SetWeight(wxFONTWEIGHT_LIGHT);
						break;
					case 400:
						font.SetWeight(wxFONTWEIGHT_NORMAL);
						break;
					case 700:
						font.SetWeight(wxFONTWEIGHT_BOLD);
						break;
					default:
						font.SetWeight(wxFONTWEIGHT_NORMAL);
						break;
					}
				}
#endif
			}
		}

		if (tkz.HasMoreTokens())
		{
			long l_size;
			wxString s_size = tkz.GetNextToken();
			if (s_size.ToLong(&l_size))
			{
				font.SetPointSize((int)l_size);
			}
		}

		if (tkz.HasMoreTokens()) {
			long l_family;
			wxString s_family = tkz.GetNextToken();
			if (s_family.ToLong(&l_family)) {
				if (l_family >= wxFONTFAMILY_DEFAULT && l_family < wxFONTFAMILY_MAX) {
					font.SetFamily(static_cast<wxFontFamily>(l_family));
				}
				else {
					font.SetFamily(wxFONTFAMILY_DEFAULT);
				}
			}
		}

		if (tkz.HasMoreTokens())
		{
			long l_underlined;
			wxString s_underlined = tkz.GetNextToken();
			if (s_underlined.ToLong(&l_underlined))
			{
				font.SetUnderlined(l_underlined != 0);
			}
		}

		return font;
	}


	inline wxSystemColour StringToSystemColour(const wxString& str)
	{
		wxSystemColour systemVal = wxSYS_COLOUR_BTNFACE;

		if (false)
		{
		}
		ElseIfSystemColourConvert(wxSYS_COLOUR_SCROLLBAR, str)
			ElseIfSystemColourConvert(wxSYS_COLOUR_BACKGROUND, str)
			ElseIfSystemColourConvert(wxSYS_COLOUR_ACTIVECAPTION, str)
			ElseIfSystemColourConvert(wxSYS_COLOUR_INACTIVECAPTION, str)
			ElseIfSystemColourConvert(wxSYS_COLOUR_MENU, str)
			ElseIfSystemColourConvert(wxSYS_COLOUR_WINDOW, str)
			ElseIfSystemColourConvert(wxSYS_COLOUR_WINDOWFRAME, str)
			ElseIfSystemColourConvert(wxSYS_COLOUR_MENUTEXT, str)
			ElseIfSystemColourConvert(wxSYS_COLOUR_WINDOWTEXT, str)
			ElseIfSystemColourConvert(wxSYS_COLOUR_CAPTIONTEXT, str)
			ElseIfSystemColourConvert(wxSYS_COLOUR_ACTIVEBORDER, str)
			ElseIfSystemColourConvert(wxSYS_COLOUR_INACTIVEBORDER, str)
			ElseIfSystemColourConvert(wxSYS_COLOUR_APPWORKSPACE, str)
			ElseIfSystemColourConvert(wxSYS_COLOUR_HIGHLIGHT, str)
			ElseIfSystemColourConvert(wxSYS_COLOUR_HIGHLIGHTTEXT, str)
			ElseIfSystemColourConvert(wxSYS_COLOUR_BTNFACE, str)
			ElseIfSystemColourConvert(wxSYS_COLOUR_BTNSHADOW, str)
			ElseIfSystemColourConvert(wxSYS_COLOUR_GRAYTEXT, str)
			ElseIfSystemColourConvert(wxSYS_COLOUR_BTNTEXT, str)
			ElseIfSystemColourConvert(wxSYS_COLOUR_INACTIVECAPTIONTEXT, str)
			ElseIfSystemColourConvert(wxSYS_COLOUR_BTNHIGHLIGHT, str)
			ElseIfSystemColourConvert(wxSYS_COLOUR_3DDKSHADOW, str)
			ElseIfSystemColourConvert(wxSYS_COLOUR_3DLIGHT, str)
			ElseIfSystemColourConvert(wxSYS_COLOUR_INFOTEXT, str)
			ElseIfSystemColourConvert(wxSYS_COLOUR_INFOBK, str)

			return systemVal;
	}

	inline wxColour StringToColour(const wxString& str) {

		// check for system colour
		if (str.find_first_of(wxT("wx")) == 0)
		{
			return wxSystemSettings::GetColour(StringToSystemColour(str));
		}
		else
		{
			wxStringTokenizer tkz(str, wxT(","));
			unsigned int red, green, blue;

			red = green = blue = 0;

			//  bool set_red, set_green, set_blue;

			//  set_red = set_green = set_blue = false;

			if (tkz.HasMoreTokens())
			{
				wxString s_red = tkz.GetNextToken();
				long l_red;

				if (s_red.ToLong(&l_red) && (l_red >= 0 && l_red <= 255))
				{
					red = (int)l_red;
					//      set_size = true;
				}
			}

			if (tkz.HasMoreTokens())
			{
				wxString s_green = tkz.GetNextToken();
				long l_green;

				if (s_green.ToLong(&l_green) && (l_green >= 0 && l_green <= 255))
				{
					green = (int)l_green;
					//      set_size = true;
				}
			}

			if (tkz.HasMoreTokens())
			{
				wxString s_blue = tkz.GetNextToken();
				long l_blue;

				if (s_blue.ToLong(&l_blue) && (l_blue >= 0 && l_blue <= 255))
				{
					blue = (int)l_blue;
					//      set_size = true;
				}
			}


			return wxColour(red, green, blue);
		}
	}

	// …and the two writers a sheet saves itself with (backend/spreadsheetDescription.cpp, the client's copy).
	inline wxString FontToString(const wxFontContainer& font) {
		// face name, style, weight, point size, family, underlined
		return wxString::Format(wxT("%s,%d,%d,%d,%d,%d"), font.GetFaceName().c_str(), font.GetStyle(), font.GetWeight(), font.GetPointSize(), font.GetFamily(), font.GetUnderlined() ? 1 : 0);
	}

	inline wxString ColourToString(const wxColour& colour) {
		return wxString::Format(wxT("%d,%d,%d"), colour.Red(), colour.Green(), colour.Blue());
	}

}

#endif

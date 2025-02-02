#ifndef _TYPECONV_H__
#define _TYPECONV_H__

#include "types.h"
#include "number.h"
#include "fontcontainer.h"

// macros para la conversión entre wxString <-> wxString
#define _WXSTR(x)  typeConv::_StringToWxString(x)
#define _STDSTR(x) typeConv::_WxStringToString(x)
#define _ANSISTR(x) typeConv::_WxStringToAnsiString(x)

#define SystemColourConvertCase( NAME )	\
	case NAME:							\
		s = wxT(#NAME);						\
		break;

#define ElseIfSystemColourConvert( NAME, value )	\
	else if ( value == wxT(#NAME) )					\
	{												\
		systemVal =	NAME;							\
	}

#include <wx/artprov.h>
#include <wx/tokenzr.h>
#include <wx/propgrid/propgrid.h>

namespace typeConv
{
	inline wxString _StringToWxString(const std::string& str) {
		return _StringToWxString(str.c_str());
	}

	inline wxString _StringToWxString(const char* str) {
		return wxString(str, wxConvUTF8);
	}

	inline std::string _WxStringToString(const wxString& str) {
		return 	std::string(str.mb_str(wxConvUTF8));
	}

	inline std::string _WxStringToAnsiString(const wxString& str) {
		return std::string(str.mb_str(wxConvISO8859_1));
	}

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

	inline wxString PointToString(const wxPoint& point) {
		return wxString::Format(wxT("%d,%d"), point.x, point.y);
	}

	inline wxString SizeToString(const wxSize& size) {
		return wxString::Format(wxT("%d,%d"), size.GetWidth(), size.GetHeight());
	}

	inline int StringToInt(const wxString& str) {
		long l = 0;
		str.ToLong(&l);
		return (int)l;
	}

	inline bool FlagSet(const wxString& flag, const wxString& currentValue) {
		bool set = false;
		wxStringTokenizer tkz(currentValue, wxT("|"));
		while (!set && tkz.HasMoreTokens()) {
			wxString token;
			token = tkz.GetNextToken();
			token.Trim(true);
			token.Trim(false);
			if (token == flag)
				set = true;
		}
		return set;
	}

	inline wxString ClearFlag(const wxString& flag, const wxString& currentValue) {
		if (flag == wxT(""))
			return currentValue;
		wxString result;
		wxStringTokenizer tkz(currentValue, wxT("|"));
		while (tkz.HasMoreTokens()) {
			wxString token;
			token = tkz.GetNextToken();
			token.Trim(true);
			token.Trim(false);
			if (token != flag) {
				if (!result.IsEmpty())
					result = result + wxT('|');
				result = result + token;
			}
		}
		return result;
	}

	inline wxString SetFlag(const wxString& flag, const wxString& currentValue) {
		if (flag == wxT(""))
			return currentValue;
		bool found = false;
		wxString result = currentValue;
		wxStringTokenizer tkz(currentValue, wxT("|"));
		while (tkz.HasMoreTokens()) {
			wxString token;
			token = tkz.GetNextToken();
			token.Trim(true);
			token.Trim(false);
			if (token == flag)
				found = true;
		}
		if (!found) {
			if (result != wxT(""))
				result = result + wxT('|');

			result = result + flag;
		}
		return result;
	}

	inline wxBitmap StringToBitmap(const wxString& filename) {
#ifndef DEBUG
		wxLogNull stopLogging;
#endif
		// Get bitmap from art provider
		if (filename.Contains(_("Load From Art Provider")))
		{
			wxString image = filename.AfterFirst(wxT(';')).Trim(false);
			wxString rid = image.BeforeFirst(wxT(';')).Trim(false);
			wxString cid = image.AfterFirst(wxT(';')).Trim(false);

			if (rid.IsEmpty() || cid.IsEmpty()) {
				return wxNullBitmap;
			}
			else
			{
				//return wxArtProvider::GetBitmap( rid, cid + wxT("_C") ){
				wxBitmap bmp = wxArtProvider::GetBitmap(rid, cid + wxT("_C"));

				if (!bmp.IsOk()) {
					// Create another bitmap of the appropriate size to show it's invalid.
					// We can get here if the user entered a custom wxArtID which, presumably,
					// they will have already installed in their app.
					bmp = wxArtProvider::GetBitmap(wxT("wxART_MISSING_IMAGE"), cid + wxT("_C"));

					if (bmp.IsOk()) {
						wxMemoryDC dc;
						dc.SelectObject(bmp);
						dc.SetPen(wxPen(*wxRED, 3));
						dc.DrawLine(wxPoint(0, 0), wxPoint(bmp.GetWidth(), bmp.GetHeight()));
						dc.SelectObject(wxNullBitmap);
					}
				}
				else {
					return bmp;
				}
			}
		}
		return wxNullBitmap;
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

	inline wxString FontToString(const wxFontContainer& font) {
		// face name, style, weight, point size, family, underlined
		return wxString::Format(wxT("%s,%d,%d,%d,%d,%d"), font.GetFaceName().c_str(), font.GetStyle(), font.GetWeight(), font.GetPointSize(), font.GetFamily(), font.GetUnderlined() ? 1 : 0);
	}

	inline wxString FontFamilyToString(wxFontFamily family) {
		wxString result;
		switch (family) {
		case wxFONTFAMILY_DECORATIVE:
			result = wxT("wxFONTFAMILY_DECORATIVE");
			break;
		case wxFONTFAMILY_ROMAN:
			result = wxT("wxFONTFAMILY_ROMAN");
			break;
		case wxFONTFAMILY_SCRIPT:
			result = wxT("wxFONTFAMILY_SCRIPT");
			break;
		case wxFONTFAMILY_SWISS:
			result = wxT("wxFONTFAMILY_SWISS");
			break;
		case wxFONTFAMILY_MODERN:
			result = wxT("wxFONTFAMILY_MODERN");
			break;
		case wxFONTFAMILY_TELETYPE:
			result = wxT("wxFONTFAMILY_TELETYPE");
			break;
		default:
			result = wxT("wxFONTFAMILY_DEFAULT");
			break;
		}
		return result;
	}

	inline wxString FontStyleToString(wxFontStyle style) {
		wxString result;

		switch (style) {
		case wxFONTSTYLE_ITALIC:
			result = wxT("wxFONTSTYLE_ITALIC");
			break;
		case wxFONTSTYLE_SLANT:
			result = wxT("wxFONTSTYLE_SLANT");
			break;
		default:
			result = wxT("wxFONTSTYLE_NORMAL");
			break;
		}

		return result;
	}

	inline wxString FontWeightToString(wxFontWeight weight) {
		wxString result;

		switch (weight) {
		case wxFONTWEIGHT_LIGHT:
			result = wxT("wxFONTWEIGHT_LIGHT");
			break;
		case wxFONTWEIGHT_BOLD:
			result = wxT("wxFONTWEIGHT_BOLD");
			break;
		default:
			result = wxT("wxFONTWEIGHT_NORMAL");
			break;
		}

		return result;
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

	inline wxString SystemColourToString(long colour) {
		wxString s;
		switch (colour)
		{
			SystemColourConvertCase(wxSYS_COLOUR_SCROLLBAR)
				SystemColourConvertCase(wxSYS_COLOUR_BACKGROUND)
				SystemColourConvertCase(wxSYS_COLOUR_ACTIVECAPTION)
				SystemColourConvertCase(wxSYS_COLOUR_INACTIVECAPTION)
				SystemColourConvertCase(wxSYS_COLOUR_MENU)
				SystemColourConvertCase(wxSYS_COLOUR_WINDOW)
				SystemColourConvertCase(wxSYS_COLOUR_WINDOWFRAME)
				SystemColourConvertCase(wxSYS_COLOUR_MENUTEXT)
				SystemColourConvertCase(wxSYS_COLOUR_WINDOWTEXT)
				SystemColourConvertCase(wxSYS_COLOUR_CAPTIONTEXT)
				SystemColourConvertCase(wxSYS_COLOUR_ACTIVEBORDER)
				SystemColourConvertCase(wxSYS_COLOUR_INACTIVEBORDER)
				SystemColourConvertCase(wxSYS_COLOUR_APPWORKSPACE)
				SystemColourConvertCase(wxSYS_COLOUR_HIGHLIGHT)
				SystemColourConvertCase(wxSYS_COLOUR_HIGHLIGHTTEXT)
				SystemColourConvertCase(wxSYS_COLOUR_BTNFACE)
				SystemColourConvertCase(wxSYS_COLOUR_BTNSHADOW)
				SystemColourConvertCase(wxSYS_COLOUR_GRAYTEXT)
				SystemColourConvertCase(wxSYS_COLOUR_BTNTEXT)
				SystemColourConvertCase(wxSYS_COLOUR_INACTIVECAPTIONTEXT)
				SystemColourConvertCase(wxSYS_COLOUR_BTNHIGHLIGHT)
				SystemColourConvertCase(wxSYS_COLOUR_3DDKSHADOW)
				SystemColourConvertCase(wxSYS_COLOUR_3DLIGHT)
				SystemColourConvertCase(wxSYS_COLOUR_INFOTEXT)
				SystemColourConvertCase(wxSYS_COLOUR_INFOBK)
		}
		return s;
	}

	inline wxString ColourToString(const wxColour& colour) {
		return wxString::Format(wxT("%d,%d,%d"), colour.Red(), colour.Green(), colour.Blue());
	}

	inline bool StringToBool(const wxString& str) {
		return str == wxT("1") ? true : false;
	}

	inline wxString BoolToString(bool val) {
		return val ? wxT("1") : wxT("0");
	}

	inline wxArrayString StringToArrayString(const wxString& str) {
		//wxArrayString result = wxStringTokenize( str, wxT(";") );
		wxArrayString result;

		WX_PG_TOKENIZER2_BEGIN(str, wxT('"'))
			result.Add(token);
		WX_PG_TOKENIZER2_END()
			return result;
	}

	inline wxString ArrayStringToString(const wxArrayString& arrayStr) {
		return wxArrayStringProperty::ArrayStringToString(arrayStr, '"', 1);
	}

	inline void ParseBitmapWithResource(const wxString& value, wxString* image, wxString* source, wxSize* icoSize) {
		// Splitting bitmap resource property value - it is of the form "path; source [width; height]"

		*image = value;
		*source = _("Load From File");
		*icoSize = wxDefaultSize;

		wxArrayString children;
		wxStringTokenizer tkz(value, wxT("[];"), wxTOKEN_RET_EMPTY);
		while (tkz.HasMoreTokens())
		{
			wxString child = tkz.GetNextToken();
			child.Trim(false);
			child.Trim(true);
			children.Add(child);
		}

		if (children.Index(_("Load From Art Provider")) == wxNOT_FOUND)
		{
			long temp;
			switch (children.size())
			{
			case 5:
			case 4:
				if (children.size() > 4)
				{
					children[4].ToLong(&temp);
					icoSize->SetHeight(temp);
				}
				wxFALLTHROUGH;
			case 3:
				if (children.size() > 3)
				{
					children[3].ToLong(&temp);
					icoSize->SetWidth(temp);
				}
				wxFALLTHROUGH;
			case 2:
				if (children.size() > 1) *image = children[1];
				wxFALLTHROUGH;
			case 1:
				if (children.size() > 0) *source = children[0];
				break;
			default:
				break;
			}
		}
		else
		{
			if (children.size() == 3)
			{
				*image = children[1] + wxT(":") + children[2];
				*source = children[0];
			}
			else
			{
				*image = wxT("");
				*source = children[0];
			}
		}
		wxLogDebug(wxT("typeConv:ParseBitmap: source:%s image:%s "), source->c_str(), image->c_str());
	}

	/**
	@internal
	Used to import old projects.
	*/
	inline wxArrayString OldStringToArrayString(const wxString& str) {
		int i = 0, size = (int)str.Length(), state = 0;
		wxArrayString result;
		wxString substr;
		while (i < size)
		{
			wxChar c = str[i];
			switch (state)
			{
			case 0: // esperando (') de comienzo de cadena
				if (c == wxT('\''))
					state = 1;
				break;
			case 1: // guardando cadena
				if (c == wxT('\''))
				{
					if (i + 1 < size && str[i + 1] == wxT('\''))
					{
						substr = substr + wxT('\'');  // sustitución ('') por (') y seguimos
						i++;
					}
					else
					{
						result.Add(substr); // fin de cadena
						substr.Clear();
						state = 0;
					}
				}
				else
					substr = substr + c; // seguimos guardado la cadena

				break;
			}
			i++;
		}
		return result;
	}

	inline void SplitFileSystemURL(const wxString& url, wxString* protocol, wxString* path, wxString* anchor) {
		wxString remainder;
		if (url.StartsWith(wxT("file:"), &remainder))
		{
			*protocol = wxT("file:");
		}
		else
		{
			protocol->clear();
			remainder = url;
		}

		*path = remainder.BeforeFirst(wxT('#'));
		if (remainder.size() > path->size())
		{
			*anchor = remainder.substr(path->size());
		}
		else
		{
			anchor->clear();
		}
	}

	// Obtiene la ruta absoluta de un archivo
	inline wxString MakeAbsolutePath(const wxString& filename, const wxString& basePath) {
		wxFileName fnFile(filename);
		wxFileName noChanges = fnFile;
		if (fnFile.IsRelative())
		{
			// Es una ruta relativa, por tanto hemos de obtener la ruta completa
			// a partir de basePath
			wxFileName fnBasePath(basePath);
			if (fnBasePath.IsAbsolute())
			{
				if (fnFile.MakeAbsolute(basePath))
				{
					wxString path = fnFile.GetFullPath();
					return path;
				}
			}
		}

		// Either it is already absolute, or it could not be made absolute, so give it back - but change to '/' for separators
		wxString path = noChanges.GetFullPath();
		return path;
	}

	inline wxString MakeAbsoluteURL(const wxString& url, const wxString& basePath) {
		wxString protocol, path, anchor;
		SplitFileSystemURL(url, &protocol, &path, &anchor);
		return protocol + MakeAbsolutePath(path, basePath) + anchor;
	}

	// Obtiene la ruta relativa de un archivo
	inline wxString MakeRelativePath(const wxString& filename, const wxString& basePath) {
		wxFileName fnFile(filename);
		wxFileName noChanges = fnFile;
		if (fnFile.IsAbsolute())
		{
			wxFileName fnBasePath(basePath);
			if (fnBasePath.IsAbsolute())
			{
				if (fnFile.MakeRelativeTo(basePath))
				{
					return fnFile.GetFullPath(wxPATH_UNIX);
				}
			}
		}

		// Either it is already relative, or it could not be made relative, so give it back - but change to '/' for separators
		if (noChanges.IsAbsolute())
		{
			wxString path = noChanges.GetFullPath();
			return path;
		}
		else
		{
			return noChanges.GetFullPath(wxPATH_UNIX);
		}
	}

	inline wxString MakeRelativeURL(const wxString& url, const wxString& basePath) {
		wxString protocol, path, anchor;
		SplitFileSystemURL(url, &protocol, &path, &anchor);
		return protocol + MakeRelativePath(path, basePath) + anchor;
	}

	// dada una cadena de caracteres obtiene otra transformando los caracteres
	// especiales denotados al estilo C ('\n' '\\' '\t')
	inline wxString StringToText(const wxString& str) {
		wxString result;
		for (unsigned int i = 0; i < str.length(); i++)
		{
			wxChar c = str[i];

			switch (c)
			{
			case wxT('\n'): result += wxT("\\n");
				break;

			case wxT('\t'): result += wxT("\\t");
				break;

			case wxT('\r'): result += wxT("\\r");
				break;

			case wxT('\\'): result += wxT("\\\\");
				break;

			default:
				result += c;
				break;
			}
		}
		return result;
	}

	inline wxString TextToString(const wxString& str) {
		wxString result;

		for (unsigned int i = 0; i < str.length(); i++)
		{
			wxChar c = str[i];
			if (c == wxT('\\'))
			{
				if (i < str.length() - 1)
				{
					wxChar next = str[i + 1];

					switch (next)
					{
					case wxT('n'): result += wxT('\n');
						i++;
						break;

					case wxT('t'): result += wxT('\t');
						i++;
						break;

					case wxT('r'): result += wxT('\r');
						i++;
						break;

					case wxT('\\'): result += wxT('\\');
						i++;
						break;
					}
				}
			}
			else
				result += c;
		}

		return result;
	}

	inline number_t StringToNumber(const wxString& str) {
		number_t out;
		out.FromString(str.ToStdWstring());
		return out;
	}

	inline wxString NumberToString(const number_t& val) {
		wxString convert;
		convert << val.ToString();
		return convert;
	}
}

#endif
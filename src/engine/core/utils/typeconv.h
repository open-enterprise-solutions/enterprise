#ifndef _TYPECONV_H__
#define _TYPECONV_H__

#include "core/common/types.h"
#include "core/common/fontcontainer.h"
#include "core/compiler/compiler.h"

// macros para la conversión entre wxString <-> wxString
#define _WXSTR(x)  TypeConv::_StringToWxString(x)
#define _STDSTR(x) TypeConv::_WxStringToString(x)
#define _ANSISTR(x) TypeConv::_WxStringToAnsiString(x)

namespace TypeConv
{
	wxString _StringToWxString(const std::string& str);
	wxString _StringToWxString(const char* str);
	std::string _WxStringToString(const wxString& str);
	std::string _WxStringToAnsiString(const wxString& str);

	wxPoint StringToPoint(const wxString& str);
	bool    StringToPoint(const wxString& str, wxPoint* point);
	wxSize  StringToSize(const wxString& str);

	wxString PointToString(const wxPoint& point);
	wxString SizeToString(const wxSize& size);

	int     BitlistToInt(const wxString& str);
	int     GetMacroValue(const wxString& str);
	int     StringToInt(const wxString& str);

	bool     FlagSet(const wxString& flag, const wxString& currentValue);
	wxString ClearFlag(const wxString& flag, const wxString& currentValue);
	wxString SetFlag(const wxString& flag, const wxString& currentValue);

	wxBitmap StringToBitmap(const wxString& filename);

	wxFontContainer StringToFont(const wxString& str);
	wxString FontToString(const wxFontContainer& font);
	wxString FontFamilyToString(wxFontFamily family);
	wxString FontStyleToString(wxFontStyle style);
	wxString FontWeightToString(wxFontWeight weight);

	wxColour StringToColour(const wxString& str);
	wxSystemColour StringToSystemColour(const wxString& str);
	wxString ColourToString(const wxColour& colour);
	wxString SystemColourToString(long colour);

	bool StringToBool(const wxString& str);
	wxString BoolToString(bool val);

	wxArrayString StringToArrayString(const wxString& str);
	wxString ArrayStringToString(const wxArrayString& arrayStr);

	void ParseBitmapWithResource(const wxString& value, wxString* image, wxString* source, wxSize* icoSize);

	/**
	@internal
	Used to import old projects.
	*/
	wxArrayString OldStringToArrayString(const wxString& str);

	wxString ReplaceSynonymous(const wxString& bitlist);

	void SplitFileSystemURL(const wxString& url, wxString* protocol, wxString* path, wxString* anchor);

	// Obtiene la ruta absoluta de un archivo
	wxString MakeAbsolutePath(const wxString& filename, const wxString& basePath);
	wxString MakeAbsoluteURL(const wxString& url, const wxString& basePath);

	// Obtiene la ruta relativa de un archivo
	wxString MakeRelativePath(const wxString& filename, const wxString& basePath);
	wxString MakeRelativeURL(const wxString& url, const wxString& basePath);

	// dada una cadena de caracteres obtiene otra transformando los caracteres
	// especiales denotados al estilo C ('\n' '\\' '\t')
	wxString StringToText(const wxString& str);
	wxString TextToString(const wxString& str);

	number_t StringToNumber(const wxString& str);
	wxString NumberToString(const number_t& val);
}

// No me gusta nada tener que usar variables globales o singletons
// pero hasta no dar con otro diseño más elegante seguiremos con este...
// TO-DO: incluirlo en GlobalApplicationData
class MacroDictionary
{
private:
	typedef std::map<wxString, int> MacroMap;
	static MacroDictionary* s_instance;

	typedef std::map<wxString, wxString> SynMap;

	MacroMap m_map;
	SynMap m_synMap;

	MacroDictionary();

public:
	static MacroDictionary* GetInstance();
	static void Destroy();
	bool SearchMacro(const wxString& name, int* result);
	void AddMacro(const wxString& name, int value);
	void AddSynonymous(const wxString& synName, const wxString& name);
	bool SearchSynonymous(const wxString& synName, wxString& result);
};

#endif
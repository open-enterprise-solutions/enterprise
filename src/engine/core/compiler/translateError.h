#ifndef _FUNCTIONS_H__
#define _FUNCTIONS_H__

#include <wx/wx.h>

class CTranslateError : public std::exception {
	static wxString s_curError;
	static bool s_simpleMode;
private:
#if !wxUSE_UTF8_LOCALE_ONLY
	static wxString DoFormatWchar(const wxChar* format, ...);
	static void DoErrorWchar(const wxChar* format, ...);
#endif
#if wxUSE_UNICODE_UTF8
	static wxString DoFormatWchar(const wxChar* format, ...);
	static void DoErrorUtf8(const wxChar* format, ...);
#endif
	//служебные процедуры обработки ошибок
	static void ErrorV(const wxString& fmt, va_list& list);
	static wxString FormatV(const wxString& fmt, va_list& list);
	static const wxString& GetErrorDesc(int codeError);
public:

	class wxFormatErrorString : public wxFormatString {

	public:
		wxFormatErrorString(int codeError)
			: wxFormatString(GetErrorDesc(codeError)) {}

#ifndef wxNO_IMPLICIT_WXSTRING_ENCODING
		wxFormatErrorString(const char* str)
			: wxFormatString(str) {}
#endif
		wxFormatErrorString(const wchar_t* str)
			: wxFormatString(str) {}
		wxFormatErrorString(const wxString& str)
			: wxFormatString(str) {}
		wxFormatErrorString(const wxCStrData& str)
			: wxFormatString(str) {}
#ifndef wxNO_IMPLICIT_WXSTRING_ENCODING
		wxFormatErrorString(const wxScopedCharBuffer& str)
			: wxFormatString(str) {}
#endif
		wxFormatErrorString(const wxScopedWCharBuffer& str)
			: wxFormatString(str) {}

	};

	CTranslateError(const wxString& errorString);

	//error from proc unit/compile module 
	static void ProcessError(const struct byteRaw_t& error, const wxString& descError);
	static void ProcessError(const wxString& fileName,
		const wxString& moduleName, const wxString& docPath,
		unsigned int currPos, unsigned int currLine,
		const wxString& codeLineError, int codeError, const wxString& descError // error code from compile codule
	);

	WX_DEFINE_VARARG_FUNC(static wxString, Format, 1, (const wxFormatErrorString&),
		DoFormatWchar, DoFormatUtf8);

	WX_DEFINE_VARARG_FUNC(static void, Error, 1, (const wxFormatErrorString&),
		DoErrorWchar, DoErrorUtf8);

	static wxString FindErrorCodeLine(const wxString& sBuffer, int nCurPos);
	static wxString GetLastError() {
		wxString lastError = s_curError;
		s_curError = wxEmptyString;
		return lastError;
	}

	static void SetSimpleMode(bool mode = true) {
		s_simpleMode = mode;
	}

	static bool IsSimpleMode() {
		return s_simpleMode;
	}
};

class CInterruptBreak : public CTranslateError
{
public:
	CInterruptBreak() :
		CTranslateError(_("The program was stopped by the user!")) {
	}
};

#endif 
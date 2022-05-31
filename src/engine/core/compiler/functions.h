#ifndef _FUNCTIONS_H__
#define _FUNCTIONS_H__

#include <wx/wx.h>

class CTranslateError : public std::exception
{
	static wxString m_sCurError;
	static bool bSimpleMode;

private:

	//служебные процедуры обработки ошибок
	static void ErrorV(const wxString &fmt, va_list &list);

	//format function
	static wxString Format(const wxString fmt, ...);
	static wxString Format(int nErr, ...);

	static wxString FormatV(const wxString &fmt, va_list &list);

public:

	CTranslateError(const wxString &errorString);

	//error from proc unit/compile module 
	static void ProcessError(const struct CByte &error, const wxString &descError);

	static void ProcessError(const wxString &fileName,
		const wxString &moduleName, const wxString &docPath,
		unsigned int currPos, unsigned int currLine,
		const wxString &codeLineError, int codeError, const wxString &descError // error code from compile codule
	);

	//special error functions
	static void Error(const wxString fmt, ...);
	static void Error(int nErr, ...);

	static wxString FindErrorCodeLine(const wxString &sBuffer, int nCurPos);
	static wxString GetLastError() {
		wxString lastError = m_sCurError;
		m_sCurError = wxEmptyString;
		return lastError;
	}

	static void ActivateSimpleMode() { bSimpleMode = true; }
	static void DeaсtivateSimpleMode() { bSimpleMode = false; }

	static bool IsSimpleMode() { return bSimpleMode; }
};

class CInterruptBreak : public CTranslateError
{
public:
	CInterruptBreak() : CTranslateError(_("The program was stopped by the user!")) {}
};

#endif 
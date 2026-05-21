#ifndef _backend_exception_h__
#define _backend_exception_h__

enum { //Error message numbers
	ERROR_USAGE = 0,
	ERROR_FILE_READ,
	ERROR_FILE_OPEN,
	ERROR_ASSERT,
	ERROR_ASSERT_VALID,
	ERROR_SYS1,
	ERROR_TRANSLATE_BYTE,
	ERROR_TRANSLATE_BOOLEAN,
	ERROR_TRANSLATE_WORD,
	ERROR_TRANSLATE_NUMBER,
	ERROR_TRANSLATE_STRING,
	ERROR_TRANSLATE_DATE,
	ERROR_IDENTIFIER_DUPLICATE,
	ERROR_LABEL_DEFINE,
	ERROR_KEYWORD_DEFINE,
	ERROR_CODE_DEFINE,
	ERROR_IDENTIFIER_DEFINE,
	ERROR_IDENTIFIER_REGION,
	ERROR_CODE,
	ERROR_DELIMETER,
	ERROR_FUNC_DELIMETER,
	ERROR_FUNC_DEFINE,
	ERROR_RETURN,
	ERROR_CONST_DEFINE,
	ERROR_ENDFUNC_DEFINE,
	ERROR_FILE_WRITE,
	ERROR_EXPRESSION,
	ERROR_KEYWORD,
	ERROR_IDENTIFIER_NOT_FOUND,
	ERROR_USE_BREAK,
	ERROR_USE_CONTINUE,
	ERROR_USE_RETURN,
	ERROR_USE_BLOCK,
	ERROR_EXPRESSION_REQUIRE,
	ERROR_CALL_FUNCTION,
	ERROR_DEF_VARIABLE,
	ERROR_DEF_FUNCTION,
	ERROR_MANY_PARAMS,
	ERROR_FEW_PARAMS,
	ERROR_VAR_NOT_FOUND,
	ERROR_END_PROGRAM,
	ERROR_ONLY_FUNCTION,
	ERROR_USE_PROCEDURE_AS_FUNCTION,
	ERROR_ARRAY_SIZE_CONST,
	ERROR_DUBLICATE_IMPORT,
	ERROR_MODULE_NOT_FOUND,
	ERROR_USE_IMPORT,
	ERROR_USE_ENDDEF,
	ERROR_USE_ENDREGION,
	ERROR_CALL_CONSTRUCTOR,
	ERROR_TYPE_DEF,
	ERROR_BAD_TYPE,
	ERROR_BAD_TYPE_EXPRESSION,
	ERROR_NUMBER_TYPE,
	ERROR_BAD_TYPE_EXPRESSION_B,
	ERROR_BAD_TYPE_EXPRESSION_N,
	ERROR_BAD_TYPE_EXPRESSION_S,
	ERROR_BAD_TYPE_EXPRESSION_D,
	ERROR_TYPE_OPERATION,

	LastError
};

#include "backend/backend.h"

class BACKEND_API ibBackendException {
protected:

	class wxFormatErrorString : public wxFormatString {
	public:
		wxFormatErrorString(int codeError)
			: wxFormatString(ibBackendException::GetErrorDesc(codeError)) {
		}

#ifndef wxNO_IMPLICIT_WXSTRING_ENCODING
		wxFormatErrorString(const char* str)
			: wxFormatString(str) {
		}
#endif
		wxFormatErrorString(const wchar_t* str)
			: wxFormatString(str) {
		}
		wxFormatErrorString(const wxString& str)
			: wxFormatString(str) {
		}
		wxFormatErrorString(const wxCStrData& str)
			: wxFormatString(str) {
		}
#ifndef wxNO_IMPLICIT_WXSTRING_ENCODING
		wxFormatErrorString(const wxScopedCharBuffer& str)
			: wxFormatString(str) {
		}
#endif
		wxFormatErrorString(const wxScopedWCharBuffer& str)
			: wxFormatString(str) {
		}
	};

	ibBackendException(const wxString& strErrorDescription);

public:

	// Thrown by value, caught by const reference. Virtual destructor keeps
	// polymorphic dynamic-cast/catch-by-base behaviour well-defined across
	// ibBackendCoreException / ibBackendInterruptException / ibBackendAccessException.
	virtual ~ibBackendException() = default;

	WX_DEFINE_VARARG_FUNC(static wxString, Format, 1, (const wxFormatErrorString&),
		DoFormatWchar, DoFormatUtf8);

	//get error description
	const wxString GetErrorDescription() const { return m_strErrorDescription; }

	//error from proc unit/compile module
	static void ProcessError(const ibBackendException& err, const struct ibByteUnit& error);
	static void ProcessError(const wxString& strFileName,
		const wxString& strModuleName, const wxString& strDocPath,
		const unsigned int currPos, const unsigned int currLine,
		const wxString& strCodeLineError, const int codeError, const wxString& strErrorDesc // error code from compile module
	);

	static wxString FindErrorCodeLine(const wxString& sBuffer, unsigned int currPos);
	static wxString GetLastError() {
		const wxString strLastError = ms_strError;
		ms_strError = wxEmptyString;
		return strLastError;
	}

	static bool IsErrorOutputProcessing();

	static void SetEvalMode(bool mode = true);
	static bool IsEvalMode();

	// Saves the current eval-mode flag in ctor, restores it in dtor.
	// Use to wrap ibProcUnit::Evaluate so an inner throw doesn't leak
	// the flag onto the session.
	class ibEvalModeScope {
	public:
		explicit ibEvalModeScope(bool newMode = true)
			: m_previous(ibBackendException::IsEvalMode())
		{
			if (m_previous != newMode)
				ibBackendException::SetEvalMode(newMode);
		}

		~ibEvalModeScope() {
			if (ibBackendException::IsEvalMode() != m_previous)
				ibBackendException::SetEvalMode(m_previous);
		}

		ibEvalModeScope(const ibEvalModeScope&) = delete;
		ibEvalModeScope& operator=(const ibEvalModeScope&) = delete;

	private:
		const bool m_previous;
	};

protected:

	static wxString FormatV(const wxString& fmt, va_list& list);
	static wxString ms_strError;

private:

	//error handling routines
	static const wxString& GetErrorDesc(int codeError);
	static wxString ProcessExceptionError(const wxString& strFileName,
		const wxString& strModuleName, const wxString& strDocPath,
		const unsigned int currPos, const unsigned int currLine,
		const wxString& strCodeLineError, const int codeError, const wxString& strErrorDesc // error code from compile module
	);

#if !wxUSE_UTF8_LOCALE_ONLY
	static wxString DoFormatWchar(const wxChar* format, ...);
#endif
#if wxUSE_UNICODE_UTF8
	static wxString DoFormatWchar(const wxChar* format, ...);
#endif

	mutable bool m_errorHandled;
	wxString m_strErrorDescription;
};

#pragma region _exception_h_

class BACKEND_API ibBackendCoreException : public ibBackendException {
protected:
	ibBackendCoreException(const wxString& strErrorDescription) : ibBackendException(strErrorDescription) {}
public:

	WX_DEFINE_VARARG_FUNC(static void, Error, 1, (const wxFormatErrorString&),
		DoErrorWchar, DoErrorUtf8);

private:

#if !wxUSE_UTF8_LOCALE_ONLY
	static void DoErrorWchar(const wxChar* format, ...);
#endif
#if wxUSE_UNICODE_UTF8
	static void DoErrorUtf8(const wxChar* format, ...);
#endif
};

class BACKEND_API ibBackendInterruptException : public ibBackendException {
	ibBackendInterruptException() : ibBackendException(_("The program was stopped by the user!")) {}
public:
	static void Error();
};

class BACKEND_API ibBackendAccessException : public ibBackendException {
	ibBackendAccessException() : ibBackendException(_("Not enough access rights for this user!")) {}
public:
	static void Error();
};

// Test-assertion failure. Distinct subclass so the test runner can catch
// it without intercepting business-logic ibBackendCoreException throws
// from inside the procedure under test. Carries enough structured detail
// (assertion name, actual + expected, optional message) for the run_tests
// MCP envelope to surface the failure shape, not just a flat string.
class BACKEND_API ibBackendTestAssertException : public ibBackendException {
public:
	// Build a fully-formed test-assert exception and throw it. Stored fields
	// power GetErrorDescription() + the structured failure block in the
	// run_tests envelope.
	static void Error(const wxString& assertion,
	                  const wxString& actualText,
	                  const wxString& expectedText,
	                  const wxString& message);

	const wxString& GetAssertion()    const { return m_assertion; }
	const wxString& GetActualText()   const { return m_actualText; }
	const wxString& GetExpectedText() const { return m_expectedText; }
	const wxString& GetMessage()      const { return m_message; }

private:
	ibBackendTestAssertException(const wxString& assertion,
	                             const wxString& actualText,
	                             const wxString& expectedText,
	                             const wxString& message,
	                             const wxString& formatted);

	wxString m_assertion;
	wxString m_actualText;
	wxString m_expectedText;
	wxString m_message;
};

#pragma endregion

#endif
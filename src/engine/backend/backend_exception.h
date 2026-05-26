#ifndef _backend_exception_h__
#define _backend_exception_h__

#include <vector>

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

	// Out-of-line — BACKEND_API class + inline body decays to
	// dllimport-only in consumer TUs. When the call sits inside a
	// lambda (e.g. cpp-httplib's set_exception_handler in wes/main.cpp)
	// MSVC can decline to inline and then the linker hunts the symbol
	// in backend.dll's export table, where the inline never got
	// emitted. Defining the body in .cpp guarantees the export.
	const wxString GetErrorDescription() const;

	//error from proc unit/compile module
	static void ProcessError(const ibBackendException& err, const struct ibByteUnit& error);
	static void ProcessError(const wxString& strFileName,
		const wxString& strModuleName, const wxString& strDocPath,
		const unsigned int currPos, const unsigned int currLine,
		const wxString& strCodeLineError, const int codeError, const wxString& strErrorDesc // error code from compile module
	);

	static wxString FindErrorCodeLine(const wxString& sBuffer, unsigned int currPos);

	// Returns the most recent error description and clears it. Kept for
	// the legacy callers (mainApp.cpp's startup `appDataCreate*` failure
	// path); new code should prefer GetLastErrorChain / DrainLastErrors
	// to see the whole sequence of failures that led to the visible one.
	static wxString GetLastError();

	// Per-thread error chain. Every ibBackendException ctor pushes its
	// message onto the calling thread's chain; readers drain it after
	// a failed startup / login / metadata-load attempt to display ALL
	// recorded reasons, not just the last one. Previous single-string
	// ms_strError lost information whenever a catch (...) {} site
	// constructed a fresh wrapped exception, masking the root cause.
	static std::vector<wxString> DrainLastErrors();

	// Read-only peek — does not clear. Useful for diagnostics dialogs
	// that want to surface the chain without consuming it.
	static std::vector<wxString> PeekLastErrors();

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

	// Push a freshly-constructed error onto the calling thread's chain.
	// Called from ibBackendException's ctor — every thrown exception
	// adds itself to the per-thread history. The chain is bounded
	// (oldest entries dropped beyond the cap) to keep memory predictable
	// when a runaway loop keeps throwing.
	static void PushLastError(const wxString& description);

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

// Concurrent-write protection failures surfaced by the Write-time
// DataVersion check or the DB-side row-lock acquisition.
// See docs/record-locks.md.
class BACKEND_API ibBackendLockException : public ibBackendException {
public:
	enum class Kind {
		// DataVersion stored on the row does not match the version
		// the caller loaded — somebody else wrote between this
		// caller's Read and Write. UI: "Object was changed by
		// another user, please reload".
		VersionChanged,

		// SELECT ... FOR UPDATE / WITH LOCK acquisition timed out
		// or returned a NOWAIT lock-conflict. UI: "Object is being
		// edited by another user, try again later".
		RowLockTimeout,

		// sys_lock acquire failed — another session holds a
		// conflicting lock on the same (namespace, key). Long-held
		// pessimistic lock (form-open by another user, etc.).
		// UI: "Object is locked by user X".
		LockConflict,
	};

	Kind GetKind() const { return m_kind; }

	// Identity of the session/user blocking us. Populated only on
	// Kind::LockConflict — empty for VersionChanged / RowLockTimeout.
	// Soft-lock UI uses this to render the form-caption badge without
	// parsing the message string.
	const wxString& GetBlockingUser() const { return m_blockingUser; }
	const wxString& GetObjectName()   const { return m_objectName; }

	// Throw a VersionChanged kind. Message is formatted as
	// "<objectSynonym>: data version changed (expected <expected>,
	// found <actual>)" — caller may show the synonym to user, the
	// version strings are diagnostic-only.
	[[noreturn]] static void VersionChangedThrow(const wxString& objectSynonym,
	                                              const wxString& expected,
	                                              const wxString& actual);

	// Throw a RowLockTimeout kind. Message includes the synonym; the
	// DB driver-level reason (timeout / NOWAIT conflict) is logged
	// via ibBackendException::ProcessError chain.
	[[noreturn]] static void RowLockTimeoutThrow(const wxString& objectSynonym);

	// Throw a LockConflict kind. Used by ibLockManager::Acquire when
	// another session holds a conflicting sys_lock row on the same
	// (namespace, key). Message reads "<objectName> is locked by
	// user <blockingUser>" — UI surfaces it directly.
	[[noreturn]] static void LockConflictThrow(const wxString& objectName,
	                                            const wxString& blockingUser);

private:
	ibBackendLockException(Kind kind, const wxString& msg)
		: ibBackendException(msg), m_kind(kind) {}
	ibBackendLockException(Kind kind, const wxString& msg,
	                       const wxString& objectName,
	                       const wxString& blockingUser)
		: ibBackendException(msg), m_kind(kind),
		  m_objectName(objectName), m_blockingUser(blockingUser) {}
	Kind m_kind;
	wxString m_objectName;
	wxString m_blockingUser;
};

// DB-tier exception classes (ibBackendDatabaseException +
// ibDatabaseLayerException) live in
// backend/databaseLayer/databaseLayerException.h — co-located with the
// rest of the database layer code. They derive from ibBackendException
// (declared here) but are not visible from this header. Code that
// needs to throw or specifically catch DB-tier failures includes
// `backend/databaseLayer/databaseLayerException.h`; everyone else
// catches at `ibBackendException` and is content with the message.

#pragma endregion

#endif
////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, 2�-team
//	Description : translate error and exception handler 
////////////////////////////////////////////////////////////////////////////

#include "backend/backend_exception.h"

#include "backend/backend_diagnostic.h"   // the failure as DATA — built here, text assembled from it
#include "backend/metadataConfiguration.h"
#include "backend/debugger/debugServer.h"
#include "backend/appData.h"
#include "backend/compiler/procUnit.h"
#include "backend/session/session.h"

#include "backend_mainFrame.h"

#include <mutex>
#include <thread>
#include <unordered_map>

namespace {
// Per-thread error history, kept in ONE process-wide registry keyed by thread
// id — deliberately not a thread_local.
//
// A thread_local with a non-trivial destructor is constructed by __dyn_tls_init
// at DLL_THREAD_ATTACH, for every thread, whether it ever raises an error or
// not, and each one costs an 8-byte destructor-registration node. A thread that
// is still running at process exit is killed without TLS teardown, so that node
// is never returned: measured 2026-07-30 as six leaked blocks, one per thread
// outliving shutdown. Per-thread state has no owner, and nothing without an
// owner can be cleaned up.
//
// A static registry has one: the CRT destroys it during exit, and the whole
// class of leak goes with it. The cost is a lock on the error path, which is by
// definition not hot.
//
// The chain is bounded to keep a runaway-throw loop (compile-side runaway, an
// FB error inside a reconnect-retry loop) from growing without limit; 64
// entries cover any startup or login sequence we want to surface to the user.
constexpr std::size_t kErrorChainMax = 64;

// Same shape as the current-session binding in session.cpp: a namespace-scope
// lock plus a map keyed by thread id. Trivially destructible flag declared
// FIRST, so it outlives the map — a thread still running during shutdown reads
// it, sees the registry is gone, and drops its error rather than touching a
// destroyed container. That window is not hypothetical here: threads outliving
// exit are exactly what this registry exists to survive.
bool s_errorsAlive = false;

std::mutex s_errorMutex;
std::unordered_map<std::thread::id, std::vector<wxString>> s_errorsByThread;

struct ibErrorsLifetime {
	ibErrorsLifetime() { s_errorsAlive = true; }
	~ibErrorsLifetime() { s_errorsAlive = false; }
} s_errorsLifetime;
} // namespace

void ibBackendException::PushLastError(const wxString& description)
{
	if (description.IsEmpty() || !s_errorsAlive) return;

	std::lock_guard<std::mutex> lock(s_errorMutex);
	std::vector<wxString>& chain = s_errorsByThread[std::this_thread::get_id()];
	if (chain.size() >= kErrorChainMax)
		chain.erase(chain.begin());
	chain.push_back(description);
}

wxString ibBackendException::GetLastError()
{
	// Drain semantics preserved for legacy callers (mainApp's
	// appDataCreate* failure path). Returns the most recent entry and
	// clears the chain — same one-string surface as before.
	if (!s_errorsAlive) return wxEmptyString;

	std::lock_guard<std::mutex> lock(s_errorMutex);
	auto found = s_errorsByThread.find(std::this_thread::get_id());
	if (found == s_errorsByThread.end() || found->second.empty()) return wxEmptyString;
	wxString last = found->second.back();
	// Drop the whole entry, not just its contents: the map is then bounded by
	// threads holding a PENDING error, not by every thread ever seen.
	s_errorsByThread.erase(found);
	return last;
}

std::vector<wxString> ibBackendException::DrainLastErrors()
{
	std::vector<wxString> out;
	if (!s_errorsAlive) return out;

	std::lock_guard<std::mutex> lock(s_errorMutex);
	auto found = s_errorsByThread.find(std::this_thread::get_id());
	if (found != s_errorsByThread.end()) {
		out.swap(found->second);
		s_errorsByThread.erase(found);
	}
	return out;
}

std::vector<wxString> ibBackendException::PeekLastErrors()
{
	if (!s_errorsAlive) return {};

	std::lock_guard<std::mutex> lock(s_errorMutex);
	auto found = s_errorsByThread.find(std::this_thread::get_id());
	return found != s_errorsByThread.end() ? found->second : std::vector<wxString>();
}

//////////////////////////////////////////////////////////////////////
//					List of error messages							//
//////////////////////////////////////////////////////////////////////
static wxString gs_listErrorString[] =
{
	_("Usage: %s <filename>"),
	_("Error reading file %s"),
	_("Error opening file %s"),
	_("ASSERT: Module system error %s in line %d"),
	_("ASSERT_VALID: Module system error %s in line %d"),
	_("System error(out of array) when trying to process an error with a number %d"),
	_("Symbol expected :\n%s"),
	_("Word expected :\n%s"),
	_("Constant boolean expected:\n%s"),
	_("Constant number expected:\n%s"),
	_("Constant string expected:\n%s"),
	_("Constant date expected:\n%s"),
	_("Duplicate identifier %s"),//ERROR_IDENTIFIER_DUPLICATE
	_("Label '%s' not defined"),
	_("One of the keywords is expected!"),
	_("Module code expected"),
	_("Identifier expected"),//ERROR_IDENTIFIER_DEFINE
	_("Region name expected"),//ERROR_IDENTIFIER_REGION
	_("Keyword or identifier expected"),//ERROR_CODE
	_("Symbol expected '%s'"),//ERROR_DELIMETER
	_("Closing parenthesis or comma expected"),//ERROR_FUNC_DELIMETER
	_("Function or procedure declaration keyword expected"),//ERROR_FUNC_DEFINE
	_("Module cannot have a return statement"),//ERROR_RETURN
	_("Expected constant"),//ERROR_CONST_DEFINE
	_("An operator is expected to complete a procedure or function!"),//ERROR_ENDFUNC_DEFINE
	_("Error writing file: %s"),//ERROR_FILE_WRITE
	_("Error in expression:\n%s"),//ERROR_EXPRESSION
	_("Keyword expected %s"),//ERROR_KEYWORD
	_("Identifier '%s' not found"),//ERROR_IDENTIFIER_NOT_FOUND
	_("Operator Break can be used only inside the cycle"),//ERROR_USE_BREAK
	_("Operator Continue can be used only inside the cycle"),//ERROR_USE_CONTINUE
	_("Operator Return cannot be used outside the procedure or function"),//ERROR_USE_RETURN
	_("Expected program operators"),//ERROR_USE_BLOCK
	_("Expected expression"),//ERROR_EXPRESSION_REQUIRE
	_("Procedure or function not detected (%s)"),//ERROR_CALL_FUNCTION
	_("Variable with the specified name is already defined (%s)"),//ERROR_DEF_VARIABLE
	_("A procedure or function with the specified name is already defined (%s)"),//ERROR_DEF_FUNCTION
	_("Too many parameters"),//ERROR_MANY_PARAMS
	_("Not enough parameters"),//ERROR_FEW_PARAMS
	_("Var is not found (%s)"),//ERROR_VAR_NOT_FOUND
	_("Unexpected program code termination"),//ERROR_END_PROGRAM
	_("This module may contain only definitions of procedures and functions"), //ERROR_ONLY_FUNCTION
	_("Use procedure as function (%s)"), //ERROR_USE_PROCEDURE_AS_FUNCTION
	_("Expected integer positive sign constant"),//ERROR_ARRAY_SIZE_CONST
	_("Re-import the parent module"),//ERROR_DUBLICATE_IMPORT
	_("Module not found"),//ERROR_MODULE_NOT_FOUND
	_("The import statement must be at the beginning of the module"),//ERROR_USE_IMPORT
	_("Final conditional compilation statement expected"),//ERROR_USE_ENDDEF
	_("A pending region statement is expected"),//ERROR_USE_ENDREGION

	_("Constructor not found (%s)"),//ERROR_CALL_CONSTRUCTOR

	_("Type error define"),//ERROR_TYPE_DEF
	_("Bad variable type"),//ERROR_BAD_TYPE
	_("Bad value type"),//ERROR_BAD_TYPE_EXPRESSION
	_("Variable must be a numeric type"),//ERROR_NUMBER_TYPE

	_("Boolean value expected"),//ERROR_BAD_TYPE_EXPRESSION_B
	_("Numeric value expected"),//ERROR_BAD_TYPE_EXPRESSION_N
	_("String value expected"),//ERROR_BAD_TYPE_EXPRESSION_S
	_("Date value expected"),//ERROR_BAD_TYPE_EXPRESSION_D

	_("Variable type does not support this operation"),//ERROR_TYPE_OPERATION

	// --- runtime (the interpreting loop) — lock-step with the enum ----------
	_("Divide by zero"),//ERROR_DIVIDE_BY_ZERO
	_("Cannot set array value '%s'"),//ERROR_ARRAY_SET
	_("Cannot get array value '%s'"),//ERROR_ARRAY_GET
	_("Object field not writable (%s)"),//ERROR_PROP_NOT_WRITABLE
	_("Object field not readable (%s)"),//ERROR_PROP_NOT_READABLE
	_("Object field is scope-local (%s)"),//ERROR_PROP_SCOPE_LOCAL
	_("No attribute or method found '%s' - a variable is not an aggregate object"),//ERROR_MEMBER_NOT_AGGREGATE
	_("Aggregate object field not found '%s'"),//ERROR_MEMBER_NOT_FOUND
};

//////////////////////////////////////////////////////////////////////

// Eval-mode and processing-backend-error flags moved from thread_local
// onto ibSession (m_evalMode, m_processingBackendError). Per-session
// scope means three sessions in debug-watch don't silence the other
// two running regular business logic, and a debug worker thread can
// read the parked session's eval-mode through Current() redirect
// instead of seeing its own (always-false) thread_local.

//////////////////////////////////////////////////////////////////////
// Error handling
//////////////////////////////////////////////////////////////////////

const wxString ibBackendException::GetErrorDescription() const
{
	return wxString::FromUTF8(m_errorDescriptionUtf8);
}

const char* ibBackendException::what() const noexcept
{
	// The stored bytes themselves — nothing is built here, which is what lets this be noexcept.
	return m_errorDescriptionUtf8.c_str();
}

ibBackendException::ibBackendException(const wxString& strErrorDescription)
	: m_errorHandled(false), m_errorDescriptionUtf8(strErrorDescription.utf8_string())
{
#ifdef DEBUG
	ibJournalInfo(wxT("exception"), wxT("%s"), strErrorDescription);
#endif // !DEBUG

	// EVERY refusal, in the same file as the ids and the SQL — the point of the trace is the ORDER of
	// events, and an error that goes only to the log pane cannot be lined up against the statement
	// that caused it. Written at CONSTRUCTION rather than at the catch site, so it records the ones
	// that are handled and swallowed too — which is exactly the class of thing this trace exists for.

	if (auto* puState = ibSession::GetPUState())
		puState->Raise();

	// Append to the calling thread's chain so consumers can see the
	// full sequence of failures that produced the visible one. Previous
	// single-string ms_strError dropped every prior failure on the next
	// throw, leaving the user with a misleading "last in wins" message.
	PushLastError(strErrorDescription);
}

#include "backend/metaCollection/metaModuleObject.h"

void ibBackendException::ProcessError(const ibBackendException& err, const ibByteUnit& error)
{
	const bool isEvalMode = ibBackendException::IsEvalMode();

	const wxString& strFileName = error.m_strFileName;
	const wxString& strModuleName = error.m_strModuleName;
	const wxString& strDocPath = error.m_strDocPath;

	if (!err.m_errorHandled) {

		if (activeMetaData != nullptr) {

			wxString strModuleData;

			if (!isEvalMode && strFileName.IsEmpty()) {
				const ibGuid& guidDocPath = error.m_strDocPath;
				const ibValueMetaObjectModuleBase* foundedDoc = activeMetaData->FindAnyObjectByFilter<ibValueMetaObjectModuleBase>(guidDocPath, true);
				wxASSERT(foundedDoc);
				strModuleData = foundedDoc->GetModuleText();
			}
			else if (!isEvalMode && !strFileName.IsEmpty()) {
				// Frame from the session's CurrentFrame() shortcut —
				// reaches this thread's pinned session via worker scope.
				if (auto* frame = ibSession::CurrentFrame()) {
					const ibMetaData* metadata = frame->FindMetadataByPath(strFileName);
					wxASSERT(metadata);
					const ibGuid& guidDocPath = error.m_strDocPath;
					const ibValueMetaObjectModuleBase* foundedDoc = metadata->FindAnyObjectByFilter<ibValueMetaObjectModuleBase>(guidDocPath, true);
					wxASSERT(foundedDoc);
					strModuleData = foundedDoc->GetModuleText();
				}
			}

			const wxString strCodeError = isEvalMode ? wxString(wxEmptyString) :
				ibBackendException::FindErrorCodeLine(strModuleData, error.m_numString);

			ibBackendException::ProcessExceptionError(strFileName,
				strModuleName, strDocPath,
				error.m_numString, isEvalMode ? error.m_numLine : error.m_numLine + 1,
				strCodeError, wxNOT_FOUND, err.GetErrorDescription(),
				ibDiagnosticKind::Runtime   // reached from procUnit's catch — the code ran
			);
		}
		else {

			ibBackendException::ProcessExceptionError(strFileName,
				strModuleName, strDocPath,
				error.m_numString, error.m_numLine + 1,
				wxEmptyString, wxNOT_FOUND, err.GetErrorDescription(),
				ibDiagnosticKind::Runtime
			);
		}

		err.m_errorHandled = true;
	}

	// Rethrow the in-flight exception — ProcessError is always called from a
	// catch block in procUnit, so `throw;` keeps the same object propagating
	// (preserving m_errorHandled) without allocating a new copy.
	throw;
}

void ibBackendException::ProcessError(const wxString& strFileName,
	const wxString& strModuleName, const wxString& strDocPath,
	const unsigned int currPos, const unsigned int currLine,
	const wxString& strCodeError, const int codeError, const wxString& strErrorDesc)
{
	//throw this exception
	// COMPILE side: the only caller is ibCompileCode::DoSetError, i.e. the text
	// was refused before it ever ran.
	ibBackendCoreException::Error(
		ibBackendException::ProcessExceptionError(strFileName, strModuleName, strDocPath, currPos, currLine, strCodeError, codeError, strErrorDesc,
			ibDiagnosticKind::Compile));
}

////////////////////////////////////////////////////////////////////

wxString ibBackendException::ProcessExceptionError(const wxString& strFileName,
	const wxString& strModuleName, const wxString& strDocPath,
	const unsigned int currPos, const unsigned int currLine,
	const wxString& strCodeError, const int codeError, const wxString& strErrorDesc,
	const ibDiagnosticKind kind)
{
	const bool isEvalMode = ibBackendException::IsEvalMode();

	// THE RECORD FIRST, the sentence after. Everything below — the message, the
	// call-stack text, the line the dialog jumps to — is assembled from these
	// fields, so a reader of the record and a reader of the message can never
	// be told two different things.
	ibDiagnostic diagnostic;
	diagnostic.m_kind = kind;
	diagnostic.m_fileName = strFileName;
	diagnostic.m_moduleName = strModuleName;
	diagnostic.m_docPath = strDocPath;
	diagnostic.m_line = currLine;
	diagnostic.m_position = currPos;
	diagnostic.m_code = codeError;
	diagnostic.m_message = codeError > 0 ? ibBackendException::Format(codeError, strErrorDesc) : strErrorDesc;
	diagnostic.m_codeLine = isEvalMode ? wxString(wxEmptyString) : strCodeError;

	// THE STACK IS COLLECTED WHETHER OR NOT ANYONE IS LOOKING. It used to be
	// gathered inside the "is there a frame" branch, so a failure in a job, a
	// background run or a headless check — precisely the ones nobody watches —
	// reported no stack at all. Walking it costs a few string formats, and only
	// on a path that has already failed.
	if (!isEvalMode) {
		auto* puState = ibSession::GetPUState();
		const unsigned int frameCount = puState ? puState->GetCountRunContext() : 0;
		for (unsigned int i = 0; i < frameCount; i++) {
			const ibRunContext* stackContext = puState->GetRunContext(i);
			wxASSERT(stackContext);
			const ibByteCode* stackByteCode = stackContext->GetByteCode();
			wxASSERT(stackByteCode);
			ibDiagnostic::Frame frame;
			frame.m_module = stackByteCode->m_strModuleName;
			frame.m_line = stackByteCode->m_listCode[stackContext->m_lCurLine].m_numLine + 1;
			diagnostic.m_stack.push_back(std::move(frame));
		}
	}

	ibDiagnostics::Publish(diagnostic);

	wxString strErrorMessage;
	strErrorMessage += wxT("{") + strModuleName + wxT("(") + (isEvalMode ? wxString(wxT(" ")) : wxString::Format(wxT("%i"), currLine)) + wxT(")}: ");
	strErrorMessage += diagnostic.m_message + wxT("\n");
	strErrorMessage += diagnostic.m_codeLine;

	if (isEvalMode) strErrorMessage.Replace(wxT('\n'), wxT(' '));

	if (!isEvalMode) {

		// Frame via the session pinned by the worker scope — single
		// canonical entry point through ibSession::CurrentFrame.
		// Null when no scope is active or session has no UI.
		auto* frame = ibSession::CurrentFrame();

		if (frame != nullptr) {
			// The call-stack TEXT, rendered from the record above.
			wxString strStackMessage;
			for (std::size_t i = 0; i < diagnostic.m_stack.size(); i++) {
				strStackMessage += wxString::Format(wxT("\n%i: %s (#line %d)"),
					static_cast<int>(i) + 1,
					diagnostic.m_stack[i].m_module,
					diagnostic.m_stack[i].m_line
				);
			}

			if (auto* sess = ibSession::Current())
				sess->SetProcessingBackendError(true);

			//show message
			frame->BackendError(
				strFileName,
				strDocPath,
				currLine,
				strErrorMessage + (strStackMessage.IsEmpty() ? wxT("") : wxT("\n\nCall stack:") + strStackMessage)
			);

			if (auto* sess = ibSession::Current())
				sess->SetProcessingBackendError(false);
		}
	}

	// Push the processed message (with module / line decoration) onto
	// the per-thread chain too — ProcessExceptionError runs after the
	// raw ctor already pushed the bare m_strErrorDescription, so the
	// chain now carries both: the bare cause + the formatted view.
	PushLastError(strErrorMessage);

#ifdef DEBUG
	ibJournalInfo(wxT("exception"), wxT("%s"), strErrorMessage);
#endif // !DEBUG

	return strErrorMessage;
}

////////////////////////////////////////////////////////////////////

const wxString& ibBackendException::GetErrorDesc(int codeError)
{
	if (0 <= codeError && codeError < LastError)
		return gs_listErrorString[codeError];
	return gs_listErrorString[ERROR_SYS1];
}

////////////////////////////////////////////////////////////////////

bool ibBackendException::IsErrorOutputProcessing()
{
	auto* sess = ibSession::Current();
	return sess != nullptr && sess->IsProcessingBackendError();
}

void ibBackendException::SetEvalMode(bool mode)
{
	if (auto* sess = ibSession::Current())
		sess->SetEvalMode(mode);
}

bool ibBackendException::IsEvalMode()
{
	auto* sess = ibSession::Current();
	return sess != nullptr && sess->IsEvalMode();
}

////////////////////////////////////////////////////////////////////

#if !wxUSE_UTF8_LOCALE_ONLY
wxString ibBackendException::DoFormatWchar(const wxChar* format, ...)
{
	va_list args;
	va_start(args, format);
	return FormatV(format, args);
}
#endif

#if wxUSE_UNICODE_UTF8
wxString ibBackendException::DoFormatUtf8(const wxChar* format, ...)
{
	va_list args;
	va_start(args, format);
	wxString strFormat;
	strFormat.FormatV(format, args);
	va_end(args);
	return strFormat;
}
#endif

wxString ibBackendException::FormatV(const wxString& format, va_list& list)
{
	wxString strErrorBuffer =
		wxString::FormatV(wxGetTranslation(format), list);

	va_end(list);

	if (ibBackendException::IsEvalMode())
		strErrorBuffer.Replace(wxT('\n'), wxT(' '));

	stringUtils::TrimAll(strErrorBuffer);
	return strErrorBuffer;
}

wxString ibBackendException::FindErrorCodeLine(const wxString& strBuffer, unsigned int currPos)
{
	const unsigned int sizeText = strBuffer.length();

	unsigned int startPos = 0;
	unsigned int endPos = sizeText;

	// EITHER TERMINATOR ENDS A LINE. The scans used to look for '\n' only, and the
	// editor hands over a buffer that can be '\r'-terminated — no match, so both
	// bounds stayed at the extremes and the "line" became the whole file: the error
	// for `Message("Hello world!");` came back carrying two more lines glued to it.
	const auto IsEol = [](wxUniChar c) { return c == wxT('\n') || c == wxT('\r'); };

	// THE MARKER STAYS WHERE THE CALLER PUT IT; only the SEARCH steps back.
	//
	// An error is reported at the END of the last lexeme read — deliberately, so
	// the excerpt reads "…this much was understood <<?>> and here it stopped". That
	// position is very often the terminator itself, and then the two scans disagree
	// by one: the backward one steps PAST it (startPos = 671) while the forward one
	// stops ON it (endPos = 670). startPos > endPos, and `currPos - startPos` is
	// UNSIGNED, so the length wrapped to 4294967295 and Mid returned the rest of the
	// file. Measured, not guessed: currPos=670 bufLen=12683 start=671 end=670
	// len=12019 — 285 lines of source printed as an error message.
	const unsigned int scanPos =
		(currPos > 0 && currPos < sizeText && IsEol(strBuffer[currPos])) ? currPos - 1
		: (currPos >= sizeText && sizeText > 0) ? sizeText - 1
		: currPos;

	for (unsigned int i = scanPos; i > 0; i--) {
		if (IsEol(strBuffer[i])) {
			startPos = i + 1;
			break;
		};
	}

	//look for the end of the line where the translation error message is returned
	for (unsigned int i = scanPos; i < sizeText; i++) {
		if (IsEol(strBuffer[i])) {
			endPos = i; break;
		};
	}

	// The line NUMBER is not computed here. Both callers already pass their own alongside
	// this string (compileCode's currPos/currLine pair, the handler's error.m_numLine), and
	// theirs comes from the parser rather than from counting newlines in a buffer. This
	// function returns the line's TEXT, marked at the offending position, and nothing else.
	// BOUNDED BY CONSTRUCTION, not by the caller's good behaviour. Every length
	// below is a subtraction of unsigned positions, so one out-of-order bound does
	// not shorten the excerpt — it returns the rest of the file.
	if (startPos > currPos) startPos = currPos;
	if (endPos   < currPos) endPos   = currPos;
	if (endPos   > sizeText) endPos  = sizeText;

	wxString strError = wxString::Format(wxT("%s <<?>> %s"),
		strBuffer.Mid(startPos, currPos - startPos),
		strBuffer.Mid(currPos, endPos - currPos));

	strError.Replace(wxT("\r"), wxEmptyString);
	strError.Replace(wxT("\t"), wxT(" "));

	stringUtils::TrimAll(strError);

	return strError;
}

#pragma region _exception_h_

//service error handling procedures

#if !wxUSE_UTF8_LOCALE_ONLY
void ibBackendCoreException::DoErrorWchar(const wxChar* format, ...)
{
	va_list args;
	va_start(args, format);

	const wxString& strErrorBuffer =
		FormatV(format, args);

	throw ibBackendCoreException(strErrorBuffer);
}
#endif

#if wxUSE_UNICODE_UTF8
void ibBackendCoreException::DoErrorUtf8(const wxChar* format, ...)
{
	va_list args;
	va_start(args, format);
	ErrorV(format, args);
}
#endif

void ibBackendInterruptException::Error()
{
	throw ibBackendInterruptException();
}

void ibBackendAccessException::Error(const wxString& subject)
{
	throw ibBackendAccessException(subject);
}

void ibBackendLockException::VersionChangedThrow(const wxString& objectSynonym,
                                                  const wxString& expected,
                                                  const wxString& actual)
{
	const wxString msg = wxString::Format(
		_("%s: data was changed by another user. Please reload (expected version %s, found %s)."),
		objectSynonym, expected, actual);
	throw ibBackendLockException(Kind::VersionChanged, msg);
}

void ibBackendLockException::RowLockTimeoutThrow(const wxString& objectSynonym)
{
	const wxString msg = wxString::Format(
		_("%s is currently being edited by another user. Please try again."),
		objectSynonym);
	throw ibBackendLockException(Kind::RowLockTimeout, msg);
}

void ibBackendLockException::LockConflictThrow(const wxString& objectName,
                                                const wxString& blockingUser)
{
	const wxString msg = blockingUser.IsEmpty()
		? wxString::Format(
			_("%s is locked by another session."), objectName)
		: wxString::Format(
			_("%s is locked by user %s."), objectName, blockingUser);
	throw ibBackendLockException(Kind::LockConflict, msg, objectName, blockingUser);
}

// ibBackendDatabaseException + ibDatabaseLayerException implementations
// live in backend/databaseLayer/databaseLayerException.cpp, next to the
// rest of the database layer.

#pragma endregion

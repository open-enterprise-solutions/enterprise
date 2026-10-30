////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, 2�-team
//	Description : translate error and exception handler 
////////////////////////////////////////////////////////////////////////////

#include "backend/backend_exception.h"

#include "backend/backend_diagnostic.h"   // the failure as DATA — built here, text assembled from it
#include "backend/metadataConfiguration.h"
#include "backend/debugger/debugServer.h"
#include "backend/appData.h"
#include "backend/logger/logger.h"   // a runtime failure is recorded where it can be read tomorrow
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
std::unordered_map<std::thread::id, std::vector<ibString>> s_errorsByThread;

struct ibErrorsLifetime {
	ibErrorsLifetime() { s_errorsAlive = true; }
	~ibErrorsLifetime() { s_errorsAlive = false; }
} s_errorsLifetime;
} // namespace

void ibBackendException::PushLastError(const ibString& description)
{
	if (description.IsEmpty() || !s_errorsAlive) return;

	std::lock_guard<std::mutex> lock(s_errorMutex);
	std::vector<ibString>& chain = s_errorsByThread[std::this_thread::get_id()];
	if (chain.size() >= kErrorChainMax)
		chain.erase(chain.begin());
	chain.push_back(description);
}

ibString ibBackendException::GetLastError()
{
	// Drain semantics preserved for legacy callers (mainApp's
	// appDataCreate* failure path). Returns the most recent entry and
	// clears the chain — same one-string surface as before.
	if (!s_errorsAlive) return ibEmptyString;

	std::lock_guard<std::mutex> lock(s_errorMutex);
	auto found = s_errorsByThread.find(std::this_thread::get_id());
	if (found == s_errorsByThread.end() || found->second.empty()) return ibEmptyString;
	ibString last = found->second.back();
	// Drop the whole entry, not just its contents: the map is then bounded by
	// threads holding a PENDING error, not by every thread ever seen.
	s_errorsByThread.erase(found);
	return last;
}

std::vector<ibString> ibBackendException::DrainLastErrors()
{
	std::vector<ibString> out;
	if (!s_errorsAlive) return out;

	std::lock_guard<std::mutex> lock(s_errorMutex);
	auto found = s_errorsByThread.find(std::this_thread::get_id());
	if (found != s_errorsByThread.end()) {
		out.swap(found->second);
		s_errorsByThread.erase(found);
	}
	return out;
}

std::vector<ibString> ibBackendException::PeekLastErrors()
{
	if (!s_errorsAlive) return {};

	std::lock_guard<std::mutex> lock(s_errorMutex);
	auto found = s_errorsByThread.find(std::this_thread::get_id());
	return found != s_errorsByThread.end() ? found->second : std::vector<ibString>();
}

//////////////////////////////////////////////////////////////////////
//					List of error messages							//
//////////////////////////////////////////////////////////////////////
static ibString gs_listErrorString[] =
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
	_("Duplicate identifier %s"),//ibScriptError_IdentifierDuplicate
	_("Label '%s' not defined"),
	_("One of the keywords is expected!"),
	_("Module code expected"),
	_("Identifier expected"),//ibScriptError_IdentifierDefine
	_("Region name expected"),//ibScriptError_IdentifierRegion
	_("Keyword or identifier expected"),//ibScriptError_Code
	_("Symbol expected '%s'"),//ibScriptError_Delimiter
	_("Closing parenthesis or comma expected"),//ibScriptError_FuncDelimiter
	_("Function or procedure declaration keyword expected"),//ibScriptError_FuncDefine
	_("Module cannot have a return statement"),//ibScriptError_Return
	_("Expected constant"),//ibScriptError_ConstDefine
	_("An operator is expected to complete a procedure or function!"),//ibScriptError_EndFuncDefine
	_("Error writing file: %s"),//ibScriptError_FileWrite
	_("Error in expression:\n%s"),//ibScriptError_Expression
	_("Keyword expected %s"),//ibScriptError_Keyword
	_("Identifier '%s' not found"),//ibScriptError_IdentifierNotFound
	_("Operator Break can be used only inside the cycle"),//ibScriptError_UseBreak
	_("Operator Continue can be used only inside the cycle"),//ibScriptError_UseContinue
	_("Operator Return cannot be used outside the procedure or function"),//ibScriptError_UseReturn
	_("Expected program operators"),//ibScriptError_UseBlock
	_("Expected expression"),//ibScriptError_ExpressionRequire
	_("Procedure or function not detected (%s)"),//ibScriptError_CallFunction
	_("Variable with the specified name is already defined (%s)"),//ibScriptError_DefVariable
	_("A procedure or function with the specified name is already defined (%s)"),//ibScriptError_DefFunction
	// ⭐ NAME THE CALL. Both of these used to say only that a count was wrong, in a module with a
	// hundred calls in it — and the runtime callsite was ALREADY handing over the function's name,
	// which the message then threw away because it had nowhere to put it. An author reading "Too
	// many parameters" has to find the call themselves; measured on a live session, the first guess
	// was the wrong construct entirely (2026-09-02).
	_("Too many parameters passed to '%s'"),//ibScriptError_ManyParams
	_("Not enough parameters passed to '%s'"),//ibScriptError_FewParams
	_("Var is not found (%s)"),//ibScriptError_VarNotFound
	_("Unexpected program code termination"),//ibScriptError_EndProgram
	_("This module may contain only definitions of procedures and functions"), //ibScriptError_OnlyFunction
	_("Use procedure as function (%s)"), //ibScriptError_UseProcedureAsFunction
	_("Expected integer positive sign constant"),//ibScriptError_ArraySizeConst
	_("Re-import the parent module"),//ibScriptError_DuplicateImport
	_("Module not found"),//ibScriptError_ModuleNotFound
	_("The import statement must be at the beginning of the module"),//ibScriptError_UseImport
	_("Final conditional compilation statement expected"),//ibScriptError_UseEndDef
	_("A pending region statement is expected"),//ibScriptError_UseEndRegion

	_("Constructor not found (%s)"),//ibScriptError_CallConstructor

	_("'%s' is not a type a declaration can name - it takes a primitive (Number), a value class (Array) or a family of references (CatalogRef, AnyRef)"),//ibScriptError_TypeDef
	_("Bad variable type"),//ibScriptError_BadType
	_("Bad value type"),//ibScriptError_BadTypeExpression
	_("Variable must be a numeric type"),//ibScriptError_NumberType

	_("Boolean value expected"),//ibScriptError_BadTypeExpressionBoolean
	_("Numeric value expected"),//ibScriptError_BadTypeExpressionNumber
	_("String value expected"),//ibScriptError_BadTypeExpressionString
	_("Date value expected"),//ibScriptError_BadTypeExpressionDate

	_("Variable type does not support this operation"),//ibScriptError_TypeOperation

	// --- runtime (the interpreting loop) — lock-step with the enum ----------
	_("Divide by zero"),//ibScriptError_DivideByZero
	_("Cannot set array value '%s'"),//ibScriptError_ArraySet
	_("Cannot get array value '%s'"),//ibScriptError_ArrayGet
	_("Object field not writable (%s)"),//ibScriptError_PropNotWritable
	_("Object field not readable (%s)"),//ibScriptError_PropNotReadable
	_("Object field is scope-local (%s)"),//ibScriptError_PropScopeLocal
	// NAMING THE RECEIVER'S TYPE HERE WAS TRIED AND TAKEN BACK OUT. The type would be the
	// interesting half, but the only honest way to ask for it is the value itself
	// (GetClassName is virtual — a reference, a manager, a record each answer their own way),
	// and a reference answers it by READING: ReadData walks reference fields and can recurse
	// (see reference.cpp's cycle guard) or raise. A diagnostic that reads is a diagnostic
	// that can fail while explaining a failure, so the receiver stays unnamed (Max, 2026-09-04).
	_("No attribute or method found '%s' - a variable is not an aggregate object"),//ibScriptError_MemberNotAggregate
	_("Aggregate object field not found '%s'"),//ibScriptError_MemberNotFound
	// STATES THE FACT, ADVISES NOTHING. It first read "call it directly, without the dot",
	// which was written for `x.ValueIsFilled()` — and then it met a CORRECT call,
	// `ValueIsFilled(Warehouse)`, that failed for a different reason, and told the person to
	// remove a dot they had not written (Max, 2026-09-04). A diagnostic may say what it knows;
	// it may not guess what was meant.
	_("'%s' is a global function - it is not a member of this value"),//ibScriptError_MemberIsGlobalFunction

	// --- compiler, appended after the runtime block — lock-step with the enum ----
	// A GROUP WITHOUT A NAME IS THE ANSWER, and a clause written after it was met as "a ';' expected"
	// on its first word - true, and no help: the author wrote a Select and was told about a semicolon.
	_("Nothing can follow a group without a name: its answer is the groups themselves, each with Key and Values. "
	  "To go on - Select, Where, OrderBy - name the group: '%s <name>'."),//ibScriptError_LinqAfterUnnamedGroup
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

ibBackendException::ibBackendException(const ibString& strErrorDescription)
	: ibCoreException(strErrorDescription), m_errorHandled(false)
{
#ifdef DEBUG
	// "thrown", said as such: a line per exception MADE — every level a cancel walks out of makes one — and
	// not a line per thing a person was shown; read as the latter, it counted four messages that were one.
	ibJournalInfo(ibT("exception"), ibT("thrown: %s"), strErrorDescription);
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

	const ibString& strFileName = error.m_strFileName;
	const ibString& strModuleName = error.m_strModuleName;
	const ibString& strDocPath = error.m_strDocPath;

	if (!err.m_errorHandled) {

		if (activeMetaData != nullptr) {

			ibString strModuleData;

			if (!isEvalMode && strFileName.IsEmpty()) {
				const ibGuid& guidDocPath = error.m_strDocPath;
				const ibValueMetaObjectModuleBase* foundedDoc = activeMetaData->FindAnyObjectByFilter<ibValueMetaObjectModuleBase>(guidDocPath, true);
				// 🛑⭐⭐ NOT EVERY FAILING BYTECODE BELONGS TO A MODULE, and this asserted that it
				// did — then dereferenced. The module text is wanted for ONE thing: quoting the
				// offending source line. A program that is not in the configuration has no line to
				// quote, which is a smaller loss than the crash: MEASURED 2026-09-06, `code_run`
				// sending a compiled program that calls `Raise("...")` reached here with a docPath
				// that names no metaobject, and the run died on the assert with the actual failure
				// never reported at all — so the caller saw a hang rather than their own error.
				//
				// ⚠ THE SAME SHAPE IS ONE BRANCH DOWN, for a module in an external file, and it is
				// fixed with it: a file that has been moved or deleted is ordinary, and a reporter
				// that crashes while reporting is the worst possible failure of a reporter.
				if (foundedDoc != nullptr)
					strModuleData = foundedDoc->GetModuleText();
			}
			else if (!isEvalMode && !strFileName.IsEmpty()) {
				// Frame from the session's CurrentFrame() shortcut —
				// reaches this thread's pinned session via worker scope.
				if (auto* frame = ibSession::CurrentFrame()) {
					const ibMetaData* metadata = frame->FindMetadataByPath(strFileName);
					// Same rule as the branch above: a source line is worth having and is not worth
					// crashing for. An external report whose file has moved answers null here.
					const ibValueMetaObjectModuleBase* foundedDoc = metadata != nullptr
						? metadata->FindAnyObjectByFilter<ibValueMetaObjectModuleBase>(
							(const ibGuid&)error.m_strDocPath, true)
						: nullptr;
					if (foundedDoc != nullptr)
						strModuleData = foundedDoc->GetModuleText();
				}
			}

			const ibString strCodeError = isEvalMode ? ibString(ibEmptyString) :
				ibBackendException::FindErrorCodeLine(strModuleData, error.m_numString);

			ibBackendException::ProcessExceptionError(strFileName,
				strModuleName, strDocPath,
				error.m_numString, isEvalMode ? error.m_numLine : error.m_numLine + 1,
				strCodeError, wxNOT_FOUND, err.GetErrorDescription(),
				ibDiagnosticKind::Runtime   // reached from procUnit's catch — the code ran
			);

			// ⭐⭐ AND INTO THE REGISTRATION JOURNAL, because a failure nobody was watching is
			// otherwise not recorded anywhere. MEASURED 2026-09-06: the journal was written from
			// twenty places in the whole backend, none of them this one — so "scan the journal for
			// the errors" found nothing, and a run that failed at three in the morning left no trace
			// at all. A background or scheduled job cannot MESSAGE anybody (its session is tied to
			// no one), which makes this its only account of having failed.
			//
			// ⚠ NOT IN EVAL MODE. A watch expression, a tooltip and an autocomplete probe fail
			// constantly and by design; recording those would bury the failures that matter under
			// the ones nobody asked about.
			if (!isEvalMode) {
				if (ibLogger* const logger = ibApplicationInstance::GetLogger())
					logger->Error(ibT("script"), ibT("runtime.error"),
						ibString::Format(ibT("%s (%s, line %d)"),
							err.GetErrorDescription(), strModuleName, (int)error.m_numLine + 1));
			}
		}
		else {

			ibBackendException::ProcessExceptionError(strFileName,
				strModuleName, strDocPath,
				error.m_numString, error.m_numLine + 1,
				ibEmptyString, wxNOT_FOUND, err.GetErrorDescription(),
				ibDiagnosticKind::Runtime
			);
		}

		err.m_errorModule  = strModuleName;
		err.m_errorLine    = error.m_numLine + 1;   // 1-based, as the journal line above writes it
		err.m_errorHandled = true;
	}

	// The caller rethrows, and it rethrows after its catch has returned.
	// `throw;` here is a throw inside the handler. On MSVC that handler is
	// a funclet above the frame, and each interpreter level that rethrows
	// from there nests another one — the slack is gone after a few dozen,
	// and the overflow then skips the frame guards. The same object is what
	// has to travel, so m_errorHandled stays set; the caller holds it with
	// std::current_exception() and rethrows that.
}

void ibBackendException::ProcessError(const ibString& strFileName,
	const ibString& strModuleName, const ibString& strDocPath,
	const unsigned int currPos, const unsigned int currLine,
	const ibString& strCodeError, const int codeError, const ibString& strErrorDesc)
{
	//throw this exception
	// COMPILE side: the only caller is ibCompileCode::DoRefuse, i.e. the text
	// was refused before it ever ran.
	//
	// 🛑 THE ASSEMBLED MESSAGE IS DATA, AND IT WAS BEING PASSED AS A FORMAT. It embeds the offending
	// SOURCE LINE, and `Error()` is printf-style — its first argument IS the format. So a per cent
	// sign in the author's own text became a conversion specifier, `FormatV` read an argument that
	// was never pushed, and the process fast-failed with 0xC0000409: no message, no dump, nothing
	// naming the module that did it.
	//
	// ⚠ AND THE TRIGGER IS ORDINARY TEXT, not an edge case. `"discount 20 %"` in a string literal is
	// enough, and a configuration written in Russian or Ukrainian hits it the same way — which is how
	// it was found: by bisecting a whole imported configuration down to one manager module that crashed the
	// compiler at base open (2026-08-24).
	ibBackendCoreException::Error(ibT("%s"),
		ibBackendException::ProcessExceptionError(strFileName, strModuleName, strDocPath, currPos, currLine, strCodeError, codeError, strErrorDesc,
			ibDiagnosticKind::Compile));
}

////////////////////////////////////////////////////////////////////

ibString ibBackendException::ProcessExceptionError(const ibString& strFileName,
	const ibString& strModuleName, const ibString& strDocPath,
	const unsigned int currPos, const unsigned int currLine,
	const ibString& strCodeError, const int codeError, const ibString& strErrorDesc,
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
	diagnostic.m_codeLine = isEvalMode ? ibString(ibEmptyString) : strCodeError;

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
			// The report of one failure must not become a second one: a frame whose position is
			// outside its own bytecode is printed with line 0 rather than read past the array.
			const long curLine = stackContext->m_lCurLine;
			frame.m_line = (curLine >= 0 && (size_t)curLine < stackByteCode->m_listCode.size())
				? stackByteCode->m_listCode[curLine].m_numLine + 1 : 0;
			diagnostic.m_stack.push_back(std::move(frame));
		}
	}

	ibDiagnostics::Publish(diagnostic);

	ibString strErrorMessage;
	strErrorMessage += ibT("{") + strModuleName + ibT("(") + (isEvalMode ? ibString(ibT(" ")) : ibString::Format(ibT("%i"), currLine)) + ibT(")}: ");
	strErrorMessage += diagnostic.m_message + ibT("\n");
	strErrorMessage += diagnostic.m_codeLine;

	if (isEvalMode) strErrorMessage.Replace(ibT("\n"), ibT(" "));

	if (!isEvalMode) {

		// Frame via the session pinned by the worker scope — single
		// canonical entry point through ibSession::CurrentFrame.
		// Null when no scope is active or session has no UI.
		auto* frame = ibSession::CurrentFrame();

		if (frame != nullptr) {
			// The call-stack TEXT, rendered from the record above.
			ibString strStackMessage;
			for (std::size_t i = 0; i < diagnostic.m_stack.size(); i++) {
				strStackMessage += ibString::Format(ibT("\n%i: %s (#line %d)"),
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
				strErrorMessage + (strStackMessage.IsEmpty() ? ibString() : ibT("\n\nCall stack:") + strStackMessage)
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
	ibJournalInfo(ibT("exception"), ibT("reported: %s"), strErrorMessage);   // the formatted one, with its place
#endif // !DEBUG

	return strErrorMessage;
}

////////////////////////////////////////////////////////////////////

ibString ibBackendException::Format(int codeError, const ibString& errorDesc)
{
	const ibString& format = GetErrorDesc(codeError);
	std::wstring spec;
	if (ibFStringFormat::Prepare(ibGetTranslation(format), nullptr, 0, spec))
		return Format(format);   // nowhere to put it
	return Format(format, errorDesc);
}

const ibString& ibBackendException::GetErrorDesc(int codeError)
{
	if (0 <= codeError && codeError < ibScriptError_Count)
		return gs_listErrorString[codeError];
	return gs_listErrorString[ibScriptError_Sys1];
}

////////////////////////////////////////////////////////////////////

bool ibBackendException::IsErrorOutputProcessing()
{
	auto* sess = ibSession::Current();
	return sess != nullptr && sess->IsProcessingBackendError();
}

void ibBackendException::SetEvalMode(ibEvalMode mode)
{
	if (auto* sess = ibSession::Current())
		sess->SetEvalMode(mode);
}

ibEvalMode ibBackendException::IsEvalMode()
{
	auto* sess = ibSession::Current();
	return sess != nullptr ? sess->IsEvalMode() : eval_none;
}

bool ibBackendException::IsEvalSandbox()
{
	auto* sess = ibSession::Current();
	return sess != nullptr && sess->IsEvalSandbox();
}

bool ibBackendException::IsEvalComplete()
{
	auto* sess = ibSession::Current();
	return sess != nullptr && sess->IsEvalComplete();
}

////////////////////////////////////////////////////////////////////

ibString ibBackendException::FindErrorCodeLine(const ibString& strBuffer, unsigned int currPos)
{
	const unsigned int sizeText = strBuffer.length();

	// 🛑⭐⭐ NO TEXT, NO LINE — and this is not a tidy-up. Every scan below INDEXES the buffer at a
	// position taken from the error, and an empty buffer with a non-zero position reads off the end:
	// `strBuffer[45]` on a string of length 0. Nothing catches that — it is an access violation, not
	// an exception — so the thread that was REPORTING a failure died silently, the background run it
	// belonged to never published its completion, and `code_status` answered "still running" for
	// ever (measured 2026-09-06, on a run whose own code raised).
	//
	// ⚠ AND IT WAS PUT HERE BY THE FIX ONE FILE UP. ProcessError used to dereference a null
	// metaobject to get the module text; made tolerant, it now passes an EMPTY text instead — so the
	// crash MOVED rather than went away. A guard against absence has to be written at every place
	// that consumes the absent thing, not only at the one that produced it.
	if (sizeText == 0)
		return ibEmptyString;

	unsigned int startPos = 0;
	unsigned int endPos = sizeText;

	// EITHER TERMINATOR ENDS A LINE. The scans used to look for '\n' only, and the
	// editor hands over a buffer that can be '\r'-terminated — no match, so both
	// bounds stayed at the extremes and the "line" became the whole file: the error
	// for `Message("Hello world!");` came back carrying two more lines glued to it.
	const auto IsEol = [](ibChar c) { return c == ibT('\n') || c == ibT('\r'); };

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
	// ⚠ AND IT IS CLAMPED, because `currPos` comes from a DIFFERENT text than this one whenever the
	// two disagree — a module that was edited, or a program with no module at all. Past the end it
	// is not a position, and both scans below dereference it.
	const unsigned int scanPos =
		(currPos > 0 && currPos < sizeText && IsEol(strBuffer[currPos])) ? currPos - 1
		: (currPos >= sizeText) ? sizeText - 1
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

	ibString strError = ibString::Format(ibT("%s <<?>> %s"),
		strBuffer.Mid(startPos, currPos - startPos),
		strBuffer.Mid(currPos, endPos - currPos));

	strError.Replace(ibT("\r"), ibEmptyString);
	strError.Replace(ibT("\t"), ibT(" "));

	stringUtils::TrimAll(strError);

	return strError;
}

#pragma region _exception_h_

//service error handling procedures

void ibBackendInterruptException::Error()
{
	throw ibBackendInterruptException();
}

void ibBackendAccessException::Error(const ibString& subject)
{
	throw ibBackendAccessException(subject);
}

void ibBackendFormException::Error(const ibString& subject)
{
	// ⚠ THE CANONICAL SENTENCE STAYS. "Context functions are not available" is what this has said for
	// years and what people recognise, and it was the wording of every frameless refusal in the tree
	// — the spreadsheet's Print and Show among them. Replacing it here would have left one rule
	// speaking with two voices; instead every one of those sites now raises THIS, so the sentence is
	// written once and they all say it.
	//
	// What was actually missing was never the wording. It was the REASON — a server has no forms, so
	// the answer is about WHERE the code is running rather than about a function somebody forgot to
	// declare — and the TYPE, which is what lets a caller recognise this refusal among all the
	// others (Max, 2026-09-06: *"the canonical one is 'context functions are not available' - you
	// just create a new kind so it is clear"*).
	throw ibBackendFormException(subject.IsEmpty()
		? ibString(_("Context functions are not available: this is a server, and a server has no forms."))
		: ibString::Format(
			_("Context functions are not available: this is a server, and a server has no forms "
			  "(asked for %s)."), subject));
}

void ibBackendLockException::VersionChangedThrow(const ibString& objectSynonym,
                                                  const ibString& expected,
                                                  const ibString& actual)
{
	const ibString msg = ibString::Format(
		_("%s: data was changed by another user. Please reload (expected version %s, found %s)."),
		objectSynonym, expected, actual);
	throw ibBackendLockException(Kind::VersionChanged, msg);
}

void ibBackendLockException::RowLockTimeoutThrow(const ibString& objectSynonym)
{
	const ibString msg = ibString::Format(
		_("%s is currently being edited by another user. Please try again."),
		objectSynonym);
	throw ibBackendLockException(Kind::RowLockTimeout, msg);
}

void ibBackendLockException::LockConflictThrow(const ibString& objectName,
                                                const ibString& blockingUser)
{
	const ibString msg = blockingUser.IsEmpty()
		? ibString::Format(
			_("%s is locked by another session."), objectName)
		: ibString::Format(
			_("%s is locked by user %s."), objectName, blockingUser);
	throw ibBackendLockException(Kind::LockConflict, msg, objectName, blockingUser);
}

// ibBackendDatabaseException + ibDatabaseLayerException implementations
// live in backend/databaseLayer/databaseLayerException.cpp, next to the
// rest of the database layer.

#pragma endregion

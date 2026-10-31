#ifndef _backend_exception_h__
#define _backend_exception_h__

#include <exception>
#include <string>
#include <vector>

#include "backend_core.h"   // ibEvalMode — which kind of evaluation a session is inside
#include "core/compiler/codeDef.h"   // the error message numbers — the translator in the core raises them too

#include "backend/backend.h"
#include "core/exception.h"   // ibCoreException — the refusal this one is, and the one a handler that only tells catches

// Forward-declared rather than included: this is a wide header, and the failure
// RECORD (diagnostic.h) is only named here, never used by value.
enum class ibDiagnosticKind;

// A std::exception, and it took a crash to make that obvious. Nothing about this class needed
// the standard base — it carries its own description and every deliberate handler asks for it by
// name — but everything AROUND it assumes one: `catch (const std::exception&)` is what generic
// code writes at a boundary (a thread body, an event handler, a top-level guard), and a project
// exception that does not derive from it lands in `catch (...)`, where the message it carries has
// nowhere to go. That is how a session-registry thread died reporting "unknown exception" while
// holding the sentence "Object 'ConstantObject.Attribute3' is not exist".
//
// Deriving costs nothing at the throw sites (still thrown by value, caught by const reference)
// and changes no existing handler: every place that catches both already lists this class FIRST,
// which is the order C++ requires anyway — derived before base. What it buys is that the places
// that catch only the standard base now SEE the description instead of losing it.
//
// The description and what() are the core's refusal's (ibCoreException), which this one is.
class BACKEND_API ibBackendException : public ibCoreException {
protected:

	ibBackendException(const ibString& strErrorDescription);

public:

	// Thrown by value, caught by const reference. Virtual destructor keeps
	// polymorphic dynamic-cast/catch-by-base behaviour well-defined across
	// ibBackendCoreException / ibBackendInterruptException / ibBackendAccessException.
	virtual ~ibBackendException() = default;

	// The format is translated, then filled; in an evaluation the message is one line. A code instead of
	// a format is its message's (codeDef.h).
	template <class... Args>
	static ibString Format(const ibString& format, const Args&... args) {
		ibString strErrorBuffer = ibString::Format(ibGetTranslation(format), args...);
		if (IsEvalMode())
			strErrorBuffer.Replace(ibT("\n"), ibT(" "));
		stringUtils::TrimAll(strErrorBuffer);
		return strErrorBuffer;
	}
	template <class... Args>
	static ibString Format(int codeError, const Args&... args) {
		return Format(GetErrorDesc(codeError), args...);
	}
	// A refusal's description goes in only where its message has a place for one: "Identifier expected" has none,
	// and the refusal hands its (empty) description all the same.
	static ibString Format(int codeError, const ibString& errorDesc);

	// ⭐ HAS THE PERSON ALREADY BEEN TOLD? Nothing new is remembered here — m_errorHandled is set
	// the moment ProcessError reports, and rides the rethrow. It was simply never READABLE from
	// outside, so every outer handler reported again (three windows for one script failure,
	// 2026-09-01). Not to be confused with IsErrorOutputProcessing(), which asks whether a report
	// is on screen RIGHT NOW and is false again by the time the exception reaches the main loop.
	bool IsErrorHandled() const { return m_errorHandled; }
	// …AND WHERE IT WAS — the module and the 1-based line ProcessError found the failure at, riding the rethrow
	// the same way. A reader told only the message (a background run's status, read later) could not say where to
	// look. Empty / 0 = not a script's failure, or not reported yet.
	const ibString& GetErrorModule() const { return m_errorModule; }
	unsigned int GetErrorLine() const { return m_errorLine; }

	//error from proc unit/compile module
	static void ProcessError(const ibBackendException& err, const struct ibByteUnit& error);
	static void ProcessError(const ibString& strFileName,
		const ibString& strModuleName, const ibString& strDocPath,
		const unsigned int currPos, const unsigned int currLine,
		const ibString& strCodeLineError, const int codeError, const ibString& strErrorDesc // error code from compile module
	);

	static ibString FindErrorCodeLine(const ibString& sBuffer, unsigned int currPos);

	// Returns the most recent error description and clears it. Kept for
	// the legacy callers (mainApp.cpp's startup `appDataCreate*` failure
	// path); new code should prefer GetLastErrorChain / DrainLastErrors
	// to see the whole sequence of failures that led to the visible one.
	static ibString GetLastError();

	// Per-thread error chain. Every ibBackendException ctor pushes its
	// message onto the calling thread's chain; readers drain it after
	// a failed startup / login / metadata-load attempt to display ALL
	// recorded reasons, not just the last one. Previous single-string
	// ms_strError lost information whenever a catch (...) {} site
	// constructed a fresh wrapped exception, masking the root cause.
	static std::vector<ibString> DrainLastErrors();

	// Read-only peek — does not clear. Useful for diagnostics dialogs
	// that want to surface the chain without consuming it.
	static std::vector<ibString> PeekLastErrors();

	static bool IsErrorOutputProcessing();

	// ⭐ THE KIND, NOT A YES/NO. `eval_none` is zero, so `if (IsEvalMode())` keeps its old meaning
	// while a caller that needs to know WHICH kind can ask the same question (backend_core.h).
	static void SetEvalMode(ibEvalMode mode);
	static ibEvalMode IsEvalMode();

	// …and the one question the WRITE gates ask, read off that kind: a watch reads, a sandbox
	// writes and is rolled back.
	static bool IsEvalSandbox();

	// …and the finer one, asked by the few places that ANSWER differently. `IsEvalMode()` still
	// holds here — completion is an evaluation like any other, so every gate that skips writes or
	// keeps the message pane quiet keeps working untouched. This only separates a watch from a
	// caret where their answers genuinely part. See eval_complete.
	static bool IsEvalComplete();

	// Saves the current eval-mode flag in ctor, restores it in dtor.
	// Use to wrap ibProcUnit::Evaluate so an inner throw doesn't leak
	// the flag onto the session.
	class ibEvalModeScope {
	public:
		// WHICH KIND this evaluation is — a watch by default, which is what eval mode has always
		// meant. The previous kind is restored on the way out, so a sandbox opened inside a watch
		// leaves the watch exactly as it found it.
		explicit ibEvalModeScope(ibEvalMode newMode = eval_watch)
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
		const ibEvalMode m_previous;
	};

protected:

	// Push a freshly-constructed error onto the calling thread's chain.
	// Called from ibBackendException's ctor — every thrown exception
	// adds itself to the per-thread history. The chain is bounded
	// (oldest entries dropped beyond the cap) to keep memory predictable
	// when a runaway loop keeps throwing.
	static void PushLastError(const ibString& description);

private:

	//error handling routines
	static const ibString& GetErrorDesc(int codeError);
	// Builds the diagnostic (see diagnostic.h), publishes it to whoever
	// subscribed, and returns the human-facing text ASSEMBLED FROM IT — the
	// text is a projection of the record, never the other way round.
	//
	// The kind is the caller's to state: a compile error means the text never
	// ran, a runtime one means it did, and no amount of looking at the message
	// tells them apart.
	static ibString ProcessExceptionError(const ibString& strFileName,
		const ibString& strModuleName, const ibString& strDocPath,
		const unsigned int currPos, const unsigned int currLine,
		const ibString& strCodeLineError, const int codeError, const ibString& strErrorDesc, // error code from compile module
		const ibDiagnosticKind kind
	);

	mutable bool         m_errorHandled;
	mutable ibString     m_errorModule;      // set where m_errorHandled is (ProcessError)
	mutable unsigned int m_errorLine = 0;
};

#pragma region _exception_h_

class BACKEND_API ibBackendCoreException : public ibBackendException {
protected:
	ibBackendCoreException(const ibString& strErrorDescription) : ibBackendException(strErrorDescription) {}
public:

	template <class... Args>
	[[noreturn]] static void Error(const ibString& format, const Args&... args) {
		throw ibBackendCoreException(Format(format, args...));
	}
	template <class... Args>
	[[noreturn]] static void Error(int codeError, const Args&... args) {
		throw ibBackendCoreException(Format(codeError, args...));
	}
};

class BACKEND_API ibBackendInterruptException : public ibBackendException {
	ibBackendInterruptException() : ibBackendException(_("The program was stopped by the user!")) {}
public:
	static void Error();
};

// 🛑⭐⭐ A FORM WAS ASKED FOR WHERE THERE ARE NO FORMS — its own type, because it is its own KIND of
// wrong and callers need to tell it apart from everything else that can fail in the same line.
//
// USING A FORM ON THE SERVER IS NOT ALLOWED, and this exception is how that rule is enforced rather
// than worked around. A server (a background job, appserver, codeRunner, a web-server worker) has
// no windows; code that reaches for one there has asked the wrong process, and softening the refusal
// to a silent null would let form-driven logic run half-done and answer DIFFERENTLY on the server
// than on the client — the failure nobody audits (Max, 2026-09-06: *"you have no right to use forms
// on the server — do not go around the rule; make a separate exception so you know the exact kind"*).
//
// ⚠ WHAT THE TYPE BUYS, AND IT IS NOT A LICENCE TO CATCH IT AT THE LEAF. A form lookup answers NULL
// for "nothing is open for this object", which is an ordinary state; folding "there are no forms in
// this process at all" into the same null makes the two indistinguishable, and then nobody can say
// WHY the form came back undefined. So the second answer is a refusal, it carries its own type, and
// it travels all the way up to whoever STARTED the code — a background job reports it in its
// `error` like any other failure, naming the line that asked. Catching it lower down is muffling:
// the run goes on doing half of what it was written to do, silently (Max, 2026-09-06: *"how will
// you understand why the form came back undefined?"* — *"you just muffled it"*).
class BACKEND_API ibBackendFormException : public ibBackendException {
	explicit ibBackendFormException(const ibString& what)
		: ibBackendException(what) {}
public:
	// The one the frameless case raises. `subject` names what was being asked for, when the caller
	// knows — "a form for 'Catalog.Goods'" — because "no forms here" is a true sentence that still
	// leaves a person hunting for which line asked.
	static void Error(const ibString& subject = ibEmptyString);
};

class BACKEND_API ibBackendAccessException : public ibBackendException {
	explicit ibBackendAccessException(const ibString& subject = ibEmptyString)
		: ibBackendException(subject.IsEmpty()
			? ibString(_("Not enough access rights for this user!"))
			: ibString::Format(_("Not enough access rights for this user: %s"), subject)) {}
public:
	// One denial, said as precisely as the caller can afford: `subject` names WHAT was refused
	// ("writing register 'Sales'") and the exception wraps it in the standard line; omitted, the
	// message reads exactly as before. During a posting run the bare form leaves the user guessing
	// which object in the chain closed the door, so gates that know pass it. The type does not
	// change, so every catch site (the web door's "forbidden" mapping included) keeps matching.
	static void Error(const ibString& subject = ibEmptyString);
};

// Concurrent-write protection failures surfaced by the Write-time
// DataVersion check or the DB-side row-lock acquisition.
// See docs/private/record-locks.md.
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
	const ibString& GetBlockingUser() const { return m_blockingUser; }
	const ibString& GetObjectName()   const { return m_objectName; }

	// Throw a VersionChanged kind. Message is formatted as
	// "<objectSynonym>: data version changed (expected <expected>,
	// found <actual>)" — caller may show the synonym to user, the
	// version strings are diagnostic-only.
	[[noreturn]] static void VersionChangedThrow(const ibString& objectSynonym,
	                                              const ibString& expected,
	                                              const ibString& actual);

	// Throw a RowLockTimeout kind. Message includes the synonym; the
	// DB driver-level reason (timeout / NOWAIT conflict) is logged
	// via ibBackendException::ProcessError chain.
	[[noreturn]] static void RowLockTimeoutThrow(const ibString& objectSynonym);

	// Throw a LockConflict kind. Used by ibLockManager::Acquire when
	// another session holds a conflicting sys_lock row on the same
	// (namespace, key). Message reads "<objectName> is locked by
	// user <blockingUser>" — UI surfaces it directly.
	[[noreturn]] static void LockConflictThrow(const ibString& objectName,
	                                            const ibString& blockingUser);

private:
	ibBackendLockException(Kind kind, const ibString& msg)
		: ibBackendException(msg), m_kind(kind) {}
	ibBackendLockException(Kind kind, const ibString& msg,
	                       const ibString& objectName,
	                       const ibString& blockingUser)
		: ibBackendException(msg), m_kind(kind),
		  m_objectName(objectName), m_blockingUser(blockingUser) {}
	Kind m_kind;
	ibString m_objectName;
	ibString m_blockingUser;
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
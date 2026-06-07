#ifndef __DATABASE_LAYER_EXCEPTION_H__
#define __DATABASE_LAYER_EXCEPTION_H__

// Database-tier exception types. Both inherit from ibBackendException
// so a single catch site can pick the level of specificity it needs:
//
//   catch (const ibDatabaseLayerException&)    // driver native code + sqlstate
//   catch (const ibBackendDatabaseException&)  // any DB-tier failure
//   catch (const ibBackendException&)          // catch-all
//
// Direct callers (business logic that wants to say "this is a DB
// failure" without knowing the driver details) can throw the category
// base; drivers throw the more specific ibDatabaseLayerException with
// native_code + sqlstate populated by the per-driver error reporter.

#include "backend/backend_exception.h"
#include "databaseLayerDef.h"

// ibBackendDatabaseException — category exception for "something went
// wrong at the DB tier". Bridges driver-internal failures into the
// shared ibBackendException hierarchy.
class BACKEND_API ibBackendDatabaseException : public ibBackendException {
public:
	enum class Kind {
		// Connection failed or was dropped mid-call. Includes initial
		// open failures (wrong host/port, refused, timeout) and lost
		// connections during a running session. Caller may attempt a
		// reconnect; the DB pool's lazy clone path normally handles
		// this transparently.
		ConnectionLost,

		// SQL parse / type / column-not-found errors. Not retryable —
		// indicates broken schema or wrong query. Caller surfaces as
		// "Bug, please report".
		Syntax,

		// Primary key, foreign key, unique, NOT NULL, CHECK violation.
		// Business-level — caller often translates into a user-facing
		// validation message ("Already exists", "Required field empty").
		Constraint,

		// Database-side deadlock (FB isc_lock_conflict, PG SQLSTATE
		// 40P01, MySQL 1213, MSSQL 1205). Always safe to retry — the
		// engine has already rolled back our TX.
		Deadlock,

		// Statement / lock timeout — distinct from Deadlock because
		// the engine didn't pick us as a victim, our request just took
		// too long against contended state. Usually retryable with
		// backoff.
		Timeout,

		// Could not classify from native code. Caller treats as
		// non-retryable by default; rich-text Kind classification can
		// be added per-driver over time without changing the catch
		// signature.
		Unknown,
	};

	Kind GetKind() const { return m_kind; }
	// Out-of-line — same reason as ibBackendException::GetErrorDescription:
	// inline body on a BACKEND_API class becomes dllimport-only in
	// consuming TUs, and MSVC may decline to inline (e.g. when the call
	// happens inside a lambda the linker can't see), then the symbol
	// isn't exported from backend.dll either → LNK2019. Out-of-line in
	// .cpp guarantees the export.
	bool IsRetryable() const;

	// Throw a categorised DB exception without native-code detail —
	// for business-level callers that know the category but don't have
	// a driver error to pass. Driver code should prefer
	// ibDatabaseLayerException::Throw below.
	[[noreturn]] static void Throw(Kind kind, const wxString& msg);

protected:
	ibBackendDatabaseException(Kind kind, const wxString& msg)
		: ibBackendException(msg), m_kind(kind) {}

private:
	Kind m_kind;
};

// ibDatabaseLayerException — concrete driver-tier failure with the
// native error metadata preserved. Thrown ONLY from
// ibDatabaseErrorReporter::ThrowDatabaseException(). The reporter sets
// the native code + sqlstate that the driver collected before
// classifying into Kind and raising this.
//
// Per-driver classification lives in ClassifyError* free functions next
// to each driver — they map native codes to Kind. Until those mappings
// exist this class is still useful: the native code + sqlstate travel
// with the exception so admin logs and ops dashboards see something
// more useful than just a message string.
class BACKEND_API ibDatabaseLayerException : public ibBackendDatabaseException {
public:
	// Out-of-line — see IsRetryable comment above for the BACKEND_API
	// + inline + lambda export issue.
	int             GetDriverErrorCode() const;
	const wxString& GetSqlState()         const;

	[[noreturn]] static void Throw(Kind kind, int nativeCode,
	                                const wxString& sqlState,
	                                const wxString& msg);

private:
	ibDatabaseLayerException(Kind kind, int nativeCode,
	                          const wxString& sqlState, const wxString& msg)
		: ibBackendDatabaseException(kind, msg)
		, m_nativeCode(nativeCode)
		, m_sqlState(sqlState) {}

	int      m_nativeCode = 0;
	wxString m_sqlState;
};

// ibBackendQueryException — an L2-side (query-layer) failure, categorically
// distinct from a DBMS fault (ibBackendDatabaseException above). The exception
// TYPE names the tier (docs/query-language-arc.md §10): catch this and you know
// the fault is in OUR translation / lifecycle, not in the database. It
// deliberately does NOT derive from the database-exception family, so it never
// inherits DB retry semantics (§12) — it sits beside it under ibBackendException.
class BACKEND_API ibBackendQueryException : public ibBackendException {
public:
	enum class Kind {
		TranslationFailure,  // could not render / prepare the IR as SQL for this dialect
		UnsupportedNode,     // an IR node with no dialect form and no fallback
		NoConnection,        // L2 could not obtain a connection from the holder
		PoolExhausted,       // §12 line 3 — leak / saturation (carries pool attribution later)
	};

	Kind GetKind() const { return m_kind; }

	[[noreturn]] static void Throw(Kind kind, const wxString& message);

private:
	ibBackendQueryException(Kind kind, const wxString& msg)
		: ibBackendException(msg), m_kind(kind) {}

	Kind m_kind;
};

#endif // __DATABASE_LAYER_EXCEPTION_H__

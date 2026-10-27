#ifndef __POSTGRESQL_DATABASE_LAYER_H__
#define __POSTGRESQL_DATABASE_LAYER_H__

#include "backend/databaseLayer/databaseLayerDef.h"
#include "backend/databaseLayer/databaseLayer.h"
#include "backend/databaseLayer/preparedStatement.h"

#include <mutex>   // m_cancelGuard

#if _USE_DYNAMIC_DATABASE_LAYER_LINKING == 1
class ibInterfacePostgres;
#endif

class BACKEND_API ibDatabaseLayerPostgres : public ibDatabaseLayer
{
public:
	// Information that can be specified for a PostgreSQL database
	//  host or hostaddr
	//  port
	//  dbname
	//  user
	//  password
	// ctor
	ibDatabaseLayerPostgres();
	ibDatabaseLayerPostgres(const ibString& strDatabase);
	ibDatabaseLayerPostgres(const ibString& strServer, const ibString& strDatabase);
	ibDatabaseLayerPostgres(const ibString& strDatabase, const ibString& strUser, const ibString& strPassword);
	ibDatabaseLayerPostgres(const ibString& strServer, const ibString& strDatabase, const ibString& strUser, const ibString& strPassword);
	ibDatabaseLayerPostgres(const ibString& strServer, const ibString& strPort, const ibString& strDatabase, const ibString& strUser, const ibString& strPassword);
	ibDatabaseLayerPostgres(void* pDatabase) { m_pDatabase = pDatabase; }
	ibDatabaseLayerPostgres(const ibDatabaseLayerPostgres& src);

	// dtor
	virtual ~ibDatabaseLayerPostgres();

	// open database
	virtual bool Open();
	virtual bool Open(const ibString& strDatabase);
	virtual bool Open(const ibString& strServer, const ibString& strDatabase);
	virtual bool Open(const ibString& strDatabase, const ibString& strUser, const ibString& strPassword);
	virtual bool Open(const ibString& strServer, const ibString& strDatabase, const ibString& strUser, const ibString& strPassword);
	virtual bool Open(const ibString& strServer, const ibString& strPort, const ibString& strDatabase, const ibString& strUser, const ibString& strPassword);

	// close database
	virtual bool Close();

	// Is the connection to the database open?
	virtual bool IsOpen();

	// Cancel what this connection is running — PQcancel, from any thread (see the base).
	virtual void Cancel();

	/// clone database
	virtual ibDatabaseLayer* Clone() { return new ibDatabaseLayerPostgres(*this); }

	// IsActiveTransaction uses the base-class default (m_txDepth > 0).
	// Driver transaction primitives (DoBeginTransaction / DoCommit /
	// DoRollBack) are protected — see below.

	// Row-lock dialect now lives in the dialect dictionary (default m_rowLockSuffix=" FOR UPDATE",
	// m_rowLockNoWaitSuffix=" NOWAIT" → raises SQLSTATE 55P03 lock_not_available instead of blocking).

	// Database schema API contributed by M. Szeftel (author of wxActiveRecordGenerator)
	virtual bool DatabaseExists(const ibString& table);
	virtual bool TableExists(const ibString& table);
	virtual bool ViewExists(const ibString& view);

	virtual std::vector<ibString> GetTables();
	virtual std::vector<ibString> GetViews();
	virtual std::vector<ibString> GetColumns(const ibString& table);

	virtual int GetDatabaseLayerType() const {
		return DATABASELAYER_POSTGRESQL;
	}

	// PG SQL dialect. Static holds the definition (a test reads it without
	// constructing the driver — no libpq). The virtual GetDialect() is the
	// polymorphic access point L2 uses (conn->GetDialect()).
	static const ibDialectDictionary& Dialect();
	virtual const ibDialectDictionary& GetDialect() const override;

	// PG DB temp-table facts (the first real temp target — ad-hoc CREATE TEMPORARY TABLE). Presence
	// of this (vs the base nullptr) flips PG onto the temp path; FB stays on RAM. (docs/private/temp-db.md)
	static const ibTempTableDialect& TempDialect();
	virtual const ibTempTableDialect* GetTempTableDialect() const override;

	// Derived-state materialisation (register totals) — the production target. PG is the
	// only per-row engine that cannot inline a trigger body (it needs a FUNCTION), and the
	// only one with a storage knob worth setting (fillfactor → HOT updates on the hot totals
	// row). Both are dictionary slots. (docs/private/register-totals-strategy.md)
	static const ibMaterializationDialect& MaterializationDialect();
	virtual const ibMaterializationDialect* GetMaterializationDialect() const override;

	// MAX / MIN over `uuid` — PostgreSQL ships neither, and a reference key IS a uuid here, so the
	// aggregates are created with the database. The only driver missing anything at all.
	virtual bool CreateMissingRoutines() override;

	// The status as the code — or the cancel, when a failed result's SQLSTATE says it was the statement cancelled.
	static int TranslateErrorCode(int nCode, const char* sqlState = nullptr);
	static bool IsAvailable();

	// SQLSTATE is the classifier — the class digits drive most of it (23 integrity violation →
	// Constraint, 40P01 → Deadlock, 40 / 55P03 → Timeout, 42 → Syntax, 08 → ConnectionLost). Every
	// reporter of the driver records this Kind with the code, from the state it just read off the
	// result (SetErrorCode(code, kind)).
	static ibBackendDatabaseException::Kind ClassifySqlState(const ibString& sqlState);

	ibString GetSqlState() const override { return m_lastSqlState; }

	// Internal hook for libpq error paths — called from places that
	// already have a PGresult* in hand to stash the SQLSTATE so the
	// next ThrowDatabaseException carries it.
	void SetLastSqlState(const ibString& s) { m_lastSqlState = s; }

protected:

	// query database
	virtual int DoRunQuery(const ibString& strQuery, bool bParseQuery);
	virtual ibDatabaseResultSet* DoRunQueryWithResults(const ibString& strQuery);

	// ibPreparedStatement support
	virtual ibPreparedStatement* DoPrepareStatement(const ibString& strQuery);

	// transaction support — driver-level operations; the nesting
	// counter lives on ibDatabaseLayer, see databaseLayer.h.
	virtual void DoBeginTransaction(const ibTxOptions& opts) override;
	virtual void DoCommit() override;
	virtual void DoRollBack() override;

private:

#if _USE_DYNAMIC_DATABASE_LAYER_LINKING == 1
	ibInterfacePostgres* m_pInterface;
#endif
	ibString m_strServer;
	ibString m_strDatabase;
	ibString m_strUser;
	ibString m_strPassword;
	ibString m_strPort;

	void* m_pDatabase;
	void* m_pCancel = nullptr;   // PGcancel*, made with the connection (Open) — what Cancel hands PQcancel
	// ⭐ WHO MAY FREE WHAT THE CANCEL IS USING. Cancel runs on whatever thread asks, the owner may be closing or
	// reopening at that moment, and PQcancel on a PGcancel that PQfreeCancel has just freed reads freed memory.
	// Held by Cancel for the call and by every place m_pCancel is freed or replaced (audit 2026-09-12).
	std::mutex m_cancelGuard;

	// Stashed by SetLastSqlState() — the most recent SQLSTATE libpq
	// surfaced via PQresultErrorField(PG_DIAG_SQLSTATE). Travels with
	// the next ThrowDatabaseException so admin logs see it.
	ibString m_lastSqlState;
};

#endif // __POSTGRESQL_DATABASE_LAYER_H__


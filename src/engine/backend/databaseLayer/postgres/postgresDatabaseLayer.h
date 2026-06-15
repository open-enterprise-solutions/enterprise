#ifndef __POSTGRESQL_DATABASE_LAYER_H__
#define __POSTGRESQL_DATABASE_LAYER_H__

// For compilers that support precompilation, includes "wx.h".
#include <wx/wxprec.h>

#ifdef __BORLANDC__
#pragma hdrstop
#endif

#ifndef WX_PRECOMP
#include <wx/wx.h>
#endif

#include "backend/databaseLayer/databaseLayerDef.h"
#include "backend/databaseLayer/databaseLayer.h"
#include "backend/databaseLayer/preparedStatement.h"

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
	ibDatabaseLayerPostgres(const wxString& strDatabase);
	ibDatabaseLayerPostgres(const wxString& strServer, const wxString& strDatabase);
	ibDatabaseLayerPostgres(const wxString& strDatabase, const wxString& strUser, const wxString& strPassword);
	ibDatabaseLayerPostgres(const wxString& strServer, const wxString& strDatabase, const wxString& strUser, const wxString& strPassword);
	ibDatabaseLayerPostgres(const wxString& strServer, const wxString& strPort, const wxString& strDatabase, const wxString& strUser, const wxString& strPassword);
	ibDatabaseLayerPostgres(void* pDatabase) { m_pDatabase = pDatabase; }
	ibDatabaseLayerPostgres(const ibDatabaseLayerPostgres& src);

	// dtor
	virtual ~ibDatabaseLayerPostgres();

	// open database
	virtual bool Open();
	virtual bool Open(const wxString& strDatabase);
	virtual bool Open(const wxString& strServer, const wxString& strDatabase);
	virtual bool Open(const wxString& strDatabase, const wxString& strUser, const wxString& strPassword);
	virtual bool Open(const wxString& strServer, const wxString& strDatabase, const wxString& strUser, const wxString& strPassword);
	virtual bool Open(const wxString& strServer, const wxString& strPort, const wxString& strDatabase, const wxString& strUser, const wxString& strPassword);

	// close database
	virtual bool Close();

	// Is the connection to the database open?
	virtual bool IsOpen();

	/// clone database  
	virtual ibDatabaseLayer* Clone() { return new ibDatabaseLayerPostgres(*this); }

	// IsActiveTransaction uses the base-class default (m_txDepth > 0).
	// Driver transaction primitives (DoBeginTransaction / DoCommit /
	// DoRollBack) are protected — see below.

	// Row-lock dialect now lives in the dialect dictionary (default m_rowLockSuffix=" FOR UPDATE",
	// m_rowLockNoWaitSuffix=" NOWAIT" → raises SQLSTATE 55P03 lock_not_available instead of blocking).

	// Database schema API contributed by M. Szeftel (author of wxActiveRecordGenerator)
	virtual bool DatabaseExists(const wxString& table);
	virtual bool TableExists(const wxString& table);
	virtual bool ViewExists(const wxString& view);

	virtual wxArrayString GetTables();
	virtual wxArrayString GetViews();
	virtual wxArrayString GetColumns(const wxString& table);

	virtual int GetDatabaseLayerType() const {
		return DATABASELAYER_POSTGRESQL;
	}

	// PG SQL dialect. Static holds the definition (a test reads it without
	// constructing the driver — no libpq). The virtual GetDialect() is the
	// polymorphic access point L2 uses (conn->GetDialect()).
	static const ibDialectDictionary& Dialect();
	virtual const ibDialectDictionary& GetDialect() const override;

	// PG DB temp-table facts (the first real temp target — ad-hoc CREATE TEMPORARY TABLE). Presence
	// of this (vs the base nullptr) flips PG onto the temp path; FB stays on RAM. (docs/temp-db.md)
	static const ibTempTableDialect& TempDialect();
	virtual const ibTempTableDialect* GetTempTableDialect() const override;

	static int TranslateErrorCode(int nCode);
	static bool IsAvailable();

	// Map the most recent error's SQLSTATE (set in m_lastSqlState by
	// the result-set / driver helpers when libpq surfaces a structured
	// error) to a portable Kind. SQLSTATE is the canonical PostgreSQL
	// error identifier — class digits (first two) drive most of the
	// classification (23 = integrity violation → Constraint, 40 =
	// transaction rollback → Deadlock, 42 = syntax/access → Syntax, 08
	// = connection exception → ConnectionLost).
	ibBackendDatabaseException::Kind ClassifyDatabaseError(int nativeCode) const override;
	wxString GetSqlState() const override { return m_lastSqlState; }

	// Internal hook for libpq error paths — called from places that
	// already have a PGresult* in hand to stash the SQLSTATE so the
	// next ThrowDatabaseException carries it.
	void SetLastSqlState(const wxString& s) { m_lastSqlState = s; }

protected:

	// query database
	virtual int DoRunQuery(const wxString& strQuery, bool bParseQuery);
	virtual ibDatabaseResultSet* DoRunQueryWithResults(const wxString& strQuery);

	// ibPreparedStatement support
	virtual ibPreparedStatement* DoPrepareStatement(const wxString& strQuery);

	// transaction support — driver-level operations; the nesting
	// counter lives on ibDatabaseLayer, see databaseLayer.h.
	virtual void DoBeginTransaction(const ibTxOptions& opts) override;
	virtual void DoCommit() override;
	virtual void DoRollBack() override;

private:

#if _USE_DYNAMIC_DATABASE_LAYER_LINKING == 1
	ibInterfacePostgres* m_pInterface;
#endif
	wxString m_strServer;
	wxString m_strDatabase;
	wxString m_strUser;
	wxString m_strPassword;
	wxString m_strPort;

	void* m_pDatabase;

	// Stashed by SetLastSqlState() — the most recent SQLSTATE libpq
	// surfaced via PQresultErrorField(PG_DIAG_SQLSTATE). Travels with
	// the next ThrowDatabaseException so admin logs see it.
	wxString m_lastSqlState;
};

#endif // __POSTGRESQL_DATABASE_LAYER_H__


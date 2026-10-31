#ifndef __SQLITE_DATABASE_LAYER_H__
#define __SQLITE_DATABASE_LAYER_H__

#include "backend/databaseLayer/databaseLayerDef.h"
#include "backend/databaseLayer/databaseLayer.h"

#include <mutex>   // m_cancelGuard

class ibPreparedStatement;

class BACKEND_API ibDatabaseLayerSQLite : public ibDatabaseLayer
{
public:
	// ctor()
	ibDatabaseLayerSQLite();
	ibDatabaseLayerSQLite(const ibString& strDatabase, bool mustExist = false);
	ibDatabaseLayerSQLite(void* pDatabase) { m_pDatabase = pDatabase; }
	ibDatabaseLayerSQLite(const ibDatabaseLayerSQLite& src);

	// dtor()
	virtual ~ibDatabaseLayerSQLite();

	// open database
	virtual bool Open(const ibString& strDatabase);
	virtual bool Open(const ibString& strDatabase, bool mustExist);

	// close database  
	virtual bool Close();

	// Is the connection to the database open?
	virtual bool IsOpen();

	// Cancel what this connection is running — sqlite3_interrupt, from any thread (see the base).
	virtual void Cancel();

	/// clone database
	virtual ibDatabaseLayer* Clone() { return new ibDatabaseLayerSQLite(*this); }

	// IsActiveTransaction inherits the base-class default
	// (m_txDepth > 0). Driver transaction primitives
	// (DoBeginTransaction / DoCommit / DoRollBack) are protected —
	// see below.

	// Database schema API contributed by M. Szeftel (author of wxActiveRecordGenerator)
	virtual bool TableExists(const ibString& table);
	virtual bool ViewExists(const ibString& view);
	virtual std::vector<ibString> GetTables();
	virtual std::vector<ibString> GetViews();
	virtual std::vector<ibString> GetColumns(const ibString& table);

	virtual int GetDatabaseLayerType() const {
		return DATABASELAYER_SQLLITE;
	}

	static const ibDialectDictionary& Dialect();                       // SQLite dialect (no instance needed)
	virtual const ibDialectDictionary& GetDialect() const override;    // polymorphic access for L2

	// DB temp-table capability (temp-db foundation): SQLite does ad-hoc CREATE TEMPORARY
	// TABLE of any shape, connection-scoped. Its PRESENCE flips SQLite off the RAM floor
	// onto the server-side temp path (heterogeneous JOIN promote works on the embedded DB).
	static const ibTempTableDialect& TempDialect();                    // SQLite temp dialect (no instance needed)
	virtual const ibTempTableDialect* GetTempTableDialect() const override;

	// Derived-state materialisation (register totals). SQLite is the FLOOR the per-row
	// family is designed against — statements only, no procedural block — so a body that
	// fits here fits Firebird / PostgreSQL too. (docs/private/register-totals-strategy.md)
	static const ibMaterializationDialect& MaterializationDialect();
	virtual const ibMaterializationDialect* GetMaterializationDialect() const override;

	static int TranslateErrorCode(int nCode);
	// The portable Kind of an SQLite result code, recorded with it wherever the driver reads one
	// (SetErrorCode(code, kind)) — the layer, the prepared statement and the result set. SQLite has
	// no SQLSTATE. BUSY / LOCKED is a lock wait under our thread model; ERROR is the catch-all "SQL
	// error or missing table". An extended code (SQLITE_CONSTRAINT_PRIMARYKEY = 1555) is its low byte.
	static ibBackendDatabaseException::Kind ClassifyDatabaseError(int code);

protected:

	// query database
	virtual int DoRunQuery(const ibString& strQuery, bool bParseQuery);
	virtual ibDatabaseResultSet* DoRunQueryWithResults(const ibString& strQuery);

	// ibPreparedStatement support
	virtual ibPreparedStatement* DoPrepareStatement(const ibString& strQuery);
	ibPreparedStatement* DoPrepareStatement(const ibString& strQuery, bool bLogForCleanup);

	// transaction support — driver-level operations; the nesting
	// counter lives on ibDatabaseLayer, see databaseLayer.h.
	virtual void DoBeginTransaction(const ibTxOptions& opts) override;
	virtual void DoCommit() override;
	virtual void DoRollBack() override;

private:

	//sqlite3* m_pDatabase;
	void* m_pDatabase;
	ibString m_strDatabasePath;
	// ⭐ WHO MAY CLOSE WHAT THE CANCEL IS USING. sqlite3_interrupt is safe from another thread, but not on a
	// connection that is closed or closes before it returns (SQLite's own words) — and Cancel runs on whatever
	// thread asks while the owner may be closing. Held by Cancel for the call and wherever m_pDatabase is opened
	// or closed (audit 2026-09-12).
	std::mutex m_cancelGuard;
};

#endif // __SQLITE_DATABASE_LAYER_H__


#ifndef __SQLITE_DATABASE_LAYER_H__
#define __SQLITE_DATABASE_LAYER_H__

// For compilers that support precompilation, includes "wx.h".
#include <wx/wxprec.h>

#ifdef __BORLANDC__
#pragma hdrstop
#endif

#ifndef WX_PRECOMP
#include <wx/wx.h>
#endif

#include <wx/arrstr.h>

#include "backend/databaseLayer/databaseLayerDef.h"
#include "backend/databaseLayer/databaseLayer.h"

class ibPreparedStatement;

class BACKEND_API ibDatabaseLayerSQLite : public ibDatabaseLayer
{
public:
	// ctor()
	ibDatabaseLayerSQLite();
	ibDatabaseLayerSQLite(const wxString& strDatabase, bool mustExist = false);
	ibDatabaseLayerSQLite(void* pDatabase) { m_pDatabase = pDatabase; }
	ibDatabaseLayerSQLite(const ibDatabaseLayerSQLite& src);

	// dtor()
	virtual ~ibDatabaseLayerSQLite();

	// open database
	virtual bool Open(const wxString& strDatabase);
	virtual bool Open(const wxString& strDatabase, bool mustExist);

	// close database  
	virtual bool Close();

	// Is the connection to the database open?
	virtual bool IsOpen();

	/// clone database  
	virtual ibDatabaseLayer* Clone() { return new ibDatabaseLayerSQLite(*this); }

	// IsActiveTransaction inherits the base-class default
	// (m_txDepth > 0). Driver transaction primitives
	// (DoBeginTransaction / DoCommit / DoRollBack) are protected —
	// see below.

	// Database schema API contributed by M. Szeftel (author of wxActiveRecordGenerator)
	virtual bool TableExists(const wxString& table);
	virtual bool ViewExists(const wxString& view);
	virtual wxArrayString GetTables();
	virtual wxArrayString GetViews();
	virtual wxArrayString GetColumns(const wxString& table);

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
	// fits here fits Firebird / PostgreSQL / MySQL too. (docs/register-totals-strategy.md)
	static const ibMaterializationDialect& MaterializationDialect();
	virtual const ibMaterializationDialect* GetMaterializationDialect() const override;

	static int TranslateErrorCode(int nCode);

	// SQLite has no SQLSTATE — classification reads its single-int
	// SQLITE_* result code (m_nErrorCode is the raw sqlite3_errcode()).
	// Common codes:
	//   SQLITE_BUSY (5) / SQLITE_LOCKED (6) → Timeout (single-process
	//       lock contention — under our wxThread / worker-pool model
	//       this is effectively a wait timeout)
	//   SQLITE_CONSTRAINT (19)              → Constraint
	//   SQLITE_ERROR (1) / SQLITE_MISUSE (21) → Syntax (best-effort —
	//       SQLITE_ERROR is the catch-all "SQL error or missing
	//       database", most often a parse/schema issue)
	ibBackendDatabaseException::Kind ClassifyDatabaseError(int nativeCode) const override;

protected:

	// query database
	virtual int DoRunQuery(const wxString& strQuery, bool bParseQuery);
	virtual ibDatabaseResultSet* DoRunQueryWithResults(const wxString& strQuery);

	// ibPreparedStatement support
	virtual ibPreparedStatement* DoPrepareStatement(const wxString& strQuery);
	ibPreparedStatement* DoPrepareStatement(const wxString& strQuery, bool bLogForCleanup);

	// transaction support — driver-level operations; the nesting
	// counter lives on ibDatabaseLayer, see databaseLayer.h.
	virtual void DoBeginTransaction(const ibTxOptions& opts) override;
	virtual void DoCommit() override;
	virtual void DoRollBack() override;

private:

	//sqlite3* m_pDatabase;
	void* m_pDatabase;
	wxString m_strDatabasePath;
};

#endif // __SQLITE_DATABASE_LAYER_H__


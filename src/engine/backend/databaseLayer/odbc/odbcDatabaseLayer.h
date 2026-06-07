#ifndef __ODBC_DATABASE_LAYER_H__ibDatabaseLayerODBC
#define __ODBC_DATABASE_LAYER_H__

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

class ibInterfaceODBC;

#define ERR_BUFFER_LEN 1024
#define ERR_STATE_LEN 10

class BACKEND_API ibDatabaseLayerODBC : public ibDatabaseLayer
{
public:
	// ctor()
	ibDatabaseLayerODBC();
	ibDatabaseLayerODBC(const ibDatabaseLayerODBC& src);

	// dtor()
	virtual ~ibDatabaseLayerODBC();

	// open database
	virtual bool Open();
	virtual bool Open(const wxString& strConnection);
	virtual bool Open(const wxString& strDSN, const wxString& strUser, const wxString& strPassword);
#if wxUSE_GUI
	virtual bool Open(const wxString& strConnection, bool bPromptForInfo, wxWindow* parent = nullptr);
#endif

	// close database  
	virtual bool Close();

	// Is the connection to the database open?
	virtual bool IsOpen();

	/// clone database  
	virtual ibDatabaseLayer* Clone() { return new ibDatabaseLayerODBC(*this); }

	// IsActiveTransaction inherits the base-class default
	// (m_txDepth > 0). Driver transaction primitives
	// (DoBeginTransaction / DoCommit / DoRollBack) are protected —
	// see below.

	// Row-lock probe for ibSessionRegistry. MSSQL behind ODBC honours
	// `SET LOCK_TIMEOUT 0` + `SELECT ... WITH (UPDLOCK, ROWLOCK)`;
	// other ODBC backends usually ignore the timeout hint and just
	// block — the registry avoids calling this on those.
	virtual bool TryProbeRowLock(const wxString& tableName,
		const wxString& pkColumn, const wxString& pkValue) override;

	// Write-time row-lock dialect (see docs/record-locks.md). MSSQL
	// behind ODBC takes the row lock via table hint
	// "WITH (UPDLOCK, ROWLOCK)" placed after FROM <table>. Callers that
	// build the SELECT need to place the hint inline (not at end of
	// statement); a future helper on the layer can wrap that. NOWAIT
	// is carried session-side via `SET LOCK_TIMEOUT 0` (ibTxOptions::
	// noWait), so the SQL clause stays empty.
	wxString RowLockHint() const override { return wxT("WITH (UPDLOCK, ROWLOCK)"); }
	wxString NoWaitClause() const override { return wxEmptyString; }

	// Database schema API contributed by M. Szeftel (author of wxActiveRecordGenerator)
	virtual bool TableExists(const wxString& table);
	virtual bool ViewExists(const wxString& view);
	virtual wxArrayString GetTables();
	virtual wxArrayString GetViews();
	virtual wxArrayString GetColumns(const wxString& table);

	virtual int GetDatabaseLayerType() const {
		return DATABASELAYER_ODBC;
	}

	// ODBC fronts many backends — the dialect is the default-constructed ANSI
	// baseline (? params, LIMIT/OFFSET). This is the home of the generic default.
	static const ibDialectDictionary& Dialect() {
		static const ibDialectDictionary s_dialect;   // default ctor = ANSI baseline
		return s_dialect;
	}
	virtual const ibDialectDictionary& GetDialect() const override { return Dialect(); }

	static bool IsAvailable();

	// SQLSTATE-based classification — ODBC's standard error identifier
	// is the same 5-char SQLSTATE as SQL standard / PostgreSQL. Backends
	// reachable through ODBC (MSSQL, Oracle, DB2, etc.) all funnel their
	// errors through this format, so the class-digit dispatch is the
	// portable choice.
	ibBackendDatabaseException::Kind ClassifyDatabaseError(int nativeCode) const override;
	wxString GetSqlState() const override { return m_lastSqlState; }

	// Stash the most recent SQLSTATE pulled from SQLGetDiagRec so the
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

	virtual ibPreparedStatement* DoPrepareStatement(const wxString& strQuery, bool bParseQuery);

	//SQLHENV m_sqlEnvHandle;
	void* m_sqlEnvHandle;
	//SQLHDBC m_sqlHDBC;
	void* m_sqlHDBC;

	wxString m_strDSN;
	wxString m_strUser;
	wxString m_strPassword;

	wxString m_strConnection;

#if wxUSE_GUI
	bool m_bPrompt;
	wxWindow* m_parentContext;
#endif

	bool m_bIsConnected;
	ibInterfaceODBC* m_pInterface;

	// Stashed by SetLastSqlState() — most recent SQLSTATE pulled from
	// SQLGetDiagRec. Travels with the next ThrowDatabaseException so
	// admin logs see what the driver actually reported.
	wxString m_lastSqlState;

public:

	// error handling
	//void InterpretErrorCodes( long nCode, SQLHSTMT stmth_ptr = nullptr );
	void InterpretErrorCodes(long nCode, void* stmth_ptr = nullptr);

	//SQLHANDLE allocStmth();
	void* allocStmth();
};

#endif // __ODBC_DATABASE_LAYER_H__


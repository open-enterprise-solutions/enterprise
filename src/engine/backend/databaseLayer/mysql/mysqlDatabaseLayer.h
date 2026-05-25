#ifndef __MYSQL_DATABASE_LAYER_H__
#define __MYSQL_DATABASE_LAYER_H__

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
#include "backend/databaseLayer/preparedStatement.h"

#if _USE_DYNAMIC_DATABASE_LAYER_LINKING == 1
class ibInterfaceMySQL;
#endif

WX_DECLARE_VOIDPTR_HASH_MAP(void*, PointerLookupMap);

class BACKEND_API ibDatabaseLayerMySQL : public ibDatabaseLayer
{
public:
	// Information that can be specified for a MySQL database
	//  host or hostaddr
	//  port
	//  dbname
	//  user
	//  password
	// ctor
	ibDatabaseLayerMySQL();
	ibDatabaseLayerMySQL(const wxString& strDatabase);
	ibDatabaseLayerMySQL(const wxString& strServer, const wxString& strDatabase);
	ibDatabaseLayerMySQL(const wxString& strDatabase, const wxString& strUser, const wxString& strPassword);
	ibDatabaseLayerMySQL(const wxString& strServer, const wxString& strDatabase, const wxString& strUser, const wxString& strPassword);
	ibDatabaseLayerMySQL(void* pDatabase) { m_pDatabase = pDatabase; }
	ibDatabaseLayerMySQL(const ibDatabaseLayerMySQL& src);

	// dtor
	virtual ~ibDatabaseLayerMySQL();

	// open database
	virtual bool Open(const wxString& strDatabase);
	virtual bool Open(const wxString& strServer, const wxString& strDatabase);
	virtual bool Open(const wxString& strDatabase, const wxString& strUser, const wxString& strPassword);
	virtual bool Open(const wxString& strServer, const wxString& strDatabase, const wxString& strUser, const wxString& strPassword);

	// close database
	virtual bool Close();

	// Is the connection to the database open?
	virtual bool IsOpen();

	/// clone database  
	virtual ibDatabaseLayer* Clone() { return new ibDatabaseLayerMySQL(*this); }

	// IsActiveTransaction uses the base-class default (m_txDepth > 0).
	// Driver transaction primitives (DoBeginTransaction / DoCommit /
	// DoRollBack) are protected — see below.

	// Row-lock probe — SESSION innodb_lock_wait_timeout=1 inside
	// BeginTransaction(noWait) + SELECT ... FOR UPDATE. Surfaces a held
	// row as an exception within ~1s instead of blocking the sweep.
	virtual bool TryProbeRowLock(const wxString& tableName,
		const wxString& pkColumn, const wxString& pkValue) override;

	// Write-time row-lock dialect (see docs/record-locks.md). MySQL/
	// InnoDB locks via "SELECT ... FOR UPDATE"; "NOWAIT" is honoured on
	// MySQL 8+. Older versions ignore the keyword and rely on the
	// session-level `innodb_lock_wait_timeout=1` already plumbed via
	// ibTxOptions::noWait — both paths fail fast under contention.
	wxString RowLockHint() const override { return wxT("FOR UPDATE"); }
	wxString NoWaitClause() const override { return wxT("NOWAIT"); }

	// Database schema API contributed by M. Szeftel (author of wxActiveRecordGenerator)
	virtual bool TableExists(const wxString& table);
	virtual bool ViewExists(const wxString& view);
	virtual wxArrayString GetTables();
	virtual wxArrayString GetViews();
	virtual wxArrayString GetColumns(const wxString& table);

	virtual int GetDatabaseLayerType() const {
		return DATABASELAYER_MYSQL;
	}

	static int TranslateErrorCode(int nCode);
	static bool IsAvailable();

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
	void InitDatabase();
	void ParseServerAndPort(const wxString& strServer);

#if _USE_DYNAMIC_DATABASE_LAYER_LINKING == 1
	ibInterfaceMySQL* m_pInterface;
#endif
	wxString m_strServer;
	wxString m_strDatabase;
	wxString m_strUser;
	wxString m_strPassword;
	int m_iPort;

	void* m_pDatabase;

#if wxUSE_UNICODE
	PointerLookupMap m_ResultSets;
#endif
};

#endif // __MYSQL_DATABASE_LAYER_H__


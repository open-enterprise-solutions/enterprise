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

	// Row-lock dialect now lives in the dialect dictionary (default m_rowLockSuffix=" FOR UPDATE",
	// m_rowLockNoWaitSuffix=" NOWAIT"); NOWAIT is honoured on MySQL 8+, older rely on the
	// session-level innodb_lock_wait_timeout=1 (ibTxOptions::noWait).

	// Database schema API contributed by M. Szeftel (author of wxActiveRecordGenerator)
	virtual bool TableExists(const wxString& table);
	virtual bool ViewExists(const wxString& view);
	virtual wxArrayString GetTables();
	virtual wxArrayString GetViews();
	virtual wxArrayString GetColumns(const wxString& table);

	virtual int GetDatabaseLayerType() const {
		return DATABASELAYER_MYSQL;
	}

	static const ibDialectDictionary& Dialect();                       // MySQL dialect (no instance needed)
	virtual const ibDialectDictionary& GetDialect() const override;    // polymorphic access for L2

	// Derived-state materialisation (register totals). MySQL is the third accumulate
	// spelling — ON DUPLICATE KEY UPDATE, which names the incoming value inline as
	// VALUES(col) and has no source alias at all. (docs/register-totals-strategy.md)
	static const ibMaterializationDialect& MaterializationDialect();
	virtual const ibMaterializationDialect* GetMaterializationDialect() const override;

	static int TranslateErrorCode(int nCode);
	static bool IsAvailable();

	// Map a MySQL errno (as returned by mysql_errno()) to a portable
	// Kind. Common codes:
	//   1213 = ER_LOCK_DEADLOCK     → Deadlock
	//   1205 = ER_LOCK_WAIT_TIMEOUT → Timeout
	//   1062 = ER_DUP_ENTRY         → Constraint
	//   1452 = ER_NO_REFERENCED_ROW → Constraint
	//   1064 = ER_PARSE_ERROR       → Syntax
	//   2002 = CR_CONNECTION_ERROR  → ConnectionLost
	//   2006 = CR_SERVER_GONE_ERROR → ConnectionLost
	//   2013 = CR_SERVER_LOST       → ConnectionLost
	ibBackendDatabaseException::Kind ClassifyDatabaseError(int nativeCode) const override;

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


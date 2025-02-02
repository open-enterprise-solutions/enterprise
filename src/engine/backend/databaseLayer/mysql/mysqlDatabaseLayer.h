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
class CMysqlInterface;
#endif

WX_DECLARE_VOIDPTR_HASH_MAP(void*, PointerLookupMap);

class BACKEND_API CMysqlDatabaseLayer : public IDatabaseLayer
{
public:
	// Information that can be specified for a MySQL database
	//  host or hostaddr
	//  port
	//  dbname
	//  user
	//  password
	// ctor
	CMysqlDatabaseLayer();
	CMysqlDatabaseLayer(const wxString& strDatabase);
	CMysqlDatabaseLayer(const wxString& strServer, const wxString& strDatabase);
	CMysqlDatabaseLayer(const wxString& strDatabase, const wxString& strUser, const wxString& strPassword);
	CMysqlDatabaseLayer(const wxString& strServer, const wxString& strDatabase, const wxString& strUser, const wxString& strPassword);
	CMysqlDatabaseLayer(void* pDatabase) { m_pDatabase = pDatabase; }

	// dtor
	virtual ~CMysqlDatabaseLayer();

	// open database
	virtual bool Open(const wxString& strDatabase);
	virtual bool Open(const wxString& strServer, const wxString& strDatabase);
	virtual bool Open(const wxString& strDatabase, const wxString& strUser, const wxString& strPassword);
	virtual bool Open(const wxString& strServer, const wxString& strDatabase, const wxString& strUser, const wxString& strPassword);

	// close database
	virtual bool Close();

	// Is the connection to the database open?
	virtual bool IsOpen();

	// transaction support
	// transaction support
	virtual void BeginTransaction();
	virtual void Commit();
	virtual void RollBack();

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
	virtual IDatabaseResultSet* DoRunQueryWithResults(const wxString& strQuery);

	// IPreparedStatement support
	virtual IPreparedStatement* DoPrepareStatement(const wxString& strQuery);

private:
	void InitDatabase();
	void ParseServerAndPort(const wxString& strServer);

#if _USE_DYNAMIC_DATABASE_LAYER_LINKING == 1
	CMysqlInterface* m_pInterface;
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


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
class CPostgresInterface;
#endif

class BACKEND_API CPostgresDatabaseLayer : public IDatabaseLayer
{
public:
	// Information that can be specified for a PostgreSQL database
	//  host or hostaddr
	//  port
	//  dbname
	//  user
	//  password
	// ctor
	CPostgresDatabaseLayer();
	CPostgresDatabaseLayer(const wxString& strDatabase);
	CPostgresDatabaseLayer(const wxString& strServer, const wxString& strDatabase);
	CPostgresDatabaseLayer(const wxString& strDatabase, const wxString& strUser, const wxString& strPassword);
	CPostgresDatabaseLayer(const wxString& strServer, const wxString& strDatabase, const wxString& strUser, const wxString& strPassword);
	CPostgresDatabaseLayer(const wxString& strServer, const wxString& strPort, const wxString& strDatabase, const wxString& strUser, const wxString& strPassword);
	CPostgresDatabaseLayer(void* pDatabase) { m_pDatabase = pDatabase; }

	// dtor
	virtual ~CPostgresDatabaseLayer();

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

	// transaction support
	virtual void BeginTransaction();
	virtual void Commit();
	virtual void RollBack();

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

	static int TranslateErrorCode(int nCode);
	static bool IsAvailable();

protected:

	// query database
	virtual int DoRunQuery(const wxString& strQuery, bool bParseQuery);
	virtual IDatabaseResultSet* DoRunQueryWithResults(const wxString& strQuery);

	// IPreparedStatement support
	virtual IPreparedStatement* DoPrepareStatement(const wxString& strQuery);

private:
#if _USE_DYNAMIC_DATABASE_LAYER_LINKING == 1
	CPostgresInterface* m_pInterface;
#endif
	wxString m_strServer;
	wxString m_strDatabase;
	wxString m_strUser;
	wxString m_strPassword;
	wxString m_strPort;

	void* m_pDatabase;
};

#endif // __POSTGRESQL_DATABASE_LAYER_H__


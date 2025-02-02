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

class IPreparedStatement;

class BACKEND_API CSqliteDatabaseLayer : public IDatabaseLayer
{
public:
	// ctor()
	CSqliteDatabaseLayer();
	CSqliteDatabaseLayer(const wxString& strDatabase, bool mustExist = false);
	CSqliteDatabaseLayer(void* pDatabase) { m_pDatabase = pDatabase; }

	// dtor()
	virtual ~CSqliteDatabaseLayer();

	// open database
	virtual bool Open(const wxString& strDatabase);
	virtual bool Open(const wxString& strDatabase, bool mustExist);

	// close database  
	virtual bool Close();

	// Is the connection to the database open?
	virtual bool IsOpen();

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
		return DATABASELAYER_SQLLITE;
	}

	static int TranslateErrorCode(int nCode);

protected:

	// query database
	virtual int DoRunQuery(const wxString& strQuery, bool bParseQuery);
	virtual IDatabaseResultSet* DoRunQueryWithResults(const wxString& strQuery);

	// IPreparedStatement support
	virtual IPreparedStatement* DoPrepareStatement(const wxString& strQuery);
	IPreparedStatement* DoPrepareStatement(const wxString& strQuery, bool bLogForCleanup);

private:

	//sqlite3* m_pDatabase;
	void* m_pDatabase;
};

#endif // __SQLITE_DATABASE_LAYER_H__


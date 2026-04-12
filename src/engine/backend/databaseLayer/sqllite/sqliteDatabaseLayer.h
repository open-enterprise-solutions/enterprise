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

	// transaction support
	virtual void BeginTransaction();
	virtual void Commit();
	virtual void RollBack();

	virtual bool IsActiveTransaction();

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
	virtual ibDatabaseResultSet* DoRunQueryWithResults(const wxString& strQuery);

	// ibPreparedStatement support
	virtual ibPreparedStatement* DoPrepareStatement(const wxString& strQuery);
	ibPreparedStatement* DoPrepareStatement(const wxString& strQuery, bool bLogForCleanup);

private:

	//sqlite3* m_pDatabase;
	void* m_pDatabase;
	wxString m_strDatabasePath;
};

#endif // __SQLITE_DATABASE_LAYER_H__


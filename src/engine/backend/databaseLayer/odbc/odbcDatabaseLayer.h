#ifndef __ODBC_DATABASE_LAYER_H__
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

class COdbcInterface;

#define ERR_BUFFER_LEN 1024
#define ERR_STATE_LEN 10

class BACKEND_API COdbcDatabaseLayer : public IDatabaseLayer
{
public:
	// ctor()
	COdbcDatabaseLayer();

	// dtor()
	virtual ~COdbcDatabaseLayer();

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
		return DATABASELAYER_ODBC;
	}

	static bool IsAvailable();

protected:
	
	// query database
	virtual int DoRunQuery(const wxString& strQuery, bool bParseQuery);
	virtual IDatabaseResultSet* DoRunQueryWithResults(const wxString& strQuery);

	// IPreparedStatement support
	virtual IPreparedStatement* DoPrepareStatement(const wxString& strQuery);

private:

	virtual IPreparedStatement* DoPrepareStatement(const wxString& strQuery, bool bParseQuery);

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
	COdbcInterface* m_pInterface;

public:

	// error handling
	//void InterpretErrorCodes( long nCode, SQLHSTMT stmth_ptr = nullptr );
	void InterpretErrorCodes(long nCode, void* stmth_ptr = nullptr);

	//SQLHANDLE allocStmth();
	void* allocStmth();
};

#endif // __ODBC_DATABASE_LAYER_H__


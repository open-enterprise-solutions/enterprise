#ifndef __FIREBIRD_DATABASE_LAYER_H__
#define __FIREBIRD_DATABASE_LAYER_H__

#include "backend/databaseLayer/databaseLayerDef.h"
#include "backend/databaseLayer/databaseLayer.h"
#include "backend/databaseLayer/firebird/engine/ibase.h"

#if _USE_DYNAMIC_DATABASE_LAYER_LINKING == 1
class ibInterfaceFirebird;
#endif

class BACKEND_API ibDatabaseLayerFirebird : public ibDatabaseLayer
{
	const int16_t m_pageSize = 8192;

	typedef struct fb_tr_list {
#if defined(_LP64) || defined(__LP64__) || defined(__arch64__) || defined(_WIN64)
		unsigned int m_pTransaction;
#else
		void *m_pTransaction;
#endif
		struct fb_tr_list *prev;
	} fb_tr_list_t;

public:
	// ctor()
	ibDatabaseLayerFirebird();
	ibDatabaseLayerFirebird(const wxString& strDatabase);
	ibDatabaseLayerFirebird(const wxString& strDatabase, const wxString& strUser, const wxString& strPassword);
	ibDatabaseLayerFirebird(const wxString& strServer, const wxString& strDatabase, const wxString& strUser, const wxString& strPassword);
	ibDatabaseLayerFirebird(const wxString& strServer, const wxString& strDatabase, const wxString& strUser, const wxString& strPassword, const wxString& strRole);

	ibDatabaseLayerFirebird(isc_db_handle pDatabase) { m_pDatabase = pDatabase; }

	ibDatabaseLayerFirebird(const ibDatabaseLayerFirebird& src);

	// dtor()
	virtual ~ibDatabaseLayerFirebird();

	// open database
	virtual bool Open();
	virtual bool Open(const wxString& strDatabase);
	virtual bool Open(const wxString& strDatabase, const wxString& strUser, const wxString& strPassword);
	virtual bool Open(const wxString& strServer, const wxString& strDatabase, const wxString& strUser = wxEmptyString, const wxString& strPassword = wxEmptyString);

	// close database  
	virtual bool Close();

	// Is the connection to the database open?
	virtual bool IsOpen();

	/// clone database  
	virtual ibDatabaseLayer* Clone() { return new ibDatabaseLayerFirebird(*this); }

	// transaction support
	virtual void BeginTransaction();
	virtual void Commit();
	virtual void RollBack();
	
	virtual bool IsActiveTransaction();

	static int TranslateErrorCode(int nCode);
	//static wxString TranslateErrorCodeToString(ibInterfaceFirebird* pInterface, int nCode, ISC_STATUS_ARRAY status);
	static wxString TranslateErrorCodeToString(ibInterfaceFirebird* pInterface, int nCode, void* status);
	static bool IsAvailable();

	void SetServer(const wxString& strServer) { m_strServer = strServer; }
	void SetDatabase(const wxString& strDatabase) { m_strDatabase = strDatabase; }
	void SetUser(const wxString& strUser) { m_strUser = strUser; }
	void SetPassword(const wxString& strPassword) { m_strPassword = strPassword; }
	void SetRole(const wxString& strRole) { m_strRole = strRole; }

	// Database schema API contributed by M. Szeftel (author of wxActiveRecordGenerator)
	virtual bool TableExists(const wxString& table);
	virtual bool ViewExists(const wxString& view);
	virtual wxArrayString GetTables();
	virtual wxArrayString GetViews();
	virtual wxArrayString GetColumns(const wxString& table);

	virtual int GetDatabaseLayerType() const {
		return DATABASELAYER_FIREBIRD;
	}

protected:

	// query database
	virtual int DoRunQuery(const wxString& strQuery, bool bParseQuery);
	virtual ibDatabaseResultSet* DoRunQueryWithResults(const wxString& strQuery);

	// ibPreparedStatement support
	virtual ibPreparedStatement* DoPrepareStatement(const wxString& strQuery);

private:

	void InterpretErrorCodes();

#if _USE_DYNAMIC_DATABASE_LAYER_LINKING == 1
	ibInterfaceFirebird* m_pInterface;
#endif

	wxString m_strServer;
	wxString m_strDatabase;
	wxString m_strUser;
	wxString m_strPassword;
	wxString m_strRole;

	fb_tr_list_t *m_fbNode;

	isc_db_handle m_pDatabase;
	void *m_pStatus;
};

#endif // __FIREBIRD_DATABASE_LAYER_H__


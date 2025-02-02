#ifndef __FIREBIRD_PREPARED_STATEMENT_WRAPPER_H__
#define __FIREBIRD_PREPARED_STATEMENT_WRAPPER_H__

// For compilers that support precompilation, includes "wx.h".
#include <wx/wxprec.h>

#ifdef __BORLANDC__
#pragma hdrstop
#endif

#ifndef WX_PRECOMP
#include <wx/wx.h>
#endif

#include "engine/ibase.h"

#include "backend/databaseLayer/databaseErrorReporter.h"
#include "backend/databaseLayer/databaseStringConverter.h"

#include "firebirdParameterCollection.h"
#include "firebirdInterface.h"

class IDatabaseResultSet;

class CFirebirdPreparedStatementWrapper : public CDatabaseErrorReporter, public CDatabaseStringConverter
{
public:
	// ctor
	CFirebirdPreparedStatementWrapper(CFirebirdInterface* pInterface, isc_db_handle pDatabase, isc_tr_handle pTransaction, const wxString& strSQL);

	// dtor
	virtual ~CFirebirdPreparedStatementWrapper();

	bool Prepare(const wxString& strSQL);
	bool Prepare();

	// set field
	void SetParam(int nPosition, int nValue);
	void SetParam(int nPosition, double dblValue);
	void SetParam(int nPosition, const number_t& numValue);
	void SetParam(int nPosition, const wxString& strValue);
	void SetParam(int nPosition);
	void SetParam(int nPosition, const void* pData, long nDataLength);
	void SetParam(int nPosition, const wxDateTime& dateValue);
	void SetParam(int nPosition, bool bValue);
	int GetParameterCount();

	int DoRunQuery();
	IDatabaseResultSet* DoRunQueryWithResults();

	void SetTransaction(isc_tr_handle pTransaction) { m_pTransaction = pTransaction; }
	bool IsSelectQuery();

private:
	void InterpretErrorCodes();

	wxString m_strSQL;
	isc_stmt_handle m_pStatement;
	XSQLDA* m_pParameters;
	isc_db_handle m_pDatabase;
	isc_tr_handle m_pTransaction;

	CFirebirdParameterCollection* m_pParameterCollection;

	ISC_STATUS_ARRAY m_Status;
	CFirebirdInterface* m_pInterface;

	bool m_bManageStatement;
	bool m_bManageTransaction;
};

#endif // __FIREBIRD_PREPARED_STATEMENT_WRAPPER_H__


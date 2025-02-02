#ifndef __POSTGRESQL_PREPARED_STATEMENT_WRAPPER_H__
#define __POSTGRESQL_PREPARED_STATEMENT_WRAPPER_H__

// For compilers that support precompilation, includes "wx.h".
#include <wx/wxprec.h>

#ifdef __BORLANDC__
#pragma hdrstop
#endif

#ifndef WX_PRECOMP
#include <wx/wx.h>
#endif

#include "postgresPreparedStatementParameterCollection.h"
#include "postgresInterface.h"

#include "backend/databaseLayer/databaseErrorReporter.h"
#include "backend/databaseLayer/databaseStringConverter.h"

#include "engine/libpq-fe.h"

class IDatabaseResultSet;

class CPostgresPreparedStatementWrapper : public CDatabaseErrorReporter, public CDatabaseStringConverter
{
public:
	// ctor
	CPostgresPreparedStatementWrapper(CPostgresInterface* pInterface, PGconn* pDatabase, const wxString& strSQL, const wxString& strStatementName);

	// dtor
	virtual ~CPostgresPreparedStatementWrapper();

	// set field
	void SetParam(int nPosition, int nValue);
	void SetParam(int nPosition, double dblValue);
	void SetParam(int nPosition, const number_t& dblValue);
	void SetParam(int nPosition, const wxString& strValue);
	void SetParam(int nPosition);
	void SetParam(int nPosition, const void* pData, long nDataLength);
	void SetParam(int nPosition, const wxDateTime& dateValue);
	void SetParam(int nPosition, bool bValue);
	int GetParameterCount();

	int DoRunQuery();
	IDatabaseResultSet* DoRunQueryWithResults();

private:
	CPostgresInterface* m_pInterface;
	PGconn* m_pDatabase;
	wxString m_strSQL;
	wxString m_strStatementName;

	CPostgresPreparedStatementParameterCollection m_Parameters;
};

#endif // __POSTGRESQL_PREPARED_STATEMENT_WRAPPER_H__


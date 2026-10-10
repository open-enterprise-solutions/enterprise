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

class ibDatabaseResultSet;

class ibPreparedStatementPostgresWrapper : public ibDatabaseErrorReporter, public ibDatabaseStringConverter
{
public:
	// ctor
	ibPreparedStatementPostgresWrapper(ibInterfacePostgres* pInterface, PGconn* pDatabase, const wxString& strSQL, const wxString& strStatementName);

	// dtor
	virtual ~ibPreparedStatementPostgresWrapper();

	// set field
	void SetParam(int nPosition, int nValue);
	void SetParam(int nPosition, double dblValue);
	void SetParam(int nPosition, const ibNumber& dblValue);
	void SetParam(int nPosition, const ibString& strValue);
	void SetParam(int nPosition);
	void SetParam(int nPosition, const void* pData, long nDataLength);
	void SetParam(int nPosition, const ibDateTimeParts& date);
	void SetParam(int nPosition, bool bValue);
	int GetParameterCount();

	int DoRunQuery();
	ibDatabaseResultSet* DoRunQueryWithResults();

	// The statement throws from here, not from the layer, so the SQLSTATE
	// has to be on this reporter or the exception leaves with an empty one.
	ibBackendDatabaseException::Kind ClassifyDatabaseError(int nativeCode) const override;
	wxString GetSqlState() const override { return m_sqlState; }

	// Frees the statement on the server. Called by its owner's Close, once — not by this dtor: the owner's
	// array holds copies, and a temporary copy dying would free a statement still in use.
	void Deallocate();

private:
	ibInterfacePostgres* m_pInterface;
	PGconn* m_pDatabase;
	wxString m_strSQL;
	wxString m_strStatementName;

	ibPreparedStatementPostgresParameterCollection m_Parameters;
	wxString m_sqlState;
};

#endif // __POSTGRESQL_PREPARED_STATEMENT_WRAPPER_H__


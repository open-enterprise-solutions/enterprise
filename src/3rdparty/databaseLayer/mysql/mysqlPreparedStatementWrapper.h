#ifndef __MYSQL_PREPARED_STATEMENT_WRAPPER_H__
#define __MYSQL_PREPARED_STATEMENT_WRAPPER_H__

// For compilers that support precompilation, includes "wx.h".
#include <wx/wxprec.h>

#ifdef __BORLANDC__
#pragma hdrstop
#endif

#ifndef WX_PRECOMP
#include <wx/wx.h>
#endif

#include "engine/mysql.h"

#include <3rdparty/databaseLayer/databaseErrorReporter.h>
#include <3rdparty/databaseLayer/databaseStringConverter.h>

#include "mysqlPreparedStatementParameterCollection.h"
#include "mysqlInterface.h"

class DatabaseResultSet;

class MysqlPreparedStatementWrapper : public DatabaseErrorReporter, public DatabaseStringConverter
{
public:
	// ctor
	MysqlPreparedStatementWrapper(MysqlInterface* pInterface, MYSQL_STMT* pStatement);

	// dtor
	virtual ~MysqlPreparedStatementWrapper();

	void Close();

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

	int RunQuery();
	DatabaseResultSet* RunQueryWithResults();

private:
	MysqlInterface* m_pInterface;
	MYSQL_STMT* m_pStatement;

	MysqlPreparedStatementParameterCollection m_Parameters;
};

#endif // __MYSQL_PREPARED_STATEMENT_WRAPPER_H__


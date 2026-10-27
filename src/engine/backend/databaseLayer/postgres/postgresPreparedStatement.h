#ifndef __POSTGRESQL_PREPARED_STATEMENT_H__
#define __POSTGRESQL_PREPARED_STATEMENT_H__

#include <vector>

#include "backend/databaseLayer/preparedStatement.h"

#include "postgresResultSet.h"
#include "postgresPreparedStatementWrapper.h"
#include "postgresInterface.h"

typedef std::vector<ibPreparedStatementPostgresWrapper> ArrayOfPostgresPreparedStatementWrappers;

class ibPreparedStatementPostgres : public ibPreparedStatement
{
public:
	// ctor
	ibPreparedStatementPostgres(ibInterfacePostgres* pInterface);
	ibPreparedStatementPostgres(ibInterfacePostgres* pInterface, PGconn* pDatabase, const ibString& strSQL, const ibString& strStatementName);

	// dtor
	virtual ~ibPreparedStatementPostgres();

	virtual void Close();

	void AddStatement(PGconn* pDatabase, const ibString& strSQL, const ibString& strStatementName);
	static ibPreparedStatementPostgres* CreateStatement(ibInterfacePostgres* pInterface, PGconn* pDatabase, const ibString& strSQL);

	// set field
	virtual void SetParamInt(int nPosition, int nValue);
	virtual void SetParamDouble(int nPosition, double dblValue);
	virtual void SetParamNumber(int nPosition, const ibNumber& dblValue);
	virtual void SetParamString(int nPosition, const ibString& strValue);
	virtual void SetParamNull(int nPosition);
	virtual void SetParamBlob(int nPosition, const void* pData, long nDataLength);
	virtual void SetParamDate(int nPosition, const ibDateTime& dateValue);
	virtual void SetParamBool(int nPosition, bool bValue);
	virtual int GetParameterCount();

	virtual int RunQuery();
	virtual ibDatabaseResultSet* RunQueryWithResults();

	static ibString TranslateSQL(const ibString& strOriginalSQL);

private:
	ibInterfacePostgres* m_pInterface;
	int FindStatementAndAdjustPositionIndex(int* pPosition);
	static ibString GenerateStatementName();

	ArrayOfPostgresPreparedStatementWrappers m_Statements;
};

#endif // __POSTGRESQL_PREPARED_STATEMENT_H__


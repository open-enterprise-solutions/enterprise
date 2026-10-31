#ifndef __ODBC_PREPARED_STATEMENT_H__
#define __ODBC_PREPARED_STATEMENT_H__

#include <atomic>
#include <vector>

#include "backend/databaseLayer/preparedStatement.h"

#include "odbcParameter.h"
#include "odbcInterface.h"

#include <sql.h>

typedef std::vector<SQLHSTMT> StatementVector;
typedef std::vector<ibDatabaseParameterODBC*> ArrayOfODBCParameters;

class ibDatabaseResultSet;

class ibPreparedStatementODBC : public ibPreparedStatement
{
public:
	// ctor — `pExecuting` is the connection's slot for the statement it is executing (ibDatabaseLayerODBC::Cancel)
	ibPreparedStatementODBC(ibInterfaceODBC* pInterface, SQLHENV sqlEnvHandle, SQLHDBC sqlHDBC, std::atomic<void*>* pExecuting);
	ibPreparedStatementODBC(ibInterfaceODBC* pInterface, SQLHENV sqlEnvHandle, SQLHDBC sqlHDBC, SQLHSTMT sqlStatementHandle);
	ibPreparedStatementODBC(ibInterfaceODBC* pInterface, SQLHENV sqlEnvHandle, SQLHDBC sqlHDBC, StatementVector statements);

	// dtor
	virtual ~ibPreparedStatementODBC();

	virtual void Close();

	void AddPreparedStatement(SQLHSTMT pStatement);

	// get field
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
	virtual ibDatabaseResultSet* RunQueryWithResults(bool bLogForCleanup);

	SQLHSTMT GetLastStatement() { return (m_Statements.size() > 0) ? m_Statements[m_Statements.size() - 1] : nullptr; }
	void SetOneTimer(bool bOneTimer = true) { m_bOneTimeStatement = bOneTimer; }

private:
	// SQLExecute, with the connection told which statement it is running — what its Cancel stops.
	SQLRETURN Execute(SQLHSTMT hstmt);

	void InterpretErrorCodes(long nCode, SQLHSTMT stmth_ptr = nullptr);
	void FreeParameters();
	void BindParameters();
	void SetParam(int nPosition, ibDatabaseParameterODBC* pParameter);


	int FindStatementAndAdjustPositionIndex(int* pPosition);

	bool m_bOneTimeStatement; // Flag to indicate that statement ownership should be handed off to any generated ODBCResultSets
	SQLHENV m_sqlEnvHandle;
	SQLHDBC m_sqlHDBC;
	StatementVector m_Statements;

	ArrayOfODBCParameters m_Parameters;
	ibInterfaceODBC* m_pInterface;
	std::atomic<void*>* m_pExecuting = nullptr;
};

#endif // __ODBC_PREPARED_STATEMENT_H__


#include "odbcInterface.h"

bool ibInterfaceODBC::Init()
{
	// If Windows, then default to odbc32 and try odbc second
	// Otherwise try odbc first and try odbc32 second
#ifdef __WIN32__
	bool bLoaded = m_ODBCDLL.Load(ibDynamicLibrary::CanonicalizeName(ibT("odbc32")));
#else
	bool bLoaded = m_ODBCDLL.Load(ibDynamicLibrary::CanonicalizeName(ibT("odbc")));
#endif
	if (!bLoaded)
	{
#ifdef __WIN32__
		bLoaded = m_ODBCDLL.Load(ibDynamicLibrary::CanonicalizeName(ibT("odbc")));
#else
		bLoaded = m_ODBCDLL.Load(ibDynamicLibrary::CanonicalizeName(ibT("odbc32")));
#endif
		if (!bLoaded)
			return false;
	}

	ibString symbol = ibT("SQLAllocHandle");
	if (m_ODBCDLL.HasSymbol(symbol))
	{
		m_pSQLAllocHandle = (SQLAllocHandleType)m_ODBCDLL.GetSymbol(symbol);
	}
	else
	{
		return false;
	}

	symbol = ibT("SQLSetEnvAttr");
	if (m_ODBCDLL.HasSymbol(symbol))
	{
		m_pSQLSetEnvAttr = (SQLSetEnvAttrType)m_ODBCDLL.GetSymbol(symbol);
	}
	else
	{
		return false;
	}

	symbol = ibT("SQLFreeHandle");
	if (m_ODBCDLL.HasSymbol(symbol))
	{
		m_pSQLFreeHandle = (SQLFreeHandleType)m_ODBCDLL.GetSymbol(symbol);
	}
	else
	{
		return false;
	}

	symbol = ibT("SQLConnect");
	if (m_ODBCDLL.HasSymbol(symbol))
	{
		m_pSQLConnect = (SQLConnectType)m_ODBCDLL.GetSymbol(symbol);
	}
	else
	{
		return false;
	}

	symbol = ibT("SQLDriverConnect");
	if (m_ODBCDLL.HasSymbol(symbol))
	{
		m_pSQLDriverConnect = (SQLDriverConnectType)m_ODBCDLL.GetSymbol(symbol);
	}
	else
	{
		return false;
	}

	symbol = ibT("SQLDisconnect");
	if (m_ODBCDLL.HasSymbol(symbol))
	{
		m_pSQLDisconnect = (SQLDisconnectType)m_ODBCDLL.GetSymbol(symbol);
	}
	else
	{
		return false;
	}

	symbol = ibT("SQLSetConnectAttr");
	if (m_ODBCDLL.HasSymbol(symbol))
	{
		m_pSQLSetConnectAttr = (SQLSetConnectAttrType)m_ODBCDLL.GetSymbol(symbol);
	}
	else
	{
		return false;
	}

	symbol = ibT("SQLEndTran");
	if (m_ODBCDLL.HasSymbol(symbol))
	{
		m_pSQLEndTran = (SQLEndTranType)m_ODBCDLL.GetSymbol(symbol);
	}
	else
	{
		return false;
	}

	symbol = ibT("SQLPrepare");
	if (m_ODBCDLL.HasSymbol(symbol))
	{
		m_pSQLPrepare = (SQLPrepareType)m_ODBCDLL.GetSymbol(symbol);
	}
	else
	{
		return false;
	}

	symbol = ibT("SQLFreeStmt");
	if (m_ODBCDLL.HasSymbol(symbol))
	{
		m_pSQLFreeStmt = (SQLFreeStmtType)m_ODBCDLL.GetSymbol(symbol);
	}
	else
	{
		return false;
	}

	symbol = ibT("SQLTables");
	if (m_ODBCDLL.HasSymbol(symbol))
	{
		m_pSQLTables = (SQLTablesType)m_ODBCDLL.GetSymbol(symbol);
	}
	else
	{
		return false;
	}

	symbol = ibT("SQLFetch");
	if (m_ODBCDLL.HasSymbol(symbol))
	{
		m_pSQLFetch = (SQLFetchType)m_ODBCDLL.GetSymbol(symbol);
	}
	else
	{
		return false;
	}

	symbol = ibT("SQLGetData");
	if (m_ODBCDLL.HasSymbol(symbol))
	{
		m_pSQLGetData = (SQLGetDataType)m_ODBCDLL.GetSymbol(symbol);
	}
	else
	{
		return false;
	}

	symbol = ibT("SQLColumns");
	if (m_ODBCDLL.HasSymbol(symbol))
	{
		m_pSQLColumns = (SQLColumnsType)m_ODBCDLL.GetSymbol(symbol);
	}
	else
	{
		return false;
	}

	symbol = ibT("SQLGetDiagRec");
	if (m_ODBCDLL.HasSymbol(symbol))
	{
		m_pSQLGetDiagRec = (SQLGetDiagRecType)m_ODBCDLL.GetSymbol(symbol);
	}
	else
	{
		return false;
	}

	symbol = ibT("SQLNumParams");
	if (m_ODBCDLL.HasSymbol(symbol))
	{
		m_pSQLNumParams = (SQLNumParamsType)m_ODBCDLL.GetSymbol(symbol);
	}
	else
	{
		return false;
	}

	symbol = ibT("SQLExecute");
	if (m_ODBCDLL.HasSymbol(symbol))
	{
		m_pSQLExecute = (SQLExecuteType)m_ODBCDLL.GetSymbol(symbol);
	}
	else
	{
		return false;
	}

	symbol = ibT("SQLParamData");
	if (m_ODBCDLL.HasSymbol(symbol))
	{
		m_pSQLParamData = (SQLParamDataType)m_ODBCDLL.GetSymbol(symbol);
	}
	else
	{
		return false;
	}

	symbol = ibT("SQLPutData");
	if (m_ODBCDLL.HasSymbol(symbol))
	{
		m_pSQLPutData = (SQLPutDataType)m_ODBCDLL.GetSymbol(symbol);
	}
	else
	{
		return false;
	}

	symbol = ibT("SQLRowCount");
	if (m_ODBCDLL.HasSymbol(symbol))
	{
		m_pSQLRowCount = (SQLRowCountType)m_ODBCDLL.GetSymbol(symbol);
	}
	else
	{
		return false;
	}

	symbol = ibT("SQLNumResultCols");
	if (m_ODBCDLL.HasSymbol(symbol))
	{
		m_pSQLNumResultCols = (SQLNumResultColsType)m_ODBCDLL.GetSymbol(symbol);
	}
	else
	{
		return false;
	}

	symbol = ibT("SQLDescribeParam");
	if (m_ODBCDLL.HasSymbol(symbol))
	{
		m_pSQLDescribeParam = (SQLDescribeParamType)m_ODBCDLL.GetSymbol(symbol);
	}
	else
	{
		return false;
	}

	symbol = ibT("SQLBindParameter");
	if (m_ODBCDLL.HasSymbol(symbol))
	{
		m_pSQLBindParameter = (SQLBindParameterType)m_ODBCDLL.GetSymbol(symbol);
	}
	else
	{
		return false;
	}

	symbol = ibT("SQLColAttributes");
	if (m_ODBCDLL.HasSymbol(symbol))
	{
		m_pSQLColAttributes = (SQLColAttributesType)m_ODBCDLL.GetSymbol(symbol);
	}
	else
	{
		return false;
	}

	symbol = ibT("SQLColAttribute");
	if (m_ODBCDLL.HasSymbol(symbol))
	{
		m_pSQLColAttribute = (SQLColAttributeType)m_ODBCDLL.GetSymbol(symbol);
	}
	else
	{
		return false;
	}

	symbol = ibT("SQLDescribeCol");
	if (m_ODBCDLL.HasSymbol(symbol))
	{
		m_pSQLDescribeCol = (SQLDescribeColType)m_ODBCDLL.GetSymbol(symbol);
	}
	else
	{
		return false;
	}

	symbol = ibT("SQLCancel");
	if (m_ODBCDLL.HasSymbol(symbol))
	{
		m_pSQLCancel = (SQLCancelType)m_ODBCDLL.GetSymbol(symbol);
	}
	else
	{
		return false;
	}

	return true;
}


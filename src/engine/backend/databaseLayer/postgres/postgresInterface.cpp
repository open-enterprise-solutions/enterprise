#include "postgresInterface.h"

bool ibInterfacePostgres::Init()
{
#ifdef __WIN32__
	bool bLoaded = m_PostgresDLL.Load(ibDynamicLibrary::CanonicalizeName(ibT("libpq")));
#else
	bool bLoaded = m_PostgresDLL.Load(ibDynamicLibrary::CanonicalizeName(ibT("pq")));
#endif
	if (!bLoaded)
	{
		return false;
	}

	ibString symbol = ibT("PQstatus");
	if (m_PostgresDLL.HasSymbol(symbol))
	{
		m_pPQstatus = (PQstatusType)m_PostgresDLL.GetSymbol(symbol);
	}
	else
	{
		return false;
	}

	symbol = ibT("PQsetdbLogin");
	if (m_PostgresDLL.HasSymbol(symbol))
	{
		m_pPQsetdbLogin = (PQsetdbLoginType)m_PostgresDLL.GetSymbol(symbol);
	}
	else
	{
		return false;
	}

	symbol = ibT("PQerrorMessage");
	if (m_PostgresDLL.HasSymbol(symbol))
	{
		m_pPQerrorMessage = (PQerrorMessageType)m_PostgresDLL.GetSymbol(symbol);
	}
	else
	{
		return false;
	}

	symbol = ibT("PQclientEncoding");
	if (m_PostgresDLL.HasSymbol(symbol))
	{
		m_pPQclientEncoding = (PQclientEncodingType)m_PostgresDLL.GetSymbol(symbol);
	}
	else
	{
		return false;
	}

	symbol = ibT("PQsetClientEncoding");
	if (m_PostgresDLL.HasSymbol(symbol))
	{
		m_pPQsetClientEncoding = (PQsetClientEncodingType)m_PostgresDLL.GetSymbol(symbol);
	}
	else
	{
		return false;
	}

	symbol = ibT("pg_encoding_to_char");
	if (m_PostgresDLL.HasSymbol(symbol))
	{
		m_pPQencodingToChar = (PQencodingToCharType)m_PostgresDLL.GetSymbol(symbol);
	}
	else
	{
		return false;
	}

	symbol = ibT("PQfinish");
	if (m_PostgresDLL.HasSymbol(symbol))
	{
		m_pPQfinish = (PQfinishType)m_PostgresDLL.GetSymbol(symbol);
	}
	else
	{
		return false;
	}

	symbol = ibT("PQexec");
	if (m_PostgresDLL.HasSymbol(symbol))
	{
		m_pPQexec = (PQexecType)m_PostgresDLL.GetSymbol(symbol);
	}
	else
	{
		return false;
	}

	symbol = ibT("PQresultStatus");
	if (m_PostgresDLL.HasSymbol(symbol))
	{
		m_pPQresultStatus = (PQresultStatusType)m_PostgresDLL.GetSymbol(symbol);
	}
	else
	{
		return false;
	}

	symbol = ibT("PQclear");
	if (m_PostgresDLL.HasSymbol(symbol))
	{
		m_pPQclear = (PQclearType)m_PostgresDLL.GetSymbol(symbol);
	}
	else
	{
		return false;
	}

	symbol = ibT("PQcmdTuples");
	if (m_PostgresDLL.HasSymbol(symbol))
	{
		m_pPQcmdTuples = (PQcmdTuplesType)m_PostgresDLL.GetSymbol(symbol);
	}
	else
	{
		return false;
	}

	symbol = ibT("PQprepare");
	if (m_PostgresDLL.HasSymbol(symbol))
	{
		m_pPQprepare = (PQprepareType)m_PostgresDLL.GetSymbol(symbol);
	}
	else
	{
		return false;
	}

	symbol = ibT("PQresultStatus");
	if (m_PostgresDLL.HasSymbol(symbol))
	{
		m_pPQresultStatus = (PQresultStatusType)m_PostgresDLL.GetSymbol(symbol);
	}
	else
	{
		return false;
	}

	symbol = ibT("PQexecPrepared");
	if (m_PostgresDLL.HasSymbol(symbol))
	{
		m_pPQexecPrepared = (PQexecPreparedType)m_PostgresDLL.GetSymbol(symbol);
	}
	else
	{
		return false;
	}

	symbol = ibT("PQresultErrorMessage");
	if (m_PostgresDLL.HasSymbol(symbol))
	{
		m_pPQresultErrorMessage = (PQresultErrorMessageType)m_PostgresDLL.GetSymbol(symbol);
	}
	else
	{
		return false;
	}

	symbol = ibT("PQntuples");
	if (m_PostgresDLL.HasSymbol(symbol))
	{
		m_pPQntuples = (PQntuplesType)m_PostgresDLL.GetSymbol(symbol);
	}
	else
	{
		return false;
	}

	symbol = ibT("PQbinaryTuples");
	if (m_PostgresDLL.HasSymbol(symbol))
	{
		m_pPQbinaryTuples = (PQbinaryTuplesType)m_PostgresDLL.GetSymbol(symbol);
	}
	else
	{
		return false;
	}

	symbol = ibT("PQfname");
	if (m_PostgresDLL.HasSymbol(symbol))
	{
		m_pPQfname = (PQfnameType)m_PostgresDLL.GetSymbol(symbol);
	}
	else
	{
		return false;
	}

	symbol = ibT("PQgetisnull");
	if (m_PostgresDLL.HasSymbol(symbol))
	{
		m_pPQgetisnull = (PQgetisnullType)m_PostgresDLL.GetSymbol(symbol);
	}
	else
	{
		return false;
	}

	symbol = ibT("PQgetvalue");
	if (m_PostgresDLL.HasSymbol(symbol))
	{
		m_pPQgetvalue = (PQgetvalueType)m_PostgresDLL.GetSymbol(symbol);
	}
	else
	{
		return false;
	}

	symbol = ibT("PQgetlength");
	if (m_PostgresDLL.HasSymbol(symbol))
	{
		m_pPQgetlength = (PQgetlengthType)m_PostgresDLL.GetSymbol(symbol);
	}
	else
	{
		return false;
	}

	symbol = ibT("PQunescapeBytea");
	if (m_PostgresDLL.HasSymbol(symbol))
	{
		m_pPQunescapeBytea = (PQunescapeByteaType)m_PostgresDLL.GetSymbol(symbol);
	}
	else
	{
		return false;
	}

	symbol = ibT("PQnfields");
	if (m_PostgresDLL.HasSymbol(symbol))
	{
		m_pPQnfields = (PQnfieldsType)m_PostgresDLL.GetSymbol(symbol);
	}
	else
	{
		return false;
	}

	symbol = ibT("PQfreemem");
	if (m_PostgresDLL.HasSymbol(symbol))
	{
		m_pPQfreemem = (PQfreememType)m_PostgresDLL.GetSymbol(symbol);
	}
	else
	{
		return false;
	}

	symbol = ibT("PQftype");
	if (m_PostgresDLL.HasSymbol(symbol))
	{
		m_pPQftype = (PQftypeType)m_PostgresDLL.GetSymbol(symbol);
	}
	else
	{
		return false;
	}

	symbol = ibT("PQfsize");
	if (m_PostgresDLL.HasSymbol(symbol))
	{
		m_pPQfsize = (PQfsizeType)m_PostgresDLL.GetSymbol(symbol);
	}
	else
	{
		return false;
	}

	symbol = ibT("PQgetCancel");
	if (m_PostgresDLL.HasSymbol(symbol))
	{
		m_pPQgetCancel = (PQgetCancelType)m_PostgresDLL.GetSymbol(symbol);
	}
	else
	{
		return false;
	}

	symbol = ibT("PQfreeCancel");
	if (m_PostgresDLL.HasSymbol(symbol))
	{
		m_pPQfreeCancel = (PQfreeCancelType)m_PostgresDLL.GetSymbol(symbol);
	}
	else
	{
		return false;
	}

	symbol = ibT("PQcancel");
	if (m_PostgresDLL.HasSymbol(symbol))
	{
		m_pPQcancel = (PQcancelType)m_PostgresDLL.GetSymbol(symbol);
	}
	else
	{
		return false;
	}

	symbol = ibT("PQresultErrorField");
	if (m_PostgresDLL.HasSymbol(symbol))
	{
		m_pPQresultErrorField = (PQresultErrorFieldType)m_PostgresDLL.GetSymbol(symbol);
	}
	else
	{
		return false;
	}

	return true;
}

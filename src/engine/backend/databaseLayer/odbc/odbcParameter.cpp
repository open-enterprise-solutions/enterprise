#include "odbcParameter.h"
#include "backend/databaseLayer/databaseLayer.h"
#include "backend/backend_exception.h"

#include <sqlext.h>

// ctor
ibDatabaseParameterODBC::ibDatabaseParameterODBC() : m_nParameterType(ibDatabaseParameterODBC::PARAM_NULL)
{
	m_nBufferLength = SQL_NULL_DATA;
}

ibDatabaseParameterODBC::ibDatabaseParameterODBC(const wxString& strValue) : m_nParameterType(ibDatabaseParameterODBC::PARAM_STRING), m_strValue(strValue)
{
	m_nBufferLength = GetEncodedStreamLength(m_strValue);
}

ibDatabaseParameterODBC::ibDatabaseParameterODBC(const ibNumber& dblValue) : m_nParameterType(ibDatabaseParameterODBC::PARAM_NUMBER), m_numValue(dblValue)
{
	m_nBufferLength = 0;
}

ibDatabaseParameterODBC::ibDatabaseParameterODBC(int nValue) : m_nParameterType(ibDatabaseParameterODBC::PARAM_INT), m_nValue(nValue)
{
	m_nBufferLength = 0;
}

ibDatabaseParameterODBC::ibDatabaseParameterODBC(double dblValue) : m_nParameterType(ibDatabaseParameterODBC::PARAM_DOUBLE), m_dblValue(dblValue)
{
	m_nBufferLength = 0;
}

ibDatabaseParameterODBC::ibDatabaseParameterODBC(bool bValue) : m_nParameterType(ibDatabaseParameterODBC::PARAM_BOOL), m_bValue(bValue)
{
	m_nBufferLength = 0;
}

ibDatabaseParameterODBC::ibDatabaseParameterODBC(const wxDateTime& dateValue) : m_nParameterType(ibDatabaseParameterODBC::PARAM_DATETIME)
{
	m_DateValue.year = dateValue.GetYear();
	m_DateValue.month = dateValue.GetMonth() + 1;
	m_DateValue.day = dateValue.GetDay();

	m_DateValue.hour = dateValue.GetHour();
	m_DateValue.minute = dateValue.GetMinute();
	m_DateValue.second = dateValue.GetSecond();
	m_DateValue.fraction = dateValue.GetMillisecond();

	m_nBufferLength = 0;
}

ibDatabaseParameterODBC::ibDatabaseParameterODBC(const void* pData, long nDataLength)
{
	m_nParameterType = ibDatabaseParameterODBC::PARAM_BLOB;
	void* pBuffer = m_BufferValue.GetWriteBuf(nDataLength);
	memcpy(pBuffer, pData, nDataLength);
	m_nBufferLength = nDataLength;
}


long ibDatabaseParameterODBC::GetDataLength()
{
	if (m_nParameterType == ibDatabaseParameterODBC::PARAM_NULL) return 0;
	else return m_nBufferLength;
}

#ifdef _WIN64
SQLLEN* ibDatabaseParameterODBC::GetDataLengthPointer()
{
	if (m_nParameterType == ibDatabaseParameterODBC::PARAM_NULL) return &m_nBufferLength;
	else return nullptr;
}
#else
long* ibDatabaseParameterODBC::GetDataLengthPointer()
{
	if (m_nParameterType == ibDatabaseParameterODBC::PARAM_NULL) return &m_nBufferLength;
	else return nullptr;
}
#endif

void ibDatabaseParameterODBC::RequireDescribable() const
{
	switch (m_nParameterType)
	{
	case PARAM_STRING: case PARAM_INT:  case PARAM_DOUBLE: case PARAM_NUMBER:
	case PARAM_DATETIME: case PARAM_BOOL: case PARAM_BLOB: case PARAM_NULL:
		return;
	}
	ibBackendCoreException::Error(
		_("ODBC: a parameter of kind %d cannot be described for binding"), m_nParameterType);
}

void* ibDatabaseParameterODBC::GetDataPtr()
{
	const void *pReturn = nullptr;

	switch (m_nParameterType)
	{
	case ibDatabaseParameterODBC::PARAM_STRING:
		m_CharBufferValue = ConvertToUnicodeStream(m_strValue);
		pReturn = m_CharBufferValue;
		//wxPrintf(_("Parameter: '%s'\n"), m_strValue.c_str());
		//pReturn = m_strValue.c_str();
		break;
	case ibDatabaseParameterODBC::PARAM_INT:
		pReturn = &m_nValue;
		break;
	case ibDatabaseParameterODBC::PARAM_DOUBLE:
		pReturn = &m_dblValue;
		break;
	case ibDatabaseParameterODBC::PARAM_NUMBER:
		pReturn = &m_numValue;
		break;
	case ibDatabaseParameterODBC::PARAM_DATETIME:
		pReturn = &m_DateValue;
		break;
	case ibDatabaseParameterODBC::PARAM_BOOL:
		pReturn = &m_bValue;
		break;
	case ibDatabaseParameterODBC::PARAM_BLOB:
		pReturn = m_BufferValue.GetData();
		break;
	case ibDatabaseParameterODBC::PARAM_NULL:
		pReturn = nullptr;
		break;
	default:
		pReturn = nullptr;
		break;
	};
	return (void*)pReturn;
}

SQLSMALLINT ibDatabaseParameterODBC::GetValueType()
{
	SQLSMALLINT nReturn = SQL_C_LONG;

	switch (m_nParameterType)
	{
	case ibDatabaseParameterODBC::PARAM_STRING:
		nReturn = SQL_C_CHAR;
		break;
	case ibDatabaseParameterODBC::PARAM_INT:
		nReturn = SQL_C_LONG;
		break;
	case ibDatabaseParameterODBC::PARAM_DOUBLE:
		nReturn = SQL_C_DOUBLE;
		break;
	case ibDatabaseParameterODBC::PARAM_NUMBER:
		nReturn = SQL_C_NUMERIC;
		break;
	case ibDatabaseParameterODBC::PARAM_DATETIME:
		nReturn = SQL_C_TYPE_TIMESTAMP;
		break;
	case ibDatabaseParameterODBC::PARAM_BOOL:
		nReturn = SQL_C_LONG;
		break;
	case ibDatabaseParameterODBC::PARAM_BLOB:
		nReturn = SQL_C_BINARY;
		break;
	case ibDatabaseParameterODBC::PARAM_NULL:
		nReturn = SQL_C_CHAR;
		break;
	default:
		nReturn = SQL_C_NUMERIC;
		break;
	};
	return nReturn;
}

SQLSMALLINT ibDatabaseParameterODBC::GetParameterType()
{
	SQLSMALLINT nReturn = SQL_INTEGER;

	switch (m_nParameterType)
	{
	case ibDatabaseParameterODBC::PARAM_STRING:
		nReturn = SQL_VARCHAR;
		break;
	case ibDatabaseParameterODBC::PARAM_INT:
		nReturn = SQL_INTEGER;
		break;
	case ibDatabaseParameterODBC::PARAM_DOUBLE:
		nReturn = SQL_DOUBLE;
		break;
	case ibDatabaseParameterODBC::PARAM_NUMBER:
		nReturn = SQL_C_NUMERIC;
		break;
	case ibDatabaseParameterODBC::PARAM_DATETIME:
		nReturn = SQL_TIMESTAMP;
		break;
	case ibDatabaseParameterODBC::PARAM_BOOL:
		nReturn = SQL_INTEGER;
		break;
	case ibDatabaseParameterODBC::PARAM_BLOB:
		nReturn = SQL_BINARY;
		break;
	case ibDatabaseParameterODBC::PARAM_NULL:
		nReturn = SQL_NULL_DATA;
		break;
	default:
		nReturn = SQL_INTEGER;
		break;
	};
	return nReturn;

}

bool ibDatabaseParameterODBC::IsBinary()
{
	return (ibDatabaseParameterODBC::PARAM_BLOB == m_nParameterType);
}

SQLSMALLINT ibDatabaseParameterODBC::GetDecimalDigits()
{
	// either decimal_digits or 0 (date, bool, int)
	SQLSMALLINT nReturn = 0;

	switch (m_nParameterType)
	{
	case ibDatabaseParameterODBC::PARAM_STRING:
	case ibDatabaseParameterODBC::PARAM_BLOB:
	case ibDatabaseParameterODBC::PARAM_INT:
	case ibDatabaseParameterODBC::PARAM_DATETIME:
	case ibDatabaseParameterODBC::PARAM_BOOL:
	case ibDatabaseParameterODBC::PARAM_NULL:
		nReturn = 0;
		break;
	case ibDatabaseParameterODBC::PARAM_DOUBLE:
		nReturn = 11;
		break;
	case ibDatabaseParameterODBC::PARAM_NUMBER:
		nReturn = 20;
		break;
	default:
		nReturn = 0;
		break;
	};
	return nReturn;
}

SQLUINTEGER ibDatabaseParameterODBC::GetColumnSize()
{
	SQLUINTEGER nReturn = 0;

	switch (m_nParameterType)
	{
	case ibDatabaseParameterODBC::PARAM_STRING:
	case ibDatabaseParameterODBC::PARAM_BLOB: nReturn = m_nBufferLength; break;
	case ibDatabaseParameterODBC::PARAM_INT:
	case ibDatabaseParameterODBC::PARAM_DOUBLE:
	case ibDatabaseParameterODBC::PARAM_NUMBER:
	case ibDatabaseParameterODBC::PARAM_BOOL:
	case ibDatabaseParameterODBC::PARAM_NULL: nReturn = 0; break;
	case ibDatabaseParameterODBC::PARAM_DATETIME: nReturn = 19; break;
	default: nReturn = 0; break;
	};

	return nReturn;
}

#ifdef _WIN64
SQLLEN* ibDatabaseParameterODBC::GetParameterLengthPtr()
{
	return &m_nBufferLength;
}
#else
SQLINTEGER* ibDatabaseParameterODBC::GetParameterLengthPtr()
{
	return &m_nBufferLength;
}
#endif
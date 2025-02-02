#include "odbcParameter.h"
#include "backend/databaseLayer/databaseLayer.h"

#include <sqlext.h>

// ctor
COdbcParameter::COdbcParameter() : m_nParameterType(COdbcParameter::PARAM_NULL)
{
	m_nBufferLength = SQL_NULL_DATA;
}

COdbcParameter::COdbcParameter(const wxString& strValue) : m_nParameterType(COdbcParameter::PARAM_STRING), m_strValue(strValue)
{
	m_nBufferLength = GetEncodedStreamLength(m_strValue);
}

COdbcParameter::COdbcParameter(const number_t& dblValue) : m_nParameterType(COdbcParameter::PARAM_NUMBER), m_numValue(dblValue)
{
	m_nBufferLength = 0;
}

COdbcParameter::COdbcParameter(int nValue) : m_nParameterType(COdbcParameter::PARAM_INT), m_nValue(nValue)
{
	m_nBufferLength = 0;
}

COdbcParameter::COdbcParameter(double dblValue) : m_nParameterType(COdbcParameter::PARAM_DOUBLE), m_dblValue(dblValue)
{
	m_nBufferLength = 0;
}

COdbcParameter::COdbcParameter(bool bValue) : m_nParameterType(COdbcParameter::PARAM_BOOL), m_bValue(bValue)
{
	m_nBufferLength = 0;
}

COdbcParameter::COdbcParameter(const wxDateTime& dateValue) : m_nParameterType(COdbcParameter::PARAM_DATETIME)
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

COdbcParameter::COdbcParameter(const void* pData, long nDataLength)
{
	m_nParameterType = COdbcParameter::PARAM_BLOB;
	void* pBuffer = m_BufferValue.GetWriteBuf(nDataLength);
	memcpy(pBuffer, pData, nDataLength);
	m_nBufferLength = nDataLength;
}


long COdbcParameter::GetDataLength()
{
	if (m_nParameterType == COdbcParameter::PARAM_NULL) return 0;
	else return m_nBufferLength;
}

#ifdef _WIN64
SQLLEN* COdbcParameter::GetDataLengthPointer()
{
	if (m_nParameterType == COdbcParameter::PARAM_NULL) return &m_nBufferLength;
	else return nullptr;
}
#else
long* COdbcParameter::GetDataLengthPointer()
{
	if (m_nParameterType == COdbcParameter::PARAM_NULL) return &m_nBufferLength;
	else return nullptr;
}
#endif

void* COdbcParameter::GetDataPtr()
{
	const void *pReturn = nullptr;

	switch (m_nParameterType)
	{
	case COdbcParameter::PARAM_STRING:
		m_CharBufferValue = ConvertToUnicodeStream(m_strValue);
		pReturn = m_CharBufferValue;
		//wxPrintf(_("Parameter: '%s'\n"), m_strValue.c_str());
		//pReturn = m_strValue.c_str();
		break;
	case COdbcParameter::PARAM_INT:
		pReturn = &m_nValue;
		break;
	case COdbcParameter::PARAM_DOUBLE:
		pReturn = &m_dblValue;
		break;
	case COdbcParameter::PARAM_NUMBER:
		pReturn = &m_numValue;
		break;
	case COdbcParameter::PARAM_DATETIME:
		pReturn = &m_DateValue;
		break;
	case COdbcParameter::PARAM_BOOL:
		pReturn = &m_bValue;
		break;
	case COdbcParameter::PARAM_BLOB:
		pReturn = m_BufferValue.GetData();
		break;
	case COdbcParameter::PARAM_NULL:
		pReturn = nullptr;
		break;
	default:
		pReturn = nullptr;
		break;
	};
	return (void*)pReturn;
}

SQLSMALLINT COdbcParameter::GetValueType()
{
	SQLSMALLINT nReturn = SQL_C_LONG;

	switch (m_nParameterType)
	{
	case COdbcParameter::PARAM_STRING:
		nReturn = SQL_C_CHAR;
		break;
	case COdbcParameter::PARAM_INT:
		nReturn = SQL_C_LONG;
		break;
	case COdbcParameter::PARAM_DOUBLE:
		nReturn = SQL_C_DOUBLE;
		break;
	case COdbcParameter::PARAM_NUMBER:
		nReturn = SQL_C_NUMERIC;
		break;
	case COdbcParameter::PARAM_DATETIME:
		nReturn = SQL_C_TYPE_TIMESTAMP;
		break;
	case COdbcParameter::PARAM_BOOL:
		nReturn = SQL_C_LONG;
		break;
	case COdbcParameter::PARAM_BLOB:
		nReturn = SQL_C_BINARY;
		break;
	case COdbcParameter::PARAM_NULL:
		nReturn = SQL_C_CHAR;
		break;
	default:
		nReturn = SQL_C_NUMERIC;
		break;
	};
	return nReturn;
}

SQLSMALLINT COdbcParameter::GetParameterType()
{
	SQLSMALLINT nReturn = SQL_INTEGER;

	switch (m_nParameterType)
	{
	case COdbcParameter::PARAM_STRING:
		nReturn = SQL_VARCHAR;
		break;
	case COdbcParameter::PARAM_INT:
		nReturn = SQL_INTEGER;
		break;
	case COdbcParameter::PARAM_DOUBLE:
		nReturn = SQL_DOUBLE;
		break;
	case COdbcParameter::PARAM_NUMBER:
		nReturn = SQL_C_NUMERIC;
		break;
	case COdbcParameter::PARAM_DATETIME:
		nReturn = SQL_TIMESTAMP;
		break;
	case COdbcParameter::PARAM_BOOL:
		nReturn = SQL_INTEGER;
		break;
	case COdbcParameter::PARAM_BLOB:
		nReturn = SQL_BINARY;
		break;
	case COdbcParameter::PARAM_NULL:
		nReturn = SQL_NULL_DATA;
		break;
	default:
		nReturn = SQL_INTEGER;
		break;
	};
	return nReturn;

}

bool COdbcParameter::IsBinary()
{
	return (COdbcParameter::PARAM_BLOB == m_nParameterType);
}

SQLSMALLINT COdbcParameter::GetDecimalDigits()
{
	// either decimal_digits or 0 (date, bool, int)
	SQLSMALLINT nReturn = 0;

	switch (m_nParameterType)
	{
	case COdbcParameter::PARAM_STRING:
	case COdbcParameter::PARAM_BLOB:
	case COdbcParameter::PARAM_INT:
	case COdbcParameter::PARAM_DATETIME:
	case COdbcParameter::PARAM_BOOL:
	case COdbcParameter::PARAM_NULL:
		nReturn = 0;
		break;
	case COdbcParameter::PARAM_DOUBLE:
		nReturn = 11;
		break;
	case COdbcParameter::PARAM_NUMBER:
		nReturn = 20;
		break;
	default:
		nReturn = 0;
		break;
	};
	return nReturn;
}

SQLUINTEGER COdbcParameter::GetColumnSize()
{
	SQLUINTEGER nReturn = 0;

	switch (m_nParameterType)
	{
	case COdbcParameter::PARAM_STRING:
	case COdbcParameter::PARAM_BLOB: nReturn = m_nBufferLength; break;
	case COdbcParameter::PARAM_INT:
	case COdbcParameter::PARAM_DOUBLE:
	case COdbcParameter::PARAM_NUMBER:
	case COdbcParameter::PARAM_BOOL:
	case COdbcParameter::PARAM_NULL: nReturn = 0; break;
	case COdbcParameter::PARAM_DATETIME: nReturn = 19; break;
	default: nReturn = 0; break;
	};

	return nReturn;
}

#ifdef _WIN64
SQLLEN* COdbcParameter::GetParameterLengthPtr()
{
	return &m_nBufferLength;
}
#else
SQLINTEGER* COdbcParameter::GetParameterLengthPtr()
{
	return &m_nBufferLength;
}
#endif
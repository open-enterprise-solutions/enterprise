#include "mysqlParameter.h"
#include "backend/databaseLayer/databaseLayer.h"

// ctor
CMysqlParameter::CMysqlParameter()
{
	m_nParameterType = CMysqlParameter::PARAM_NULL;
}

CMysqlParameter::CMysqlParameter(const wxString& strValue)
{
	m_nParameterType = CMysqlParameter::PARAM_STRING;
	m_strValue = strValue;
	m_CharBufferValue = ConvertToUnicodeStream(m_strValue);
	if (_("") == strValue)
	{
		m_nBufferLength = 0;
	}
	else
	{
		m_nBufferLength = GetEncodedStreamLength(m_strValue);
	}
}

CMysqlParameter::CMysqlParameter(int nValue)
{
	m_nParameterType = CMysqlParameter::PARAM_INT;
	m_nValue = nValue;
	//m_strValue = wxString::Format(_("%d"), nValue);
}

CMysqlParameter::CMysqlParameter(double dblValue)
{
	m_nParameterType = CMysqlParameter::PARAM_DOUBLE;
	m_dblValue = dblValue;
	//m_strValue = wxString::Format(_("%f"), dblValue);
}

CMysqlParameter::CMysqlParameter(const number_t &numValue)
{
	m_nParameterType = CMysqlParameter::PARAM_DOUBLE;
	m_dblValue = numValue.ToDouble();
}

CMysqlParameter::CMysqlParameter(bool bValue)
{
	m_nParameterType = CMysqlParameter::PARAM_BOOL;
	m_bValue = bValue;
}

CMysqlParameter::CMysqlParameter(const wxDateTime& dateValue)
{
	m_nParameterType = CMysqlParameter::PARAM_DATETIME;

	m_pDate = new MYSQL_TIME();

	memset(m_pDate, 0, sizeof(MYSQL_TIME));

	m_pDate->year = dateValue.GetYear();
	m_pDate->month = dateValue.GetMonth() + 1;
	m_pDate->day = dateValue.GetDay();
	m_pDate->hour = dateValue.GetHour();
	m_pDate->minute = dateValue.GetMinute();
	m_pDate->second = dateValue.GetSecond();
	m_pDate->neg = 0;

	m_nBufferLength = sizeof(MYSQL_TIME);
}

CMysqlParameter::CMysqlParameter(const void* pData, long nDataLength)
{
	m_nParameterType = CMysqlParameter::PARAM_BLOB;
	void* pBuffer = m_BufferValue.GetWriteBuf(nDataLength);
	memcpy(pBuffer, pData, nDataLength);
	m_nBufferLength = nDataLength;
}

CMysqlParameter::~CMysqlParameter()
{
	if ((m_nParameterType == CMysqlParameter::PARAM_DATETIME) && (m_pDate != nullptr))
	{
		delete m_pDate;
		m_pDate = nullptr;
	}
}

long unsigned int CMysqlParameter::GetDataLength()
{
	return m_nBufferLength;
}

long unsigned int* CMysqlParameter::GetDataLengthPtr()
{
	return &m_nBufferLength;
}

const void* CMysqlParameter::GetDataPtr()
{
	const void *pReturn = nullptr;

	switch (m_nParameterType)
	{
	case CMysqlParameter::PARAM_STRING:
		pReturn = m_CharBufferValue;
		break;
	case CMysqlParameter::PARAM_INT:
		pReturn = &m_nValue;
		break;
	case CMysqlParameter::PARAM_DOUBLE:
		pReturn = &m_dblValue;
		break;
	case CMysqlParameter::PARAM_DATETIME:
		pReturn = m_pDate;
		break;
	case CMysqlParameter::PARAM_BOOL:
		pReturn = &m_bValue;
		break;
	case CMysqlParameter::PARAM_BLOB:
		pReturn = m_BufferValue.GetData();
		break;
	case CMysqlParameter::PARAM_NULL:
		pReturn = nullptr;
		break;
	default:
		pReturn = nullptr;
		break;
	};
	return pReturn;
}

int CMysqlParameter::GetParameterType()
{
	return m_nParameterType;
}

enum_field_types CMysqlParameter::GetBufferType()
{
	enum_field_types returnType = MYSQL_TYPE_NULL;

	switch (m_nParameterType)
	{
	case CMysqlParameter::PARAM_STRING:
		returnType = MYSQL_TYPE_VAR_STRING;
		break;
	case CMysqlParameter::PARAM_INT:
		returnType = MYSQL_TYPE_LONG;
		break;
	case CMysqlParameter::PARAM_DOUBLE:
		returnType = MYSQL_TYPE_DOUBLE;
		break;
	case CMysqlParameter::PARAM_DATETIME:
		returnType = MYSQL_TYPE_TIMESTAMP;
		break;
	case CMysqlParameter::PARAM_BOOL:
		returnType = MYSQL_TYPE_TINY;
		break;
	case CMysqlParameter::PARAM_BLOB:
		returnType = MYSQL_TYPE_LONG_BLOB;
		break;
	case CMysqlParameter::PARAM_NULL:
		returnType = MYSQL_TYPE_NULL;
		break;
	default:
		returnType = MYSQL_TYPE_NULL;
		break;
	};

	return returnType;
}


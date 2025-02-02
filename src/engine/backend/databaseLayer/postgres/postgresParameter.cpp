#include "postgresParameter.h"
#include "backend/databaseLayer/databaseLayer.h"

// ctor
CPostgresParameter::CPostgresParameter() : m_nParameterType(CPostgresParameter::PARAM_NULL)
{
}

CPostgresParameter::CPostgresParameter(const wxString& strValue) : m_nParameterType(CPostgresParameter::PARAM_STRING), m_strValue(strValue), m_nBufferLength(strValue.Length())
{
}

CPostgresParameter::CPostgresParameter(int nValue) : m_nParameterType(CPostgresParameter::PARAM_INT)
{
	m_strValue = wxString::Format(_("%d"), nValue);
}

CPostgresParameter::CPostgresParameter(double dblValue) : m_nParameterType(CPostgresParameter::PARAM_DOUBLE)
{
	m_strValue = wxString::Format(_("%f"), dblValue);
}

CPostgresParameter::CPostgresParameter(const number_t &dblValue) : m_nParameterType(CPostgresParameter::PARAM_NUMBER)
{
	m_strValue = dblValue.ToWString();
}

CPostgresParameter::CPostgresParameter(bool bValue) : m_nParameterType(CPostgresParameter::PARAM_BOOL)
{
	m_strValue = wxString::Format(_("%d"), bValue);
}

CPostgresParameter::CPostgresParameter(const wxDateTime& dateValue) : m_nParameterType(CPostgresParameter::PARAM_DATETIME)
{
	m_strDateValue = dateValue.Format(_("%Y-%m-%d %H:%M:%S"));
	m_nBufferLength = m_strDateValue.Length();
}

CPostgresParameter::CPostgresParameter(const void* pData, long nDataLength) : m_nParameterType(CPostgresParameter::PARAM_BLOB)
{
	void* pBuffer = m_BufferValue.GetWriteBuf(nDataLength);
	memcpy(pBuffer, pData, nDataLength);
	m_nBufferLength = nDataLength;
}

long CPostgresParameter::GetDataLength()
{
	return m_nBufferLength;
}

long* CPostgresParameter::GetDataLengthPointer()
{
	return &m_nBufferLength;
}

const void* CPostgresParameter::GetDataPtr()
{
	const void *pReturn = nullptr;

	switch (m_nParameterType)
	{
	case CPostgresParameter::PARAM_STRING:
		m_CharBufferValue = ConvertToUnicodeStream(m_strValue);
		pReturn = m_CharBufferValue;
		break;
	case CPostgresParameter::PARAM_INT:
		//pReturn = &m_nValue;
		m_CharBufferValue = ConvertToUnicodeStream(m_strValue);
		pReturn = m_CharBufferValue;
		break;
	case CPostgresParameter::PARAM_DOUBLE:
		//pReturn = &m_dblValue;
		m_CharBufferValue = ConvertToUnicodeStream(m_strValue);
		pReturn = m_CharBufferValue;
		break;
	case CPostgresParameter::PARAM_NUMBER:
		//pReturn = &m_dblValue;
		m_CharBufferValue = ConvertToUnicodeStream(m_strValue);
		pReturn = m_CharBufferValue;
		break;
	case CPostgresParameter::PARAM_DATETIME:
		m_CharBufferValue = ConvertToUnicodeStream(m_strDateValue);
		pReturn = m_CharBufferValue;
		break;
	case CPostgresParameter::PARAM_BOOL:
		//pReturn = &m_bValue;
		m_CharBufferValue = ConvertToUnicodeStream(m_strValue);
		pReturn = m_CharBufferValue;
		break;
	case CPostgresParameter::PARAM_BLOB:
		pReturn = m_BufferValue.GetData();
		break;
	case CPostgresParameter::PARAM_NULL:
		pReturn = nullptr;
		break;
	default:
		pReturn = nullptr;
		break;
	};
	return pReturn;
}

int CPostgresParameter::GetParameterType()
{
	return m_nParameterType;
}

bool CPostgresParameter::IsBinary()
{
	return (CPostgresParameter::PARAM_BLOB == m_nParameterType);
}


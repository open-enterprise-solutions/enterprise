#include "postgresPreparedStatementParameterCollection.h"

#include <wx/arrimpl.cpp>

WX_DEFINE_OBJARRAY(ArrayOfPostgresParameters);

CPostgresPreparedStatementParameterCollection::~CPostgresPreparedStatementParameterCollection()
{
	m_Parameters.Clear();
}

int CPostgresPreparedStatementParameterCollection::GetSize()
{
	return m_Parameters.size();
}

char** CPostgresPreparedStatementParameterCollection::GetParamValues()
{
	char** paramValues = new char*[m_Parameters.size()];

	for (unsigned int i = 0; i < m_Parameters.size(); i++)
	{
		// Get a pointer to the appropriate data member variable for this parameter
		paramValues[i] = (char*)(m_Parameters[i].GetDataPtr());
	}

	return paramValues;
}

int* CPostgresPreparedStatementParameterCollection::GetParamLengths()
{
	int* paramLengths = new int[m_Parameters.size()];

	for (unsigned int i = 0; i < m_Parameters.size(); i++)
	{
		// Get a pointer to the m_nBufferLength member variable for this parameter
		paramLengths[i] = m_Parameters[i].GetDataLength();
	}

	return paramLengths;
}

int* CPostgresPreparedStatementParameterCollection::GetParamFormats()
{
	int* paramFormats = new int[m_Parameters.size()];

	for (unsigned int i = 0; i < m_Parameters.size(); i++)
	{
		paramFormats[i] = (m_Parameters[i].IsBinary()) ? 1 : 0;
	}

	return paramFormats;
}

void CPostgresPreparedStatementParameterCollection::SetParam(int nPosition, int nValue)
{
	CPostgresParameter Parameter(nValue);
	SetParam(nPosition, Parameter);
}

void CPostgresPreparedStatementParameterCollection::SetParam(int nPosition, double dblValue)
{
	CPostgresParameter Parameter(dblValue);
	SetParam(nPosition, Parameter);
}

void CPostgresPreparedStatementParameterCollection::SetParam(int nPosition, const number_t& dblValue)
{
	CPostgresParameter Parameter(dblValue);
	SetParam(nPosition, Parameter);
}

void CPostgresPreparedStatementParameterCollection::SetParam(int nPosition, const wxString& strValue)
{
	CPostgresParameter Parameter(strValue);
	SetParam(nPosition, Parameter);
}

void CPostgresPreparedStatementParameterCollection::SetParam(int nPosition)
{
	CPostgresParameter Parameter;
	SetParam(nPosition, Parameter);
}

void CPostgresPreparedStatementParameterCollection::SetParam(int nPosition, const void* pData, long nDataLength)
{
	CPostgresParameter Parameter(pData, nDataLength);
	SetParam(nPosition, Parameter);
}

void CPostgresPreparedStatementParameterCollection::SetParam(int nPosition, const wxDateTime& dateValue)
{
	CPostgresParameter Parameter(dateValue);
	SetParam(nPosition, Parameter);
}

void CPostgresPreparedStatementParameterCollection::SetParam(int nPosition, bool bValue)
{
	CPostgresParameter Parameter(bValue);
	SetParam(nPosition, Parameter);
}

void CPostgresPreparedStatementParameterCollection::SetParam(int nPosition, CPostgresParameter& Parameter)
{
	// First make sure that there are enough elements in the collection
	while (m_Parameters.size() < (unsigned int)(nPosition))
	{
		CPostgresParameter EmptyParameter;
		m_Parameters.push_back(EmptyParameter);
	}
	m_Parameters[nPosition - 1] = Parameter;
}


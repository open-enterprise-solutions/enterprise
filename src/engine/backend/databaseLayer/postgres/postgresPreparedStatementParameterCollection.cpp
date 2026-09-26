#include "postgresPreparedStatementParameterCollection.h"

#include <wx/arrimpl.cpp>

WX_DEFINE_OBJARRAY(ArrayOfPostgresParameters);

ibPreparedStatementPostgresParameterCollection::~ibPreparedStatementPostgresParameterCollection()
{
	m_Parameters.Clear();
}

int ibPreparedStatementPostgresParameterCollection::GetSize()
{
	return m_Parameters.size();
}

char** ibPreparedStatementPostgresParameterCollection::GetParamValues()
{
	char** paramValues = new char*[m_Parameters.size()];

	for (unsigned int i = 0; i < m_Parameters.size(); i++)
	{
		// Get a pointer to the appropriate data member variable for this parameter
		paramValues[i] = (char*)(m_Parameters[i].GetDataPtr());
	}

	return paramValues;
}

int* ibPreparedStatementPostgresParameterCollection::GetParamLengths()
{
	int* paramLengths = new int[m_Parameters.size()];

	for (unsigned int i = 0; i < m_Parameters.size(); i++)
	{
		// Get a pointer to the m_nBufferLength member variable for this parameter
		paramLengths[i] = m_Parameters[i].GetDataLength();
	}

	return paramLengths;
}

int* ibPreparedStatementPostgresParameterCollection::GetParamFormats()
{
	int* paramFormats = new int[m_Parameters.size()];

	for (unsigned int i = 0; i < m_Parameters.size(); i++)
	{
		paramFormats[i] = (m_Parameters[i].IsBinary()) ? 1 : 0;
	}

	return paramFormats;
}

void ibPreparedStatementPostgresParameterCollection::SetParam(int nPosition, int nValue)
{
	ibDatabaseParameterPostgres Parameter(nValue);
	SetParam(nPosition, Parameter);
}

void ibPreparedStatementPostgresParameterCollection::SetParam(int nPosition, double dblValue)
{
	ibDatabaseParameterPostgres Parameter(dblValue);
	SetParam(nPosition, Parameter);
}

void ibPreparedStatementPostgresParameterCollection::SetParam(int nPosition, const ibNumber& dblValue)
{
	ibDatabaseParameterPostgres Parameter(dblValue);
	SetParam(nPosition, Parameter);
}

void ibPreparedStatementPostgresParameterCollection::SetParam(int nPosition, const wxString& strValue)
{
	ibDatabaseParameterPostgres Parameter(strValue);
	SetParam(nPosition, Parameter);
}

void ibPreparedStatementPostgresParameterCollection::SetParam(int nPosition)
{
	ibDatabaseParameterPostgres Parameter;
	SetParam(nPosition, Parameter);
}

void ibPreparedStatementPostgresParameterCollection::SetParam(int nPosition, const void* pData, long nDataLength)
{
	ibDatabaseParameterPostgres Parameter(pData, nDataLength);
	SetParam(nPosition, Parameter);
}

void ibPreparedStatementPostgresParameterCollection::SetParam(int nPosition, const ibDateParts& date)
{
	ibDatabaseParameterPostgres Parameter(date);
	SetParam(nPosition, Parameter);
}

void ibPreparedStatementPostgresParameterCollection::SetParam(int nPosition, bool bValue)
{
	ibDatabaseParameterPostgres Parameter(bValue);
	SetParam(nPosition, Parameter);
}

void ibPreparedStatementPostgresParameterCollection::SetParam(int nPosition, ibDatabaseParameterPostgres& Parameter)
{
	// First make sure that there are enough elements in the collection
	while (m_Parameters.size() < (unsigned int)(nPosition))
	{
		ibDatabaseParameterPostgres EmptyParameter;
		m_Parameters.push_back(EmptyParameter);
	}
	m_Parameters[nPosition - 1] = Parameter;
}


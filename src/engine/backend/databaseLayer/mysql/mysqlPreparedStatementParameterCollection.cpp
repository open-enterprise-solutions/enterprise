#include "mysqlPreparedStatementParameterCollection.h"

CMysqlPreparedStatementParameterCollection::CMysqlPreparedStatementParameterCollection()
{
}

CMysqlPreparedStatementParameterCollection::~CMysqlPreparedStatementParameterCollection()
{
	MysqlParameterArray::iterator start = m_Parameters.begin();
	MysqlParameterArray::iterator stop = m_Parameters.end();

	while (start != stop)
	{
		if ((*start) != nullptr)
		{
			CMysqlParameter* pParameter = (CMysqlParameter*)(*start);
			wxDELETE(pParameter);
			(*start) = nullptr;
		}
		start++;
	}
}

int CMysqlPreparedStatementParameterCollection::GetSize()
{
	return m_Parameters.size();
}

MYSQL_BIND* CMysqlPreparedStatementParameterCollection::GetMysqlParameterBindings()
{
	MYSQL_BIND* pBindings = new MYSQL_BIND[m_Parameters.size()];

	memset(pBindings, 0, sizeof(MYSQL_BIND)*m_Parameters.size());

	for (unsigned int i = 0; i < m_Parameters.size(); i++)
	{
		pBindings[i].buffer_type = m_Parameters[i]->GetBufferType();
		pBindings[i].buffer = (void*)m_Parameters[i]->GetDataPtr();
		pBindings[i].buffer_length = m_Parameters[i]->GetDataLength();
		pBindings[i].length = m_Parameters[i]->GetDataLengthPtr();
	}

	return pBindings;
}

void CMysqlPreparedStatementParameterCollection::SetParam(int nPosition, int nValue)
{
	//CMysqlParameter Parameter(nValue);
	//SetParam(nPosition, Parameter);
	CMysqlParameter* pParameter = new CMysqlParameter(nValue);
	pParameter->SetEncoding(GetEncoding());
	SetParam(nPosition, pParameter);
}

void CMysqlPreparedStatementParameterCollection::SetParam(int nPosition, double dblValue)
{
	//CMysqlParameter Parameter(dblValue);
	//SetParam(nPosition, Parameter);
	CMysqlParameter* pParameter = new CMysqlParameter(dblValue);
	pParameter->SetEncoding(GetEncoding());
	SetParam(nPosition, pParameter);
}

void CMysqlPreparedStatementParameterCollection::SetParam(int nPosition, const number_t &numValue)
{
	//CMysqlParameter Parameter(dblValue);
	//SetParam(nPosition, Parameter);
	CMysqlParameter* pParameter = new CMysqlParameter(numValue);
	pParameter->SetEncoding(GetEncoding());
	SetParam(nPosition, pParameter);
}

void CMysqlPreparedStatementParameterCollection::SetParam(int nPosition, const wxString& strValue)
{
	//CMysqlParameter Parameter(strValue);
	//SetParam(nPosition, Parameter);
	CMysqlParameter* pParameter = new CMysqlParameter(strValue);
	pParameter->SetEncoding(GetEncoding());
	SetParam(nPosition, pParameter);
}

void CMysqlPreparedStatementParameterCollection::SetParam(int nPosition)
{
	//CMysqlParameter Parameter;
	//SetParam(nPosition, Parameter);
	CMysqlParameter* pParameter = new CMysqlParameter();
	pParameter->SetEncoding(GetEncoding());
	SetParam(nPosition, pParameter);
}

void CMysqlPreparedStatementParameterCollection::SetParam(int nPosition, const void* pData, long nDataLength)
{
	//CMysqlParameter Parameter(pData, nDataLength);
	//SetParam(nPosition, Parameter);
	CMysqlParameter* pParameter = new CMysqlParameter(pData, nDataLength);
	pParameter->SetEncoding(GetEncoding());
	SetParam(nPosition, pParameter);
}

void CMysqlPreparedStatementParameterCollection::SetParam(int nPosition, const wxDateTime& dateValue)
{
	//CMysqlParameter Parameter(dateValue);
	//SetParam(nPosition, Parameter);
	CMysqlParameter* pParameter = new CMysqlParameter(dateValue);
	pParameter->SetEncoding(GetEncoding());
	SetParam(nPosition, pParameter);
}

void CMysqlPreparedStatementParameterCollection::SetParam(int nPosition, bool bValue)
{
	//CMysqlParameter Parameter(bValue);
	//SetParam(nPosition, Parameter);
	CMysqlParameter* pParameter = new CMysqlParameter(bValue);
	pParameter->SetEncoding(GetEncoding());
	SetParam(nPosition, pParameter);
}

void CMysqlPreparedStatementParameterCollection::SetParam(int nPosition, CMysqlParameter* pParameter)
{
	// First make sure that there are enough elements in the collection
	while (m_Parameters.size() < (unsigned int)(nPosition))
	{
		//CMysqlParameter EmptyParameter;
		m_Parameters.push_back(nullptr);//EmptyParameter);
	}
	// Free up any data that is being replaced so the allocated memory isn't lost
	if (m_Parameters[nPosition - 1] != nullptr)
	{
		delete (m_Parameters[nPosition - 1]);
	}
	// Now set the new data
	m_Parameters[nPosition - 1] = pParameter;
}


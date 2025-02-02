#include "firebirdParameterCollection.h"

CFirebirdParameterCollection::CFirebirdParameterCollection(CFirebirdInterface* pInterface, XSQLDA* pParameters)
{
	m_pInterface = pInterface;
	m_FirebirdParameters = pParameters;
	AllocateParameterSpace();
}

CFirebirdParameterCollection::~CFirebirdParameterCollection()
{
	if (m_FirebirdParameters) FreeParameterSpace();

	FirebirdParameterArray::iterator start = m_Parameters.begin();
	FirebirdParameterArray::iterator stop = m_Parameters.end();

	while (start != stop)
	{
		CFirebirdParameter* pParameter = (CFirebirdParameter*)(*start);
		wxDELETE(pParameter);
		(*start) = nullptr;
		start++;
	}

	m_Parameters.clear();
}

// set field
void CFirebirdParameterCollection::SetParam(int nPosition, int nValue)
{
	CFirebirdParameter* pParameter = new CFirebirdParameter(m_pInterface, &m_FirebirdParameters->sqlvar[nPosition - 1], nValue);
	SetParam(nPosition, pParameter);
}

void CFirebirdParameterCollection::SetParam(int nPosition, double dblValue)
{
	CFirebirdParameter* pParameter = new CFirebirdParameter(m_pInterface, &m_FirebirdParameters->sqlvar[nPosition - 1], dblValue);
	SetParam(nPosition, pParameter);
}

void CFirebirdParameterCollection::SetParam(int nPosition, const number_t& dblValue)
{
	CFirebirdParameter* pParameter = new CFirebirdParameter(m_pInterface, &m_FirebirdParameters->sqlvar[nPosition - 1], dblValue);
	SetParam(nPosition, pParameter);
}


void CFirebirdParameterCollection::SetParam(int nPosition, const wxString& strValue)
{
	CFirebirdParameter* pParameter = new CFirebirdParameter(m_pInterface, &m_FirebirdParameters->sqlvar[nPosition - 1], strValue, GetEncoding());
	SetParam(nPosition, pParameter);
}

void CFirebirdParameterCollection::SetParam(int nPosition)
{
	CFirebirdParameter* pParameter = new CFirebirdParameter(m_pInterface, &m_FirebirdParameters->sqlvar[nPosition - 1]);
	SetParam(nPosition, pParameter);
}

void CFirebirdParameterCollection::SetParam(int nPosition, const void* pData, long nDataLength)
{
	CFirebirdParameter* pParameter = new CFirebirdParameter(m_pInterface, &m_FirebirdParameters->sqlvar[nPosition - 1], pData, nDataLength);
	SetParam(nPosition, pParameter);
}

void CFirebirdParameterCollection::SetParam(int nPosition, const wxDateTime& dateValue)
{
	CFirebirdParameter* pParameter = new CFirebirdParameter(m_pInterface, &m_FirebirdParameters->sqlvar[nPosition - 1], dateValue);
	SetParam(nPosition, pParameter);
}

void CFirebirdParameterCollection::SetParam(int nPosition, bool bValue)
{
	CFirebirdParameter* pParameter = new CFirebirdParameter(m_pInterface, &m_FirebirdParameters->sqlvar[nPosition - 1], bValue);
	SetParam(nPosition, pParameter);
}

void CFirebirdParameterCollection::SetParam(int nPosition, CFirebirdParameter* pParameter)
{
	// First make sure that there are enough elements in the collection
	while (m_Parameters.size() < (unsigned int)(nPosition))
	{
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

bool CFirebirdParameterCollection::ResetBlobParameters(isc_db_handle database, isc_tr_handle transaction)
{
	if (m_FirebirdParameters == nullptr)
		return false;

	FirebirdParameterArray::iterator start = m_Parameters.begin();
	FirebirdParameterArray::iterator stop = m_Parameters.end();

	while (start != stop)
	{
		const XSQLVAR* pVar = ((CFirebirdParameter*)(*start))->GetFirebirdSqlVarPtr();
		if ((pVar->sqltype & ~1) == SQL_BLOB)
		{
			bool result = ((CFirebirdParameter*)(*start))->ResetBlob(database, transaction);
			if (!result)
				return false;
		}
		start++;
	}

	return true;
}

void CFirebirdParameterCollection::AllocateParameterSpace()
{
	if (m_FirebirdParameters == nullptr)
		return;

	for (int i = 0; i < m_FirebirdParameters->sqld; i++)
	{
		XSQLVAR* pVar = &(m_FirebirdParameters->sqlvar[i]);
		switch (pVar->sqltype & ~1)
		{
		case SQL_ARRAY:
		case SQL_BLOB:
			pVar->sqldata = nullptr;
			break;
		case SQL_TIMESTAMP:
			pVar->sqldata = nullptr;
			break;
		case SQL_TYPE_TIME:
			pVar->sqldata = (char*)new ISC_TIME;
			memset(pVar->sqldata, 0, sizeof(ISC_TIME));
			break;
		case SQL_TYPE_DATE:
			pVar->sqldata = (char*)new ISC_DATE;
			memset(pVar->sqldata, 0, sizeof(ISC_DATE));
			break;
		case SQL_TEXT:
			pVar->sqldata = (char*)new wxChar[pVar->sqllen + 1];
			memset(pVar->sqldata, '\0', pVar->sqllen);
			pVar->sqldata[pVar->sqllen] = '\0';
			break;
		case SQL_VARYING:
			pVar->sqldata = (char*)new wxChar[pVar->sqllen + 3];
			memset(pVar->sqldata, 0, 2);
			memset(pVar->sqldata + 2, '\0', pVar->sqllen);
			pVar->sqldata[pVar->sqllen + 2] = '\0';
			break;
		case SQL_SHORT:
			pVar->sqldata = nullptr;
			break;
		case SQL_LONG:
			pVar->sqldata = nullptr;
			break;
		case SQL_INT64:
			pVar->sqldata = (char*)new wxChar[pVar->sqllen];
			break;
		case SQL_INT128:
			pVar->sqldata = (char*)new wxChar[pVar->sqllen];
			break;
		case SQL_FLOAT:
			pVar->sqldata = nullptr;
			break;
		case SQL_DOUBLE:
			pVar->sqldata = nullptr;
			break;
		default:
			wxLogError(_("Error allocating space for unknown parameter type\n"));
			break;
		}
	}
}

void CFirebirdParameterCollection::FreeParameterSpace()
{
	if (m_FirebirdParameters)
	{
		for (int i = 0; i < m_FirebirdParameters->sqld; i++)
		{
			XSQLVAR* pVar = &(m_FirebirdParameters->sqlvar[i]);
			if (pVar->sqldata != 0)
			{
				switch (pVar->sqltype & ~1)
				{
				case SQL_ARRAY:
				case SQL_BLOB:
					//wxDELETE(pVar->sqldata);
					break;
				case SQL_TIMESTAMP:
					//wxDELETE(pVar->sqldata);
					break;
				case SQL_TYPE_TIME:
					wxDELETE(pVar->sqldata);
					break;
				case SQL_TYPE_DATE:
					//wxDELETE(pVar->sqldata);
					break;
				case SQL_TEXT:
				case SQL_VARYING:
					wxDELETEA(pVar->sqldata);
					break;
				case SQL_SHORT:
					//wxDELETE(pVar->sqldata);
					break;
				case SQL_LONG:
					//wxDELETE(pVar->sqldata);
					break;
				case SQL_INT64:
					wxDELETE(pVar->sqldata);
					break;
				case SQL_INT128:
					wxDELETE(pVar->sqldata);
					break;
				case SQL_FLOAT:
					//wxDELETE(pVar->sqldata);
					break;
				case SQL_DOUBLE:
					//wxDELETE(pVar->sqldata);
					break;
				default:
					wxLogError(_("Error deleting unknown parameter type\n"));
					break;
				}
			}
			//if (pVar->sqlind != 0)
			  //delete pVar->sqlind;
			//wxDELETE(pVar->sqlind);
		}
		wxDELETEA(m_FirebirdParameters);
	}

}


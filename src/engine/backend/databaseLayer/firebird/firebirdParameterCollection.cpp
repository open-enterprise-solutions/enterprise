#include "firebirdParameterCollection.h"

#include "backend/backend_exception.h"

ibDatabaseParameterFirebirdCollection::ibDatabaseParameterFirebirdCollection(ibInterfaceFirebird* pInterface, XSQLDA* pParameters)
{
	m_pInterface = pInterface;
	m_FirebirdParameters = pParameters;
	AllocateParameterSpace();
}

ibDatabaseParameterFirebirdCollection::~ibDatabaseParameterFirebirdCollection()
{
	if (m_FirebirdParameters) FreeParameterSpace();

	FirebirdParameterArray::iterator start = m_Parameters.begin();
	FirebirdParameterArray::iterator stop = m_Parameters.end();

	while (start != stop)
	{
		ibDatabaseParameterFirebird* pParameter = (ibDatabaseParameterFirebird*)(*start);
		wxDELETE(pParameter);
		(*start) = nullptr;
		start++;
	}

	m_Parameters.clear();
}

// set field
void ibDatabaseParameterFirebirdCollection::SetParam(int nPosition, int nValue)
{
	ibDatabaseParameterFirebird* pParameter = new ibDatabaseParameterFirebird(m_pInterface, &m_FirebirdParameters->sqlvar[nPosition - 1], nValue);
	SetParam(nPosition, pParameter);
}

void ibDatabaseParameterFirebirdCollection::SetParam(int nPosition, double dblValue)
{
	ibDatabaseParameterFirebird* pParameter = new ibDatabaseParameterFirebird(m_pInterface, &m_FirebirdParameters->sqlvar[nPosition - 1], dblValue);
	SetParam(nPosition, pParameter);
}

void ibDatabaseParameterFirebirdCollection::SetParam(int nPosition, const ibNumber& dblValue)
{
	ibDatabaseParameterFirebird* pParameter = new ibDatabaseParameterFirebird(m_pInterface, &m_FirebirdParameters->sqlvar[nPosition - 1], dblValue);
	SetParam(nPosition, pParameter);
}


void ibDatabaseParameterFirebirdCollection::SetParam(int nPosition, const wxString& strValue)
{
	ibDatabaseParameterFirebird* pParameter = new ibDatabaseParameterFirebird(m_pInterface, &m_FirebirdParameters->sqlvar[nPosition - 1], strValue, GetEncoding());
	SetParam(nPosition, pParameter);
}

void ibDatabaseParameterFirebirdCollection::SetParam(int nPosition)
{
	ibDatabaseParameterFirebird* pParameter = new ibDatabaseParameterFirebird(m_pInterface, &m_FirebirdParameters->sqlvar[nPosition - 1]);
	SetParam(nPosition, pParameter);
}

void ibDatabaseParameterFirebirdCollection::SetParam(int nPosition, const void* pData, long nDataLength)
{
	ibDatabaseParameterFirebird* pParameter = new ibDatabaseParameterFirebird(m_pInterface, &m_FirebirdParameters->sqlvar[nPosition - 1], pData, nDataLength);
	SetParam(nPosition, pParameter);
}

void ibDatabaseParameterFirebirdCollection::SetParam(int nPosition, const wxDateTime& dateValue)
{
	ibDatabaseParameterFirebird* pParameter = new ibDatabaseParameterFirebird(m_pInterface, &m_FirebirdParameters->sqlvar[nPosition - 1], dateValue);
	SetParam(nPosition, pParameter);
}

void ibDatabaseParameterFirebirdCollection::SetParam(int nPosition, bool bValue)
{
	ibDatabaseParameterFirebird* pParameter = new ibDatabaseParameterFirebird(m_pInterface, &m_FirebirdParameters->sqlvar[nPosition - 1], bValue);
	SetParam(nPosition, pParameter);
}

void ibDatabaseParameterFirebirdCollection::SetParam(int nPosition, ibDatabaseParameterFirebird* pParameter)
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

bool ibDatabaseParameterFirebirdCollection::ResetBlobParameters(isc_db_handle database, isc_tr_handle transaction)
{
	if (m_FirebirdParameters == nullptr)
		return false;

	FirebirdParameterArray::iterator start = m_Parameters.begin();
	FirebirdParameterArray::iterator stop = m_Parameters.end();

	while (start != stop)
	{
		ibDatabaseParameterFirebird* p = (ibDatabaseParameterFirebird*)(*start);
		// NULL slot — caller never bound a value at this position. Skip
		// rather than dereferencing a null pointer.
		if (p == nullptr)
		{
			start++;
			continue;
		}
		const XSQLVAR* pVar = p->GetFirebirdSqlVarPtr();
		const bool isNullBound = (pVar->sqlind != nullptr) && (*pVar->sqlind < 0);
		if ((pVar->sqltype & ~1) == SQL_BLOB && !isNullBound)
		{
			if (!p->ResetBlob(database, transaction))
				return false;
		}
		start++;
	}

	return true;
}

void ibDatabaseParameterFirebirdCollection::AllocateParameterSpace()
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
			// Uniform array allocation across all paths so FreeParameterSpace
			// can use a single wxDELETEA without type-mismatch UB (the
			// previous `(char*)new ISC_TIME` + scalar delete was a typed-vs-
			// char* delete mismatch).
			pVar->sqldata = new char[sizeof(ISC_TIME)];
			memset(pVar->sqldata, 0, sizeof(ISC_TIME));
			break;
		case SQL_TYPE_DATE:
			pVar->sqldata = new char[sizeof(ISC_DATE)];
			memset(pVar->sqldata, 0, sizeof(ISC_DATE));
			break;
		case SQL_TEXT:
			pVar->sqldata = new char[pVar->sqllen + 1];
			memset(pVar->sqldata, '\0', pVar->sqllen);
			pVar->sqldata[pVar->sqllen] = '\0';
			break;
		case SQL_VARYING:
			pVar->sqldata = new char[pVar->sqllen + 3];
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
			pVar->sqldata = new char[pVar->sqllen];
			memset(pVar->sqldata, 0, pVar->sqllen);
			break;
		case SQL_INT128:
			pVar->sqldata = new char[pVar->sqllen];
			memset(pVar->sqldata, 0, pVar->sqllen);
			break;
		case SQL_FLOAT:
			pVar->sqldata = nullptr;
			break;
		case SQL_DOUBLE:
			pVar->sqldata = nullptr;
			break;
		default:
			// ⭐ RAISES, and used to only log. Every case above DECIDES about sqldata — a buffer, or
			// the deliberate nullptr that a setter later points at a member of its own. This one
			// decided nothing and carried on, leaving sqldata as the describe left it, and the bind
			// that follows writes a value through it anyway. A type this collection cannot make room
			// for is a statement that cannot be bound, and here is the last point where that is
			// still the plain truth: one step on it is a corrupt write or a crash inside the CRT
			// with nothing left connecting it to its cause.
			ibBackendCoreException::Error(
				_("Firebird: no room can be made for a parameter of SQL type %d"),
				(int)(pVar->sqltype & ~1));
			break;
		}
	}
}

void ibDatabaseParameterFirebirdCollection::FreeParameterSpace()
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
				case SQL_TYPE_DATE:
				case SQL_TEXT:
				case SQL_VARYING:
				case SQL_INT64:
				case SQL_INT128:
					// All these paths now allocate via new char[size] in
					// AllocateParameterSpace — array delete required.
					// Previously TIME used scalar delete on (char*)new ISC_TIME
					// (type-mismatch UB), DATE was never freed (leak), INT64
					// and INT128 used scalar delete on `new char[]` (array
					// delete required).
					wxDELETEA(pVar->sqldata);
					break;
				case SQL_SHORT:
				case SQL_LONG:
					// Owned by ibDatabaseParameterFirebird (sqldata points
					// at member fields like m_nValue / m_sValue); not freed
					// here.
					break;
				case SQL_FLOAT:
					//wxDELETE(pVar->sqldata);
					break;
				case SQL_DOUBLE:
					//wxDELETE(pVar->sqldata);
					break;
				default:
					wxLogError(wxT("Error deleting unknown parameter type\n"));
					break;
				}
			}
			//if (pVar->sqlind != 0)
			  //delete pVar->sqlind;
			//wxDELETE(pVar->sqlind);
		}
		// XSQLDA is allocated with malloc() in firebirdPreparedStatementWrapper;
		// pair the release with free(). delete[] here was UB (undefined for
		// malloc'd memory) and silently worked only because the runtime
		// happened to keep the block intact long enough for the process.
		free(m_FirebirdParameters);
		m_FirebirdParameters = nullptr;
	}

}


#include "postgresResultSet.h"
#include "postgresResultSetMetaData.h"

#include "backend/databaseLayer/databaseErrorCodes.h"
#include "backend/databaseLayer/databaseLayer.h"
#include "backend/databaseLayer/databaseLayerException.h"

#include "engine/libpq/libpq-fs.h"

ibDatabaseResultSetPostgres::ibDatabaseResultSetPostgres(ibInterfacePostgres* pInterface)
	: ibDatabaseResultSet()
{
	m_pInterface = pInterface;
	m_pResult = nullptr;
	m_FieldLookupMap.clear();
	m_nCurrentRow = -1;
	m_nTotalRows = 0;
	m_bBinaryResults = false;
}

ibDatabaseResultSetPostgres::ibDatabaseResultSetPostgres(ibInterfacePostgres* pInterface, PGresult* pResult)
	: ibDatabaseResultSet()
{
	m_pInterface = pInterface;
	m_pResult = pResult;
	m_nCurrentRow = -1;
	m_nTotalRows = m_pInterface->GetPQntuples()(m_pResult);
	m_bBinaryResults = m_pInterface->GetPQbinaryTuples()(m_pResult);

	int nFields = m_pInterface->GetPQnfields()(m_pResult);
	for (int i = 0; i < nFields; i++)
	{
		m_FieldLookupMap[ConvertFromUnicodeStream(m_pInterface->GetPQfname()(pResult, i))] = i;   // as written: the map is case-blind
	}
}

ibDatabaseResultSetPostgres::~ibDatabaseResultSetPostgres()
{
	Close();
}

bool ibDatabaseResultSetPostgres::Next()
{
	if (m_nTotalRows < 1)
		return false;

	m_nCurrentRow++;

	return (m_nCurrentRow < m_nTotalRows);
}

void ibDatabaseResultSetPostgres::Close()
{
	CloseMetaData();

	if (m_pResult != nullptr)
	{
		m_pInterface->GetPQclear()(m_pResult);
		m_pResult = nullptr;
	}
	m_FieldLookupMap.clear();
}

// get field
int ibDatabaseResultSetPostgres::GetResultInt(int nField)
{
	// Don't use nField-1 here since GetResultLong will take care of that
	return GetResultLong(nField);
}

ibString ibDatabaseResultSetPostgres::GetResultString(int nField)
{
	ibString strValue;
	if (m_bBinaryResults)
	{
		// BINARY RESULTS ARE NOT DECODED HERE — and this must be an exception,
		// not a log line. Logging and returning an empty value hands the caller a
		// plausible answer for a column it never read: an empty string, a zero, a
		// false. On PostgreSQL — the production engine — that is a wrong number in
		// a report nobody traces back to a driver that quietly declined.
		//
		// Nothing turns binary mode on today (m_bBinaryResults is never set), so
		// this raises only if somebody enables it before implementing the decode.
		ibDatabaseLayerException::Throw(
			ibBackendDatabaseException::Kind::Unknown,
			DATABASE_LAYER_ERROR,
			/*sqlState*/ wxEmptyString,
			wxT("PostgreSQL: binary result decoding is not implemented"));
	}
	else
	{
		if (nField != -1)
		{
			if (m_pInterface->GetPQgetisnull()(m_pResult, m_nCurrentRow, nField - 1) != 1)
			{
				ConvertFromUnicodeStream(m_pInterface->GetPQgetvalue()(m_pResult, m_nCurrentRow, nField - 1), strValue);
			}
		}
	}

	return strValue;
}

long long ibDatabaseResultSetPostgres::GetResultLong(int nField)
{
	long long nValue = 0;
	if (m_bBinaryResults)
	{
		// BINARY RESULTS ARE NOT DECODED HERE — and this must be an exception,
		// not a log line. Logging and returning an empty value hands the caller a
		// plausible answer for a column it never read: an empty string, a zero, a
		// false. On PostgreSQL — the production engine — that is a wrong number in
		// a report nobody traces back to a driver that quietly declined.
		//
		// Nothing turns binary mode on today (m_bBinaryResults is never set), so
		// this raises only if somebody enables it before implementing the decode.
		ibDatabaseLayerException::Throw(
			ibBackendDatabaseException::Kind::Unknown,
			DATABASE_LAYER_ERROR,
			/*sqlState*/ wxEmptyString,
			wxT("PostgreSQL: binary result decoding is not implemented"));
	}
	else
	{
		if (nField != -1)
		{
			if (m_pInterface->GetPQgetisnull()(m_pResult, m_nCurrentRow, nField - 1) != 1)
			{
				wxString strValue = ConvertFromUnicodeStream(m_pInterface->GetPQgetvalue()(m_pResult, m_nCurrentRow, nField - 1));
				strValue.ToLongLong(&nValue);
			}
		}
	}

	return nValue;
}

bool ibDatabaseResultSetPostgres::GetResultBool(int nField)
{
	bool bValue = false;
	if (m_bBinaryResults)
	{
		// BINARY RESULTS ARE NOT DECODED HERE — and this must be an exception,
		// not a log line. Logging and returning an empty value hands the caller a
		// plausible answer for a column it never read: an empty string, a zero, a
		// false. On PostgreSQL — the production engine — that is a wrong number in
		// a report nobody traces back to a driver that quietly declined.
		//
		// Nothing turns binary mode on today (m_bBinaryResults is never set), so
		// this raises only if somebody enables it before implementing the decode.
		ibDatabaseLayerException::Throw(
			ibBackendDatabaseException::Kind::Unknown,
			DATABASE_LAYER_ERROR,
			/*sqlState*/ wxEmptyString,
			wxT("PostgreSQL: binary result decoding is not implemented"));
	}
	else
	{
		if (nField != -1)
		{
			if (m_pInterface->GetPQgetisnull()(m_pResult, m_nCurrentRow, nField - 1) != 1)
			{
				wxString strValue = ConvertFromUnicodeStream(m_pInterface->GetPQgetvalue()(m_pResult, m_nCurrentRow, nField - 1));
				bValue = (strValue != wxT("0"));
			}
		}
	}

	return bValue;
}

ibDateTime ibDatabaseResultSetPostgres::GetResultDate(int nField)
{
	// TIMESTAMP results are text in binary and text mode alike: the ISO spelling of the parts, which is
	// the reading - read by the date's own door (fdatetime.h, ibDateTime::FromString; a fraction of a
	// second beyond the millisecond is finer than the reading and dropped). NULL is the empty date.
	ibDateTime date;
	if (m_pInterface->GetPQgetisnull()(m_pResult, m_nCurrentRow, nField - 1) == 1)
		return date;
	ibString text;
	ConvertFromUnicodeStream(m_pInterface->GetPQgetvalue()(m_pResult, m_nCurrentRow, nField - 1), text);
	date.FromString(text);
	return date;
}

void* ibDatabaseResultSetPostgres::GetResultBlob(int nField, wxMemoryBuffer& buffer)
{
	//int nLength = m_pInterface->GetPQgetlength()(m_pResult, m_nCurrentRow, nIndex);
	unsigned char* pBlob = (unsigned char*)m_pInterface->GetPQgetvalue()(m_pResult, m_nCurrentRow, nField - 1);
#if defined(_LP64) || defined(__LP64__) || defined(__arch64__) || defined(_WIN64)
	unsigned long long nUnescapedLength = 0;
#else 
	unsigned int nUnescapedLength = 0;
#endif
	unsigned char* pUnescapedBlob = m_pInterface->GetPQunescapeBytea()(pBlob, &nUnescapedLength);

	wxMemoryBuffer tempBuffer(nUnescapedLength);
	void* pUnescapedBuffer = tempBuffer.GetWriteBuf(nUnescapedLength);
	memcpy(pUnescapedBuffer, pUnescapedBlob, nUnescapedLength);
	m_pInterface->GetPQfreemem()(pUnescapedBlob);
	tempBuffer.UngetWriteBuf(nUnescapedLength);

	tempBuffer.SetBufSize(nUnescapedLength);
	tempBuffer.SetDataLen(nUnescapedLength);

	buffer = tempBuffer;
	buffer.UngetWriteBuf(nUnescapedLength);

	if (nUnescapedLength < 1)
		return nullptr;

	return buffer.GetData();
}

double ibDatabaseResultSetPostgres::GetResultDouble(int nField)
{
	double dblValue = 0;
	if (m_bBinaryResults)
	{
		// BINARY RESULTS ARE NOT DECODED HERE — and this must be an exception,
		// not a log line. Logging and returning an empty value hands the caller a
		// plausible answer for a column it never read: an empty string, a zero, a
		// false. On PostgreSQL — the production engine — that is a wrong number in
		// a report nobody traces back to a driver that quietly declined.
		//
		// Nothing turns binary mode on today (m_bBinaryResults is never set), so
		// this raises only if somebody enables it before implementing the decode.
		ibDatabaseLayerException::Throw(
			ibBackendDatabaseException::Kind::Unknown,
			DATABASE_LAYER_ERROR,
			/*sqlState*/ wxEmptyString,
			wxT("PostgreSQL: binary result decoding is not implemented"));
	}
	else
	{
		if (m_pInterface->GetPQgetisnull()(m_pResult, m_nCurrentRow, nField - 1) != 1)
		{
			wxString strValue = ConvertFromUnicodeStream(m_pInterface->GetPQgetvalue()(m_pResult, m_nCurrentRow, nField - 1));
			strValue.ToDouble(&dblValue);
		}
	}

	return dblValue;
}

ibNumber ibDatabaseResultSetPostgres::GetResultNumber(int nField)
{
	ibNumber dblValue = 0;
	if (m_bBinaryResults)
	{
		// BINARY RESULTS ARE NOT DECODED HERE — and this must be an exception,
		// not a log line. Logging and returning an empty value hands the caller a
		// plausible answer for a column it never read: an empty string, a zero, a
		// false. On PostgreSQL — the production engine — that is a wrong number in
		// a report nobody traces back to a driver that quietly declined.
		//
		// Nothing turns binary mode on today (m_bBinaryResults is never set), so
		// this raises only if somebody enables it before implementing the decode.
		ibDatabaseLayerException::Throw(
			ibBackendDatabaseException::Kind::Unknown,
			DATABASE_LAYER_ERROR,
			/*sqlState*/ wxEmptyString,
			wxT("PostgreSQL: binary result decoding is not implemented"));
	}
	else
	{
		if (m_pInterface->GetPQgetisnull()(m_pResult, m_nCurrentRow, nField - 1) != 1)
		{
			wxString strValue = ConvertFromUnicodeStream(m_pInterface->GetPQgetvalue()(m_pResult, m_nCurrentRow, nField - 1));
			dblValue.FromString(strValue.wchar_str());
		}
	}

	return dblValue;
}

bool ibDatabaseResultSetPostgres::IsFieldNull(int nField)
{
	return (m_pInterface->GetPQgetisnull()(m_pResult, m_nCurrentRow, nField - 1) == 1);
}

int ibDatabaseResultSetPostgres::LookupField(const wxString& strField)
{
	// Found, not walked — the names are kept as written (constructor) and the map ignores case; see firebirdResultSet.cpp.
	StringToIntMap::iterator SearchIterator = m_FieldLookupMap.find(strField);

	if (SearchIterator == m_FieldLookupMap.end())
	{
		// See sqliteResultSet.cpp for the rationale — caller code rarely
		// checks the -1 sentinel, so we throw to land in the unified
		// ibBackendException handler chain instead.
		ibDatabaseLayerException::Throw(
			ibBackendDatabaseException::Kind::Unknown,
			DATABASE_LAYER_FIELD_NOT_IN_RESULTSET,
			/*sqlState*/ wxEmptyString,
			wxT("Field '") + strField + wxT("' not found in the resultset"));
	}
	else
	{
		return ((*SearchIterator).second + 1); // Add +1 to make the result set 1-based rather than 0-based
	}
}

ibResultSetMetaData* ibDatabaseResultSetPostgres::GetMetaData()
{
	ibResultSetMetaData* pMetaData = new ibResultSetMetaDataPostgres(m_pInterface, m_pResult);
	LogMetaDataForCleanup(pMetaData);
	return pMetaData;
}


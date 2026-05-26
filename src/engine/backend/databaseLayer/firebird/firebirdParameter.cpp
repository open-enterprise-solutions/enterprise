#include "firebirdParameter.h"
#include "firebirdDatabaseLayer.h"
#include "firebirdBlobCompression.h"
#include "backend/databaseLayer/databaseLayerException.h"

// ctor
ibDatatabaseParameterFirebird::ibDatatabaseParameterFirebird(ibInterfaceFirebird* pInterface, XSQLVAR* pVar) : m_nParameterType(ibDatatabaseParameterFirebird::PARAM_NULL)
{
	m_pInterface = pInterface;
	m_pParameter = pVar;

	m_nNullFlag = -1;
	m_pParameter->sqlind = &m_nNullFlag; // NULL indicator
}

ibDatatabaseParameterFirebird::ibDatatabaseParameterFirebird(ibInterfaceFirebird* pInterface, XSQLVAR* pVar, const wxString& strValue, const wxCSConv* conv) : m_nParameterType(ibDatatabaseParameterFirebird::PARAM_STRING), m_strValue(strValue)
{
	m_pInterface = pInterface;
	m_pParameter = pVar;

	SetEncoding(conv);

	// Set to SQL_TEXT manually
	wxCharBuffer valueBuffer = ConvertToUnicodeStream(m_strValue);
	unsigned int length = GetEncodedStreamLength(m_strValue);

	m_pParameter->sqltype = SQL_TEXT | 1;

	// Raw bytes — wxStrncpy treated the buffer as wide chars and on Windows
	// (wxChar = wchar_t = 2 bytes) walked twice as far as `length`. The
	// driver hands UTF-8 to FB, so a plain memcpy of `length` bytes is the
	// only correct copy.
	memcpy(m_pParameter->sqldata, (const char*)valueBuffer, length);
	m_pParameter->sqllen = (ISC_SHORT)length;

	m_nNullFlag = 0;
	m_pParameter->sqlind = &m_nNullFlag; // NULL indicator
}

ibDatatabaseParameterFirebird::ibDatatabaseParameterFirebird(ibInterfaceFirebird* pInterface, XSQLVAR* pVar, const ibNumber& dblValue) : m_nParameterType(ibDatatabaseParameterFirebird::PARAM_NUMBER)
{
	m_pInterface = pInterface;
	m_pParameter = pVar;

	int nType = (m_pParameter->sqltype & ~1);

	if (nType == SQL_SHORT)
	{
		m_numValue = dblValue;
		m_sValue = dblValue.ToInt();
		m_pParameter->sqldata = (char*)&m_sValue;
	}
	else if (nType == SQL_FLOAT)
	{
		m_numValue = dblValue;
		m_fValue = dblValue.ToFloat();
		m_pParameter->sqldata = (char*)&m_fValue;
	}
	else if (nType == SQL_DOUBLE)
	{
		m_numValue = dblValue;
		m_dblValue = dblValue.ToDouble();
		m_pParameter->sqldata = (char*)&m_dblValue;
	}
	else if (nType == SQL_LONG)
	{
		m_numValue = dblValue;
		m_nValue = dblValue.ToInt();
		m_pParameter->sqldata = (char*)&m_nValue;
	}
	else if (nType == SQL_INT64)
	{
		m_numValue = dblValue;
		for (int i = 0; i < -pVar->sqlscale; i++)
			m_numValue *= 10;
		int64_t int64val = 0;
		m_numValue.ToInt(int64val);
		memcpy(m_pParameter->sqldata, &int64val, sizeof(int64val));
	}
	else if (nType == SQL_INT128)
	{
		m_numValue = dblValue;
		for (int i = 0; i < -pVar->sqlscale; i++)
			m_numValue *= 10;
		m_numValue.To128Bytes(reinterpret_cast<uint8_t*>(m_pParameter->sqldata));
	}
	else
	{
		// Error?
		wxLogError(wxT("Parameter type is not compatible with parameter of type number\n"));
	}

	m_nNullFlag = 0;
	m_pParameter->sqlind = &m_nNullFlag; // NULL indicator
}

ibDatatabaseParameterFirebird::ibDatatabaseParameterFirebird(ibInterfaceFirebird* pInterface, XSQLVAR* pVar, int nValue) : m_nParameterType(ibDatatabaseParameterFirebird::PARAM_INT)
{
	m_pInterface = pInterface;
	m_pParameter = pVar;
	m_nValue = nValue;

	m_pParameter->sqldata = (char*)&m_nValue;

	m_nNullFlag = 0;
	m_pParameter->sqlind = &m_nNullFlag; // NULL indicator
}

ibDatatabaseParameterFirebird::ibDatatabaseParameterFirebird(ibInterfaceFirebird* pInterface, XSQLVAR* pVar, double dblValue) : m_nParameterType(ibDatatabaseParameterFirebird::PARAM_DOUBLE)
{
	m_pInterface = pInterface;
	m_pParameter = pVar;

	int nType = (m_pParameter->sqltype & ~1);

	if (nType == SQL_FLOAT)
	{
		m_fValue = dblValue;
		m_pParameter->sqldata = (char*)&m_fValue;
	}
	else if (nType == SQL_DOUBLE)
	{
		m_dblValue = dblValue;
		m_pParameter->sqldata = (char*)&m_dblValue;
	}
	else
	{
		// Error?
		wxLogError(wxT("Parameter type is not compatible with parameter of type double\n"));
	}

	m_nNullFlag = 0;
	m_pParameter->sqlind = &m_nNullFlag; // NULL indicator
}

ibDatatabaseParameterFirebird::ibDatatabaseParameterFirebird(ibInterfaceFirebird* pInterface, XSQLVAR* pVar, bool bValue) : m_nParameterType(ibDatatabaseParameterFirebird::PARAM_BOOL)
{
	m_pInterface = pInterface;
	m_pParameter = pVar;

	m_bValue = bValue;
	m_nValue = (m_bValue) ? 1 : 0;

	m_pParameter->sqldata = (char*)&m_nValue;

	m_nNullFlag = 0;
	m_pParameter->sqlind = &m_nNullFlag; // NULL indicator
}

const long TIME_T_FACTOR = 1000l;

ibDatatabaseParameterFirebird::ibDatatabaseParameterFirebird(ibInterfaceFirebird* pInterface, XSQLVAR* pVar, const wxDateTime& dateValue) : m_nParameterType(ibDatatabaseParameterFirebird::PARAM_DATETIME)
{
	m_pInterface = pInterface;
	m_pParameter = pVar;

	struct tm dateAsTm;
	wxDateTime::Tm tm = dateValue.GetTm();
	dateAsTm.tm_sec = tm.sec;
	dateAsTm.tm_min = tm.min;
	dateAsTm.tm_hour = tm.hour;
	dateAsTm.tm_mday = tm.mday;
	dateAsTm.tm_mon = tm.mon;
	dateAsTm.tm_year = tm.year - 1900;
	m_pInterface->GetIscEncodeTimestamp()(&dateAsTm, &m_Date);

	m_nBufferLength = sizeof(ISC_TIMESTAMP);

	m_pParameter->sqldata = (char*)&m_Date;

	m_nNullFlag = 0;
	m_pParameter->sqlind = &m_nNullFlag; // NULL indicator
}

ibDatatabaseParameterFirebird::ibDatatabaseParameterFirebird(ibInterfaceFirebird* pInterface, XSQLVAR* pVar, const void* pData, long nDataLength) : m_nParameterType(ibDatatabaseParameterFirebird::PARAM_BLOB)
{
	m_pInterface = pInterface;
	m_pParameter = pVar;

	int nType = (m_pParameter->sqltype & ~1);

	if (nType == SQL_BLOB) {
		// Wrap with our OESC-magic header. The wrap step decides
		// whether to zlib-compress the body (size threshold + worth-it
		// ratio) or store raw under the header. Either way the result
		// is what ends up on disk via ResetBlob's putSegment loop.
		// Read path (firebirdResultSet::GetResultBlob) detects the
		// magic and decompresses transparently; legacy BLOBs that
		// pre-date this code path have no magic and pass through
		// untouched.
		wxMemoryBuffer wrapped = ibFirebirdBlobCompression::Wrap(pData, nDataLength);
		const size_t wrappedSize = wrapped.GetDataLen();
		void* pBuffer = m_BufferValue.GetWriteBuf(wrappedSize);
		memcpy(pBuffer, wrapped.GetData(), wrappedSize);
		m_BufferValue.UngetWriteBuf(wrappedSize);
		m_nBufferLength = static_cast<long unsigned int>(wrappedSize);
	}
	else if (nType == SQL_TEXT) {
		// Fixed-length CHAR / BINARY column: raw bytes go straight into
		// sqldata (allocated to sqllen + 1 in AllocateParameterSpace).
		// Clamp to the column's declared length to avoid running past
		// the allocation; FB compares CHAR/BINARY by full declared
		// length so a short-bound buffer wouldn't match anyway.
		const long cap = (long)m_pParameter->sqllen;
		const long n   = (nDataLength > cap) ? cap : nDataLength;
		memcpy(m_pParameter->sqldata, pData, n);
	}
	else if (nType == SQL_VARYING) {
		// Variable-length VARCHAR / VARBINARY — FB describes unnamed
		// `?` parameters bound for CHAR(N) columns as SQL_VARYING in
		// some builds.  Layout in sqldata is [u16 length][N data bytes]
		// (length prefix is little-endian on x86/x64). Without this
		// branch SetParamBlob was a silent no-op against varying-typed
		// params, leaving sqldata zero-init and breaking equality
		// against a binary column whose stored value is non-zero.
		const long cap = (long)m_pParameter->sqllen;
		const long n   = (nDataLength > cap) ? cap : nDataLength;
		// Length prefix as ISC_USHORT.
		ISC_USHORT len = (ISC_USHORT)n;
		memcpy(m_pParameter->sqldata, &len, sizeof(ISC_USHORT));
		memcpy(m_pParameter->sqldata + sizeof(ISC_USHORT), pData, n);
	}

	m_nNullFlag = 0;
	m_pParameter->sqlind = &m_nNullFlag; // NULL indicator
}

bool ibDatatabaseParameterFirebird::ResetBlob(isc_db_handle database, isc_tr_handle transaction)
{
	// If the databaes and transaction handles aren't valid then don't try to do anything
	if ((database == NULL) || (transaction == NULL))
		return false;

	//m_BlobId = NULL;
	m_pBlob = NULL;
	ISC_STATUS_ARRAY    status;              /* status vector */
	void* pData = m_BufferValue.GetData();
	int nDataLength = m_nBufferLength;//m_BufferValue.GetDataLen();

	memset(&m_BlobId, 0, sizeof(m_BlobId));
	int nReturn = m_pInterface->GetIscCreateBlob2()(status, &database, &transaction, &m_pBlob, &m_BlobId, 0, NULL);
	if (nReturn != 0)
	{
		const long nSqlCode = m_pInterface->GetIscSqlcode()(status);
		ibDatabaseLayerException::Throw(
			ibBackendDatabaseException::Kind::Unknown,
			ibDatabaseLayerFirebird::TranslateErrorCode(nSqlCode),
			/*sqlState*/ wxEmptyString,
			ibDatabaseLayerFirebird::TranslateErrorCodeToString(m_pInterface, nSqlCode, status));
		return false;
	}

	int dataFetched = 0;
	char* dataPtr = (char*)pData;
	while (dataFetched < nDataLength)
	{
		unsigned short segLen = (nDataLength - dataFetched) < 0xFFFF ? (nDataLength - dataFetched) : 0xFFFF;
		nReturn = m_pInterface->GetIscPutSegment()(status, &m_pBlob, segLen, dataPtr);
		if (nReturn != 0)
		{
			const long nSqlCode = m_pInterface->GetIscSqlcode()(status);
			ibDatabaseLayerException::Throw(
				ibBackendDatabaseException::Kind::Unknown,
				ibDatabaseLayerFirebird::TranslateErrorCode(nSqlCode),
				/*sqlState*/ wxEmptyString,
				ibDatabaseLayerFirebird::TranslateErrorCodeToString(m_pInterface, nSqlCode, status));
			return false;
		}

		dataFetched += segLen;
		dataPtr += segLen;
	}

	nReturn = m_pInterface->GetIscCloseBlob()(status, &m_pBlob);

	if (nReturn != 0)
	{
		const long nSqlCode = m_pInterface->GetIscSqlcode()(status);
		ibDatabaseLayerException::Throw(
			ibBackendDatabaseException::Kind::Unknown,
			ibDatabaseLayerFirebird::TranslateErrorCode(nSqlCode),
			/*sqlState*/ wxEmptyString,
			ibDatabaseLayerFirebird::TranslateErrorCodeToString(m_pInterface, nSqlCode, status));
		return false;
	}

	m_pParameter->sqldata = (char*)&m_BlobId;

	m_nNullFlag = 0;
	m_pParameter->sqlind = &m_nNullFlag; // NULL indicator

	return true;
}

ibDatatabaseParameterFirebird::~ibDatatabaseParameterFirebird()
{
}


#include "firebirdParameter.h"
#include "firebirdDatabaseLayer.h"
#include "backend/databaseLayer/databaseLayerException.h"

// ctor
CFirebirdParameter::CFirebirdParameter(CFirebirdInterface* pInterface, XSQLVAR* pVar) : m_nParameterType(CFirebirdParameter::PARAM_NULL)
{
	m_pInterface = pInterface;
	m_pParameter = pVar;

	m_nNullFlag = -1;
	m_pParameter->sqlind = &m_nNullFlag; // nullptr indicator
}

CFirebirdParameter::CFirebirdParameter(CFirebirdInterface* pInterface, XSQLVAR* pVar, const wxString& strValue, const wxCSConv* conv) : m_nParameterType(CFirebirdParameter::PARAM_STRING), m_strValue(strValue)
{
	m_pInterface = pInterface;
	m_pParameter = pVar;

	SetEncoding(conv);

	// Set to SQL_TEXT manually
	wxCharBuffer valueBuffer = ConvertToUnicodeStream(m_strValue);
	unsigned int length = GetEncodedStreamLength(m_strValue);

	m_pParameter->sqltype = SQL_TEXT | 1;

	wxStrncpy((wxChar*)m_pParameter->sqldata, (wxChar*)(const char*)valueBuffer, length);
	m_pParameter->sqllen = length;

	m_nNullFlag = 0;
	m_pParameter->sqlind = &m_nNullFlag; // nullptr indicator
}

CFirebirdParameter::CFirebirdParameter(CFirebirdInterface* pInterface, XSQLVAR* pVar, const number_t& dblValue) : m_nParameterType(CFirebirdParameter::PARAM_NUMBER)
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
		ttmath::Int<TTMATH_BITS(64)> int64val;
		m_numValue.ToInt(int64val);
		memcpy(m_pParameter->sqldata, &int64val, sizeof(int64val));
	}
	else if (nType == SQL_INT128)
	{
		m_numValue = dblValue;
		for (int i = 0; i < -pVar->sqlscale; i++) 
			m_numValue *= 10;
		ttmath::Int<TTMATH_BITS(128)> int128val;
		m_numValue.ToInt(int128val);
		memcpy(m_pParameter->sqldata, &int128val, sizeof(int128val));
	}
	else
	{
		// Error?
		wxLogError(_("Parameter type is not compatible with parameter of type number\n"));
	}

	m_nNullFlag = 0;
	m_pParameter->sqlind = &m_nNullFlag; // nullptr indicator
}

CFirebirdParameter::CFirebirdParameter(CFirebirdInterface* pInterface, XSQLVAR* pVar, int nValue) : m_nParameterType(CFirebirdParameter::PARAM_INT)
{
	m_pInterface = pInterface;
	m_pParameter = pVar;
	m_nValue = nValue;

	m_pParameter->sqldata = (char*)&m_nValue;

	m_nNullFlag = 0;
	m_pParameter->sqlind = &m_nNullFlag; // nullptr indicator
}

CFirebirdParameter::CFirebirdParameter(CFirebirdInterface* pInterface, XSQLVAR* pVar, double dblValue) : m_nParameterType(CFirebirdParameter::PARAM_DOUBLE)
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
		wxLogError(_("Parameter type is not compatible with parameter of type double\n"));
	}

	m_nNullFlag = 0;
	m_pParameter->sqlind = &m_nNullFlag; // nullptr indicator
}

CFirebirdParameter::CFirebirdParameter(CFirebirdInterface* pInterface, XSQLVAR* pVar, bool bValue) : m_nParameterType(CFirebirdParameter::PARAM_BOOL)
{
	m_pInterface = pInterface;
	m_pParameter = pVar;

	m_bValue = bValue;
	m_nValue = (m_bValue) ? 1 : 0;

	m_pParameter->sqldata = (char*)&m_nValue;

	m_nNullFlag = 0;
	m_pParameter->sqlind = &m_nNullFlag; // nullptr indicator
}

const long TIME_T_FACTOR = 1000l;

CFirebirdParameter::CFirebirdParameter(CFirebirdInterface* pInterface, XSQLVAR* pVar, const wxDateTime& dateValue) : m_nParameterType(CFirebirdParameter::PARAM_DATETIME)
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
	m_pParameter->sqlind = &m_nNullFlag; // nullptr indicator
}

CFirebirdParameter::CFirebirdParameter(CFirebirdInterface* pInterface, XSQLVAR* pVar, const void* pData, long nDataLength) : m_nParameterType(CFirebirdParameter::PARAM_BLOB)
{
	m_pInterface = pInterface;
	m_pParameter = pVar;

	// Just copy the data into the memory buffer for now.  We'll move the data over to the blob in the call to ResetBlob
	void* pBuffer = m_BufferValue.GetWriteBuf(nDataLength);
	memcpy(pBuffer, pData, nDataLength);
	m_nBufferLength = nDataLength;
}

bool CFirebirdParameter::ResetBlob(isc_db_handle database, isc_tr_handle transaction)
{
	// If the databaes and transaction handles aren't valid then don't try to do anything
	if ((database == nullptr) || (transaction == nullptr))
		return false;

	//m_BlobId = nullptr;
	m_pBlob = nullptr;
	ISC_STATUS_ARRAY    status;              /* status vector */
	void* pData = m_BufferValue.GetData();
	int nDataLength = m_nBufferLength;//m_BufferValue.GetDataLen();

	memset(&m_BlobId, 0, sizeof(m_BlobId));
	int nReturn = m_pInterface->GetIscCreateBlob2()(status, &database, &transaction, &m_pBlob, &m_BlobId, 0, nullptr);
	if (nReturn != 0)
	{
#if _USE_DATABASE_LAYER_EXCEPTIONS == 1
		long nSqlCode = m_pInterface->GetIscSqlcode()(status);
		DatabaseLayerException error(CFirebirdDatabaseLayer::TranslateErrorCode(nSqlCode),
			CFirebirdDatabaseLayer::TranslateErrorCodeToString(m_pInterface, nSqlCode, status));

		throw error;
#endif
		//isc_print_status(status);
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
#if _USE_DATABASE_LAYER_EXCEPTIONS == 1
			long nSqlCode = m_pInterface->GetIscSqlcode()(status);
			DatabaseLayerException error(CFirebirdDatabaseLayer::TranslateErrorCode(nSqlCode),
				CFirebirdDatabaseLayer::TranslateErrorCodeToString(m_pInterface, nSqlCode, status));

			throw error;
#endif

			//isc_print_status(status);
			return false;
		}

		dataFetched += segLen;
		dataPtr += segLen;
	}

	nReturn = m_pInterface->GetIscCloseBlob()(status, &m_pBlob);

	if (nReturn != 0)
	{
#if _USE_DATABASE_LAYER_EXCEPTIONS == 1
		long nSqlCode = m_pInterface->GetIscSqlcode()(status);
		DatabaseLayerException error(CFirebirdDatabaseLayer::TranslateErrorCode(nSqlCode),
			CFirebirdDatabaseLayer::TranslateErrorCodeToString(m_pInterface, nSqlCode, status));

		throw error;
#endif

		//isc_print_status(status);
		return false;
	}

	m_pParameter->sqldata = (char*)&m_BlobId;

	m_nNullFlag = 0;
	m_pParameter->sqlind = &m_nNullFlag; // nullptr indicator

	return true;
}

CFirebirdParameter::~CFirebirdParameter()
{
}


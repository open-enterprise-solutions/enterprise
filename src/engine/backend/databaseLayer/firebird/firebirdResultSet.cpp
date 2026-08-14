#include "firebirdResultSet.h"
#include "firebirdResultSetMetaData.h"
#include "firebirdDatabaseLayer.h"
#include "firebirdBlobCompression.h"
#include "backend/databaseLayer/databaseErrorCodes.h"
#include "backend/databaseLayer/databaseLayerException.h"

ibDatabaseResultSetFirebird::ibDatabaseResultSetFirebird(ibInterfaceFirebird* pInterface)
	: ibDatabaseResultSet()
{
	m_pInterface = pInterface;
	m_pDatabase = 0;
	m_pTransaction = 0;
	m_pStatement = 0;
	m_pFields = NULL;
	m_bManageStatement = false;
	m_bManageTransaction = false;
}

//ibDatabaseResultSetFirebird::ibDatabaseResultSetFirebird(const IBPP::Statement& statement)
ibDatabaseResultSetFirebird::ibDatabaseResultSetFirebird(ibInterfaceFirebird* pInterface, isc_db_handle pDatabase, isc_tr_handle pTransaction, isc_stmt_handle pStatement, XSQLDA* pFields, bool bManageStmt /*= false*/, bool bManageTrans /*= false*/)
	: ibDatabaseResultSet()
{
	m_pInterface = pInterface;
	m_pDatabase = pDatabase;
	m_pTransaction = pTransaction;
	m_pStatement = pStatement;
	m_pFields = pFields;
	m_bManageStatement = bManageStmt;
	m_bManageTransaction = bManageTrans;

	AllocateFieldSpace();
	PopulateFieldLookupMap();
}

ibDatabaseResultSetFirebird::~ibDatabaseResultSetFirebird()
{
	Close();
}

bool ibDatabaseResultSetFirebird::Next()
{
	ResetErrorCodes();

	int nReturn = m_pInterface->GetIscDsqlFetch()(m_Status, &m_pStatement, SQL_DIALECT_CURRENT, m_pFields);
	if (nReturn == 0) // No errors and retrieval successful
	{
		return true;
	}
	else if (nReturn == 100L) // No errors and no more rows
	{
		return false;
	}
	else  // Errors!!!
	{
		wxLogError(wxT("Error retrieving Next record\n"));
		InterpretErrorCodes();
		ThrowDatabaseException();
		return false;
	}
}

void ibDatabaseResultSetFirebird::Close()
{
	CloseMetaData();

	if (m_bManageTransaction && m_pTransaction)
	{
		int nReturn = m_pInterface->GetIscCommitTransaction()(m_Status, &m_pTransaction);
		// We're done with the transaction, so set it to NULL so that we know that a new transaction must be started if we run any queries
		m_pTransaction = 0;
		if (nReturn != 0)
		{
			InterpretErrorCodes();
			ThrowDatabaseException();
		}
	}

	// Delete the statement if we have ownership of it
	if (m_bManageStatement && m_pStatement)
	{
		int nReturn = m_pInterface->GetIscDsqlFreeStatement()(m_Status, &m_pStatement, DSQL_drop);
		m_pStatement = 0;
		if (nReturn != 0)
		{
			InterpretErrorCodes();
			ThrowDatabaseException();
		}
	}

	// Free the output fields structure
	if (m_pFields)
	{
		FreeFieldSpace();
		free(m_pFields);
		m_pFields = NULL;
	}
}


// get field
int ibDatabaseResultSetFirebird::GetResultInt(int nField)
{
	ResetErrorCodes();

	// Don't use nField-1 here since GetResultString will take care of that
	return GetResultLong(nField);
}

wxString ibDatabaseResultSetFirebird::GetResultString(int nField)
{
	ResetErrorCodes();

	wxString strReturn = wxEmptyString;
	XSQLVAR* pVar = &(m_pFields->sqlvar[nField - 1]);
	if (IsNull(pVar))
	{
		// The column is NULL
		strReturn = wxEmptyString;
	}
	else
	{
		short nType = pVar->sqltype & ~1;
		if (nType == SQL_TEXT)
		{
			strReturn = ConvertFromUnicodeStream(pVar->sqldata);
		}
		else if (nType == SQL_VARYING)
		{
			PARAMVARY* pVary = (PARAMVARY*)pVar->sqldata;
			pVary->vary_string[pVary->vary_length] = '\0';
			strReturn = ConvertFromUnicodeStream((const char*)pVary->vary_string);
		}
		else
		{
			// Incompatible field type
			// Set error codes and throw an exception here
			strReturn = wxT("");

			SetErrorMessage(wxT("Invalid field type"));
			SetErrorCode(DATABASE_LAYER_INCOMPATIBLE_FIELD_TYPE);

			ThrowDatabaseException();
		}
	}

	return strReturn;
}

long long ibDatabaseResultSetFirebird::GetResultLong(int nField)
{
	ResetErrorCodes();

	long long nReturn = 0;

	XSQLVAR* pVar = &(m_pFields->sqlvar[nField - 1]);
	if (IsNull(pVar))
	{
		// The column is NULL
		nReturn = 0;
	}
	else
	{
		short nType = pVar->sqltype & ~1;
		if (nType == SQL_SHORT)
		{
			short v = 0;
			memcpy(&v, pVar->sqldata, sizeof(v));
			nReturn = v;
		}
		else if (nType == SQL_LONG)
		{
			// FB SQL_LONG is exactly 32 bits — read int32_t. `long` is
			// 32 bits on Windows but 64 bits on LP64 Linux, so the
			// platform-default `long*` cast would over-read on Linux.
			int32_t v = 0;
			memcpy(&v, pVar->sqldata, sizeof(v));
			nReturn = v;
		}
		else if (nType == SQL_INT64)
		{
			int64_t v = 0;
			memcpy(&v, pVar->sqldata, sizeof(v));
			nReturn = v;
		}
		else if (nType == SQL_INT128)
		{
			// Caller asked for `long long`; we expose the low 64 bits and
			// silently truncate the high half. Anything that needs the
			// full 128-bit value goes through GetResultNumber instead.
			int64_t lo = 0;
			memcpy(&lo, pVar->sqldata, sizeof(lo));
			nReturn = lo;
		}
		else
		{
			// Incompatible field type
			// We should really set error codes and throw an exception here
			nReturn = 0;

			SetErrorMessage(wxT("Invalid field type"));
			SetErrorCode(DATABASE_LAYER_INCOMPATIBLE_FIELD_TYPE);

			ThrowDatabaseException();
		}

		// Apply the scale to the value. sqlscale is the exponent to
		// multiply by 10^scale; the previous formula (abs(scale)*10) was
		// only right for |scale|==1 and silently corrupted everything
		// else. Negative scale means stored value = real * 10^|scale|.
		if (nReturn != 0)
		{
			short nScale = pVar->sqlscale;
			if (nScale > 0)
			{
				for (short i = 0; i < nScale; ++i) nReturn *= 10;
			}
			else if (nScale < 0)
			{
				for (short i = 0; i < -nScale; ++i) nReturn /= 10;
			}
		}
	}

	return nReturn;
}

bool ibDatabaseResultSetFirebird::GetResultBool(int nField)
{
	ResetErrorCodes();

	// Don't use nField-1 here since GetResultString will take care of that
	int nValue = GetResultLong(nField);
	return (nValue != 0);
}

wxDateTime ibDatabaseResultSetFirebird::GetResultDate(int nField)
{
	ResetErrorCodes();

	wxDateTime dateReturn = wxDefaultDateTime;
	XSQLVAR* pVar = &(m_pFields->sqlvar[nField - 1]);
	if (IsNull(pVar))
	{
		// The column is NULL
		dateReturn = wxDefaultDateTime;
	}
	else
	{
		short nType = pVar->sqltype & ~1;
		if (nType == SQL_TIMESTAMP)
		{
			struct tm timeInTm;
			m_pInterface->GetIscDecodeTimestamp()((ISC_TIMESTAMP*)pVar->sqldata, &timeInTm);
			SetDateTimeFromTm(dateReturn, timeInTm);
		}
		else if (nType == SQL_TYPE_DATE)
		{
			struct tm timeInTm;
			m_pInterface->GetIscDecodeSqlDate()((ISC_DATE*)pVar->sqldata, &timeInTm);
			SetDateTimeFromTm(dateReturn, timeInTm);
		}
		else if (nType == SQL_TYPE_TIME)
		{
			struct tm timeInTm;
			m_pInterface->GetIscDecodeSqlTime()((ISC_TIME*)pVar->sqldata, &timeInTm);
			SetDateTimeFromTm(dateReturn, timeInTm);
		}
		else
		{
			// Incompatible field type
			// Set error codes and throw an exception here
			dateReturn = wxDefaultDateTime;

			SetErrorMessage(wxT("Invalid field type"));
			SetErrorCode(DATABASE_LAYER_INCOMPATIBLE_FIELD_TYPE);

			ThrowDatabaseException();
		}
	}

	return dateReturn;
}

void ibDatabaseResultSetFirebird::SetDateTimeFromTm(wxDateTime& dateReturn, struct tm& timeInTm)
{
	dateReturn.Set(timeInTm.tm_mday, wxDateTime::Month(timeInTm.tm_mon), timeInTm.tm_year + 1900, timeInTm.tm_hour, timeInTm.tm_min, timeInTm.tm_sec);
}

double ibDatabaseResultSetFirebird::GetResultDouble(int nField)
{
	double dblReturn = 0.00;

	XSQLVAR* pVar = &(m_pFields->sqlvar[nField - 1]);
	if (IsNull(pVar))
	{
		// The column is NULL
		dblReturn = 0.00;
	}
	else
	{
		short nType = pVar->sqltype & ~1;
		if (nType == SQL_FLOAT)
		{
			float v = 0.0f;
			memcpy(&v, pVar->sqldata, sizeof(v));
			dblReturn = v;
		}
		else if (nType == SQL_DOUBLE)
		{
			double v = 0.0;
			memcpy(&v, pVar->sqldata, sizeof(v));
			dblReturn = v;
		}
		else if (nType == SQL_LONG)
		{
			int32_t v = 0;
			memcpy(&v, pVar->sqldata, sizeof(v));
			dblReturn = v;
			for (int i = 0; i < -pVar->sqlscale; dblReturn /= 10, i++);
		}
		else if (nType == SQL_INT64)
		{
			int64_t v = 0;
			memcpy(&v, pVar->sqldata, sizeof(v));
			dblReturn = static_cast<double>(v);
			for (int i = 0; i < -pVar->sqlscale; dblReturn /= 10, i++);
		}
		else if (nType == SQL_SHORT)
		{
			short v = 0;
			memcpy(&v, pVar->sqldata, sizeof(v));
			dblReturn = v;
			for (int i = 0; i < -pVar->sqlscale; dblReturn /= 10, i++);
		}
		else
		{
			// Incompatible field type
			// Set error codes and throw an exception here
			dblReturn = 0.00;

			SetErrorMessage(wxT("Invalid field type"));
			SetErrorCode(DATABASE_LAYER_INCOMPATIBLE_FIELD_TYPE);

			ThrowDatabaseException();
		}
	}

	return dblReturn;
}

ibNumber ibDatabaseResultSetFirebird::GetResultNumber(int nField)
{
	ibNumber dblReturn = 0.00;

	XSQLVAR* pVar = &(m_pFields->sqlvar[nField - 1]);
	if (IsNull(pVar))
	{
		// The column is NULL
		dblReturn = 0.00;
	}
	else
	{
		short nType = pVar->sqltype & ~1;
		if (nType == SQL_FLOAT)
		{
			float v = 0.0f;
			memcpy(&v, pVar->sqldata, sizeof(v));
			dblReturn = v;
		}
		else if (nType == SQL_DOUBLE)
		{
			double v = 0.0;
			memcpy(&v, pVar->sqldata, sizeof(v));
			dblReturn = v;
		}
		else if (nType == SQL_LONG)
		{
			int32_t v = 0;
			memcpy(&v, pVar->sqldata, sizeof(v));
			dblReturn = v;
			for (int i = 0; i < -pVar->sqlscale; i++) dblReturn /= 10;
		}
		else if (nType == SQL_INT64)
		{
			int64_t int64val = 0;
			memcpy(&int64val, pVar->sqldata, sizeof(int64val));
			dblReturn = ibNumber(int64val);
			for (int i = 0; i < -pVar->sqlscale; i++)
				dblReturn /= 10;
		}
		else if (nType == SQL_INT128)
		{
			dblReturn.From128Bytes(reinterpret_cast<const uint8_t*>(pVar->sqldata));
			for (int i = 0; i < -pVar->sqlscale; i++)
				dblReturn /= 10;
		}
		else if (nType == SQL_SHORT)
		{
			short v = 0;
			memcpy(&v, pVar->sqldata, sizeof(v));
			dblReturn = v;
			for (int i = 0; i < -pVar->sqlscale; i++) dblReturn /= 10;
		}
		else
		{
			// Incompatible field type
			// Set error codes and throw an exception here
			dblReturn = 0.00;

			SetErrorMessage(wxT("Invalid field type"));
			SetErrorCode(DATABASE_LAYER_INCOMPATIBLE_FIELD_TYPE);

			ThrowDatabaseException();
		}
	}

	return dblReturn;
}

void* ibDatabaseResultSetFirebird::GetResultBlob(int nField, wxMemoryBuffer& buffer)
{
	ResetErrorCodes();

	XSQLVAR* pVar = &(m_pFields->sqlvar[nField - 1]);
	if (IsNull(pVar))
	{
		// The column is NULL
		wxMemoryBuffer tempBuffer(0);
		tempBuffer.SetBufSize(0);
		tempBuffer.SetDataLen(0);
		buffer = tempBuffer;
		return NULL;
	}
	else
	{
		short nType = pVar->sqltype & ~1;
		if (nType == SQL_BLOB)
		{
			ISC_QUAD blobId = *(ISC_QUAD*)pVar->sqldata;
			isc_blob_handle pBlob = 0;
			char szSegment[128];
			// Initialised, because a failed read leaves it untouched and it was being used as a
			// LENGTH regardless — the buffer below was sized from whatever happened to be on the
			// stack, and the loop appended that many bytes of it.
			unsigned short nSegmentLength = 0;

			// ⭐ ASK WHETHER IT OPENED. The status of isc_open_blob2 was dropped, and everything after
			// it read as if the blob were open: isc_get_segment on a null handle fails, but the loop's
			// second condition consults m_Status[1] — which, on that path, still holds the code from
			// whatever ran BEFORE this call. When that leftover happens to be isc_segment the loop
			// never ends, appending the same uninitialised segment forever. A blob that cannot be
			// opened has to say so, not hang.
			if (m_pInterface->GetIscOpenBlob2()(m_Status, &m_pDatabase, &m_pTransaction, &pBlob, &blobId, 0, NULL) != 0)
			{
				InterpretErrorCodes();
				ThrowDatabaseException();
				return NULL;
			}

			wxMemoryBuffer tempBuffer;
			unsigned int bufferSize = 0;
			for (;;)
			{
				const ISC_STATUS blobStatus =
					m_pInterface->GetIscGetSegment()(m_Status, &pBlob, &nSegmentLength, sizeof(szSegment), szSegment);

				// A segment arrived: either the whole one (0) or a partial one that continues
				// (isc_segment). Both carry data; anything else ends the read.
				if (blobStatus != 0 && m_Status[1] != isc_segment)
				{
					// isc_segstr_eof is the ORDINARY end of a blob, not a failure. Any other code is,
					// and it must not be returned as a short buffer that reads like a valid blob.
					const bool atEnd = (m_Status[1] == isc_segstr_eof);
					// READ THE FAILURE BEFORE CLOSING. isc_close_blob writes its own outcome into the
					// same status vector, so interpreting after it reports how the CLOSE went and
					// loses why the read stopped.
					if (!atEnd)
						InterpretErrorCodes();
					m_pInterface->GetIscCloseBlob()(m_Status, &pBlob);
					if (!atEnd)
					{
						ThrowDatabaseException();
						return NULL;
					}
					break;
				}

				tempBuffer.AppendData(szSegment, nSegmentLength);
				bufferSize += nSegmentLength;
			}

			tempBuffer.SetDataLen(bufferSize);
			tempBuffer.SetBufSize(bufferSize);

			// Unwrap the OESC magic-header envelope. Three cases:
			//   1. Magic present, flag = zlib → decompress.
			//   2. Magic present, flag = raw  → strip the 6-byte header.
			//   3. No magic (legacy BLOB written before this code path) →
			//      return the bytes verbatim.
			// The decision lives entirely in Unwrap; the read path never
			// branches on type-of-payload.
			buffer = ibFirebirdBlobCompression::Unwrap(tempBuffer.GetData(),
			                                           bufferSize);
		}
		else if (nType == SQL_TEXT)
		{
			wxMemoryBuffer tempBuffer(pVar->sqllen);
			tempBuffer.AppendData(pVar->sqldata, pVar->sqllen);		
			buffer = tempBuffer;
		}
		else
		{
			// Incompatible field type
			// Set error codes and throw an exception here
			wxMemoryBuffer tempBuffer(0);
			tempBuffer.SetBufSize(0);
			tempBuffer.SetDataLen(0);
			buffer = tempBuffer;

			SetErrorMessage(wxT("Invalid field type"));
			SetErrorCode(DATABASE_LAYER_INCOMPATIBLE_FIELD_TYPE);

			ThrowDatabaseException();
		}
	}

	return buffer.GetData();
}

bool ibDatabaseResultSetFirebird::IsFieldNull(int nField)
{
	XSQLVAR* pVar = &(m_pFields->sqlvar[nField - 1]);
	return IsNull(pVar);
}

bool ibDatabaseResultSetFirebird::IsNull(XSQLVAR* pVar)
{
	return ((pVar->sqltype & 1) && (*pVar->sqlind < 0));
}

void ibDatabaseResultSetFirebird::AllocateFieldSpace()
{
	if (m_pFields == NULL)
		return;

	for (int i = 0; i < m_pFields->sqld; i++)
	{
		XSQLVAR* pVar = &(m_pFields->sqlvar[i]);
		// Uniform array-of-char allocation per slot — FB only cares about
		// `sqldata` pointing to a memory region of the right size; the
		// engine writes typed bytes through it.  Storing as `new char[n]`
		// (instead of `(char*)new ISC_X` etc.) lets FreeFieldSpace use a
		// single wxDELETEA without typed-vs-char* delete UB.
		switch (pVar->sqltype & ~1)
		{
		case SQL_ARRAY:
		case SQL_BLOB:
			pVar->sqldata = new char[sizeof(ISC_QUAD)];
			memset(pVar->sqldata, 0, sizeof(ISC_QUAD));
			break;
		case SQL_TIMESTAMP:
			pVar->sqldata = new char[sizeof(ISC_TIMESTAMP)];
			memset(pVar->sqldata, 0, sizeof(ISC_TIMESTAMP));
			break;
		case SQL_TYPE_TIME:
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
			pVar->sqldata = new char[sizeof(short)];
			memset(pVar->sqldata, 0, sizeof(short));
			break;
		case SQL_LONG:
			// FB SQL_LONG is exactly 32 bits — allocate by int32_t size.
			// `long` is 64 bits on LP64 (Linux x86_64); using sizeof(long)
			// would over-allocate on Linux and confuse the engine.
			pVar->sqldata = new char[sizeof(int32_t)];
			memset(pVar->sqldata, 0, sizeof(int32_t));
			break;
		case SQL_INT64:
			pVar->sqldata = new char[sizeof(ISC_INT64)];
			memset(pVar->sqldata, 0, sizeof(ISC_INT64));
			break;
		case SQL_INT128:
			pVar->sqldata = new char[sizeof(FB_I128)];
			memset(pVar->sqldata, 0, sizeof(FB_I128));
			break;
		case SQL_FLOAT:
			pVar->sqldata = new char[sizeof(float)];
			memset(pVar->sqldata, 0, sizeof(float));
			break;
		case SQL_DOUBLE:
			pVar->sqldata = new char[sizeof(double)];
			memset(pVar->sqldata, 0, sizeof(double));
			break;
		default:
			break;
		}
		if (pVar->sqltype & 1)
			pVar->sqlind = new short(-1);	// 0 indicator
	}
}

void ibDatabaseResultSetFirebird::FreeFieldSpace()
{
	if (m_pFields == NULL)
		return;

	// Iterate sqld (described columns), not sqln (allocated capacity) —
	// AllocateFieldSpace only touches [0, sqld); slots in [sqld, sqln) hold
	// uninitialised bytes from malloc and `pVar->sqldata != 0` would be
	// reading garbage.  Symmetric to AllocateFieldSpace.
	for (int i = 0; i < m_pFields->sqld; i++)
	{
		XSQLVAR* pVar = &(m_pFields->sqlvar[i]);
		// All sqldata slots are now `new char[size]` array allocations —
		// uniform array delete is correct and previous typed scalar delete
		// (e.g. `delete (char*)new ISC_TIME`) was UB.
		if (pVar->sqldata != 0)
			wxDELETEA(pVar->sqldata);
		if ((pVar->sqltype & 1) && (pVar->sqlind != 0))
			wxDELETE(pVar->sqlind);
	}

	// XSQLDA itself is allocated with malloc() (see firebirdDatabaseLayer
	// DoRunQueryWithResults / firebirdPreparedStatementWrapper); the
	// matching free() lives in Close(). Don't null m_pFields here — that
	// would leave Close()'s free() looking at NULL and leak the outer
	// struct.
}

void ibDatabaseResultSetFirebird::PopulateFieldLookupMap()
{
	m_FieldLookupMap.clear();

	// Iterate through all the XSQLVAR structures mapping their synonymname fields to their positions in the XSQLDA->sqlvar array
	XSQLVAR* pVar = m_pFields->sqlvar;
	for (int i = 0; i < m_pFields->sqld; i++, pVar++)
	{
		wxString strField = ConvertFromUnicodeStream(pVar->synonymname);
		m_FieldLookupMap[strField] = i;
	}
}

int ibDatabaseResultSetFirebird::LookupField(const wxString& strField)
{
	StringToIntMap::iterator SearchIterator = std::find_if(m_FieldLookupMap.begin(), m_FieldLookupMap.end(),
		[strField](const auto pair) { return stringUtils::CompareString(pair.first, strField); });

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

void ibDatabaseResultSetFirebird::InterpretErrorCodes()
{
	// Diagnostic-only — the real error is decoded below and surfaced
	// via SetErrorMessage. Was wxLogError previously, but that just
	// printed "I'm in this function" on every fbclient failure
	// without adding context.
	long nSqlCode = m_pInterface->GetIscSqlcode()(m_Status);
	SetErrorCode(ibDatabaseLayerFirebird::TranslateErrorCode(nSqlCode));
	SetErrorMessage(ibDatabaseLayerFirebird::TranslateErrorCodeToString(m_pInterface, nSqlCode, m_Status));
}

ibResultSetMetaData* ibDatabaseResultSetFirebird::GetMetaData()
{
	ibResultSetMetaData* pMetaData = new ibDatabaseResultSetMetaDataFirebird(m_pFields);
	LogMetaDataForCleanup(pMetaData);
	return pMetaData;
}



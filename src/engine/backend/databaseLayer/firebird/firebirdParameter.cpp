#include "firebirdParameter.h"
#include "firebirdDatabaseLayer.h"
#include "firebirdBlobCompression.h"
#include "backend/databaseLayer/databaseLayerException.h"

void ibDatabaseParameterFirebird::RequireParameterBuffer() const
{
	if (m_pParameter != nullptr && m_pParameter->sqldata != nullptr)
		return;
	ibBackendCoreException::Error(
		_("Firebird: parameter buffer is not allocated (the prepared statement has no such parameter) — the table structure is likely out of date"));
}

// ctor
ibDatabaseParameterFirebird::ibDatabaseParameterFirebird(ibInterfaceFirebird* pInterface, XSQLVAR* pVar) : m_nParameterType(ibDatabaseParameterFirebird::PARAM_NULL)
{
	m_pInterface = pInterface;
	m_pParameter = pVar;

	m_nNullFlag = -1;
	m_pParameter->sqlind = &m_nNullFlag; // NULL indicator
}

ibDatabaseParameterFirebird::ibDatabaseParameterFirebird(ibInterfaceFirebird* pInterface, XSQLVAR* pVar, const wxString& strValue, const wxCSConv* conv) : m_nParameterType(ibDatabaseParameterFirebird::PARAM_STRING), m_strValue(strValue)
{
	m_pInterface = pInterface;
	m_pParameter = pVar;

	SetEncoding(conv);

	// Set to SQL_TEXT manually
	wxCharBuffer valueBuffer = ConvertToUnicodeStream(m_strValue);
	unsigned int length = GetEncodedStreamLength(m_strValue);

	m_pParameter->sqltype = SQL_TEXT | 1;

	// NO BUFFER MEANS NO SUCH PARAMETER — say so, do not write into nothing.
	//
	// sqldata is allocated by the driver when the statement is DESCRIBED, one slot per placeholder
	// the prepared SQL actually has. A null here means the bind is addressing a parameter the
	// statement does not carry — the usual cause being a schema the code believes in and the
	// database does not (a column added to the layout, the table not yet restructured).
	//
	// It used to memcpy straight into it, so that mismatch arrived as an access violation inside
	// the CRT with nothing on the stack naming a column — a crash where a message belonged.
	if (m_pParameter->sqldata == nullptr) {
		RequireParameterBuffer();
		return;
	}

	// Raw bytes — wxStrncpy treated the buffer as wide chars and on Windows
	// (wxChar = wchar_t = 2 bytes) walked twice as far as `length`. The
	// driver hands UTF-8 to FB, so a plain memcpy of `length` bytes is the
	// only correct copy.
	//
	// ⭐⭐ CLAMPED TO THE BUFFER THAT EXISTS, AND THE CAPACITY IS READ FIRST.
	//
	// sqldata was allocated when the statement was DESCRIBED, sized from the parameter's declared
	// width (AllocateParameterSpace: `new char[sqllen + 1]` for SQL_TEXT, `+ 3` for SQL_VARYING).
	// The value's encoded length is an unrelated number — a string longer than the column it is
	// compared against wrote straight past the allocation, and heap corruption surfaces wherever the
	// next allocation happens to live, never here.
	//
	// The order matters as much as the clamp: the assignment below REPLACES sqllen with the value's
	// length, so after it there is no record of how much room the buffer has. Anything that clamps
	// later would be clamping against the value it just wrote. The two sibling branches in
	// SetParamBlob already clamp this way; this constructor was the one that did not.
	// ⚠ AND ZERO MEANS "I DO NOT KNOW", NOT "THERE IS NO ROOM". Written as
	// `capacity = sqllen > 0 ? sqllen : 0`, an undescribed parameter clamped every value to NOTHING —
	// so a string went in and came back EMPTY, with the write reporting success. Caught 2026-08-14 on
	// a chart of accounts: the code survived (it is written through another path) and the description
	// was blank after every save.
	//
	// Two opposite facts must not share one number. Clamp only where the room is actually known.
	if (m_pParameter->sqllen > 0 && length > (unsigned int)m_pParameter->sqllen)
		length = (unsigned int)m_pParameter->sqllen;

	memcpy(m_pParameter->sqldata, (const char*)valueBuffer, length);
	m_pParameter->sqllen = (ISC_SHORT)length;

	m_nNullFlag = 0;
	m_pParameter->sqlind = &m_nNullFlag; // NULL indicator
}

ibDatabaseParameterFirebird::ibDatabaseParameterFirebird(ibInterfaceFirebird* pInterface, XSQLVAR* pVar, const ibNumber& dblValue) : m_nParameterType(ibDatabaseParameterFirebird::PARAM_NUMBER)
{
	m_pInterface = pInterface;
	m_pParameter = pVar;

	int nType = (m_pParameter->sqltype & ~1);

	// ⭐⭐ A SCALED COLUMN IS AN INTEGER WITH THE POINT IMPLIED, AND EVERY INTEGER TYPE CARRIES ONE.
	//
	// Firebird stores NUMERIC(p,s) as an integer of p digits and remembers the scale beside it, so a
	// value must be multiplied by 10^-sqlscale before it is written and divided by the same power when
	// it is read. The read side does that for every integer type (firebirdResultSet.cpp); the write
	// side did it in TWO of the four — SQL_INT64 and SQL_INT128 scaled, SQL_SHORT and SQL_LONG did not.
	//
	// It is not a corner: Firebird maps NUMERIC(1..4,s) to SMALLINT and NUMERIC(5..9,s) to INTEGER, so
	// an ordinary money declaration — `Number(9,2)` — took the unscaled branch. 1234.56 was written as
	// 1234 and read back as 12.34: a hundredfold understatement, silent, on the write path of the
	// default driver. The multiplication is written ONCE now, because a rule spelled per branch is a
	// rule two branches can miss.
	const auto scaledForStorage = [&pVar](const ibNumber& value) {
		ibNumber stored = value;
		for (int i = 0; i < -pVar->sqlscale; i++)
			stored *= 10;
		return stored;
	};

	// ⚠ AND THE STORED INTEGER MUST FIT. `ToInt()` (no argument) CLAMPS at INT_MAX and the clamped
	// value then wraps modularly into a 16-bit field — 40000 stored as -25536, a sign flip on money.
	// The checked form reports instead, and a report is the only honest answer here: a number that does
	// not fit the column cannot be written, and writing a different one is worse than refusing.
	//
	// ⚠ The description is DATA, so it rides as an ARGUMENT: `Error` is a printf-style vararg, and a
	// value handed in as the format string would read any '%' inside it as a specifier.
	const auto storedInteger = [](const ibNumber& stored, int64_t low, int64_t high, const wxChar* what) {
		int64_t out = 0;
		if (stored.ToInt(out) != 0 || out < low || out > high) {
			ibBackendCoreException::Error(
				_("Firebird: the value does not fit the %s column it is written to (scaled: %s)"),
				what, stored.ToString());
		}
		return out;
	};

	if (nType == SQL_SHORT)
	{
		m_numValue = scaledForStorage(dblValue);
		m_sValue   = static_cast<short>(storedInteger(m_numValue, -32768, 32767, wxT("SMALLINT")));
		m_pParameter->sqldata = (char*)&m_sValue;
	}
	else if (nType == SQL_FLOAT)
	{
		// Not scaled, and not scalable: a FLOAT column carries no sqlscale — the point is in the value.
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
		m_numValue = scaledForStorage(dblValue);
		m_nValue   = static_cast<int32_t>(storedInteger(m_numValue, -2147483648LL, 2147483647LL, wxT("INTEGER")));
		m_pParameter->sqldata = (char*)&m_nValue;
	}
	else if (nType == SQL_INT64)
	{
		// ⚠ THE RETURN CODE IS READ. On overflow ToInt leaves `out` UNTOUCHED, so ignoring it wrote a
		// ZERO — and this branch carries more than money: `_RTRef` holds an ibClassID, an UNSIGNED
		// 64-bit value whose plugin range (0x80..0xFF) and raw-FNV ids have the top bit set. A reference
		// whose type stored as 0 is a row whose identity cannot be read back.
		//
		// So a value past INT64_MAX is not an error here — it is 64 bits that do not happen to be a
		// signed number, and a BIGINT column holds 64 bits either way. It is stored as the same bit
		// pattern, which round-trips exactly; only a value that fits NEITHER reading is refused.
		m_numValue = scaledForStorage(dblValue);
		int64_t int64val = 0;
		if (m_numValue.ToInt(int64val) != 0) {
			unsigned long long bits = 0;
			if (m_numValue.ToInt(bits) != 0) {
				ibBackendCoreException::Error(
					_("Firebird: the value does not fit the BIGINT column it is written to (scaled: %s)"),
					m_numValue.ToString());
			}
			memcpy(&int64val, &bits, sizeof(int64val));
		}
		// The same guard the string constructor carries, for the same reason: these two branches are
		// the only numeric ones that write into a buffer the DESCRIBE allocated rather than pointing
		// sqldata at a member of their own. A null here is a bind addressing a parameter the
		// statement does not have, and it used to arrive as an access violation inside the CRT.
		RequireParameterBuffer();
		memcpy(m_pParameter->sqldata, &int64val, sizeof(int64val));
	}
	else if (nType == SQL_INT128)
	{
		m_numValue = scaledForStorage(dblValue);
		RequireParameterBuffer();
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

ibDatabaseParameterFirebird::ibDatabaseParameterFirebird(ibInterfaceFirebird* pInterface, XSQLVAR* pVar, int nValue) : m_nParameterType(ibDatabaseParameterFirebird::PARAM_INT)
{
	m_pInterface = pInterface;
	m_pParameter = pVar;
	m_nValue = nValue;

	m_pParameter->sqldata = (char*)&m_nValue;

	m_nNullFlag = 0;
	m_pParameter->sqlind = &m_nNullFlag; // NULL indicator
}

ibDatabaseParameterFirebird::ibDatabaseParameterFirebird(ibInterfaceFirebird* pInterface, XSQLVAR* pVar, double dblValue) : m_nParameterType(ibDatabaseParameterFirebird::PARAM_DOUBLE)
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

ibDatabaseParameterFirebird::ibDatabaseParameterFirebird(ibInterfaceFirebird* pInterface, XSQLVAR* pVar, bool bValue) : m_nParameterType(ibDatabaseParameterFirebird::PARAM_BOOL)
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

ibDatabaseParameterFirebird::ibDatabaseParameterFirebird(ibInterfaceFirebird* pInterface, XSQLVAR* pVar, const wxDateTime& dateValue) : m_nParameterType(ibDatabaseParameterFirebird::PARAM_DATETIME)
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

ibDatabaseParameterFirebird::ibDatabaseParameterFirebird(ibInterfaceFirebird* pInterface, XSQLVAR* pVar, const void* pData, long nDataLength) : m_nParameterType(ibDatabaseParameterFirebird::PARAM_BLOB)
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
		// ⚠ cap == 0 means UNDESCRIBED, not "no room" — clamping to it would bind nothing (see the string
		// ctor above, where exactly that emptied every value).
		const long cap = (long)m_pParameter->sqllen;
		const long n   = (cap > 0 && nDataLength > cap) ? cap : nDataLength;
		RequireParameterBuffer();
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
		// ⚠ cap == 0 means UNDESCRIBED, not "no room" — clamping to it would bind nothing (see the string
		// ctor above, where exactly that emptied every value).
		const long cap = (long)m_pParameter->sqllen;
		const long n   = (cap > 0 && nDataLength > cap) ? cap : nDataLength;
		// Length prefix as ISC_USHORT.
		ISC_USHORT len = (ISC_USHORT)n;
		RequireParameterBuffer();
		memcpy(m_pParameter->sqldata, &len, sizeof(ISC_USHORT));
		memcpy(m_pParameter->sqldata + sizeof(ISC_USHORT), pData, n);
	}

	m_nNullFlag = 0;
	m_pParameter->sqlind = &m_nNullFlag; // NULL indicator
}

bool ibDatabaseParameterFirebird::ResetBlob(isc_db_handle database, isc_tr_handle transaction)
{
	// If the databaes and transaction handles aren't valid then don't try to do anything
	if ((database == 0) || (transaction == 0))
		return false;

	//m_BlobId = NULL;
	m_pBlob = 0;
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
	}

	m_pParameter->sqldata = (char*)&m_BlobId;

	m_nNullFlag = 0;
	m_pParameter->sqlind = &m_nNullFlag; // NULL indicator

	return true;
}

ibDatabaseParameterFirebird::~ibDatabaseParameterFirebird()
{
}


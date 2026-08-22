#include "databaseLayer.h"
#include "databaseErrorCodes.h"
#include "databaseLayerException.h"

#include "connectionPool.h"

// ctor()
ibDatabaseLayer::ibDatabaseLayer()
	: ibDatabaseErrorReporter()
{
}

// dtor()
ibDatabaseLayer::~ibDatabaseLayer()
{
	CloseResultSets();
	CloseStatements();
}

// GetDialect() is pure virtual — each concrete driver owns its dialect
// (see <driver>/<driver>DatabaseLayer.cpp::Dialect; ODBC returns the ANSI default).

// --- Transaction wrappers (Option A: nested-safe counter layer) ------------
//
// See databaseLayer.h for the semantics. Drivers override the Do* methods;
// the wrappers here enforce "only the outermost level touches the driver".

void ibDatabaseLayer::BeginTransaction(const ibTxOptions& opts)
{
	if (m_txDepth == 0) {
		DoBeginTransaction(opts);   // may throw — depth stays 0, state clean
		m_txAborted = false;
		// Pin this conn to the current holder (ibSession::Current())
		// for the whole TX. While set, every db_query call from the
		// same session — across threads, across worker dispatch —
		// resolves to this exact conn. SetActiveTxConnection is a
		// no-op on threads that have no Current() session bound; in
		// that mode the TX runs at the driver level only and the
		// caller's own scope is responsible for routing.
		ibConnectionPool::SetActiveTxConnection(shared_from_this());
	}
	++m_txDepth;
}

void ibDatabaseLayer::Commit()
{
	if (m_txDepth == 0)
		return;                      // no open transaction; silent no-op

	if (m_txDepth > 1) {
		--m_txDepth;                 // nested inner commit — count down, defer
		return;
	}

	// Outermost level — resolve to the driver. Reset state before the
	// driver call so that if DoCommit / DoRollBack throws the depth
	// still reflects "no transaction".
	//
	// ⚠ THE TX IS *NOT* GONE EITHER WAY — that is what this comment used to claim, and Firebird is the
	// counter-example. A commit that fails there leaves the transaction ACTIVE and holding every lock
	// it took; only the DEPTH went to zero, so this layer answered IsActiveTransaction() = false while
	// the database went on blocking everyone. The next apply then waited on locks nobody would ever
	// release and died as a "deadlock" that named neither the holder nor the original fault.
	// Firebird compiles views and triggers AT COMMIT, so a refusal here is not exotic — it is the
	// normal way a bad bundle reports itself.
	m_txDepth = 0;
	const bool aborted = m_txAborted;
	m_txAborted = false;
	// Release the holder's TX pin BEFORE the driver call. The layer
	// stays alive through whatever other shared_ptr the caller holds
	// (scope, pool entry after drop, etc.); if this was the only
	// reference it will be released after the driver op completes
	// naturally via RAII.
	ibConnectionPool::ClearActiveTxConnection(this);
	if (aborted) {
		DoRollBack();
		return;
	}

	// A REFUSED COMMIT IS ROLLED BACK HERE, once, for every driver and every caller. Doing it at the
	// call sites would mean each of them knowing this about Firebird — and the two that mattered
	// (the restructuring commit, the post-commit re-read) could not have done it anyway: the depth
	// above is already zero, so the state they would have tested says there is nothing to roll back.
	// The refusal itself still travels; what does not travel any more is the lock.
	try {
		DoCommit();
	}
	catch (...) {
		try { DoRollBack(); } catch (...) { /* swallowed: cleanup after a failed commit — the caller's
		                                       exception is the one worth reporting, and a rollback
		                                       that also fails leaves nothing further to attempt */ }
		throw;
	}
}

void ibDatabaseLayer::RollBack()
{
	if (m_txDepth == 0)
		return;                      // nothing to roll back

	m_txAborted = true;              // poison any pending outer commit

	if (m_txDepth > 1) {
		--m_txDepth;                 // nested inner rollback — count down, defer real op
		return;
	}

	m_txDepth = 0;
	m_txAborted = false;
	ibConnectionPool::ClearActiveTxConnection(this);
	DoRollBack();
}

#if !wxUSE_UTF8_LOCALE_ONLY
int ibDatabaseLayer::DoRunQueryWchar(const wxChar* format, ...)
{
	va_list args;
	va_start(args, format);
	wxString strQuery;

	strQuery.PrintfV(format, args);
	va_end(args);

	return DoRunQuery(strQuery, true);
}

ibDatabaseResultSet* ibDatabaseLayer::DoRunQueryWithResultsWchar(const wxChar* format, ...)
{
	va_list args;
	va_start(args, format);
	wxString strQuery;

	strQuery.PrintfV(format, args);
	va_end(args);

	return DoRunQueryWithResults(strQuery);
}

ibPreparedStatement* ibDatabaseLayer::DoPrepareStatementWchar(const wxChar* format, ...)
{
	va_list args;
	va_start(args, format);
	wxString strQuery;

	strQuery.PrintfV(format, args);
	va_end(args);

	return DoPrepareStatement(strQuery);
}
#endif

#if wxUSE_UNICODE_UTF8
int ibDatabaseLayer::DoRunQueryUtf8(const wxChar* format, ...)
{
	va_list args;
	va_start(args, format);
	wxString strQuery;

	strQuery.PrintfV(format, args);
	va_end(args);

	return DoRunQuery(strQuery, true);
}

ibDatabaseResultSet* ibDatabaseLayer::DoRunQueryWithResultsUtf8(const wxChar* format, ...)
{
	va_list args;
	va_start(args, format);
	wxString strQuery;

	strQuery.PrintfV(format, args);
	va_end(args);

	return DoRunQueryWithResults(strQuery);
}

ibPreparedStatement* ibDatabaseLayer::DoPrepareStatementUtf8(const wxChar* format, ...)
{
	va_list args;
	va_start(args, format);
	wxString strQuery;

	strQuery.PrintfV(format, args);
	va_end(args);

	return DoPrepareStatement(strQuery);
}
#endif

void ibDatabaseLayer::CloseResultSets()
{
	// Iterate through all of the result sets and close them all
	DatabaseResultSetHashSet::iterator start = m_ResultSets.begin();
	DatabaseResultSetHashSet::iterator stop = m_ResultSets.end();
	while (start != stop)
	{
		ibJournalInfo(wxT("db"),wxT("ResultSet NOT closed and cleaned up by the ibDatabaseLayer dtor"));
		delete(*start++);
	}
	m_ResultSets.clear();
}

void ibDatabaseLayer::CloseStatements()
{

	// Iterate through all of the statements and close them all
	DatabaseStatementHashSet::iterator start = m_Statements.begin();
	DatabaseStatementHashSet::iterator stop = m_Statements.end();
	while (start != stop)
	{
		ibJournalInfo(wxT("db"),wxT("PreparedStatement NOT closed and cleaned up by the DatabaseLayer dtor"));
		//delete (*start); start++;
		delete(*start++);
	}
	m_Statements.clear();
}

bool ibDatabaseLayer::CloseResultSet(ibDatabaseResultSet*& pResultSet)
{
	if (pResultSet != nullptr)
	{
		// Check if we have this result set in our list
		if (m_ResultSets.find(pResultSet) != m_ResultSets.end())
		{
			// Remove the result set pointer from the list and delete the pointer
			m_ResultSets.erase(pResultSet); wxDELETE(pResultSet);
			return true;
		}

		// If not then iterate through all of the statements and see
		//  if any of them have the result set in their lists
		DatabaseStatementHashSet::iterator it;
		for (it = m_Statements.begin(); it != m_Statements.end(); ++it)
		{
			// If the statement knows about the result set then it will close the 
			//  result set and return true, otherwise it will return false
			ibPreparedStatement* pStatement = *it;
			if (pStatement != nullptr)
			{
				if (pStatement->CloseResultSet(pResultSet))
				{
					return true;
				}
			}
		}

		// If we don't know about the result set and the statements don't
		//  know about it, the just delete it
		wxDELETE(pResultSet);
		return true;
	}
	else
	{
		// Return false on nullptr pointer
		return false;
	}

}

bool ibDatabaseLayer::CloseStatement(ibPreparedStatement*& pStatement)
{
	if (pStatement != nullptr)
	{
		// See if we know about this pointer, if so then remove it from the list
		if (m_Statements.find(pStatement) != m_Statements.end()) {
			// Remove the statement pointer from the list and delete the pointer
			m_Statements.erase(pStatement); wxDELETE(pStatement);
			return true;
		}

		// Otherwise just delete it
		wxDELETE(pStatement);
		return true;
	}
	else
	{
		// Return false on nullptr pointer
		return false;
	}
}


int ibDatabaseLayer::GetSingleResultInt(const wxString& strSQL, const wxString& strField, bool bRequireUniqueResult /*= true*/)
{
	wxVariant variant(strField);
	return GetSingleResultInt(strSQL, &variant, bRequireUniqueResult);
}

int ibDatabaseLayer::GetSingleResultInt(const wxString& strSQL, int nField, bool bRequireUniqueResult /*= true*/)
{
	wxVariant variant((long)nField);
	return GetSingleResultInt(strSQL, &variant, bRequireUniqueResult);
}

int ibDatabaseLayer::GetSingleResultInt(const wxString& strSQL, const wxVariant* field, bool bRequireUniqueResult /*= true*/)
{
	bool valueRetrievedFlag = false;
	int value = -1;

	ibDatabaseResultSet* pResult = nullptr;
	try {
		pResult = ExecuteQuery(strSQL);

		while (pResult->Next())
		{
			if (valueRetrievedFlag)
			{
				// Close the result set, reset the value and throw an exception
				CloseResultSet(pResult);
				pResult = nullptr;
				value = -1;
				SetErrorCode(DATABASE_LAYER_NON_UNIQUE_RESULTSET);
				SetErrorMessage(wxT("A non-unique result was returned."));
				ThrowDatabaseException();
				return value;
			}
			else
			{
				if (field->IsType(wxT("string")))
					value = pResult->GetResultInt(field->GetString());
				else
					value = pResult->GetResultInt(field->GetLong());
				valueRetrievedFlag = true;

				// If the user isn't concerned about returning a unique result,
				//  then just exit after the first record is found
				if (!bRequireUniqueResult)
					break;
			}
		}

		if (pResult != nullptr)
		{
			CloseResultSet(pResult);
			pResult = nullptr;
		}

		// Make sure that a value was retrieved from the database
		if (!valueRetrievedFlag)
		{
			value = -1;
			SetErrorCode(DATABASE_LAYER_NO_ROWS_FOUND);
			SetErrorMessage(wxT("No result was returned."));
			ThrowDatabaseException();
			return value;
		}
	}
	catch (const ibBackendException&) {
		// Close any still-open result set before propagating; preserves the
		// in-flight exception (sqlstate / native_code on derived types).
		if (pResult != nullptr) {
			CloseResultSet(pResult);
			pResult = nullptr;
		}
		throw;
	}

	return value;
}

wxString ibDatabaseLayer::GetSingleResultString(const wxString& strSQL, int nField, bool bRequireUniqueResult /*= true*/)
{
	wxVariant variant((long)nField);
	return GetSingleResultString(strSQL, &variant, bRequireUniqueResult);
}

wxString ibDatabaseLayer::GetSingleResultString(const wxString& strSQL, const wxString& strField, bool bRequireUniqueResult /*= true*/)
{
	wxVariant variant(strField);
	return GetSingleResultString(strSQL, &variant, bRequireUniqueResult);
}

wxString ibDatabaseLayer::GetSingleResultString(const wxString& strSQL, const wxVariant* field, bool bRequireUniqueResult /*= true*/)
{
	bool valueRetrievedFlag = false;
	wxString value = wxEmptyString;

	ibDatabaseResultSet* pResult = nullptr;
	try {
		pResult = ExecuteQuery(strSQL);

		while (pResult->Next())
		{
			if (valueRetrievedFlag)
			{
				// Close the result set, reset the value and throw an exception
				CloseResultSet(pResult);
				pResult = nullptr;
				value = wxEmptyString;
				SetErrorCode(DATABASE_LAYER_NON_UNIQUE_RESULTSET);
				SetErrorMessage(wxT("A non-unique result was returned."));
				ThrowDatabaseException();
				return value;
			}
			else
			{
				if (field->IsType(wxT("string")))
					value = pResult->GetResultString(field->GetString());
				else
					value = pResult->GetResultString(field->GetLong());
				valueRetrievedFlag = true;

				// If the user isn't concerned about returning a unique result,
				//  then just exit after the first record is found
				if (!bRequireUniqueResult)
					break;
			}
		}

		if (pResult != nullptr)
		{
			CloseResultSet(pResult);
			pResult = nullptr;
		}

		// Make sure that a value was retrieved from the database
		if (!valueRetrievedFlag)
		{
			value = wxEmptyString;
			SetErrorCode(DATABASE_LAYER_NO_ROWS_FOUND);
			SetErrorMessage(wxT("No result was returned."));
			ThrowDatabaseException();
			return value;
		}
	}
	catch (const ibBackendException&) {
		// Close any still-open result set before propagating; preserves the
		// in-flight exception (sqlstate / native_code on derived types).
		if (pResult != nullptr) {
			CloseResultSet(pResult);
			pResult = nullptr;
		}
		throw;
	}

	return value;
}

long ibDatabaseLayer::GetSingleResultLong(const wxString& strSQL, int nField, bool bRequireUniqueResult /*= true*/)
{
	wxVariant variant((long)nField);
	return GetSingleResultLong(strSQL, &variant, bRequireUniqueResult);
}

long ibDatabaseLayer::GetSingleResultLong(const wxString& strSQL, const wxString& strField, bool bRequireUniqueResult /*= true*/)
{
	wxVariant variant(strField);
	return GetSingleResultLong(strSQL, &variant, bRequireUniqueResult);
}

long ibDatabaseLayer::GetSingleResultLong(const wxString& strSQL, const wxVariant* field, bool bRequireUniqueResult /*= true*/)
{
	bool valueRetrievedFlag = false;
	long value = -1;

	ibDatabaseResultSet* pResult = nullptr;
	try {
		pResult = ExecuteQuery(strSQL);

		while (pResult->Next())
		{
			if (valueRetrievedFlag)
			{
				// Close the result set, reset the value and throw an exception
				CloseResultSet(pResult);
				pResult = nullptr;
				value = -1;
				SetErrorCode(DATABASE_LAYER_NON_UNIQUE_RESULTSET);
				SetErrorMessage(wxT("A non-unique result was returned."));
				ThrowDatabaseException();
				return value;
			}
			else
			{
				if (field->IsType(wxT("string")))
					value = pResult->GetResultLong(field->GetString());
				else
					value = pResult->GetResultLong(field->GetLong());
				valueRetrievedFlag = true;

				// If the user isn't concerned about returning a unique result,
				//  then just exit after the first record is found
				if (!bRequireUniqueResult)
					break;
			}
		}

		if (pResult != nullptr)
		{
			CloseResultSet(pResult);
			pResult = nullptr;
		}

		// Make sure that a value was retrieved from the database
		if (!valueRetrievedFlag)
		{
			value = -1;
			SetErrorCode(DATABASE_LAYER_NO_ROWS_FOUND);
			SetErrorMessage(wxT("No result was returned."));
			ThrowDatabaseException();
			return value;
		}
	}
	catch (const ibBackendException&) {
		// Close any still-open result set before propagating; preserves the
		// in-flight exception (sqlstate / native_code on derived types).
		if (pResult != nullptr) {
			CloseResultSet(pResult);
			pResult = nullptr;
		}
		throw;
	}

	return value;
}

bool ibDatabaseLayer::GetSingleResultBool(const wxString& strSQL, int nField, bool bRequireUniqueResult /*= true*/)
{
	wxVariant variant((long)nField);
	return GetSingleResultBool(strSQL, &variant, bRequireUniqueResult);
}

bool ibDatabaseLayer::GetSingleResultBool(const wxString& strSQL, const wxString& strField, bool bRequireUniqueResult /*= true*/)
{
	wxVariant variant(strField);
	return GetSingleResultBool(strSQL, &variant, bRequireUniqueResult);
}

bool ibDatabaseLayer::GetSingleResultBool(const wxString& strSQL, const wxVariant* field, bool bRequireUniqueResult /*= true*/)
{
	bool valueRetrievedFlag = false;
	bool value = false;

	ibDatabaseResultSet* pResult = nullptr;
	try {
		pResult = ExecuteQuery(strSQL);

		while (pResult->Next())
		{
			if (valueRetrievedFlag)
			{
				// Close the result set, reset the value and throw an exception
				CloseResultSet(pResult);
				pResult = nullptr;
				value = false;
				SetErrorCode(DATABASE_LAYER_NON_UNIQUE_RESULTSET);
				SetErrorMessage(wxT("A non-unique result was returned."));
				ThrowDatabaseException();
				return value;
			}
			else
			{
				if (field->IsType(wxT("string")))
					value = pResult->GetResultBool(field->GetString());
				else
					value = pResult->GetResultBool(field->GetLong());
				valueRetrievedFlag = true;

				// If the user isn't concerned about returning a unique result,
				//  then just exit after the first record is found
				if (!bRequireUniqueResult)
					break;
			}
		}

		if (pResult != nullptr)
		{
			CloseResultSet(pResult);
			pResult = nullptr;
		}

		// Make sure that a value was retrieved from the database
		if (!valueRetrievedFlag)
		{
			value = false;
			SetErrorCode(DATABASE_LAYER_NO_ROWS_FOUND);
			SetErrorMessage(wxT("No result was returned."));
			ThrowDatabaseException();
			return value;
		}
	}
	catch (const ibBackendException&) {
		// Close any still-open result set before propagating; preserves the
		// in-flight exception (sqlstate / native_code on derived types).
		if (pResult != nullptr) {
			CloseResultSet(pResult);
			pResult = nullptr;
		}
		throw;
	}

	return value;
}

wxDateTime ibDatabaseLayer::GetSingleResultDate(const wxString& strSQL, int nField, bool bRequireUniqueResult /*= true*/)
{
	wxVariant variant((long)nField);
	return GetSingleResultDate(strSQL, &variant, bRequireUniqueResult);
}

wxDateTime ibDatabaseLayer::GetSingleResultDate(const wxString& strSQL, const wxString& strField, bool bRequireUniqueResult /*= true*/)
{
	wxVariant variant(strField);
	return GetSingleResultDate(strSQL, &variant, bRequireUniqueResult);
}

wxDateTime ibDatabaseLayer::GetSingleResultDate(const wxString& strSQL, const wxVariant* field, bool bRequireUniqueResult /*= true*/)
{
	bool valueRetrievedFlag = false;
	wxDateTime value = wxDefaultDateTime;

	ibDatabaseResultSet* pResult = nullptr;
	try {
		pResult = ExecuteQuery(strSQL);

		while (pResult->Next())
		{
			if (valueRetrievedFlag)
			{
				// Close the result set, reset the value and throw an exception
				CloseResultSet(pResult);
				pResult = nullptr;
				value = wxDefaultDateTime;
				SetErrorCode(DATABASE_LAYER_NON_UNIQUE_RESULTSET);
				SetErrorMessage(wxT("A non-unique result was returned."));
				ThrowDatabaseException();
				return value;
			}
			else
			{
				if (field->IsType(wxT("string")))
					value = pResult->GetResultDate(field->GetString());
				else
					value = pResult->GetResultDate(field->GetLong());
				valueRetrievedFlag = true;

				// If the user isn't concerned about returning a unique result,
				//  then just exit after the first record is found
				if (!bRequireUniqueResult)
					break;
			}
		}

		if (pResult != nullptr)
		{
			CloseResultSet(pResult);
			pResult = nullptr;
		}

		// Make sure that a value was retrieved from the database
		if (!valueRetrievedFlag)
		{
			value = wxDefaultDateTime;
			SetErrorCode(DATABASE_LAYER_NO_ROWS_FOUND);
			SetErrorMessage(wxT("No result was returned."));
			ThrowDatabaseException();
			return value;
		}
	}
	catch (const ibBackendException&) {
		// Close any still-open result set before propagating; preserves the
		// in-flight exception (sqlstate / native_code on derived types).
		if (pResult != nullptr) {
			CloseResultSet(pResult);
			pResult = nullptr;
		}
		throw;
	}

	return value;
}

void* ibDatabaseLayer::GetSingleResultBlob(const wxString& strSQL, int nField, wxMemoryBuffer& buffer, bool bRequireUniqueResult /*= true*/)
{
	wxVariant variant((long)nField);
	return GetSingleResultBlob(strSQL, &variant, buffer, bRequireUniqueResult);
}

void* ibDatabaseLayer::GetSingleResultBlob(const wxString& strSQL, const wxString& strField, wxMemoryBuffer& buffer, bool bRequireUniqueResult /*= true*/)
{
	wxVariant variant(strField);
	return GetSingleResultBlob(strSQL, &variant, buffer, bRequireUniqueResult);
}

void* ibDatabaseLayer::GetSingleResultBlob(const wxString& strSQL, const wxVariant* field, wxMemoryBuffer& buffer, bool bRequireUniqueResult /*= true*/)
{
	bool valueRetrievedFlag = false;
	void* value = nullptr;

	ibDatabaseResultSet* pResult = nullptr;
	try {
		pResult = ExecuteQuery(strSQL);

		while (pResult->Next())
		{
			if (valueRetrievedFlag)
			{
				// Close the result set, reset the value and throw an exception
				CloseResultSet(pResult);
				pResult = nullptr;
				value = nullptr;
				SetErrorCode(DATABASE_LAYER_NON_UNIQUE_RESULTSET);
				SetErrorMessage(wxT("A non-unique result was returned."));
				ThrowDatabaseException();
				return value;
			}
			else
			{
				if (field->IsType(wxT("string")))
					value = pResult->GetResultBlob(field->GetString(), buffer);
				else
					value = pResult->GetResultBlob(field->GetLong(), buffer);
				valueRetrievedFlag = true;

				// If the user isn't concerned about returning a unique result,
				//  then just exit after the first record is found
				if (!bRequireUniqueResult)
					break;
			}
		}

		if (pResult != nullptr)
		{
			CloseResultSet(pResult);
			pResult = nullptr;
		}

		// Make sure that a value was retrieved from the database
		if (!valueRetrievedFlag)
		{
			value = nullptr;
			SetErrorCode(DATABASE_LAYER_NO_ROWS_FOUND);
			SetErrorMessage(wxT("No result was returned."));
			ThrowDatabaseException();
			return value;
		}
	}
	catch (const ibBackendException&) {
		// Close any still-open result set before propagating; preserves the
		// in-flight exception (sqlstate / native_code on derived types).
		if (pResult != nullptr) {
			CloseResultSet(pResult);
			pResult = nullptr;
		}
		throw;
	}

	return value;
}

double ibDatabaseLayer::GetSingleResultDouble(const wxString& strSQL, int nField, bool bRequireUniqueResult /*= true*/)
{
	wxVariant variant((long)nField);
	return GetSingleResultDouble(strSQL, &variant, bRequireUniqueResult);
}

double ibDatabaseLayer::GetSingleResultDouble(const wxString& strSQL, const wxString& strField, bool bRequireUniqueResult /*= true*/)
{
	wxVariant variant(strField);
	return GetSingleResultDouble(strSQL, &variant, bRequireUniqueResult);
}

double ibDatabaseLayer::GetSingleResultDouble(const wxString& strSQL, const wxVariant* field, bool bRequireUniqueResult /*= true*/)
{
	bool valueRetrievedFlag = false;
	double value = -1;

	ibDatabaseResultSet* pResult = nullptr;
	try {
		pResult = ExecuteQuery(strSQL);

		while (pResult->Next())
		{
			if (valueRetrievedFlag)
			{
				// Close the result set, reset the value and throw an exception
				CloseResultSet(pResult);
				pResult = nullptr;
				value = -1;
				SetErrorCode(DATABASE_LAYER_NON_UNIQUE_RESULTSET);
				SetErrorMessage(wxT("A non-unique result was returned."));
				ThrowDatabaseException();
				return value;
			}
			else
			{
				if (field->IsType(wxT("string")))
					value = pResult->GetResultDouble(field->GetString());
				else
					value = pResult->GetResultDouble(field->GetLong());
				valueRetrievedFlag = true;

				// If the user isn't concerned about returning a unique result,
				//  then just exit after the first record is found
				if (!bRequireUniqueResult)
					break;
			}
		}

		if (pResult != nullptr)
		{
			CloseResultSet(pResult);
			pResult = nullptr;
		}

		// Make sure that a value was retrieved from the database
		if (!valueRetrievedFlag)
		{
			value = -1;
			SetErrorCode(DATABASE_LAYER_NO_ROWS_FOUND);
			SetErrorMessage(wxT("No result was returned."));
			ThrowDatabaseException();
			return value;
		}
	}
	catch (const ibBackendException&) {
		// Close any still-open result set before propagating; preserves the
		// in-flight exception (sqlstate / native_code on derived types).
		if (pResult != nullptr) {
			CloseResultSet(pResult);
			pResult = nullptr;
		}
		throw;
	}

	return value;
}

ibNumber ibDatabaseLayer::GetSingleResultNumber(const wxString& strSQL, int nField, bool bRequireUniqueResult /*= true*/)
{
	wxVariant variant((long)nField);
	return GetSingleResultNumber(strSQL, &variant, bRequireUniqueResult);
}

ibNumber ibDatabaseLayer::GetSingleResultNumber(const wxString& strSQL, const wxString& strField, bool bRequireUniqueResult /*= true*/)
{
	wxVariant variant(strField);
	return GetSingleResultNumber(strSQL, &variant, bRequireUniqueResult);
}

ibNumber ibDatabaseLayer::GetSingleResultNumber(const wxString& strSQL, const wxVariant* field, bool bRequireUniqueResult /*= true*/)
{
	bool valueRetrievedFlag = false;
	ibNumber value = -1;

	ibDatabaseResultSet* pResult = nullptr;
	try {
		pResult = ExecuteQuery(strSQL);

		while (pResult->Next())
		{
			if (valueRetrievedFlag)
			{
				// Close the result set, reset the value and throw an exception
				CloseResultSet(pResult);
				pResult = nullptr;
				value = -1;
				SetErrorCode(DATABASE_LAYER_NON_UNIQUE_RESULTSET);
				SetErrorMessage(wxT("A non-unique result was returned."));
				ThrowDatabaseException();
				return value;
			}
			else
			{
				if (field->IsType(wxT("string")))
					value = pResult->GetResultNumber(field->GetString());
				else
					value = pResult->GetResultNumber(field->GetLong());

				valueRetrievedFlag = true;

				// If the user isn't concerned about returning a unique result,
				//  then just exit after the first record is found
				if (!bRequireUniqueResult)
					break;
			}
		}

		if (pResult != nullptr)
		{
			CloseResultSet(pResult);
			pResult = nullptr;
		}

		// Make sure that a value was retrieved from the database
		if (!valueRetrievedFlag)
		{
			value = -1;
			SetErrorCode(DATABASE_LAYER_NO_ROWS_FOUND);
			SetErrorMessage(wxT("No result was returned."));
			ThrowDatabaseException();
			return value;
		}
	}
	catch (const ibBackendException&) {
		// Close any still-open result set before propagating; preserves the
		// in-flight exception (sqlstate / native_code on derived types).
		if (pResult != nullptr) {
			CloseResultSet(pResult);
			pResult = nullptr;
		}
		throw;
	}

	return value;
}

wxArrayInt ibDatabaseLayer::GetResultsArrayInt(const wxString& strSQL, int nField)
{
	wxVariant variant((long)nField);
	return GetResultsArrayInt(strSQL, &variant);
}

wxArrayInt ibDatabaseLayer::GetResultsArrayInt(const wxString& strSQL, const wxString& strField)
{
	wxVariant variant(strField);
	return GetResultsArrayInt(strSQL, &variant);
}

wxArrayInt ibDatabaseLayer::GetResultsArrayInt(const wxString& strSQL, const wxVariant* field)
{
	wxArrayInt returnArray;

	ibDatabaseResultSet* pResult = nullptr;
	try {
		pResult = ExecuteQuery(strSQL);

		while (pResult->Next())
		{
			if (field->IsType(wxT("string")))
				returnArray.Add(pResult->GetResultInt(field->GetString()));
			else
				returnArray.Add(pResult->GetResultInt(field->GetLong()));
		}

		if (pResult != nullptr)
		{
			CloseResultSet(pResult);
			pResult = nullptr;
		}
	}
	catch (const ibBackendException&) {
		// Close any still-open result set before propagating; preserves the
		// in-flight exception (sqlstate / native_code on derived types).
		if (pResult != nullptr) {
			CloseResultSet(pResult);
			pResult = nullptr;
		}
		throw;
	}

	return returnArray;
}

wxArrayString ibDatabaseLayer::GetResultsArrayString(const wxString& strSQL, int nField)
{
	wxVariant variant((long)nField);
	return GetResultsArrayString(strSQL, &variant);
}

wxArrayString ibDatabaseLayer::GetResultsArrayString(const wxString& strSQL, const wxString& strField)
{
	wxVariant variant(strField);
	return GetResultsArrayString(strSQL, &variant);
}

wxArrayString ibDatabaseLayer::GetResultsArrayString(const wxString& strSQL, const wxVariant* field)
{
	wxArrayString returnArray;

	ibDatabaseResultSet* pResult = nullptr;
	try {
		pResult = ExecuteQuery(strSQL);

		while (pResult->Next())
		{
			if (field->IsType(wxT("string")))
				returnArray.Add(pResult->GetResultString(field->GetString()));
			else
				returnArray.Add(pResult->GetResultString(field->GetLong()));
		}

		if (pResult != nullptr)
		{
			CloseResultSet(pResult);
			pResult = nullptr;
		}
	}
	catch (const ibBackendException&) {
		// Close any still-open result set before propagating; preserves the
		// in-flight exception (sqlstate / native_code on derived types).
		if (pResult != nullptr) {
			CloseResultSet(pResult);
			pResult = nullptr;
		}
		throw;
	}

	return returnArray;
}

wxArrayLong ibDatabaseLayer::GetResultsArrayLong(const wxString& strSQL, int nField)
{
	wxVariant variant((long)nField);
	return GetResultsArrayLong(strSQL, &variant);
}

wxArrayLong ibDatabaseLayer::GetResultsArrayLong(const wxString& strSQL, const wxString& strField)
{
	wxVariant variant(strField);
	return GetResultsArrayLong(strSQL, &variant);
}

wxArrayLong ibDatabaseLayer::GetResultsArrayLong(const wxString& strSQL, const wxVariant* field)
{
	wxArrayLong returnArray;

	ibDatabaseResultSet* pResult = nullptr;
	try {
		pResult = ExecuteQuery(strSQL);

		while (pResult->Next())
		{
			if (field->IsType(wxT("string")))
				returnArray.Add(pResult->GetResultLong(field->GetString()));
			else
				returnArray.Add(pResult->GetResultLong(field->GetLong()));
		}

		if (pResult != nullptr)
		{
			CloseResultSet(pResult);
			pResult = nullptr;
		}
	}
	catch (const ibBackendException&) {
		// Close any still-open result set before propagating; preserves the
		// in-flight exception (sqlstate / native_code on derived types).
		if (pResult != nullptr) {
			CloseResultSet(pResult);
			pResult = nullptr;
		}
		throw;
	}

	return returnArray;
}

#if wxCHECK_VERSION(2, 7, 0)
wxArrayDouble ibDatabaseLayer::GetResultsArrayDouble(const wxString& strSQL, int nField)
{
	wxVariant variant((long)nField);
	return GetResultsArrayDouble(strSQL, &variant);
}

wxArrayDouble ibDatabaseLayer::GetResultsArrayDouble(const wxString& strSQL, const wxString& strField)
{
	wxVariant variant(strField);
	return GetResultsArrayDouble(strSQL, &variant);
}

wxArrayDouble ibDatabaseLayer::GetResultsArrayDouble(const wxString& strSQL, const wxVariant* field)
{
	wxArrayDouble returnArray;

	ibDatabaseResultSet* pResult = nullptr;
	try {
		pResult = ExecuteQuery(strSQL);

		while (pResult->Next())
		{
			if (field->IsType(wxT("string")))
				returnArray.Add(pResult->GetResultDouble(field->GetString()));
			else
				returnArray.Add(pResult->GetResultDouble(field->GetLong()));
		}

		if (pResult != nullptr)
		{
			CloseResultSet(pResult);
			pResult = nullptr;
		}
	}
	catch (const ibBackendException&) {
		// Close any still-open result set before propagating; preserves the
		// in-flight exception (sqlstate / native_code on derived types).
		if (pResult != nullptr) {
			CloseResultSet(pResult);
			pResult = nullptr;
		}
		throw;
	}

	return returnArray;
}
#endif


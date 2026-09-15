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

	// ⭐⭐ COMMIT IS "KEPT" OR AN EXCEPTION — IT NEVER ROLLS BACK ON ITS OWN. A rollback is its owner's
	// act, taken knowingly: the owner ends with Commit inside its try and rolls back in its catch.
	// Committing a transaction an inner level has already rolled back is a mistake, so it is refused
	// here, at ANY depth, and nothing is touched — the transaction stays open for its owner to roll
	// back. The next write after a caught failure therefore fails at once, not an hour later.
	//
	// It used to turn into a rollback and RETURN: a script that caught one refused posting and went on
	// writing was told "committed" over an empty base (2026-09-15, a committing code_run seeding a demo
	// base; a script's own CommitTransaction() answered the same way). Max: *"either success, or an
	// exception if it was rolled back"*.
	if (m_txAborted)
		ibBackendDatabaseException::Throw(ibBackendDatabaseException::Kind::RolledBack,
			_("Cannot commit: this transaction has already been rolled back at an inner level (a write "
			  "that failed and whose error was caught). Nothing of it can be kept - roll it back."));

	if (m_txDepth > 1) {
		--m_txDepth;                 // nested inner commit — count down, defer
		return;
	}

	// Outermost level — resolve to the driver. A REFUSAL LEAVES THE TRANSACTION OPEN, depth and pin
	// included, and travels to the owner, whose catch rolls it back (the same rule as above).
	//
	// ⚠ THE TX IS *NOT* GONE ON A REFUSAL, and Firebird is why this matters: a commit that fails there
	// leaves the transaction ACTIVE and holding every lock it took. This layer once zeroed its depth
	// first, answered IsActiveTransaction() = false while the database went on blocking everyone, and
	// the next apply died as a "deadlock" that named neither the holder nor the fault; it then rolled
	// back here on the owner's behalf, because with the depth at zero the owner could not. Now the
	// depth is kept, so the owner can. Firebird compiles views and triggers AT COMMIT, so a refusal
	// here is not exotic — it is the normal way a bad bundle reports itself.
	DoCommit();

	m_txDepth = 0;
	m_txAborted = false;
	// Release the holder's TX pin LAST: the layer stays alive through whatever other shared_ptr the
	// caller holds (scope, pool entry after drop, etc.), and nothing below this line touches `this`.
	ibConnectionPool::ClearActiveTxConnection(this);
}

void ibDatabaseLayer::RollBack()
{
	// ⭐ A ROLLBACK OF NOTHING IS A MISTAKE, SAID. Max: *"rollback, or an exception if there is nothing
	// to roll back"* — a second rollback, or one after the owner already closed its transaction, means
	// the books disagree somewhere, and a quiet return hid exactly where. Cleanup that may meet an
	// already-closed transaction asks IsActiveTransaction() first, or swallows (a destructor).
	if (m_txDepth == 0)
		ibBackendDatabaseException::Throw(ibBackendDatabaseException::Kind::NoTransaction,
			_("Cannot roll back: no transaction is open"));

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
	// Take the list first, then delete — each result set erases itself from it as it dies, so deleting
	// while walking the member would be erasing from the container being iterated.
	DatabaseResultSetHashSet resultSets;
	resultSets.swap(m_ResultSets);

	for (ibDatabaseResultSet* pResultSet : resultSets)
	{
		ibJournalInfo(wxT("db"),wxT("ResultSet NOT closed and cleaned up by the ibDatabaseLayer dtor"));
		delete pResultSet;
	}
}

void ibDatabaseLayer::CloseStatements()
{
	// ⚠ TAKE THE LIST FIRST, THEN DELETE. Each statement now strikes itself out of m_Statements as it
	// dies (see ~ibPreparedStatement), so deleting while walking the member would be erasing from the
	// container being iterated. Moving it out settles that: what is deleted below belongs to nobody
	// else, and the members' erase finds an empty set.
	DatabaseStatementHashSet statements;
	statements.swap(m_Statements);

	for (ibPreparedStatement* pStatement : statements)
	{
		ibJournalInfo(wxT("db"),wxT("PreparedStatement NOT closed and cleaned up by the DatabaseLayer dtor"));
		delete pStatement;
	}
}

bool ibDatabaseLayer::CloseResultSet(ibDatabaseResultSet*& pResultSet)
{
	if (pResultSet == nullptr)
		return false;

	// ONE ACT NOW, AND NO SEARCH. This used to look in our own list, then ask every prepared statement
	// in turn whether the result set was theirs — a hunt for the answer to "who is keeping this",
	// which the result set has known all along. It knows because whoever registered it said so, and it
	// tells that owner on the way out (~ibDatabaseResultSet). So deleting it takes it out of the right
	// books, whichever those are, and asking around is no longer any part of it.
	wxDELETE(pResultSet);
	return true;
}

bool ibDatabaseLayer::CloseStatement(ibPreparedStatement*& pStatement)
{
	if (pStatement == nullptr)
		return false;

	// ONE ACT NOW: deleting it takes it out of the books, because it does that itself on the way out
	// (~ibPreparedStatement). This used to erase first and delete second, and the two halves were the
	// defect — a statement freed by any other route stayed in the list as a dangling pointer, so
	// "closed" and "released" were different things and only this door did both.
	wxDELETE(pStatement);
	return true;
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


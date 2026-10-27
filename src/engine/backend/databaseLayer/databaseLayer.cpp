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
		// Pin this conn for the whole TX, in its own pool (m_pool — the
		// base the TX runs in): to the calling thread's db_query channel
		// when that is free, else to its own holder. While set, db_query
		// on that channel resolves to this exact conn. A layer no pool
		// knows is not pinned: the TX runs at the driver level and the
		// caller's own scope routes it.
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

void ibDatabaseLayer::CloseResultSets()
{
	// Take the list first, then delete — each result set erases itself from it as it dies, so deleting
	// while walking the member would be erasing from the container being iterated.
	DatabaseResultSetHashSet resultSets;
	resultSets.swap(m_ResultSets);

	for (ibDatabaseResultSet* pResultSet : resultSets)
	{
		ibJournalInfo(ibT("db"),ibT("ResultSet NOT closed and cleaned up by the ibDatabaseLayer dtor"));
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
		ibJournalInfo(ibT("db"),ibT("PreparedStatement NOT closed and cleaned up by the DatabaseLayer dtor"));
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
	delete pResultSet;
	pResultSet = nullptr;
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
	delete pStatement;
	pStatement = nullptr;
	return true;
}

int ibDatabaseLayer::GetSingleResultInt(const ibString& strSQL, int nField, bool bRequireUniqueResult /*= true*/)
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
				SetErrorMessage(ibT("A non-unique result was returned."));
				ThrowDatabaseException();
				return value;
			}
			else
			{
				value = pResult->GetResultInt(nField);
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
			SetErrorMessage(ibT("No result was returned."));
			ThrowDatabaseException();
			return value;
		}
	}
	catch (const ibCoreException&) {
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

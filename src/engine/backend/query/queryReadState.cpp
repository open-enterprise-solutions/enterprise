////////////////////////////////////////////////////////////////////////////
//	Description : the read snapshot — one state of the data for as long as
//	              an answer is being read (queryReadState.h)
////////////////////////////////////////////////////////////////////////////

#include "queryReadState.h"

#include "backend/session/session.h"                 // ibSession::Current / OpenConnectionScope
#include "backend/databaseLayer/connectionScope.h"   // ibConnectionScope::SafeBeginTransaction

std::shared_ptr<ibQueryReadState> ibQueryReadState::Open()
{
	ibSession* const session = ibSession::Current();
	if (session == nullptr)
		return nullptr;   // no session to open one on — see the header

	std::unique_ptr<ibConnectionScope> scope =
		std::make_unique<ibConnectionScope>(session->OpenConnectionScope());

	// Somebody's window is already open and it is wider than this read. Adding a nested one would
	// change nothing about what is visible and would only mean a depth to unwind.
	if ((*scope)->IsActiveTransaction())
		return nullptr;

	ibDbTxOptions oneState;
	oneState.snapshot = true;

	// WRITE MODE, though nothing here means to write. A read is allowed to write on our behalf: a
	// source computed in RAM is promoted into a DB temp table and filled, which is how a subquery or
	// a register slice reaches the server instead of being joined by hand. A read-only transaction
	// refuses that, failing a query that used to work — so the promise is not made.
	oneState.readOnly = false;

	scope->SafeBeginTransaction(oneState);
	return std::shared_ptr<ibQueryReadState>(new ibQueryReadState(std::move(scope)));
}

ibQueryReadState::ibQueryReadState(std::unique_ptr<ibConnectionScope> scope)
	: m_scope(std::move(scope))
{
}

ibQueryReadState::~ibQueryReadState()
{
	if (!m_scope)
		return;

	// A read has nothing to commit, but the transaction has to END — and ending it here rather than
	// leaving it to the scope's rollback retires the statements prepared inside it at a predictable
	// moment. Swallowed because this runs from a destructor: whatever the caller was doing when the
	// last holder went away is the thing worth reporting, and a read that fails to commit has
	// nothing to lose by being rolled back instead.
	try { m_scope->SafeCommitTransaction(); } catch (...) {}
}

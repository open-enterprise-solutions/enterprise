#ifndef __QUERY_READ_STATE_H__
#define __QUERY_READ_STATE_H__

#include "backend/backend.h"

#include <memory>

class ibConnectionScope;

//*******************************************************************************************

// ⭐⭐ ONE ANSWER, ONE STATE OF THE DATA — held for as long as the answer is being read.
//
// What an author writes as ONE query is several statements down here: a join the server would not
// take is stitched from two reads, a subquery is promoted to a temp table and read back, a page is
// fetched at a time, and every printed reference fetches its own row. Under read-committed each of
// those reads whatever has committed by the moment it starts, so documents posted while an answer is
// being drawn land in some parts of it and not others — a total that disagrees with the rows beneath
// it, and nothing in the result to say which half is which.
//
// ⭐ IT IS HELD, NOT SCOPED TO A CALL. That distinction is the whole reason this is an object rather
// than a guard at the top of a function: a query does not FINISH when its execute returns. It hands
// back a live cursor and the caller draws rows from it afterwards — a script iterating a selection, a
// list drawing a page. A transaction ended at the return kills the cursor its own result depends on,
// which is exactly what happened on 2026-08-22: "-504, cursor is not open", on the first row of the
// first query. So the snapshot travels WITH the result and dies when the last holder of it does.
//
// ⭐ IT DEFERS TO A TRANSACTION ALREADY OPEN. Open() returns nothing when one is active — the caller's
// window is wider and already gives the guarantee, and a second one would only nest. That is what
// lets a report say "every query in this build reads one state" simply by opening a transaction
// first: each query then finds it and adds nothing.
//
// ⚠ AND IT IS NOT FREE, which is why it belongs to the answer and not to the screen. Holding one
// state means the server keeps the record versions that state needs. An answer is read and released;
// a list left open and scrolled for minutes must NOT hold one, because between its pages the data
// legitimately moves. What this does NOT add is the transaction itself — a live cursor already keeps
// one open today (with no outer transaction the prepared statement starts and manages its own), so
// what changes is the ISOLATION of a transaction that existed either way.
// ⛔ NOTHING OPENS ONE TODAY — 2026-08-23, and the reason belongs here rather than in a commit nobody
// will find. Opened at the four read doors (composer, the text query, the LINQ pipeline and its
// decorator), it hung the application within minutes. The journal's last lines say how: a list had
// been reading under one of these, and the thread that stopped was WRITING — a delete and an insert
// into sys_bytecode_cache. Only the session heartbeat kept ticking afterwards.
//
// ⚠ THE MISSING PIECE IS WHAT A READ TRANSACTION DOES TO THE WRITES AROUND IT. This asks for a
// write-mode snapshot (it must — a read may materialise a temp table), holds it for the length of an
// answer, and pins the connection to the session for that whole time (ibDatabaseLayer::BeginTransaction
// -> SetActiveTxConnection). Everything incidental that writes on the same session while an answer is
// being drawn — the bytecode cache, the heartbeat — then meets it, and `isc_tpb_wait` waits.
//
// So the consistency argument stands and the placement does not: what needs settling first is which
// writes may happen inside a read, and whether they belong in the same transaction or must be kept
// out of it. Kept here, unopened, because the reasoning is worth more than the code is.
class BACKEND_API ibQueryReadState {
public:
	// The snapshot for a read that is about to start, or null when there is nothing to open: a
	// transaction is already active, or there is no session at all (bring-up, a tool, a test).
	// Callers hold the result and let it go when the answer is finished with; null is not a failure
	// and needs no branch — a null holder simply holds nothing.
	static std::shared_ptr<ibQueryReadState> Open();

	~ibQueryReadState();

	ibQueryReadState(const ibQueryReadState&) = delete;
	ibQueryReadState& operator=(const ibQueryReadState&) = delete;

private:
	explicit ibQueryReadState(std::unique_ptr<ibConnectionScope> scope);

	std::unique_ptr<ibConnectionScope> m_scope;
};

#endif // __QUERY_READ_STATE_H__

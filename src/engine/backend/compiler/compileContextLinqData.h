#ifndef _COMPILE_CONTEXT_LINQ_DATA__H_
#define _COMPILE_CONTEXT_LINQ_DATA__H_

// LINQ block compile-state types. Owned by ibCompileContext through
// `unique_ptr<ibLinqContextData> m_linqData` on the RETURN_BLOCK-kind
// linq scope (see ibCompileCode::CompileLinqExpression). Defined in a
// dedicated header so compileContext.h can hold a complete-type
// unique_ptr without needing an out-of-line destructor.

#include <vector>

#include "backend/backend_core.h"
#include "backend/compiler/byteCode.h"   // ibParamUnit, ibClassID

struct ibLinqBinding {
	enum Origin {
		FromSource,   // from <id> in <expr>           — Phase 1
		FromLet,      // var alias = <expr>             — Phase 1
		FromJoin,     // join <id> in <expr> on ...     — Phase 1.5
		FromGroup,    // group ... by <key> into <id>   — Phase 1.5
	};
	wxString    name;
	Origin      origin = FromSource;
	ibParamUnit valueSlot;       // current iter value (`o`)
	ibParamUnit iterInSlot;      // @in_ — OPER_LET cel for source
	ibParamUnit iterItSlot;      // @it_ — OPER_FOREACH state
	ibClassID   typeHint = 0;
	int         foreachStartIp = -1;
};

// Pending join — deferred-emission record for `join b in T on K1 equals K2`.
//
// A key maps to a BUCKET (an Array of every matching inner row), never to one
// row: an inner table with repeated keys is the ordinary case (orders per
// customer), and a single-value dict either loses rows or refuses the
// duplicate outright — Container::Insert raises on a repeated key, which used
// to kill the whole query at hash-build time. The bucket loop below is the
// fan-out: one outer row joins K inner rows into K result rows, same as the
// `.Join()` method executor (ibValueJoinState) and SQL.
//
// At per-iter lookup site (inside outer foreach body), we emit:
//   1. tmp_isEmpty = !hashSlot         (OPER_NOT)
//   2. OPER_IF tmp_isEmpty, skip_label (fall through on first iter,
//                                       jump on subsequent)
//   3. OPER_GOTO trampoline_label      (placeholder — back-patched
//                                       to trampoline body in
//                                       CompileLinqBlock's tail)
//   4. skip_label:                     (= ip right after the GOTO)
//   5. found = hashSlot.Property(K1, bucketSlot)
//   6. OPER_IF found, past-this-bucket (missIfIp — patched at close:
//                                       the PREVIOUS join's continue,
//                                       or the level NEXT_ITER for
//                                       the first join)
//   7. OPER_LET bucketInSlot = bucketSlot
//   8. OPER_FOREACH b in bucketInSlot  (bucketForeachIp; the rest of
//                                       the body runs once per match)
//
// CompileLinqBlock's close section emits, per join in REVERSE order
// (innermost bucket loop first), NEXT_ITER back to (8) and patches
// (8)'s exhaustion jump past it — so a finished bucket falls into the
// next-outer bucket's NEXT_ITER and finally into the level's own.
//
// At trampoline emit time (after outer NEXT_ITER, before
// foreach m_param4 patch — i.e. OUTSIDE outer body, reachable only
// via absolute-ip GOTO):
//   9. trampoline_label: emit hash build using T_lex/K2_lex replay —
//      per inner row, lookup-or-create the key's bucket Array and
//      Add the row into it (the same emitted shape GROUP BY uses)
//  10. OPER_GOTO skip_label  (return into body after first build)
//
// Lex ranges (m_lexStart..m_lexEnd) are saved by parse-and-discard
// during the join clause: we call GetExpression to advance the
// cursor + record the start/end, then resize m_listCode back to
// drop the discardable emit. Replay sets m_numCurrentCompile = start
// and calls GetExpression again at trampoline emit time — naturally
// advances cursor to end, which we then restore.
struct ibLinqPendingJoin {
	ibParamUnit hashSlot;             // persistent: holds the New("Container")
	ibParamUnit bindSlot;             // persistent: the current matched inner row (bucket iter var)
	ibParamUnit bucketSlot;           // the key's bucket Array — Property's out-param at lookup,
	                                  // and the build temp inside the trampoline
	ibParamUnit bucketInSlot;         // @jbkt_in_* — OPER_LET cell the bucket FOREACH reads
	ibParamUnit bucketItSlot;         // @jbkt_it_* — the bucket FOREACH's iterator state
	int         bucketForeachIp = -1; // the bucket OPER_FOREACH header (close patches m_param4)
	int         missIfIp        = -1; // the join-miss OPER_IF (close patches its target)
	int         recheckIfIp     = -1; // the per-match key equality re-check OPER_IF — drops a row
	                                  // the language's `equals` refuses but the Container's looser
	                                  // key comparator let into the bucket (close patches its
	                                  // target to THIS bucket's continue)
	int         tLexStart    = 0;     // inner-source expression token range
	int         tLexEnd      = 0;
	int         k2LexStart   = 0;     // inner key (K2 references b)
	int         k2LexEnd     = 0;
	int         placeholderGotoIp = 0;  // OPER_GOTO ip to back-patch with trampoline_label
	int         skipLabelIp       = 0;  // return target for trampoline's tail GOTO
	// When T's lex range references an outer (= not-current-level)
	// from-binding identifier, the hash dict built from T@iter-1 is
	// stale for iter-2's T. Compile-side detect (scan tLexStart..End
	// for any identifier matching m_bindings names except the most
	// recent / current-level entry) → mark m_needsReset; per-row
	// reset opcode `hashSlot = empty` before the IsEmpty guard at the
	// lookup emit site. Per-row rebuild defeats the hash amortisation
	// (O(M²) for inner row count M) — acceptable correctness over
	// perf for now; future fix: hoist reset to level-N foreach body
	// entry (= per-outer-iter rebuild, O(M+N)). Constant-T joins
	// (m_needsReset=false) keep one-shot rebuild.
	bool        m_needsReset    = false;
};

// LINQ block compile-state — owned by the LINQ-scope ibCompileContext
// via unique_ptr. Lifetime = LINQ-block compile pass. Slots referenced
// here (m_resultArray, counters) are allocated in the CALLER's context,
// so the slot references survive after this struct destructs.
struct ibLinqContextData {
	std::vector<ibLinqBinding> m_bindings;

	// Result accumulator — every clause emits Add into this slot.
	// Allocated in CALLER's context (not the linq scope) so the slot
	// survives the linq context teardown — CompileDeclaration's
	// OPER_LET copies it into the user variable.
	ibParamUnit m_resultArray;

	// Skip/take counters + constant 1 — also caller's context, same
	// lifetime concern as m_resultArray.
	ibParamUnit m_skipCounter;
	ibParamUnit m_takeCounter;
	ibParamUnit m_constOne;

	// ORDERBY parallel-keys array + per-iteration key slot. m_orderKeys
	// is allocated unconditionally in caller's context; Add into it
	// happens only when m_hasOrderBy is set. After the foreach loop
	// closes, CompileLinqExpression emits resultArray.SortByKeys
	// (orderKeys, descending) to reorder rows by computed keys.
	ibParamUnit m_orderKeys;
	ibParamUnit m_orderByKeySlot;       // slot where current key is recomputed per row
	bool        m_hasOrderBy        = false;
	bool        m_orderByDescending = false;

	// GROUP BY — TRULY linq-scope (one group accumulator per query;
	// terminal at exactly one level). Shared here so the per-row
	// aggregation in CompileLinqBlock (at whichever level group was
	// found) can hand state to CompileLinqExpression's post-block
	// expansion (one-shot, after the outermost foreach exhausts).
	// Per-level CompileLinqBlock locals would emit expansion per
	// inner-foreach exhaust → fires per outer iter → duplicates.
	ibParamUnit m_groupsContainer;
	bool        m_hasGroup = false;

	// Non-terminal `group X by K into <g>`. m_hasGroupInto means the
	// post-block expansion in CompileLinqExpression must open a NEW
	// foreach over m_groupsContainer, bind `m_groupIntoName` to a
	// Structure{Key, Values} per pair, and re-enter CompileLinqBlock
	// for the continuation clauses (where/select/orderby on g)
	// instead of the terminal `__r.Add(Structure(...))` path. The
	// outer CompileLinqBlock leaves the lex cursor positioned at the
	// continuation's first token; the post-block re-enters from
	// there.
	wxString    m_groupIntoName;
	bool        m_hasGroupInto = false;

	// JOIN pendingJoins moved to CompileLinqBlock local var
	// (2026-05-11 PM) — per-level state. Each level's trampolines
	// emit at THAT level's NEXT_ITER (absolute-ip GOTO from level's
	// body to trampoline requires same-level emit scope).

	// Phase 2 lazy-chain parking — empty in Phase 1.
	struct LinqFunction {
		long        funcIndex;
		ibParamUnit valueSlot;
		wxString    purpose;
	};
	std::vector<LinqFunction> m_functions;

	// JOIN pending trampolines moved to CompileLinqBlock local var
	// (2026-05-11 PM) — see comment above re per-level state and
	// nested-from recursion correctness.

	const ibLinqBinding* FindByName(const wxString& name) const {
		for (const auto& b : m_bindings)
			if (stringUtils::CompareString(b.name, name))
				return &b;
		return nullptr;
	}
};

#endif // _COMPILE_CONTEXT_LINQ_DATA__H_

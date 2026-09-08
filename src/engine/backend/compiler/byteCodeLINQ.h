#ifndef _BYTE_CODE_LINQ__H_
#define _BYTE_CODE_LINQ__H_

// ⭐⭐ THE LINQ SECTION OF THE FULL BYTECODE.
//
// One tape, three modes: autocomplete reads the instructions, the runtime executes them, and the
// compiler reads them as a TREE. The third mode needs more than the instructions while it is
// building them — what each name in a query is bound to, which joins are still owed a hash, where
// the answer is being accumulated — and that is what lives here.
//
// It used to hang off the compile CONTEXT — a unique_ptr on the LINQ scope — which made a query's
// data die with a scope rather than with the compilation, and put the type in a header named after
// the context that merely held it. The bytecode is the tree; this is a section of it, and it is not
// reachable below the compiler at all — everything down there is typed on the BASE, `ibByteCode`.
// That conversion to the base IS the slice; the section is cleared at the end of compilation only to
// give the memory back.
//
// ⚠ A SECTION, NOT A HEADER OF ITS OWN: it is included from the middle of byteCode.h, after
// ibParamUnit and before ibByteCode, because it uses the first and is used by the second.
#ifndef __BYTE_CODE_H__
#error "byteCodeLINQ.h is a section of byteCode.h — include byteCode.h instead."
#endif

#include <deque>    // ibByteExtCode::m_listLinq — a nested query must not move the one it is inside
#include <vector>

struct ibLinqBinding {
	enum Origin {
		FromSource,   // from <id> in <expr>
		FromLet,      // var alias = <expr>
		FromJoin,     // join <id> in <expr> on ...
		FromGroup,    // group ... by <key> into <id>
		FromRestrict, // restrict <id> in <expr> — the row being narrowed
	};
	wxString    name;
	Origin      origin = FromSource;
	ibParamUnit valueSlot;       // current iter value (`o`)
	ibParamUnit iterInSlot;      // @in_ — OPER_LET cell for the source
	ibParamUnit iterItSlot;      // @it_ — OPER_FOREACH state

	// Where this binding's loop header sits. It is the ONE number the readers on this side need and
	// cannot work out: the header produces the row's cell, so a walk that starts here resolves the
	// row inside its own query instead of scanning the tape from the end and leaving the frame.
	// IntelliSense uses it twice over — for what the name OFFERS, and, with the header's own exit
	// operand, for the span the name is alive in.
	int         foreachStartIp = -1;

	// 🛑 THERE WAS A `typeHint` HERE AND NOTHING EVER WROTE IT. A declared type that is always zero
	// is worse than no field: a reader takes it for an answer. The question it was meant to answer —
	// what type is this row — is answered by ASKING THE SOURCE for a sample of what it yields
	// (scriptComplete.cpp, the OPER_FOREACH step), which is the same road a `foreach` variable is
	// answered by and cannot go stale.
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

// ONE QUERY, while it is being compiled. Owned by the BYTECODE (ibByteExtCode::m_listLinq) — the
// compile context that is inside this query only points at it, so the entry outlives every scope
// that reads it and the slice at the end of compilation is what ends its life.
//
// Slots referenced here (m_resultArray, the counters) are allocated in the enclosing FUNCTION
// context, not in the LINQ scope: a slot number is only a name within its own frame, and the code
// that reads the answer runs after the LINQ scope is gone.
struct ibLinqQuery {
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

	// ⭐ NO PARALLEL KEYS ARRAY ANY MORE. A row's ordering key travels WITH the row into the one
	// light collection (OPER_LINQ_KEEP), so the two cannot come apart however many rows are dropped
	// between them — which a second Array, grown by its own method call per row, could never
	// guarantee. What remains is the slot the current row's key is computed into before it is kept.
	// ⭐⭐ ONE SLOT PER KEY THE CLAUSE NAMED. `orderby a, b` is how a report is ordered — by
	// warehouse, then by item — and a single slot could not hold the second, so the language
	// refused the comma and the only way round was a second sort in script. Each key gets its own
	// cell, recomputed per row, and they are kept together in clause order (procUnitLINQ.cpp,
	// KeepKey); the comparison walks them until one differs, which is what "then by" means.
	std::vector<ibParamUnit> m_orderByKeySlots;
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

	// 🛑 …AND A SECOND FLAG THAT IS NOT THE SAME QUESTION. `m_hasGroup` is PARSE STATE: it is cleared
	// before the `into` continuation is re-entered, so that the leaf parser stops suppressing SELECT.
	// By the end of the compile it therefore says `false` about a query that plainly grouped — which
	// is right for the parser and a lie to anybody asking what the query IS.
	//
	// This one is the FACT, set when the clause is read and never taken back. It exists because the
	// tree is now asked questions (scriptComplete.h, ibOutlineScriptQueries) and an answer taken from a
	// parser's working state is an answer about the parser.
	bool        m_grouped = false;

	// ⭐⭐ …AND WHICH OF THE TWO THINGS THIS ENTRY IS. A `restrict` is a query — it names a row, joins
	// tables to it and filters — but it has no loop and no projection of its own: it takes a query
	// that already exists and hands back the same query with the joins and the filter folded in.
	// Recorded here so it can be TAKEN APART like any other (Max, 2026-09-08: *"it could build a
	// restrict separately and queries separately, so that you know how to build them"*), and told
	// apart by a field rather than by a reader noticing there are no columns.
	bool        m_restrict = false;

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

	// ⭐⭐ THE COLUMNS THIS QUERY ANSWERS WITH, in the order the projection named them — the same
	// names that become the shape of every row and the columns of the answer table.
	//
	// They are written down HERE, while the compiler knows them, rather than read back out of the
	// instructions afterwards: the tree is what the compiler understood, and this is part of it.
	// What reads them is not the runtime — it never sees this section — but the two readers on this
	// side: IntelliSense, and the door that hands a query's structure to whoever is writing one
	// (scriptComplete.h, ibOutlineScriptQueries).
	std::vector<wxString> m_columns;

	// ⭐⭐ WHERE THE QUERY IS IN THE TEXT — the two character positions that bracket it.
	//
	// Structure alone cannot be written back out: an outline says a query filters, not WHAT it
	// filters by, and a constructor that has to hand a person their own query back would be
	// regenerating text it never had. It does not have to. The compiler read the query FROM
	// somewhere, and that somewhere is two numbers it already holds — the position of the `from`
	// token, and the position it had reached when the query closed.
	//
	// So a caller gets the structure AND the exact text it was read from, side by side: edit the
	// text, ask again, and the structure follows (Max, 2026-09-08: *"so that I could write a query
	// by hand and work with it"*).
	unsigned int m_textFrom = 0;
	unsigned int m_textTo   = 0;

	// ⚠ A JOIN'S PENDING STATE IS PER LEVEL, so it is a local of CompileLinqBlock and not a member
	// here: each level's trampolines are emitted at THAT level's NEXT_ITER, and an absolute-ip GOTO
	// from a level's body into its trampoline has to be emitted in the same scope. Sharing it here
	// would make a nested `from` inside a join read the outer level's pending list.
	//
	// 🛑 AND THERE WAS A `m_functions` HERE — a "lazy-chain parking" list from a phase that never
	// arrived: nothing wrote it and nothing read it, in this file or any other. A member that is
	// always empty is a promise the tree does not keep, and the next reader has to prove it is empty
	// before ignoring it. The chain road parks ONE number (`m_numLinqChainAt`, compileCode.h) and
	// re-reads the verbs from the tokens, which is why there was nothing to park here.

	const ibLinqBinding* FindByName(const wxString& name) const {
		for (const auto& b : m_bindings)
			if (stringUtils::CompareString(b.name, name))
				return &b;
		return nullptr;
	}
};


#endif // _BYTE_CODE_LINQ__H_

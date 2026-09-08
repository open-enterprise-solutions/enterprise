
#ifndef _COMPILE_ERROR_H__
#define _COMPILE_ERROR_H__

enum { //instruction types
	OPER_NOP = 0,
	OPER_ADD,
	OPER_DIV,
	OPER_MULT,
	OPER_SUB,
	OPER_NOT,
	OPER_AND,
	OPER_OR,
	OPER_RET,
	OPER_GOTO,
	OPER_FOR,
	OPER_FOREACH,
	OPER_IN,
	OPER_IF,
	OPER_LET,
	OPER_CONST,
	OPER_CONSTN,//integer constant
	OPER_NEXT,
	OPER_NEXT_ITER,
	OPER_MOD,
	OPER_INVERT,
	OPER_ITER,//?
	OPER_GT,//>
	OPER_EQ,//=
	OPER_LS,//<
	OPER_GE,//>=
	OPER_LE,//<=
	OPER_NE,//<>
	OPER_TRY,
	OPER_RAISE,
	OPER_RAISE_T,
	OPER_FUNC,//29 — function/procedure entry (also serves as FUNC_BEGIN tape marker)
	OPER_ENDFUNC,         // FUNC_END
	OPER_FUNC_PARAM,      // tape declarator: parameter slot in current function frame
	OPER_FUNC_LOCAL,      // tape declarator: named local in current function frame
	OPER_CTX_BEGIN,       // tape declarator: enter block scope ({ ... })
	OPER_CTX_END,         // tape declarator: exit block scope
	OPER_CALL,//function call
	OPER_SET, // setting the parameter as a variable
	OPER_SETREF,//setting the parameter as a variable by reference
	OPER_SETCONST, // setting the parameter as a constant
	OPER_ADDCONS,
	OPER_DIVCONS,
	OPER_MULTCONS,
	OPER_SUBCONS,
	OPER_GTCONS,//>
	OPER_EQCONS,//=
	OPER_LSCONS,//<
	OPER_GECONS,//>=
	OPER_LECONS,//<=
	OPER_NECONS,//<>
	OPER_MODCONS,
	OPER_SET_A,
	OPER_GET_A,
	OPER_ENTER_A,
	OPER_CALL_METHOD,
	OPER_CALL_CLOSURE,    // function call where target has m_needsHeapFrame:
	                      // allocate the callee frame on the heap (shared_ptr-
	                      // managed ibRunContext) so that inner lambdas
	                      // materialised during the call can capture the
	                      // frame via weak_from_this(). Operand layout is
	                      // identical to OPER_CALL; the split avoids a
	                      // per-OPER_CALL FindFunctionByEntry probe + flag
	                      // check on the hot non-closure path.
	OPER_CALL_LINQ,       // universal pipeline method on an iterable receiver
	                      // (Where / Select / OrderBy / GroupBy / Join /
	                      // Skip / Take / Aggregate / ...). Compile detects
	                      // LINQ method names at emit time and chooses this
	                      // opcode instead of OPER_CALL_METHOD; runtime
	                      // dispatches via the virtual DispatchLinqMethod
	                      // on the receiver, reading the enum id directly
	                      // from m_param3.m_numIndex (no FindMethod string
	                      // resolution / const-pool lookup needed). Operand
	                      // layout mirrors OPER_CALL_METHOD with one diff:
	                      //   m_param3.m_numIndex = ibLinqMethod enum value
	                      //                        (NOT a const-string index)
	                      //   m_param3.m_numArray = caller arg count
	OPER_GET_ARRAY,
	OPER_SET_ARRAY,
	OPER_CHECK_ARRAY,
	OPER_SET_ARRAY_SIZE,
	OPER_ENDTRY,
	OPER_SET_TYPE,
	// ⭐ CREATE A VALUE OF A BUILT-IN TYPE — `New Structure(…)`, `New Array`, and every other
	// registered VALUE ctor. Configuration objects are not made this way; they come from their
	// manager (`Catalogs.X.CreateElement()`), which is why the compiler refuses any name that is not
	// an `ibCtorObjectType_object_value`.
	//   m_param1            — the destination
	//   m_param2.m_numIndex — the class NAME in the const pool, for a refusal to be able to name it
	//   m_param2.m_numArray — argument count; the arguments follow as OPER_SET / OPER_SETCONST
	//   m_param3.m_numIndex — ⭐⭐ THE CLASS ID, written at compile time. Not a second saying of the
	//                         name: it is the ANSWER to the name, and the compiler had to work it
	//                         out anyway to refuse an unknown class. Stored as a signed 64 that
	//                         carries a uint64's bits (the same trick `OPER_CALL_LINQ` uses for its
	//                         method enum, above).
	OPER_NEW,
	// Anonymous functions / function-as-value:
	//
	// OPER_LFUNC is the active materialiser for a lambda definition.
	// Operand layout:
	//   m_param1                 — dest slot for the resulting ibValueFunction
	//   m_param2.m_numIndex      — end IP (OPER_ENDLFUNC position; patched
	//                              at compile time after the body is emitted)
	//   m_param3.m_numIndex      — lazy cache of derived ibByteCode*
	//                              (reinterpret-cast through intptr_t).
	//                              0 = not yet built. AOT skips this on
	//                              write so loaded blobs rebuild on first use.
	// On bDelta=false (regular execution): if cache is empty, scan the
	// range [LFUNC+1 .. ENDLFUNC-1] in the parent bytecode, copy body +
	// referenced const-pool entries into a fresh self-contained
	// ibByteCode (m_parent = current bc — bc parent walk reaches root
	// Context bindings: Catalogs / Documents / CommonModules / system
	// functions), stash it in m_param3 and in parent's m_lambdaBcs. Then
	// materialise an ibValueFunction wrapper into the dest slot and jump
	// IP to m_param2.m_numIndex (past OPER_ENDLFUNC). On bDelta=true
	// (module-init skip): walk forward to matching OPER_ENDLFUNC.
	//
	// OPER_ENDLFUNC marks the end of the lambda body in the parent bc.
	// Never reached in normal flow — OPER_LFUNC always jumps past it
	// after materialisation. NOP at runtime; kept as a structural fence
	// for the compile-time scan and for the AOT-resilient first-fire
	// extraction.
	//
	// OPER_CALL_LAMBDA is the dynamic counterpart of OPER_CALL: the call
	// target is an ibValue (must wrap an ibValueFunction) read from a
	// slot in m_param4 instead of a static index in m_param2. Frame push
	// + param bind + jump-to-entry are the same machinery as OPER_CALL,
	// but dispatch goes through fn->m_runtime (the root mm captured at
	// materialise time) on fn->m_byteCode (the derived self-contained
	// lambda bc).
	OPER_LFUNC,
	OPER_ENDLFUNC,
	OPER_CALL_LAMBDA,
	// Bound-variable access by bind-kind — 1:1 with Bind{Context,Scope,Export}Variable.
	// Resolve the bound value LAZILY at access (vs the eager binder pre-flight that
	// pre-fills frame slots and rejects null — the "Required binding not provided"
	// trap), and carry the kind explicitly so interpreter and designer dispatch on it.
	//   EXTERN / CONTEXT — named handle (RegisterRecords / Filter / ThisObject / ...)
	//   SCOPE            — bare member resolved through the scope-provider chain
	// Operand layout mirrors OPER_GET_A / OPER_SET_A (see procUnit dispatch).
	OPER_GET_EXTERN,
	OPER_SET_EXTERN,
	OPER_GET_SCOPE,
	OPER_SET_SCOPE,
	OPER_GET_CONTEXT,
	OPER_SET_CONTEXT,

	// ⭐⭐ THE LOOP OFFERS ITS PREDICATE TO THE SOURCE — the instruction that lets a chain compiled as
	// a loop still reach the database.
	//
	// A chain is a loop now (compileCode.cpp, CompileLinqChain), and a loop over a database
	// source would otherwise stream every row to the client to filter it here — the same answer, by
	// the worst possible route. So before the loop opens, this says: here is the source, here is the
	// stretch of instructions that decides a row, and here is the cell the row arrives in. A source
	// that can run that server-side narrows ITSELF and hands back fewer rows; one that cannot does
	// nothing at all, and the loop filters them as it would have.
	//
	// ⭐ NOTHING IS STORED FOR IT. The tree is READ off the instructions the loop already contains
	// (lambdaQueryAST.h, ibBuildQueryAstFromRange), which is why the runtime's sliced bytecode needs
	// to carry no query tree, no version for one, and no serialisation of one.
	//
	// Operands:
	//   m_param1                            — the source slot, narrowed in place when it can be
	//   m_param2 (m_numIndex .. m_numArray) — the predicate's instructions, first and one-past-last
	//   m_param3                            — the cell the predicate's answer lands in
	//   m_param4                            — the cell the row arrives in
	//
	// m_param2.m_numIndex == 0 means the offer was never completed (no filter in the chain, or the
	// compiler had nothing to say) — the handler does nothing at all.
	OPER_LINQ_NARROW,

	// ⭐⭐ HAVE I SEEN THIS ROW BEFORE — the whole of `Distinct`, in one instruction.
	//
	// The obvious emission is a script Array and `Contains` per row, which is what BOTH roads used to
	// do. It is O(n) PER ROW: a linear walk with an ibValue comparison at every step, so a
	// distinct over ten thousand rows performs fifty million comparisons. It also builds the Array
	// through `New` — a lookup of the class BY NAME (procUnit.cpp, ibValue::CreateObject) — for an
	// object nobody can see or reach.
	//
	// None of that is needed for something INTERNAL. The runtime keeps an ordered set in the slot
	// (ibValue has a total order and no hash, which is why a set and not a table), creates it on
	// first use, and answers in log n. No script type, no name, no user-visible object.
	//
	// Operands:
	//   m_param1 — the answer: TRUE the first time a value is seen, FALSE after
	//   m_param2 — the slot LINQ's own collection lives in, for the life of the loop
	//   m_param3 — the value being asked about
	OPER_LINQ_SEEN,

	// ⭐ KEEP THIS ROW, and the key it will be ordered by. Both go into the same collection, so they
	// stay paired however many rows are dropped between them.
	//   m_param1 — the collection
	//   m_param2 — the row; m_numArray == DEF_VAR_SKIP means "no row here, only a key" (below)
	//   m_param3 — an ordering key; m_numArray == DEF_VAR_SKIP when the chain has no ordering
	//   m_param4.m_numIndex — WHICH key this is, counting from 0
	//
	// ⭐⭐ SEVERAL KEYS ARE SEVERAL KEEPS, NOT A SECOND OPCODE. `orderby a, b` emits one KEEP that
	// carries the row together with key 0, then one KEEP PER FURTHER KEY carrying only that key —
	// the row operand marked DEF_VAR_SKIP so the runtime appends a key to the row it just kept
	// instead of keeping the row twice. The keys of a row therefore arrive in the order they were
	// written, which is the order they decide in, and the sort compares them lexicographically:
	// the second is consulted only where the first is equal.
	//
	// Written this way because the alternative was an operand that holds a LIST, and an instruction
	// whose operand is a list of unbounded length is no longer one step of a tape.
	OPER_LINQ_KEEP,

	// ⭐ WHAT THE CHAIN ANSWERS WITH, once the loop is over — the one place where LINQ's own
	// collection becomes something the language can hold.
	//   m_param1              — the destination
	//   m_param2              — the collection
	//   m_param3.m_numIndex   — 0: an Array of the rows · 1: the first row (empty when there is none)
	//   m_param3.m_numArray   — 1 to put the rows in key order first (descending), 2 ascending
	OPER_LINQ_RESULT,

	// ⭐ GROUPING AND JOINING, in the same collection and with no name anywhere. A bucket is found by
	// its KEY; what this replaces was a script `Container` created BY NAME whose every lookup went
	// through the language's member machinery.
	//   OPER_LINQ_BUCKET      p1 = the collection, p2 = the key, p3 = the row to put under it
	//   OPER_LINQ_BUCKET_GET  p1 = the rows under that key (empty when none), p2 = collection, p3 = key
	OPER_LINQ_BUCKET,
	OPER_LINQ_BUCKET_GET,

	// ⭐⭐ THE PROJECTED ROW — `select { name = expr, … }`, which is where the compiled road last
	// went through the object factory by NAME.
	//
	// It used to emit, per row: `New Structure` (the class resolved by name at run time) and then
	// one `Insert(name, value)` per field — a method resolved by name, arguments loaded through a
	// call frame, and the field name stored inside the object so that every later `row.Field` had
	// to look it up again. All of it to express a shape the compiler had in full while compiling.
	//
	// Here the names are written down ONCE, as a single constant, and become the SHAPE — made in a
	// frame slot by the first row and shared by every row after it. Filling a row is then a store
	// at a position the compiler already knew.
	//
	//   OPER_LINQ_ROW    p1 = the row · p2 = the slot the shape lives in
	//                    p3.m_numIndex = const index of the field names (joined by `\n`)
	//                    p3.m_numArray = how many fields
	//   OPER_LINQ_FIELD  p1 = the row · p2 = the value · p3.m_numIndex = which field, by POSITION
	OPER_LINQ_ROW,
	OPER_LINQ_FIELD,

	OPER_END,
};

// NOTE: the outer parens are load-bearing. Without them `x % TYPE_DELTA1`
// expands to `x % 1 * (OPER_END+1)` == `(x % 1) * N` == 0 (same precedence,
// left-assoc), which silently killed the shortLet peephole below. Additive
// uses (`OPER_ADD + TYPE_DELTAn`) worked regardless; modulo/divide did not.
#define TYPE_DELTA1 (1 * (OPER_END + 1))  // for numeric operations
#define TYPE_DELTA2 (2 * TYPE_DELTA1)		// for string operations
#define TYPE_DELTA3 (3 * TYPE_DELTA1)		// for date operations
#define TYPE_DELTA4 (4 * TYPE_DELTA1)		// for operations with booleans

enum { //token types
	ERRORTYPE = 0,
	DELIMITER,	// single-character delimiters and operators
	IDENTIFIER, // unrecognized identifier (translation stage)
	CONSTANT,	// constant
	KEYWORD,	// contains the keyword number
	ENDPROGRAM, // end of the program module
};

enum { // numbers of keywords (in strict sequence as the values ​​themselves are specified)
	KEY_IF = 0,
	KEY_THEN,
	KEY_ELSE,
	KEY_ELSEIF,
	KEY_ENDIF,
	KEY_FOR,
	KEY_FOREACH,
	KEY_TO,
	KEY_IN,
	KEY_DO,
	KEY_ENDDO,
	KEY_WHILE,
	KEY_GOTO,
	KEY_NOT,
	KEY_AND,
	KEY_OR,
	// `a Mod b` — the second spelling of `%`, beside the operator words it
	// behaves like. Three places have to agree, and two of them are easy to miss:
	// s_listKeyWord[] (translateCode.cpp, same index — a static_assert holds the
	// two lists in lock-step), gs_operPriority (compileCode.cpp — 30, with `*`
	// and `/`), and the WORD-OPERATOR GATE in GetExpression, which names And / Or
	// / Mod explicitly. Without that last one the priority entry is never read
	// and an expression simply stops at the word.
	//
	// gs_operPriority is one 256-entry array indexed by m_numData — a delimiter
	// CHARACTER for `%`, a keyword ID for `Mod` — so the two do share an index
	// space. It is harmless only because that gate filters by lexeme type first;
	// the ids that collide with `'>'` or `'='` are never looked up. Keep the
	// placement here anyway: an id of 16 collides with nothing, and the next
	// person should not have to re-derive why it is safe.
	//
	// Keyword ids never reach the bytecode (the AOT format has none), so
	// inserting mid-list is free apart from that lock-step.
	KEY_MOD,
	KEY_PROCEDURE,
	KEY_ENDPROCEDURE,
	KEY_FUNCTION,
	KEY_ENDFUNCTION,
	// === access modifiers — replaced the old single `Export` ===
	// TRAILING, all three of them, and there is no leading form anywhere: a
	// routine takes the modifier after its signature, a variable after its
	// name. The word "(leading)" stood here and was simply wrong; it had
	// already been copied into the language reference and the syntax helper,
	// which taught `Public Var total;` — a line that does not compile.
	// Pinned by CompilerTest.AccessModifiersAreTrailingForEveryOneOfThem and
	// .AVariableTakesItsModifierAfterTheNameToo.
	KEY_PUBLIC,           // `Public`    — exported / visible everywhere (was `Export`)
	KEY_PRIVATE,          // `Private`   — module-local (default; optional explicit-intent)
	KEY_PROTECTED,        // `Protected` — visible to children (object -> its forms)
	// === memoisation (a SECOND axis, not a fourth access) ===
	// `Cached` combines with an access modifier rather than replacing one —
	// `Private Cached` / `Public Cached` are both well-formed. Access answers
	// WHO SEES the function; this answers WHEN IT IS EVALUATED.
	KEY_CACHED,           // `Cached`    — the result is kept per argument tuple
	KEY_VAL,
	KEY_RETURN,
	KEY_TRY,
	KEY_EXCEPT,
	KEY_ENDTRY,
	KEY_CONTINUE,
	KEY_BREAK,
	KEY_RAISE,
	KEY_VAR,
	KEY_NEW,
	KEY_UNDEFINED,
	KEY_NULL,
	KEY_TRUE,
	KEY_FALSE,
	KEY_DEFINE,
	KEY_UNDEF,
	KEY_IFDEF,
	KEY_IFNDEF,
	KEY_ELSEDEF,
	KEY_ENDIFDEF,
	KEY_REGION,
	KEY_ENDREGION,
	// === LINQ keywords ===
	// Recognised by the compiler in CompileLinqExpression and (for
	// FROM only) at expression-start in GetExpression / statement-start
	// in CompileBlock. KEY_IN reuses the existing keyword above
	// (already used by `For Each o In X`). All registered up-front so
	// IsNextKeyWord(KEY_*) works without identifier-text fallback,
	// and so the code editor's keyword highlighter picks them up the
	// moment translateCode.cpp's s_listKeyWord is updated in lock-step.
	KEY_FROM,           // `from <id> in <expr>`            — block entry
	KEY_WHERE,          // `where <expr>`                    — filter
	KEY_SELECT,         // `select <expr>`                   — projection
	KEY_ORDERBY,        // `orderby <expr> [ascending|descending]`
	KEY_ASCENDING,      // sort modifier (default)
	KEY_DESCENDING,     // sort modifier
	KEY_TAKE,           // `take <n>`                        — limit
	KEY_SKIP,           // `skip <n>`                        — offset
	KEY_DISTINCT,       // `distinct`                        — dedup
	KEY_JOIN,           // `join <id> in <expr> on ...`      — inner join
	KEY_ON,             // `... on <left> equals <right>`    — join key
	KEY_EQUALS,         // join-key matcher (typed-vs '==' alt)
	KEY_GROUP,          // `group <expr> by <key> [into <id>]`
	KEY_BY,             // group-by / orderby key separator
	KEY_INTO,           // `group ... into <id>`             — group binding
	KEY_RESTRICT,       // `restrict <id> in <src> join ... where ...` — access-policy filter
	LastKeyWord
};

#endif
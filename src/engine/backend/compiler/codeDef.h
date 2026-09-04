
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
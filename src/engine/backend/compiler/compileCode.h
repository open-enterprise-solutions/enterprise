#ifndef _COMPILE_CODE_H__
#define _COMPILE_CODE_H__

#include "translateCode.h"
#include "compileContext.h"

// One entry in a module's context-variable map. scopeContext marks a
// transparent container (Manager / EnumManager / SystemManager): its
// methods flatten into the surrounding scope, and its OWN name is NOT an
// identifier in the code editor. Default false — a bound value's name is
// visible (ThisObject / ThisForm / export vars).
struct ibContextVar {
	ibValue* m_value = nullptr;
	bool m_scopeContext = false;
};

//*******************************************************************
//*							  Class: compiler                       *
//*******************************************************************

class BACKEND_API ibCompileCode : public ibTranslateCode {

	struct ibCallFunction {

		wxString m_strName;			// name of called function
		wxString m_strRealName;		// name of called function

		ibParamUnit m_puRetValue;	// variable where the result of the function execution should be returned
		ibParamUnit m_puContextVal;  // pointer to the Context variable

		unsigned int m_numAddLine = 0;	// position in the bytecode array where the call was encountered (for the case where there is a call, but the function has not yet been declared)
		unsigned int m_numError = 0;	// to display error messages

		unsigned int m_numString = 0;	// source text number (for error output)
		unsigned int m_numLine = 0;		// source line number (for breakpoints)

		int m_numIsSet = 0;			// a sign that there is no assignment

		wxString m_strModuleName;	// module name (since it is possible to include connections from different modules)

		std::vector<ibParamUnit> m_listParam; // list of passed parameters (list of variables, if the value is not specified, then d.b. (-1, -1))
	};

	friend class ibProcUnit;

public:

	ibCompileCode();
	ibCompileCode(const wxString& strModuleName, const wxString& strDocPath, bool onlyFunction = false);
	ibCompileCode(const wxString& strFileName);

	virtual ~ibCompileCode();

	// basic methods:
	void Reset(); // resetting data to reuse an object	
	void ResetAndFree() { Reset(); ClearLexem(); } // resetting and free data to reuse an object

	void PrepareModuleData();

	void AddVariable(const wxString& strName, const ibValue& value);	// support for external variables
	void AddVariable(const wxString& strName, ibValue* pValue);		// support for external variables

	void AddContextVariable(const wxString& strName, const ibValue& value, bool scopeContext = false);
	void AddContextVariable(const wxString& strName, ibValue* pValue, bool scopeContext = false);

	void AddLocalVariable(const wxString& strName, ibValue* pValue);	// bound local — binder-filled frame slot

	void RemoveVariable(const wxString& strName);

	void SetParent(ibCompileCode* parent); // setting the parent module and prohibited max. ancestor

	// Base ibCompileCode has no compile-side parent — only the
	// ibCompileModule subclass holds a typed `m_parentModule` for
	// compile-orchestration cascade (parent-recompile in Designer
	// mode, parent bytecode wire-up on Reset). Subclass call sites
	// reach `m_parentModule` directly; no virtual on base needed.
	//
	// This shape replaces the bytecode → ibCompileCode back-pointer
	// (`m_compileModule`): parent chain lives entirely on the
	// ibCompileModule side, bytecode is self-contained — foundation
	// for AOT cache + production-without-source.

	virtual bool Recompile(); // recompiling a module from a meta object

	virtual bool Compile(); // compiling a module from a meta object
	virtual bool Compile(const wxString& strCode); // Compiling module

	// Eval host function (the function frame eval expression is opening
	// into). Non-eval compile modules return nullptr; ibCompileEval
	// (defined in procUnit.cpp) overrides to return the host fn pointer
	// captured at ctor time. compileContext.cpp's bc walk reads this to
	// expose host's params + locals to eval expressions (e.g. watch on
	// a function parameter `x`).
	virtual const ibByteCode::ibByteFunction* GetEvalHostFunction() const { return nullptr; }

public:

	// InitializeCompileModule() lived here — every ctor called it to fill the
	// operator-priority table behind a sentinel check. The table is a constant,
	// so it now builds itself once at load time; see compileCode.cpp.
	static void SetCodeStyle(short codeStyle);
	static short GetCodeStyle();

	ibCompileContext* GetContext() const { return m_rootContext; }

	// Wire one bytecode dependency on this module's bytecode. Forward
	// to ibByteCode::AddDependency, which atomically pushes pointer
	// + id + version snapshot. Preferred entry point at compile-time
	// call sites (CompileExpression eval setup, future common-module
	// import wiring) — keeps callers from reaching into m_cByteCode
	// directly.
	void AddDependency(ibByteCode* dep) {
		m_cByteCode.AddDependency(dep);
	}

	// Build a runtime binding session from this compile-code's
	// extern / context value maps. Forwards to bytecode's CreateBinder
	// for the empty session, then fills via SetVar() — name → live
	// ibValue* — so the result is ready for procUnit->Execute().
	// AOT path bypasses this entirely (manager fills binder directly
	// from its own registry, no compile-context staging).
	ibByteBinder CreateBinder(bool delta = true) {
		ibByteBinder br = m_cByteCode.CreateBinder(delta);
		for (auto& kv : m_listExternValue)  br.SetVar(kv.first, kv.second);
		for (auto& kv : m_listContextValue) br.SetVar(kv.first, kv.second.m_value);
		for (auto& kv : m_listLocalValue)   br.SetVar(kv.first, kv.second);
		return br;
	}


public:

	ibParamUnit GetExpression(ibCompileContext* context, int priority = 0);

	//attributes:
	bool m_onlyFunction; // true - only functions and export functions

	// ⭐⭐ THE COMPILER'S FORM, and this declaration is the whole of what makes it one. `ibByteExtCode`
	// IS an `ibByteCode` plus the tree (byteCode.h): everyone downstream — the runtime, the AOT
	// cache, the binder — takes the base type and therefore cannot reach the tree at all, while the
	// compiler, which builds it, is the only one who names it.
	ibByteExtCode		m_cByteCode;        // output array of bytecodes for execution by the virtual machine

	ibCompileContext* m_rootContext; // root context 

	// Cursor over m_listLexem — "before the first token" is -1, and the
	// INITIALISER is the only thing that says so on the eval path. A module
	// compile is set up by CompileModule(), which opens by parking the cursor;
	// ibProcUnit::CompileExpression (watch / Evaluate / Execute) never calls it
	// — it goes PrepareLexem → GetExpression directly. With the field left
	// uninitialised that walk started from heap garbage, so the first GETLexem
	// read past the end of the token array and every watch expression, `4`
	// included, came back as "Module code expected".
	int				m_numCurrentCompile = wxNOT_FOUND;	// current position in the token array
	bool			m_changedCode;

	// Compile-mode predicate. Default false (regular module compile).
	// ibCompileEval (procUnit.cpp) overrides to return true so the
	// "no new function declarations / no GOTO" parser gates fire for
	// eval / watch expressions. Replaces the legacy m_bExpressionOnly
	// field — kept off the base class since it's true for exactly one
	// derived class.
	virtual bool IsExpressionOnly() const { return false; }

	// ⭐⭐ WHAT THIS COMPILE IS FOR — and there are two answers, not two compilers.
	//
	// Runtime is the demanding one: the first refusal ends the compile, because the artefact is
	// going to be EXECUTED and half of it is worth nothing. It raises, and the raise is what ends
	// it.
	//
	// Tolerant is the same compiler READING. It is answering "what is at this caret" for an editor,
	// and a reader has nobody to tell and nowhere to stop: it says nothing at all and never
	// unwinds. That is not a weaker demand, it is a different act — and it is exactly how the
	// precompiler this replaces has always worked (one SetError and zero throws in 2900 lines),
	// which is what lets somebody edit at line one thousand of a module broken at line three.
	//
	// ⚠ SILENCE IS THE POINT, NOT A SIDE EFFECT. A raise leaves the loop that has to keep going, so
	// a compile that both REPORTS and CONTINUES is a contradiction on this road (Max, 2026-09-07:
	// *"any publication is an exception — an exit from the loop, and you need to continue it"*). A
	// reader that genuinely wants the messages accumulated gives the compile somewhere to write
	// them; it does not ask the refusal to carry them.
	enum class ibCompileMode {
		Runtime = 0,   // the first refusal ends the compile, and is reported
		Tolerant,      // a refusal is not reported and ends nothing; the read goes on
	};

	void SetCompileMode(ibCompileMode mode) { m_compileMode = mode; }
	ibCompileMode GetCompileMode() const { return m_compileMode; }

	// ⭐⭐ WHAT THIS COMPILE REFUSED, AS TEXT, WHETHER OR NOT ANYTHING WAS RAISED. A refusal used to
	// be an exception and carried its own sentence to whoever caught it; a tolerant compile raises
	// nothing, so those sentences would simply be gone — and "why did that dot not resolve" is
	// exactly the question they answer (Max, 2026-09-07: *"since it used to be an exception, you
	// now have to log it somewhere — a logger on the compiler's side, a string variable"*).
	//
	// One line per refusal, in the order they happened. A runtime compile stops at the first, so
	// its log holds one line; a tolerant compile reads on and collects them all.
	const wxString& GetRefusal() const { return m_strRefusal; }

	// Compile-time block-scope nesting depth. ++ on `{` (RETURN_BLOCK
	// CompileBlock entry), -- on matching `}`. Stamped into each var
	// at PushVariable time as ibVariable::m_scopeDepth → mirrors into
	// ibByteCodeVarInfo::m_scopeDepth → SendLocalVariables filter at
	// runtime. Function / lambda body envelopes don't push.
	int m_compileScopeDepth = 0;

	ibCompileMode m_compileMode = ibCompileMode::Runtime;   // see SetCompileMode

	// See GetRefusal. Mutable because DoSetError is const — reporting a refusal does not change
	// what the compile IS, and the const there predates this by years.
	mutable wxString m_strRefusal;

	// See SetCaret. -1 = nobody asked, and then NoteCaret does nothing at all.
	long m_caretPos = -1;
	long m_caretOwner = kCaretNowhere;

	// The caret fell inside [spanStart, here] — so `owner` is the declaration holding it. The
	// INNERMOST one wins: a body finishes before the body enclosing it, so the first claim is the
	// tightest, and an enclosing span must not take it back.
	void NoteCaret(long spanStart, long owner) {
		if (m_caretPos < 0 || m_caretOwner != kCaretNowhere)
			return;
		if (m_caretPos >= spanStart && m_caretPos <= CaretCursor())
			m_caretOwner = owner;
	}

	// ⭐ THE STREAM IS EXHAUSTED — a different fact from "what you wrote is wrong", and the
	// difference only matters in a tolerant compile. A malformed name is the text's fault; a name
	// that is not there yet is the text being UNFINISHED, which is the ordinary state of a module
	// somebody is typing into. The lexer marks the end with ENDPROGRAM, so this asks it rather than
	// counting.
	bool IsEndOfProgram() const {
		const int next = m_numCurrentCompile + 1;
		return next >= (int)m_listLexem.size() || m_listLexem[next].m_lexType == ENDPROGRAM;
	}

	// Where the parse has read up TO, in the text — the END of the token just consumed, not its
	// start. The difference is one token wide and it decides a span: a body closed by `}` at 41
	// holds every caret up to 42, and 42 is exactly where a caret sits when the text was cut at it,
	// which is how the value door compiles (scriptComplete.cpp, upToCaret). Measured 2026-09-07:
	// comparing against the START lost that caret, the declaration went unclaimed, and the name
	// search then found a slot of the same number in a table one floor up.
	long CaretCursor() const {
		return (m_numCurrentCompile >= 0 && m_numCurrentCompile < (int)m_listLexem.size())
			? (long)m_listLexem[m_numCurrentCompile].EndPos() : 0;
	}

	// ⭐⭐ THE DECLARATION A CARET IS STANDING IN, CAUGHT WHILE COMPILING — not read back out of the
	// tape afterwards. A body owns the span from where it began to the token that closed it, and
	// the only moment BOTH ends are in hand is the end of that body: the parser is standing on the
	// closer, and where it started was noted on the way in. So the answer is taken there.
	//
	// EACH SPAN IS THE ONE ITS OWNER ACTUALLY HOLDS, which is what keeps two of them from claiming
	// the same caret: a declaration's span is its body, and the MODULE's span starts after the
	// declarations rather than at the top of the file. An inner body finishes first and is recorded
	// first; the outer one no longer contains the caret and does not overwrite it.
	//
	// ⚠ THIS IS THE MECHANISM THE PRECOMPILER USED, and it is here because the alternative was
	// worse: reading the answer back off the tape means the tape has to carry it, and the position
	// on a closing instruction was never a boundary — it is what AddLineInfo stamped, the token
	// AFTER the body, which for the last declaration in a module is the end of the text. Every
	// patch to make that field mean something else is a patch to a field somebody else owns.
	void SetCaret(long position) { m_caretPos = position; m_caretOwner = kCaretNowhere; }

	// The entry IP of the declaration the caret stands in; kCaretAtModule when it stands in the
	// module body, kCaretNowhere when no caret was set or the text never reached it.
	long GetCaretOwner() const { return m_caretOwner; }

	static const long kCaretNowhere = -2;
	static const long kCaretAtModule = -1;

	// matching external variables
	std::map<wxString, ibValue*> m_listExternValue;

	// matching context variables
	std::map<wxString, ibContextVar> m_listContextValue;

	// bound LOCAL variables — registered as plain frame locals (kind=Local, NOT
	// External/Context), the binder fills the slot at init; the module body reads/
	// writes them as ordinary locals (e.g. a constant's Value). No required/type
	// pre-flight, no kind opcode — a normal slot.
	std::map<wxString, ibValue*> m_listLocalValue;

protected:

	virtual void DoSetError(int codeError,
		const wxString& strFileName, const wxString& strModuleName, const wxString& strDocPath,
		unsigned int currPos, unsigned int currLine,
		const wxString& strErrorDesc) const;

	const ibLexem& PreviewGetLexem();
	const ibLexem& GetLexem();
	const ibLexem& GETLexem();

	void GETDelimeter(const wxUniChar& c);

	bool IsDelimeter(const wxUniChar& c);
	bool IsKeyWord(int nKey);

	bool IsNextDelimeter(const wxUniChar& c);
	bool IsNextKeyWord(int nKey);

	void GETKeyWord(int nKey);

	// strRealName — return source-case string from m_valData (via GetString())
	//   (otherwise the uppercase m_strData is returned).
	// acceptKeyword — accept a KEYWORD lexem as if it were an identifier;
	//   used in property-access positions (`obj.<X>`) where contextual
	//   LINQ keywords (`Where`/`Select`/...) clash with valid method
	//   names. Returns m_strData for keywords (their lexer-emitted form).
	wxString GETIdentifier(bool strRealName = false, bool acceptKeyword = false);
	ibValue GETConstant();

	void AddLineInfo(ibByteUnit& code);

	// ⭐⭐ THE FIVE VERBS THIS COMPILER NAMES ITS METHODS WITH, and what each one PROMISES. Written
	// down because it had drifted twice: a `Compile…` that emitted nothing, and an `Emit…` beside a
	// `Compile…` doing the same job one level apart. A reader who cannot tell them apart has to open
	// each one to find out what it does to the cursor and to the tape.
	//
	// The distinction is NOT "does it read tokens" — all but the last do. It is WHO DECIDED, and
	// WHAT COMES BACK:
	//
	//   Compile<X>  the construct is RECOGNISED HERE, at the cursor, from the stream. This function
	//               owns the decision that this is an X, consumes it, and puts it on the tape.
	//               CompileIf · CompileForeach · CompileLinqExpression · CompileLambdaExpression.
	//
	//   Emit<X>     the CALLER already decided there is an X and where it is; this lays its
	//               instructions down. It may still read the tokens — the decision moved, not the
	//               reading — and it may answer with the cell the value landed in.
	//               EmitFunctionBody · EmitLambdaBody · EmitRestrictBody · EmitLinqChainClauses.
	//
	//   Get<X>      the EXPRESSION GRAMMAR: the product is an OPERAND for the caller to use.
	//               GetExpression · GetCurrentIdentifier · GetCallFunction · GETIdentifier.
	//
	//   Try<X>      a GATE: looks at what follows, and true means THIS ROAD TOOK IT — the tokens are
	//               consumed and the caller is done. Nothing is put on the tape here; the road that
	//               claimed it emits later. The name is the tree's own for this contract
	//               (ibValueQueryable::TryJoinThroughL3, TryFoldTotalsInDbms).
	//               TryFoldLinqChainSource.
	//
	//   Find<X>     a LOOK-UP that answers with a position or a handle and moves NOTHING — not the
	//               cursor, not the tape. It may be asked before a thing is read, which is usually
	//               the only moment its answer is of any use. The word is the tree's own for this
	//               (ibByteCode::FindVariable / FindFunctionByEntry).
	//               FindConst · FindForeachHeaderEnd · (file-static) FindLinqChain.
	//
	//   Parse<X>    reads and fills structures, and puts NOTHING on the tape.
	//               ParseFunctionSignature.
	//
	//   Add<X>      one instruction, from data already in hand. Reads no tokens.
	//               AddTypeSet · AddLineInfo.
	//
	// ⚠ The test is mechanical and worth applying to a new name: does the body contain a single
	// `m_listCode.emplace_back`? Every `Compile…` in this class does — the one that did not was
	// misnamed, and it is now the `Try…` above.

	bool CompileModule();
	bool CompileFunction(ibCompileContext* context);

	// Helpers split out of CompileFunction's body — pure mechanical
	// extraction for reuse from anonymous-lambda expression compile.
	// Behaviour preserved exactly: CompileFunction is now a thin
	// orchestrator that calls these in sequence with the dedup +
	// m_listFunction registration in between.
	//
	// Anonymous form is detected from the token stream itself: keyword
	// followed by `(` → anonymous (synthetic `__lambda_<guid>` name);
	// keyword followed by an identifier → named declaration. No
	// external gate needed — context (next token) is the single
	// source of truth.
	bool ParseFunctionSignature(ibCompileContext* context,
		std::shared_ptr<ibCompileContext::ibFunction>& outFunction,
		std::unique_ptr<ibCompileContext>& outFunctionContext,
		int& outErrorPlace);
	// Discriminator (named vs lambda) is read from
	// functionContext->m_numReturn — RETURN_LAMBDA_FUNCTION /
	// RETURN_LAMBDA_PROCEDURE for anonymous bodies, RETURN_FUNCTION /
	// RETURN_PROCEDURE for named — stamped by ParseFunctionSignature
	// based on the token stream (anonymous form: keyword followed by
	// `(`). Picks body fences accordingly: OPER_FUNC/OPER_ENDFUNC for
	// named, OPER_LFUNC/OPER_ENDLFUNC for lambda. The two pairs are
	// intentionally distinct opcodes so the module-init skip-through
	// over a named function body — which matches only OPER_ENDFUNC —
	// doesn't terminate prematurely on a nested lambda's terminator.
	// Lambdas additionally skip the m_listFunc push (their identity
	// lives entirely in OPER_LFUNC's operands).
	// bareExprBody = the body is a single `Return <expr>` (a `restrict` clause) instead of a
	// `{ … }` / VES block. Default = a normal block.
	bool EmitFunctionBody(ibCompileContext* context,
		const std::shared_ptr<ibCompileContext::ibFunction>& createdFunction,
		ibCompileContext* functionContext, bool bareExprBody = false);

	// Compiles an anonymous Function/Procedure expression. Parses the
	// signature into a context whose parent is nullptr (lambda body
	// sees only own params/locals + root-level Context bindings reached
	// via bc parent walk; closures rejected).
	// Emits OPER_LFUNC + body + OPER_ENDLFUNC inline, then back-patches
	// OPER_LFUNC's operands with the dest slot (allocated in caller's
	// context) and the OPER_ENDLFUNC IP. Returns the dest slot — runtime
	// OPER_LFUNC materialises an ibValueFunction into it on first
	// fire (and then jumps past the body); subsequent re-execution of
	// the same definition site re-fires the same materialisation.
	ibParamUnit CompileLambdaExpression(ibCompileContext* context);

	// LINQ — block expression triggered by KEY_FROM. GetExpression's
	// KEYWORD switch catches `KEY_FROM` (mirroring KEY_NEW) and calls
	// CompileLinqExpression which emits an inline foreach + array-build
	// materialising the result into a temp slot. The surrounding
	// assignment / declaration machinery copies the result via standard
	// OPER_LET. KEY_FROM also handled at statement-level in CompileBlock
	// for consistency with KEY_NEW.
	//
	// Multi-from (`from a in X from b in Y ...`) is implemented through
	// recursive descent: CompileLinqExpression allocates the shared
	// result array + counters, then enters CompileLinqBlock which emits
	// one foreach. If the next token is KEY_FROM, it recursively calls
	// itself inside the loop body (= nested foreach). Otherwise it
	// processes the tail clauses (where/skip/take/select) at the
	// deepest level. On unwind each level emits its own OPER_NEXT_ITER
	// + back-patches.
	//
	// The query being compiled lives in the BYTECODE (ibByteExtCode::m_listLinq); the LINQ scope
	// only names it through ibCompileContext::m_linqQuery, so CompileLinqBlock reads it from the
	// context it is given — no extra parameter needed.
	ibParamUnit CompileLinqExpression(ibCompileContext* context);

	// Access-policy restriction (RLS query patch) —
	//     restrict <s> in <source> [ join <a> in <T> on <lk> <op> <rk> ]* [ where <cond> ]
	// Folds each join / where INTO the <source> query via the decorator's Join / Where
	// (OPER_CALL_LINQ → ibValueQueryDecorator::DispatchLinqMethod → the database), returning the
	// patched source — nothing is materialised. Entered on its own `restrict` keyword, by
	// analogy with KEY_FROM -> CompileLinqExpression.
	ibParamUnit CompileRestrictExpression(ibCompileContext* context);
	// Emit a field lambda `Function(<param> [, <param2>]){ Return <expr>; }` for the clause at the
	// cursor and record its L4 pushdown AST from the same span. One param = a where predicate
	// (`s => cond`); a non-empty param2 = a join ON predicate (`(s, a) => s.k <op> a.k`).
	ibParamUnit EmitRestrictBody(ibCompileContext* context,
	                             const wxString& paramName, const wxString& paramName2);

	// CompileLinqBlock — outer entry: parses `<id> in <expr>` from
	// source, emits OPER_LET + OPER_FOREACH, dives into the leaf
	// (or recurses for nested-from). The `preBound` overload lets
	// the post-block group-into expansion re-enter the leaf parsing
	// machinery with a synthesized binding: caller pre-emits the
	// OPER_LET / OPER_FOREACH, populates the binding fully (incl.
	// foreachStartIp), and we skip the from-clause parse + slot
	// allocation at the top. Body / leaf / NEXT_ITER / back-patches
	// are shared between the two entries.
	void CompileLinqBlock(ibCompileContext* linqCtx);
	void CompileLinqBlock(ibCompileContext* linqCtx, const ibLinqBinding& preBound);

	// ⭐⭐ THE CHAIN SYNTAX, COMPILED THE WAY THE BLOCK SYNTAX ALREADY IS — as a LOOP.
	//
	// `src.Where(Function(x){ return P; }).Count()` and `from x in src where P` are the same
	// question, and until now they compiled to two different things: the block to
	// OPER_FOREACH + OPER_IF + OPER_NEXT_ITER — ordinary instructions with ordinary branching —
	// and the chain to a row of OPER_CALL_LINQ that builds C++ state objects and calls a lambda
	// VALUE once per row, paying a frame, an argument binding and a virtual step for each one
	// (measured: 1.31x a loop written by hand). The loop was never missing; the chain simply did
	// not take it.
	//
	// So this is not a new machine — it is the same one, entered from the other syntax. The
	// lambda's parameter becomes the loop's binding: an ordinary local of the enclosing frame,
	// chosen by the compiler. Nothing is captured, because there is nothing to capture from; no
	// frame is built, because the body is not a call.
	//
	// Called with the receiver already in hand and BEFORE the dot is consumed. Answers false for
	// anything outside the slice it is sure of — a lambda that is not written inline, a body that
	// is not one `return <expr>`, a verb it does not compile, a chain that does not end in a
	// terminal — and the caller then emits the ordinary OPER_CALL_LINQ, which is always correct.
	bool CompileLinqChain(ibCompileContext* context, const ibParamUnit& receiver,
	                               ibParamUnit& outResult);

	// Everything a chain IS lives in the lexemes and, once emitted, in the instructions. The reading
	// of it — link spans, whether the lambdas are written here, whether it may leave the door — is
	// file-static in compileCodeLINQ.cpp, because none of it is state and none of it is anyone
	// else's business. Declaring it here would put a struct and four signatures into a header the
	// whole tree includes, for a thing that lives for one statement — and it is also what let the
	// LINQ half move to a file of its own without opening anything.

	// Called from the postfix walker with the receiver in hand and BEFORE the dot is consumed.
	// True = the links are parked and the chain's tokens are consumed, so the caller keeps the
	// RECEIVER as the value of the expression; the verbs are emitted later, inside the loop.
	bool TryFoldLinqChainSource(ibCompileContext* context, const ibParamUnit& receiver);

	// Emit the parked verbs into the body of a foreach that has just been opened, and answer with
	// the `Where` jumps that still need the address of the next iteration. `rowSlot` is the loop's
	// own variable — a `Select` writes the projection straight back into it, which is what makes
	// the body see the projected value under the name the person wrote.
	void EmitLinqChainClauses(ibCompileContext* context, const ibParamUnit& rowSlot,
	                           std::vector<int>& outSkipIps);

	// The body of a lambda that is becoming somebody else's instructions. Cursor just before the
	// body's `{`; leaves it on the matching `}`; answers with the cell the body's value is in. One
	// `return <expr>` compiles to exactly what it always did; anything wider goes through
	// CompileBlock with a return-capture (ibReturnCapture, compileContext.h). `outSingleReturn`
	// reports which — a filter is only offered to its source when the body is one expression.
	ibParamUnit EmitLambdaBody(ibCompileContext* context, bool* outSingleReturn = nullptr);
	// JOIN lookup emit — KEY_JOIN already consumed by caller. Parses
	// `b in T on K1 equals K2`, binds b, emits per-iter lookup + a
	// placeholder OPER_GOTO to the (yet-unemitted) trampoline, pushes
	// an ibLinqPendingJoin into the caller-owned `pendingJoins` vector.
	// Trampoline is emitted after THIS level's NEXT_ITER by the
	// matching CompileLinqBlock call. Local-per-level vector (not
	// stored on ibLinqQuery) so nested from-levels don't share
	// pending-trampoline state — each level emits trampolines for its
	// own joins only.
	void CompileLinqJoin(ibCompileContext* linqCtx,
	                     std::vector<ibLinqPendingJoin>& pendingJoins);

	bool CompileDeclaration(ibCompileContext* context);
	bool CompileBlock(ibCompileContext* context);
	bool CompileNewObject(ibCompileContext* context);
	bool CompileGoto(ibCompileContext* context);
	bool CompileIf(ibCompileContext* context);
	bool CompileWhile(ibCompileContext* context);
	bool CompileFor(ibCompileContext* context);
	bool CompileForeach(ibCompileContext* context);
	bool CompileException(ibCompileContext* context);

	ibParamUnit GetCallFunction(ibCompileContext* context, const wxString& strName, const int& nIsSet);
	ibParamUnit GetCurrentIdentifier(ibCompileContext* context, int& nIsSet);

	ibParamUnit FindConst(const ibValue& constData);

	bool PushCallFunction(const std::shared_ptr<ibCallFunction>& function);
	bool GetFunction(const wxString& strName, std::shared_ptr<ibCompileContext::ibFunction>& function, int* pNumFunction = nullptr);

	// Look ahead for a type name in a DECLARATION position without consuming it.
	// Handles the one-lexem form ("Boolean flag") and the dotted metadata form
	// ("CatalogRef.Item value"), and requires an identifier after the type — so
	// extending this beyond the five primitives cannot change what existing code
	// means.
	bool IsTypeVar(const wxString& sVariable = wxEmptyString);
	ibClassID GetTypeVar(const wxString& sVariable = wxEmptyString);
	void AddTypeSet(const ibParamUnit& sVariable);

	const int GetConstString(const wxString& strConstName);

	std::map<wxString, unsigned int> m_listHashConst;
	std::vector<std::shared_ptr<ibCallFunction>> m_listCallFunc;	// list of encountered procedure and function calls

private:

	// ⭐ ARMED FOR EXACTLY ONE SOURCE READ, AND DISARMED ON EVERY EXIT — a raise out of the source
	// expression included. It used to be two bare assignments around the read, and a refusal between
	// them left the compiler armed: `Reset()` does not clear compile-time scratch, so a REUSED
	// compiler (Recompile, the designer rebuilding a module) would claim the first chain it met
	// anywhere in the next module — consuming its verbs from the token stream and emitting none of
	// them, which is a `Where` that silently disappears.
	//
	// The house already answers this shape the same way (ibBackendException::ibEvalModeScope): a
	// thing that must be true for exactly one act is a scope, not a pair of assignments.
	class ibLinqSourceScope {
	public:
		ibLinqSourceScope(ibCompileCode* owner, int closerAt)
			: m_owner(owner) {
			m_owner->m_numLinqSourceEnd = closerAt;
			m_owner->m_numLinqChainAt   = -1;
		}
		~ibLinqSourceScope() { m_owner->m_numLinqSourceEnd = -1; }

		ibLinqSourceScope(const ibLinqSourceScope&) = delete;
		ibLinqSourceScope& operator=(const ibLinqSourceScope&) = delete;
	private:
		ibCompileCode* m_owner;
	};

	// The token that closes a `foreach` header, scanned from `at` (the `In` keyword) with brackets
	// balanced: the first `)` at depth zero in the braces dialect, the first `Do` at depth zero in
	// the other. wxNOT_FOUND when there is none — a malformed header, which simply means no chain is
	// folded and the ordinary parser reports what is wrong with it.
	int FindForeachHeaderEnd(int at) const;

	// ⭐⭐ WHERE A QUERY'S TEXT ENDS — the START of the first token the query did NOT read, not the
	// end of the last one it did. The two differ by the whitespace between them, and that gap is
	// exactly where a person stands while typing the next clause: `restrict s in Source where |;`
	// puts the caret one character past the end of `where`, and a span measured to the token's end
	// dropped it out of the query it is plainly inside — the alias `s` then went unoffered
	// (measured 2026-09-08, caret battery). Everything up to the next token belongs to the
	// construct that was being written; there is nothing else it could belong to.
	//
	// The end of what was READ when there is no next token — a query at the very end of the text.
	unsigned int FindQueryTextEnd() const;

	// methods for displaying errors during compilation::
	void SetError(int nErr, const wxString& strError = wxEmptyString);
	void SetError(int nErr, const wxUniChar& c);

	wxString m_strCurFuncName;//name of the current compiled function (for processing the recursive function call option)

	// ⭐⭐ `foreach (r in src.Where(λ))` — THE SAME FOLD, INTO A LOOP THAT ALREADY EXISTS.
	//
	// A chain with no terminal cannot own a loop, and it does not need to: `foreach` is emitting one
	// anyway. So the verbs are folded INTO its body — the filter becomes an `OPER_IF` skipping to
	// the next iteration, the projection an assignment over the loop variable — and the state
	// objects, the per-row lambda call and its frame all go, exactly as in the terminal road.
	//
	// It is the shape most RAM code is written in, and since the gate now also admits a database
	// source whose predicate cannot travel, it is the shape that pays most there too.
	//
	// Two halves, because the chain is READ before the loop exists and COMPILED after it — and what
	// travels between them is ONE NUMBER: the lexeme the chain starts at. The verbs are read again
	// from there when the body is emitted; re-reading costs nothing (it is a scan over lexemes
	// already in memory) and it means no structure, no vector and no second representation of the
	// program is kept anywhere. -1 = nothing parked.
	// ⭐⭐ WHICH HEADER'S SOURCE IS BEING READ — AS A POSITION, NOT AS A FLAG, and that is the whole of
	// the difference. A bool could only say "a source is being read", and the fold then had to guess
	// whether the chain it found was THE source or something nested inside it. The guess was the
	// closing token: what follows a source is `)` in the braces dialect and `Do` in the other — true,
	// and not enough. In `foreach (r in f(a.Where(g)))` the token after `Where(g)` is also a `)`, the
	// one that closes `f(`, so the chain was claimed, `a` went to `f` unfiltered and the filter was
	// applied to the ROWS OF `f(a)`. A different program, compiled silently.
	//
	// The position cannot be mistaken: the header's closer is found once, by a balanced scan, and a
	// chain is the source only when it ends exactly there. The dialect branch goes with it — one
	// comparison says the same thing for both.
	//
	// -1 means no source is being read, so the same field arms the fold and identifies it.
	int m_numLinqSourceEnd = -1;

	// …and where the chain that WAS claimed begins. Two halves, because the chain is READ before the
	// loop exists and COMPILED after it — and what travels between them is ONE NUMBER: the lexeme the
	// chain starts at. The verbs are read again from there when the body is emitted; re-reading costs
	// nothing (a scan over lexemes already in memory) and it means no structure, no vector and no
	// second representation of the program is kept anywhere. -1 = nothing parked.
	int m_numLinqChainAt = -1;

	friend struct ibCompileContext;
};

#endif 
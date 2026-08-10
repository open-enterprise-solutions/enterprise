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

	// current context of variables, functions and labels
	ibByteCode		m_cByteCode;        // output array of bytecodes for execution by the virtual machine

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

	// Compile-time block-scope nesting depth. ++ on `{` (RETURN_BLOCK
	// CompileBlock entry), -- on matching `}`. Stamped into each var
	// at PushVariable time as ibVariable::m_scopeDepth → mirrors into
	// ibByteCodeVarInfo::m_scopeDepth → SendLocalVariables filter at
	// runtime. Function / lambda body envelopes don't push.
	int m_compileScopeDepth = 0;

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
	// LINQ block compile state lives on ibCompileContext::m_linqData
	// (non-owning pointer at a stack-allocated struct owned by
	// CompileLinqExpression). CompileLinqBlock reads from the linq
	// context's m_linqData — no extra parameter needed.
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
	// JOIN lookup emit — KEY_JOIN already consumed by caller. Parses
	// `b in T on K1 equals K2`, binds b, emits per-iter lookup + a
	// placeholder OPER_GOTO to the (yet-unemitted) trampoline, pushes
	// an ibLinqPendingJoin into the caller-owned `pendingJoins` vector.
	// Trampoline is emitted after THIS level's NEXT_ITER by the
	// matching CompileLinqBlock call. Local-per-level vector (not
	// stored on ibLinqContextData) so nested from-levels don't share
	// pending-trampoline state — each level emits trampolines for its
	// own joins only.
	void CompileLinqJoin(ibCompileContext* linqCtx,
	                     std::vector<int>& whereSkipIps,
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

	// methods for displaying errors during compilation::
	void SetError(int nErr, const wxString& strError = wxEmptyString);
	void SetError(int nErr, const wxUniChar& c);

	wxString m_strCurFuncName;//name of the current compiled function (for processing the recursive function call option)

	friend struct ibCompileContext;
};

#endif 
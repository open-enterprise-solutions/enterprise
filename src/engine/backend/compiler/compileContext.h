#ifndef _COMPILE_CONTEXT__H_
#define _COMPILE_CONTEXT__H_

#include <memory>

#include "backend/compiler/byteCode.h"
#include "backend/compiler/compileContextLinqData.h"

class BACKEND_API ibCompileCode;

//function properties — RETURN_* tags the kind of compile-context, used
//both for `Return` statement validation and for closure-capture /
//parent-walk discipline (anonymous bodies stop the parent search).
//Function/Procedure axis is encoded directly in the enum so callers can
//derive m_bCodeRet from m_numReturn without a side flag — no separate
//"keyword returns value" local needed at signature parse time.
enum {
	RETURN_NONE = 0,           //no return — module-level (NOP for `Return`)
	RETURN_PROCEDURE,          //named procedure body (returns nothing)
	RETURN_FUNCTION,           //named function body (returns a value)
	RETURN_LAMBDA_PROCEDURE,   //anonymous Procedure(...) body
	RETURN_LAMBDA_FUNCTION,    //anonymous Function(...) body
	RETURN_BLOCK,              //block-scope (`{ }` in CES, control-structure body)
};

// True when a context is any lambda boundary — Phase B compile
// discipline stops parent-context walks here so lambda bodies can't
// see outer-function locals.
inline bool IsReturnLambda(short numReturn) {
	return numReturn == RETURN_LAMBDA_FUNCTION
	    || numReturn == RETURN_LAMBDA_PROCEDURE;
}

// True when a context body is expected to produce a return value
// (Function form, named or anonymous). Used to gate `Return` syntax
// and to stamp ibFunction::m_bCodeRet at compile finalize.
inline bool IsReturnFunction(short numReturn) {
	return numReturn == RETURN_FUNCTION
	    || numReturn == RETURN_LAMBDA_FUNCTION;
}

//variable flags (specified with a negative value in the nArray attribute of the bytecode)
enum {
	DEF_VAR_SKIP = -1,// missing parameter
	DEF_VAR_DEFAULT = -2,//default parameter
	DEF_VAR_TEMP = -3,//flag of a temporary local variable
	DEF_VAR_NORET = -7,//function (procedure) does not return values
	DEF_VAR_CONST = 1000,//loading constants
};

enum {
	CODE_VES = ibProgramSyntax::syntax_ves,
	CODE_CES = ibProgramSyntax::syntax_ces
};

struct ibCompileContext {

#pragma region __context_unit_h__

	//variable definition
	struct ibVariable
	{
		ibVariable() : m_bExport(false), m_bContext(false), m_bExternal(false), m_bTempVar(false), m_bScoped(false), m_numVariable(0), m_clsid(0) {}
		ibVariable(const wxString& strVariableName) : m_bExport(false), m_bContext(false), m_bExternal(false), m_bTempVar(false), m_bScoped(false), m_numVariable(0), m_clsid(0), m_strName(strVariableName) {}

		// Construct from bytecode-side info — used by FindVariable's
		// bytecode fallback so eval scopes (no parent compile-context
		// chain) still produce a transient ibVariable for the caller's
		// emission path.
		ibVariable(const wxString& strVariableName, const ibByteCode::ibByteCodeVarInfo& info)
			// m_bExport on compile-side = "cross-bc visible". For
			// synth-from-bc entries that's any non-private kind:
			// Export / External / Context / ContextProp. Plain
			// kind=Local entries are private — never reach this ctor
			// (FindVariable filters them).
			: m_bExport(!info.IsLocal()),
			  m_bContext(info.IsContext() || info.IsContextProp()),
			  m_bExternal(info.IsExternal()),
			  // Temps are filtered out at bc-mirror sites — synth from
			  // bc-info is never a temp.
			  m_bTempVar(false),
			  m_bScoped(info.m_bScoped),
			  m_scopeDepth(info.m_scopeDepth),
			  m_numVariable(info.m_slotIndex),
			  m_clsid(info.m_clsid),
			  m_strName(strVariableName),
			  m_strRealName(info.m_strRealName.IsEmpty() ? strVariableName : info.m_strRealName),
			  m_strContext(info.m_strContext)
		{
		}

		bool m_bExport;
		bool m_bContext;
		// Set in PrepareModuleData Pass 1 for entries declared via
		// AddExternalValue. Distinct from m_bContext: externs are bound
		// by the binder but expose no helper props; contexts (self-ref)
		// expose props/methods through PrepareNames. Drives kind=External
		// on the bc mirror so the binder treats them as required-to-bind
		// alongside Context entries.
		bool m_bExternal;
		bool m_bTempVar;
		// Scope-local marker (e.g. ThisObject / ThisForm) — invisible
		// to children through cross-bc resolution. Mirrored to
		// ibByteCode::ibByteCodeVarInfo::m_bScoped at compile finalize.
		bool m_bScoped;
		// Nesting depth of the OPER_CTX_BEGIN stack at declaration site.
		// 0 = fn-frame / module-body. Stamped at PushVariable time from
		// m_compileModule->m_activeScopes.size(). Copied into bc-side
		// ibByteCodeVarInfo::m_scopeDepth at compile finalize; runtime
		// compares against ibRunContext::m_currentScopeDepth to gate
		// debugger Locals visibility.
		int m_scopeDepth = 0;
		unsigned int m_numVariable;
		// Target class id for External / Context entries — used by the
		// runtime pre-flight to verify the bound ibValue matches the
		// declared type. Stamped in PrepareModuleData from the live
		// extern / context value's GetClassType(). 0 for plain Locals
		// (no static type).
		ibClassID m_clsid;
		wxString m_strName; // Variable name
		wxString m_strType; // Value type
		wxString m_strRealName; // Real variable name
		wxString m_strContext; //name of the context variable

		// "Is this a context-related entry?" — bare context binding
		// (Manager / ThisForm, m_bContext=true with empty m_strContext)
		// or a Pass-3 prop of a binding (Catalogs of Manager, m_strContext
		// set). Used by the identifier-path emitter to decide between
		// OPER_GET (bare binding → frame slot) and OPER_GET_A (prop on
		// parent var) — see compileCode.cpp's isContextProp gate.
		bool IsContextRelated() const {
			return m_bContext || !m_strContext.IsEmpty();
		}
	};

	//function definition
	struct ibFunction
	{
		struct ibParamVariable
		{
			ibParamVariable() : m_bByRef(false) {
				m_puValue.m_numArray = -1;
				m_puValue.m_numIndex = -1;
			}

			// Construct from bytecode-side ibByteParam + the param's
			// real-cased name (stored separately on ibByteFunction).
			ibParamVariable(const wxString& strParamName, const ibByteCode::ibByteParam& bp)
				: m_bByRef(bp.m_bByRef),
				  m_strName(strParamName),
				  m_puValue(bp.m_defaultValue)
			{
			}

			bool m_bByRef;
			wxString m_strName; // Variable name
			wxString m_strType; // Value type
			ibParamUnit m_puValue; // Default value
		};

		ibFunction(const wxString& strFuncName, ibCompileContext* compileContext = nullptr) :
			m_bExport(false),
			m_bContext(false),
			m_strName(strFuncName),
			m_lVarCount(0), m_nStart(0), m_nFinish(0), m_numLine(0)
		{
			// Wire the back-pointer (functionContext->m_functionContext = this)
			// so compile-time scope walks know the active function.
			// Lifetime of the compile-context is owned by the caller
			// (CompileFunction's local unique_ptr) — ibFunction no
			// longer holds or deletes it.
			if (compileContext != nullptr)
				compileContext->m_functionContext = this;
		}

		// Construct from bytecode-side ibByteFunction — used by
		// FindFunction's bytecode fallback. No compile-context to
		// wire (eval / synthesized path); back-pointer stays null.
		ibFunction(const wxString& strFuncName, const ibByteCode::ibByteFunction& fn)
			: m_bExport(fn.IsExport() || fn.IsContextMethod()),
			  m_bContext(fn.IsContextMethod()),
			  m_strRealName(fn.m_strRealName.IsEmpty() ? strFuncName : fn.m_strRealName),
			  m_strName(strFuncName),
			  m_strContext(fn.m_strContext),
			  m_bCodeRet(fn.m_bCodeRet),
			  // Same class of bug as the context-method m_bCodeRet gap:
			  // a default-false flag the reconstruction path must restore.
			  // A cross-module call to an exported function whose body has
			  // an inner lambda capturing locals needs OPER_CALL_CLOSURE;
			  // dropping this here would emit a plain OPER_CALL and dangle
			  // the capture (compileCode.cpp:1165 reads m_needsHeapFrame).
			  m_needsHeapFrame(fn.m_needsHeapFrame),
			  m_lVarCount(fn.m_lVarCount),
			  m_nStart(fn.m_lCodeLine), m_nFinish(0), m_numLine(0)
		{
			m_listParam.reserve(fn.m_listParam.size());
			for (size_t i = 0; i < fn.m_listParam.size(); i++) {
				const wxString& realName = (i < fn.m_listParamRealName.size())
					? fn.m_listParamRealName[i]
					: wxString();
				m_listParam.emplace_back(realName, fn.m_listParam[i]);
			}
		}

		~ibFunction() = default;

		bool m_bExport, m_bContext;

		// Mirror of bytecode-side m_bCodeRet — true for FUNCTION (returns
		// a value), false for PROCEDURE. Settled at CompileFunction
		// finalize. Used by PushCallFunction to gate "called as function
		// without LHS" (only PROCEDURE allowed). Replaces the legacy
		// m_compileContext->m_numReturn == RETURN_FUNCTION check, so the
		// synth path in GetFunction (cross-module bytecode-resolved
		// function) no longer needs a stub ibCompileContext just to
		// carry the return-kind.
		bool m_bCodeRet = false;

		// Closure capture — set lazily by GetVariable when an
		// identifier resolves into this function's m_listVariable
		// past a crossed lambda boundary. PushCallFunction reads it
		// directly to choose OPER_CALL vs OPER_CALL_CLOSURE (heap-frame
		// variant). Mirrored to ibByteFunction::m_needsHeapFrame at
		// EmitFunctionBody finalize via the templated ctor; the bc-
		// side mirror is what OPER_CALL_LAMBDA reads at runtime to
		// decide heap-promotion for a dynamically-called lambda
		// whose body has its own inner-lambda capture chain.
		bool m_needsHeapFrame = false;

		wxString m_strRealName; //Function name
		wxString m_strName; //Function name in uppercase
		wxString m_strType; //type (in English notation), if it is a typed function
		wxString m_strContext; //name of the context variable

		unsigned int m_lVarCount;// number of local variables
		unsigned int m_nStart;// starting position in bytecode array
		unsigned int m_nFinish;//final position in bytecode array

		//for IntelliSense
		unsigned int m_numLine; //source line number (for breakpoints)

		wxString m_strShortDescription;//includes the entire line after the Function (Procedure) keyword
		wxString m_strLongDescription;//includes the entire merged (i.e. without empty lines) comment block before the function (procedure) definition

		std::vector<ibParamVariable> m_listParam;

		// "Is this a context-related entry?" — context-method (bound
		// to a binding's helper, m_bContext=true with m_strContext set)
		// or any function with a parent context. Used by the call-path
		// emitter to decide OPER_CALL vs OPER_CALL_METHOD; mirrors
		// ibVariable::IsContextRelated().
		bool IsContextRelated() const {
			return m_bContext || !m_strContext.IsEmpty();
		}
	};

	//label definition
	struct ibLabel
	{
		int		 m_numLine = 0;
		int		 m_numError = 0;
		wxString m_strName;
	};

#pragma endregion

	ibCompileContext(ibCompileCode* compileCode) :
		m_compileModule(compileCode), m_parentContext(nullptr), m_functionContext(nullptr),
		m_numTempVar(0), m_numFindLocalInParent(1), m_numReturn(0), m_numDoNumber(0) {
	}

	ibCompileContext(ibCompileContext* compileContext) :
		m_compileModule(nullptr), m_parentContext(compileContext), m_functionContext(nullptr),
		m_numTempVar(0), m_numFindLocalInParent(1), m_numReturn(0), m_numDoNumber(0) {
	}

	~ibCompileContext() {}

	//Create new context 
	ibCompileContext* CreateContext(short numReturn)
	{
		ibCompileContext* compileContext = new ibCompileContext(this);

		compileContext->m_numReturn = numReturn;
		compileContext->m_compileModule = m_compileModule;

		return compileContext;
	}

	//Setting jump addresses for Continue and Break commands
	void StartLoopList() {

		//create lists for Continue and Break commands (they will store the addresses of byte codes where the corresponding commands were encountered)
		m_numDoNumber++;
		m_listContinue[m_numDoNumber] = new std::vector<int>();
		m_listBreak[m_numDoNumber] = new std::vector<int>();
	}

	//Setting jump addresses for Continue and Break commands
	void FinishLoopList(ibByteCode& cByteCode, int gotoContinue, int gotoBreak) {
		std::vector<int>* pListC = m_listContinue[m_numDoNumber];
		std::vector<int>* pListB = m_listBreak[m_numDoNumber];
		if (pListC == 0 || pListB == 0) {
#ifdef DEBUG 
			wxLogDebug(wxT("Error (FinishLoopList) gotoContinue=%d, gotoBreak=%d\n"), gotoContinue, gotoBreak);
			wxLogDebug(wxT("m_numDoNumber=%d\n"), m_numDoNumber);
#endif 
			m_numDoNumber--;
			return;
		}
		for (unsigned int i = 0; i < pListC->size(); i++) {
			cByteCode.m_listCode[*pListC[i].data()].m_param1.m_numIndex = gotoContinue;
		}
		for (unsigned int i = 0; i < pListB->size(); i++) {
			cByteCode.m_listCode[*pListB[i].data()].m_param1.m_numIndex = gotoBreak;
		}
		m_listContinue.erase(m_numDoNumber);
		m_listContinue.erase(m_numDoNumber);
		delete pListC;
		delete pListB;
		m_numDoNumber--;
	}

	void CreateLabels();

	ibParamUnit CreateVariable(const wxString& strPrefix = wxT("@temp_"));
	ibParamUnit AddVariable(const wxString& strVarName, const wxString& strType = wxEmptyString, bool bExport = false, bool bContext = false, bool bTempVar = false);
	ibParamUnit GetVariable(const wxString& strVarName, bool bFindInParent = true, bool bCheckError = false, bool bContext = false, bool bTempVar = false);

	void PushVariable(const wxString& strVarName, const wxString& strContextVar, unsigned int numVariable,
		const wxString& typeVar = wxEmptyString, bool exportVar = true, bool contextVar = true, bool tempVar = false);
	void PushFunction(const wxString& strFuncName, const wxString& strContextVar, const wxString& strShortDescription, unsigned int numFunction,
		bool hasRetVal = true, int argCount = 0);

	bool FindVariable(const wxString& strVarName, std::shared_ptr<ibVariable>& foundedVar, bool context = false);
	bool FindFunction(const wxString& strFuncName, std::shared_ptr<ibFunction>& foundedFunc, bool context = false);

	//Reset compile context
	void Reset() {

		m_numDoNumber = 0;
		m_numReturn = 0;
		m_numTempVar = 0;

		m_numFindLocalInParent = 1;

		m_listContinue.clear();
		m_listBreak.clear();

		m_listLabel.clear();
		m_listLabelDef.clear();

		// clear functions & variables 
		m_listVariable.clear();
		m_listFunction.clear();
	}

	ibCompileCode* m_compileModule;
	ibCompileContext* m_parentContext; //parent context

	//current context 
	ibFunction* m_functionContext;

	//VARIABLES
	std::map<wxString, std::shared_ptr<ibVariable>> m_listVariable;

	int m_numTempVar;//current temporary variable number
	int m_numFindLocalInParent;//flag for searching variables in the parent (one level up), in other cases only export variables are searched in parents)

	//FUNCTIONS AND PROCEDURES
	std::map<wxString, std::shared_ptr<ibFunction>> m_listFunction; //list of encountered function definitions

	short m_numReturn;//RETURN operator processing mode: RETURN_NONE,RETURN_PROCEDURE,RETURN_FUNCTION

	// LINQ — exclusive ownership of LINQ-scope compile state. Non-null
	// only on the RETURN_BLOCK-kind context that CompileLinqExpression
	// allocates for the LINQ scope. Lifetime tied to the context's
	// shared_ptr lifetime. Allocated via std::make_unique in
	// CompileLinqExpression; freed automatically when the context dies.
	std::unique_ptr<ibLinqContextData> m_linqData;

	// LINQ-scope predicate helpers. IsLinq() — this context IS the
	// LINQ scope (carries m_linqData itself). IsInLinq() — this
	// context or any ancestor is a LINQ scope; used by IntelliSense /
	// validation hooks that need to know "are we inside a LINQ block?"
	// without caring which level introduced the scope.
	bool IsLinq() const { return m_linqData != nullptr; }
	bool IsInLinq() const {
		for (const ibCompileContext* c = this; c; c = c->m_parentContext)
			if (c->m_linqData) return true;
		return false;
	}

	//LOOPS
	//Service attributes
	unsigned short m_numDoNumber;//nested loop number

	std::map<unsigned short, std::vector<int>*> m_listContinue;//addresses of Continue operators
	std::map<unsigned short, std::vector<int>*> m_listBreak;//addresses of Break operators

	//LABELS
	std::map<wxString, unsigned int> m_listLabelDef; //declarations
	std::vector<std::shared_ptr<ibLabel>> m_listLabel; //list of encountered transitions to labels
};

#endif
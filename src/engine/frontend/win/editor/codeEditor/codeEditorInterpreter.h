#ifndef __IB_CODE_EDITOR_INTERPRETER_H__
#define __IB_CODE_EDITOR_INTERPRETER_H__

#include "backend/moduleInfo.h"
#include "backend/compiler/compileCode.h"

struct ibParamValue {
	wxString m_paramName;//variable name 
	wxString m_paramType;//variable type in English notation (in case of explicit typing)
	ibValue m_paramObject;
};

class ibPrecompileCode;
struct ibPrecompileFunction;

struct ibPrecompileVariable
{
	bool m_isExport  = false;
	bool m_isContext = false;
	bool m_isTempVar = false;

	int  m_number    = 0;
	int  m_declPos   = 0;  // source-text offset of the declaration; 0 = always
	                       // visible (parameter / context-injected). LoadSys-
	                       // Keyword filters out variables with declPos > caret.

	wxString m_name;     // variable name
	wxString m_type;     // variable type (in english notation, when typed)
	wxString m_realName;

	ibValue m_valContext;
	ibValue m_valObject;

	ibPrecompileVariable() = default;
	explicit ibPrecompileVariable(wxString varName) : m_name(std::move(varName)) {}
};

struct ibPrecompileContext
{
	ibPrecompileCode*    m_module         = nullptr;
	ibPrecompileContext* m_parent         = nullptr;  // enclosing context
	ibPrecompileContext* m_stopParent     = nullptr;  // target for `Break` (enclosing loop)
	ibPrecompileContext* m_continueParent = nullptr;  // target for `Continue` (enclosing loop)

	std::map<wxString, ibPrecompileVariable> m_variables;
	std::map<wxString, ibPrecompileFunction*> m_functions;  // compiled functions/procedures registered in this scope

	int      m_tempVarCounter      = 0;   // running id for synthesised temporary variables
	int      m_findLocalInParent   = 1;   // 1: walk into parent scopes when resolving names; 0: stop at this scope
	int      m_returnKind          = 0;   // last seen RETURN kind: RETURN_NONE / RETURN_PROCEDURE / RETURN_FUNCTION
	wxString m_currentFunctionName;       // name of the currently-compiled function (for recursive-call handling)

	void SetModule(ibPrecompileCode* setModule) { m_module = setModule; }

	ibParamValue GetVariable(const wxString& varName, bool findInParent = true, bool checkError = false, const ibValue& value = ibValue(), int declPos = 0);
	ibParamValue AddVariable(const wxString& varName, const wxString& varType = wxEmptyString, bool isExport = false, bool isTempVar = false, const ibValue& value = ibValue(), int declPos = 0);
	void         SetVariable(const wxString& varName, const ibValue& value);

	// valContext is an out-parameter and so cannot carry a temporary as its default (that binds a
	// non-const reference to a prvalue — an MSVC extension). The name-only forms below, which is
	// how every existence check calls these, keep the resolved context on a local and discard it.
	bool FindVariable(const wxString& name, ibValue& valContext, bool isContext = false);
	bool FindFunction(const wxString& name, ibValue& valContext, bool isContext = false);

	bool FindVariable(const wxString& name) { ibValue valContext; return FindVariable(name, valContext); }
	bool FindFunction(const wxString& name) { ibValue valContext; return FindFunction(name, valContext); }

	void RemoveVariable(const wxString& name);

	explicit ibPrecompileContext(ibPrecompileContext* setParent = nullptr)
		: m_parent(setParent),
		  m_stopParent    (setParent ? setParent->m_stopParent     : nullptr),
		  m_continueParent(setParent ? setParent->m_continueParent : nullptr)
	{
	}

	~ibPrecompileContext();
};

// Compiled-function descriptor — IntelliSense-side mirror of a script
// procedure or function declaration. Owns its private context if any.
struct ibPrecompileFunction
{
	wxString m_realName;                     // function identifier as written
	wxString m_name;                         // upper-cased name (matching key)
	std::vector<ibParamValue> m_params;
	bool m_isExport  = false;
	bool m_isContext = false;
	ibPrecompileContext* m_context = nullptr; // private function context (locals + params); owned

	ibValue m_valContext;

	ibParamValue m_returnValue;              // return-value placeholder for the walker
	wxString m_type;                         // type (in english notation) when typed

	// IntelliSense:
	wxString m_shortDescription;               // short tooltip (one line)

	explicit ibPrecompileFunction(const wxString& funcName, ibPrecompileContext* setContext = nullptr)
		: m_name(funcName), m_context(setContext)
	{
	}

	~ibPrecompileFunction() { delete m_context; }

	ibPrecompileFunction(const ibPrecompileFunction&)            = delete;
	ibPrecompileFunction& operator=(const ibPrecompileFunction&) = delete;
};

//*******************************************************************
//*                       Class: precompiler walker                 *
//*******************************************************************
class ibPrecompileCode : public ibTranslateCode
{
	int m_cursor = wxNOT_FOUND;        // current position in the lexem array

	ibValueMetaObjectModuleBase* m_moduleObject = nullptr;

	std::map<wxString, unsigned int> m_constHashes;

	// Context tree owned by the precompiler:
	//   m_rootContext     — module-level scope (always live; root of the tree).
	//   m_activeContext   — context currently being compiled (during
	//                       CompileFunction it points at a freshly allocated
	//                       child of root for the function's locals; outside
	//                       of CompileFunction it equals &m_rootContext).
	//   m_cursorContext   — context at the user's caret position; used by
	//                       IntelliSense to expose the locals of the
	//                       function the caret is inside.
	ibPrecompileContext  m_rootContext;
	ibPrecompileContext* m_activeContext = nullptr;
	ibPrecompileContext* m_cursorContext = nullptr;

	ibValue m_valObject;

	unsigned int m_lastPosition = 0;

	wxString m_lastExpression;
	wxString m_lastKeyword;
	wxString m_lastParentKeyword;

	bool         m_calcValue = false;
	// User caret position in the buffer (set externally via
	// SetCurrentPos) — distinct from the base class's m_currentPos which
	// is the lexer's running tokenize cursor. The two MUST stay separate;
	// shadowing them silently breaks ibTranslateCode::IsEnd() (it would
	// read the derived caret value instead of the lexer's progress).
	unsigned int m_caretPos = 0;

public:

	// IntelliSense state — last expression / keyword / parent keyword
	// the walk has populated. Read by codeEditorLoader to drive
	// auto-complete suggestions; written by ExpectDelimeter and the
	// expression-walk methods. Read-only accessors keep these private
	// without requiring `friend ibCodeEditor`.
	const wxString& GetLastExpression()    const { return m_lastExpression; }
	const wxString& GetLastKeyword()       const { return m_lastKeyword; }
	const wxString& GetLastParentKeyword() const { return m_lastParentKeyword; }

	// Lexem stream produced by PrepareLexem — read-only view for
	// fold-level scanning and intellisense walkers.
	const std::vector<ibLexem>& GetLexems() const { return m_listLexem; }

	// Cursor + computation-mode setters — driven by codeEditorLoader to
	// pin the IntelliSense walk to the user's cursor position and to
	// signal whether ibValue computation should run (debugger watch
	// path) or be skipped (autocomplete preview).
	void SetCurrentPos(unsigned int pos) { m_caretPos = pos; }
	void SetCalcValue (bool value)        { m_calcValue  = value; }

	// Cleanup for reuse:
	virtual void Clear();// resets state for repeated translation calls
	void PrepareModuleData();

	ibPrecompileCode(ibValueMetaObjectModuleBase* moduleObject);
	virtual ~ibPrecompileCode();

	ibValue GetComputeValue() const { return m_valObject; }

	// Pure getters. The root context's back-pointer to this module is
	// wired once in the constructor — no longer mutated on every read.
	ibPrecompileContext*       GetContext()              { return &m_rootContext; }
	const ibPrecompileContext* GetContext()       const  { return &m_rootContext; }
	ibPrecompileContext*       GetCurrentContext() const { return m_cursorContext; }

	bool Compile();

	bool PrepareLexem();
#ifdef UTF8_LEXEM_TRANSLATE
	void PrepareLexem(unsigned int line, int offsetLine, const int& str_length, const int& str_utf8_length);
#else 
	void PrepareLexem(unsigned int line, int offsetLine, const int& str_length);
#endif

	// Post-lex pass: a keyword right after a member-access `.` is a MEMBER NAME, not a keyword
	// (`q.Execute().Select()` — `Select` is the method, not the SELECT projection keyword). Retags
	// such lexems IDENTIFIER so autocomplete chains through them and they are not highlighted as keywords.
	void ReclassifyMemberKeywords();

protected:

	bool CompileFunction();
	bool CompileDeclaration();

	// allowSingleStmt: when true (default), CES brace-less form
	// `if (cond) stmt;` exits after exactly one statement. Module-level
	// body passes false — there's no `{` opener but the body is a
	// multi-statement sequence, not a single-stmt scope.
	bool CompileBlock(bool allowSingleStmt = true);

	bool CompileNewObject();
	bool CompileGoto();
	bool CompileIf();
	bool CompileWhile();
	bool CompileFor();
	bool CompileForeach();

	// Block-syntax LINQ walker — minimal precompile. Parses
	// `from <id> in <expr> [...clauses...] (select|group)`,
	// registers from/let/join/group-into bindings in m_activeContext
	// so `o.` inside the block sees `o`. Element-type inference is
	// not done: bindings carry empty type. On unrecognised tail
	// keyword the walker bails — the rest of the module parses
	// normally. Mirrors backend ibCompileCode::CompileLinqExpression
	// shape; see docs/linq.md "Editor integration".
	ibParamValue CompileLinqExpression();

	// Access-policy restriction walker — mirror of backend
	// ibCompileCode::CompileRestrictExpression. Parses
	// `restrict <s> in <src> [ join <a> in <T> on <cond> ]* [ where <cond> ]`
	// and registers the source / join aliases in m_activeContext so `s.` and
	// `a.` inside the on / where clauses autocomplete. Entered on KEY_RESTRICT
	// so the walker does not bail on the keyword.
	ibParamValue CompileRestrictExpression();

protected:

	bool CompileModule();

	const ibLexem& PreviewGetLexem();
	const ibLexem& GetLexem();
	const ibLexem& ExpectLexem();
	void ExpectDelimeter(const wxUniChar& c);

	bool IsNextDelimeter(const wxUniChar& c);
	bool IsNextKeyWord(int keyword);
	void ExpectKeyword(int keyword);
	wxString ExpectIdentifier(bool realName = false);
	ibValue ExpectConstant();
	int GetConstString(const wxString& method);

	int IsTypeVar(const wxString& type = wxEmptyString);
	wxString GetTypeVar(const wxString& type = wxEmptyString);

	ibParamValue GetExpression(int priority = 0);

	ibParamValue GetCurrentIdentifier(int& isSet);
	ibParamValue GetCallFunction(const wxString& name);

	void AddVariable(const wxString& varName, const ibValue& value);

	ibParamValue GetVariable(const wxString& varName, bool checkError = false, int declPos = 0);
	ibParamValue GetVariable();

	void SetVariable(const wxString& varName, const ibValue& value);

	ibParamValue FindConst(ibValue& vData);
};

#endif 
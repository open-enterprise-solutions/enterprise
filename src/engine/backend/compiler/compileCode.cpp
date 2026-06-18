////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, 2�-team
//	Description : compile module 
////////////////////////////////////////////////////////////////////////////

#include "compileCode.h"
#include "codeDef.h"
#include "lambdaQueryAst.h"   // L4-2 — lambda body -> L4 query AST (pushdown)

#include "system/systemManager.h"
#include "backend/guid.h"  // wxNewUniqueGuid for anonymous-lambda synthetic naming

#pragma warning(push)
#pragma warning(disable : 4018)

//////////////////////////////////////////////////////////////////////
//                           Constants
//////////////////////////////////////////////////////////////////////

// array of mathematical operation priorities
static std::array<int, 256> gs_operPriority = { 0 };

// set code style by file extension
// CES is the default — modern brace/paren syntax with `;` terminators.
// VES (Visual Basic-style ES + 1С/BSL mix) remains supported for legacy ES
// configurations migrated from 1С / BSL; loading a VES module flips this
// via SetCodeStyle().
static short gs_codeStyle = CODE_CES;

//////////////////////////////////////////////////////////////////////
// Construction/Destruction ibCompileCode
//////////////////////////////////////////////////////////////////////

ibCompileCode::ibCompileCode() :
	ibTranslateCode(),
	m_rootContext(new ibCompileContext(this)),
	m_changedCode(false),
	m_onlyFunction(false)
{
	InitializeCompileModule();

	// we don�t look for local variables in parent contexts!
	m_rootContext->m_numFindLocalInParent = 0;
}

ibCompileCode::ibCompileCode(const wxString& strModuleName, const wxString& strDocPath, bool onlyFunction) :
	ibTranslateCode(strModuleName, strDocPath),
	m_rootContext(new ibCompileContext(this)),
	m_changedCode(false),
	m_onlyFunction(onlyFunction)
{
	InitializeCompileModule();

	// we don�t look for local variables in parent contexts!
	m_rootContext->m_numFindLocalInParent = 0;
}

ibCompileCode::ibCompileCode(const wxString& strFileName) :
	ibTranslateCode(strFileName),
	m_rootContext(new ibCompileContext(this)),
	m_changedCode(false),
	m_onlyFunction(false)
{
	InitializeCompileModule();

	// we don�t look for local variables in parent contexts!
	m_rootContext->m_numFindLocalInParent = 0;
}

ibCompileCode::~ibCompileCode()
{
	Reset();

	m_listExternValue.clear();
	m_listContextValue.clear();

	wxDELETE(m_rootContext);
}

void ibCompileCode::InitializeCompileModule()
{
	if (gs_operPriority[gs_operPriority.size() - 1])
		return;

	gs_operPriority['+'] = 10;
	gs_operPriority['-'] = 10;
	gs_operPriority['*'] = 30;
	gs_operPriority['/'] = 30;
	gs_operPriority['%'] = 30;
	gs_operPriority['!'] = 50;

	gs_operPriority[KEY_OR] = 1;
	gs_operPriority[KEY_AND] = 2;

	gs_operPriority['>'] = 3;
	gs_operPriority['<'] = 3;
	gs_operPriority['='] = 3;

	gs_operPriority[gs_operPriority.size() - 1] = 1;
}

void ibCompileCode::SetCodeStyle(short codeStyle)
{
	gs_codeStyle = codeStyle;
}

// Definition of ibTranslateCode::IsAllowedKey — declared on the base
// (translateCode.h), bodied here so the gate can read gs_codeStyle
// without forcing translateCode.cpp to include compileCode.h. The
// include chain stays one-way (compile → translate). In CES, block-
// fence keywords (Then / Do / EndIf / EndDo / EndFunction /
// EndProcedure / EndTry) are filtered out — they have no syntactic
// place in brace-style sources. VES leaves every keyword in.
bool ibTranslateCode::IsAllowedKey(int keywordId)
{
	if (gs_codeStyle == CODE_CES) {
		switch (keywordId) {
			case KEY_THEN:
			case KEY_DO:
			case KEY_ENDIF:
			case KEY_ENDDO:
			case KEY_ENDFUNCTION:
			case KEY_ENDPROCEDURE:
			case KEY_ENDTRY:
				return false;
		}
	}
	return true;
}


short ibCompileCode::GetCodeStyle()
{
	return gs_codeStyle;
}

void ibCompileCode::Reset()
{
	m_cByteCode.Reset();

	if (m_rootContext != nullptr)
		m_rootContext->Reset();

	m_listHashConst.clear();
	m_listCallFunc.clear();
}

void ibCompileCode::PrepareModuleData()
{
	// Helper — locate the freshly-added compile-context entry by name
	// to stamp post-AddVariable flags (External / clsid / scoped).
	auto stampOnContext = [&](const wxString& name, auto&& mutate) {
		auto it = std::find_if(m_rootContext->m_listVariable.begin(), m_rootContext->m_listVariable.end(),
			[&](const auto& kv) { return stringUtils::CompareString(name, kv.first); });
		if (it != m_rootContext->m_listVariable.end() && it->second)
			mutate(*it->second);
	};

	// Pass 1: external values — kind=External on the bc mirror. Binder
	// fills the slot at runtime; pre-flight verifies clsid match.
	for (auto& externValue : m_listExternValue) {
		m_rootContext->AddVariable(externValue.first, wxEmptyString, true);
		const ibClassID clsid = externValue.second ? externValue.second->GetClassType() : ibClassID(0);
		stampOnContext(externValue.first, [&](ibCompileContext::ibVariable& v) {
			v.m_bExternal = true;
			v.m_clsid     = clsid;
		});
	}

	// Pass 1b: local binds — plain frame locals (bExport=false, NO m_bExternal /
	// m_bContext stamp → kind=Local). The binder fills the slot at runtime (no
	// required/type pre-flight); the module reads/writes it as a normal local
	// (e.g. a constant's Value backed by &m_constValue).
	for (auto& localValue : m_listLocalValue) {
		m_rootContext->AddVariable(localValue.first, wxEmptyString, false);
	}

	// Pass 2: context values — currently all self-referencing.
	// Stamp scoped (per-instance "self" handles like ThisForm) and
	// clsid onto the compile-context entry; the bc mirror then carries
	// these into m_listVar with kind=Context so the binder treats them
	// as required and resolve walks them visibility-aware.
	for (auto& contextValue : m_listContextValue) {
		m_rootContext->AddVariable(contextValue.first, wxEmptyString, true, true);
		bool scoped = false;
		ibClassID clsid = 0;
		if (contextValue.second.m_value) {
			contextValue.second.m_value->InvalidateNames();
			clsid = contextValue.second.m_value->GetClassType();
			const long selfPropIdx = contextValue.second.m_value->FindProp(contextValue.first);
			if (selfPropIdx >= 0)
				scoped = contextValue.second.m_value->IsPropScoped(selfPropIdx);
		}
		stampOnContext(contextValue.first, [&](ibCompileContext::ibVariable& v) {
			v.m_clsid = clsid;
			if (scoped) v.m_bScoped = true;
		});
	}

	ibCompileContext* mainContext = GetContext();

	for (auto& pair : m_listContextValue) {

		ibValue* contextValue = pair.second.m_value;
		wxASSERT(contextValue);
		contextValue->InvalidateNames();

		// adding variables from context
		for (unsigned int i = 0; i < contextValue->GetNProps(); i++) {
			const wxString& propName = contextValue->GetPropName(i);
			// Skip the self-named prop (ThisForm exposes a "ThisForm"
			// prop pointing at itself; ThisObject does the same). The
			// binding entry from Pass 2 already owns this name as
			// kind=Context with a real frame slot — pushing the
			// self-prop here would overwrite that via PushVariable's
			// insert_or_assign (Pass-2 entry lost → binder skips the
			// slot at pre-flight → runtime reads garbage). Scoped
			// semantics are already on the Pass-2 entry from the
			// stampOnContext step above.
			if (stringUtils::CompareString(propName, pair.first))
				continue;
			mainContext->PushVariable(propName, pair.first, i);
			// Non-self per-instance handles (Controls / DataSource of
			// ThisForm) — flag scoped from helper. (RegisterRecords of a
			// document is EXPORTED, not scoped — see documentObject.cpp.)
			if (contextValue->IsPropScoped(i)) {
				auto pushed = std::find_if(mainContext->m_listVariable.begin(), mainContext->m_listVariable.end(),
					[&propName](const auto& kv) { return stringUtils::CompareString(propName, kv.first); });
				if (pushed != mainContext->m_listVariable.end() && pushed->second)
					pushed->second->m_bScoped = true;
			}
		}

		// add methods from context
		for (unsigned int i = 0; i < contextValue->GetNMethods(); i++) {
			// define the number and type of the function
			mainContext->PushFunction(
				contextValue->GetMethodName(i), pair.first, contextValue->GetMethodHelper(i), i, contextValue->HasRetVal(i), contextValue->GetNParams(i));
		}
	}
}

/**
 * SetError
 * Purpose:
 * Remember the translation error and throw an exception
 * Return value:
 * The method does not return control!
 */

void ibCompileCode::SetError(int codeError, const wxString& errorDesc)
{
	wxString strFileName, strModuleName, strDocPath; int currPos = 0, currLine = 0;

	if (m_numCurrentCompile >= m_listLexem.size()) {
		m_numCurrentCompile = m_listLexem.size() - 1;
	}

	if (m_numCurrentCompile >= 0 && m_numCurrentCompile < m_listLexem.size()) {

		strFileName = m_listLexem[m_numCurrentCompile].GetFileName();
		strModuleName = m_listLexem[m_numCurrentCompile].GetModuleName();
		strDocPath = m_listLexem[m_numCurrentCompile].GetDocPath();

		if (m_listLexem[m_numCurrentCompile].m_lexType != ENDPROGRAM) {
			currLine = m_listLexem[m_numCurrentCompile].GetLine();
			currPos = m_listLexem[m_numCurrentCompile].EndPos();
		}
		else {
			currLine = m_listLexem[m_numCurrentCompile - 1].GetLine();
			currPos = m_listLexem[m_numCurrentCompile - 1].EndPos();
		}
	}

	ibTranslateCode::SetError(codeError,
		strFileName, strModuleName, strDocPath,
		currPos, currLine,
		errorDesc);
}

/**
 * another function option
 */

void ibCompileCode::SetError(int nErr, const wxUniChar& c)
{
	SetError(nErr, wxString::Format(wxT("%c"), c));
}

/**
 * DoSetError
 * Purpose:
 * Remember the translation error and throw an exception
 * Return value:
 * The method does not return control!
 */

void ibCompileCode::DoSetError(int codeError,
	const wxString& strFileName, const wxString& strModuleName, const wxString& strDocPath,
	unsigned int currPos, unsigned int currLine,
	const wxString& strErrorDesc) const
{
	const wxString& strCodeError =
		ibBackendException::FindErrorCodeLine(m_strBuffer, currPos);

	ibBackendException::ProcessError(
		strFileName,
		strModuleName, strDocPath,
		currPos, currLine,
		strCodeError, codeError, strErrorDesc
	);
}

//////////////////////////////////////////////////////////////////////
// Compiling
//////////////////////////////////////////////////////////////////////

/**
 *adding information about the current line to the byte code
 */

void ibCompileCode::AddLineInfo(ibByteUnit& code)
{
	code.m_strModuleName = m_strModuleName;
	code.m_strDocPath = m_strDocPath;
	code.m_strFileName = m_strFileName;

	if (m_numCurrentCompile >= 0 && m_numCurrentCompile < m_listLexem.size()) {
		if (m_listLexem[m_numCurrentCompile].m_lexType != ENDPROGRAM) {
			code.m_strModuleName = m_listLexem[m_numCurrentCompile].GetModuleName();
			code.m_strDocPath = m_listLexem[m_numCurrentCompile].GetDocPath();
			code.m_strFileName = m_listLexem[m_numCurrentCompile].GetFileName();
		}
		code.m_numString = m_listLexem[m_numCurrentCompile].m_numString;
		code.m_numLine = m_listLexem[m_numCurrentCompile].m_numLine;
	}
}

/**
 * GetLexem
 * Purpose:
 * Get the next token from the list of byte codes and increment the current position counter by 1
 * Return value:
 * 0 or pointer to token
 */

const ibLexem& ibCompileCode::GetLexem()
{
	if (m_numCurrentCompile + 1 < m_listLexem.size())
		return m_listLexem[++m_numCurrentCompile];
	return gs_nullLexem;
}

// get the next token from a list of bytecodes without incrementing the current position counter
const ibLexem& ibCompileCode::PreviewGetLexem()
{
	while (true) {
		const ibLexem& lex = GetLexem();
		if (!(lex.m_lexType == DELIMITER && lex.m_numData == ';')) {
			m_numCurrentCompile--;
			return lex;
		}
	}

	return gs_nullLexem;
}

/**
 * GETLexem
 * Purpose:
 * Get the next token from the list of byte codes and increment the current position counter by 1
 * Return value:
 * no (if failure occurs, an exception is thrown)
 */

const ibLexem& ibCompileCode::GETLexem()
{
	const ibLexem& lex = GetLexem();
	if (lex.m_lexType == ERRORTYPE) {
		m_numCurrentCompile--;
		SetError(ERROR_CODE_DEFINE);
		return gs_nullLexem;
	}
	return lex;
}

/**
 * GETDelimeter
 * Purpose:
 * Get the next token as the given delimiter
 * Return value:
 * no (if failure occurs, an exception is thrown)
 */

void ibCompileCode::GETDelimeter(const wxUniChar& c)
{
	const ibLexem& lex = GetLexem();
	if (!(lex.m_lexType == DELIMITER && c == lex.m_numData)) {
		m_numCurrentCompile--;
		SetError(ERROR_DELIMETER, c);
	}
}

/**
 * IsKeyWord
 * Purpose:
 * Check if the current bytecode token is a given keyword
 * Return value:
 * true, false
 */

bool ibCompileCode::IsKeyWord(int k)
{
	if (m_numCurrentCompile + 1 < m_listLexem.size()) {
		const ibLexem& lex = m_listLexem[m_numCurrentCompile];
		if (lex.m_lexType == KEYWORD && lex.m_numData == k)
			return true;
	}
	return false;
}

/**
 * IsNextKeyWord
 * Purpose:
 * Check if the next bytecode token is a given keyword
 * Return value:
 * true,false
 */

bool ibCompileCode::IsNextKeyWord(int k)
{
	if (m_numCurrentCompile + 1 < m_listLexem.size()) {
		const ibLexem& lex = m_listLexem[m_numCurrentCompile + 1];
		if (lex.m_lexType == KEYWORD && k == lex.m_numData)
			return true;
	}
	return false;
}

/**
 * IsDelimeter
 * Purpose:
 * Check if the current bytecode token is a given delimiter
 * Return value:
 * true,false
 */

bool ibCompileCode::IsDelimeter(const wxUniChar& c)
{
	if (m_numCurrentCompile + 1 < m_listLexem.size()) {
		const ibLexem& lex = m_listLexem[m_numCurrentCompile];
		if (lex.m_lexType == DELIMITER && c == lex.m_numData)
			return true;
	}
	return false;
}

/**
 * IsNextDelimeter
 * Purpose:
 * Check if the next bytecode token is a given delimiter
 * Return value:
 * true,false
 */

bool ibCompileCode::IsNextDelimeter(const wxUniChar& c)
{
	if (m_numCurrentCompile + 1 < m_listLexem.size()) {
		const ibLexem& lex = m_listLexem[m_numCurrentCompile + 1];
		if (lex.m_lexType == DELIMITER && c == lex.m_numData)
			return true;
	}
	return false;
}

/**
 * GETKeyWord
 * Get the next token as the given keyword
 * Return value:
 * no (if failure occurs, an exception is thrown)
 */

void ibCompileCode::GETKeyWord(int nKey)
{
	const ibLexem& lex = GetLexem();
	if (!(lex.m_lexType == KEYWORD && lex.m_numData == nKey)) {
		m_numCurrentCompile--;
		SetError(ERROR_KEYWORD,
			wxString::Format(wxT("%s"), s_listKeyWord[nKey].m_strKeyWord)
		);
	}
}

/**
 * GETIdentifier
 * Get the next token as the given keyword
 * Return value:
 * identifier string
 */

wxString ibCompileCode::GETIdentifier(bool strRealName, bool acceptKeyword)
{
	const ibLexem& lex = GetLexem();
	if (lex.m_lexType != IDENTIFIER) {
		// In property-access positions (`obj.<X>`) contextual LINQ
		// keywords (Where/Select/...) must work as method names.
		// When acceptKeyword is on, take the keyword's string form
		// from m_strData (lexer-emitted, source-cased on KEYWORD
		// lexems) and proceed as if it were an identifier.
		if (acceptKeyword && lex.m_lexType == KEYWORD) {
			return lex.m_strData;
		}
		m_numCurrentCompile--;
		SetError(ERROR_IDENTIFIER_DEFINE);
		return wxEmptyString;
	}

	if (strRealName) {
		return lex.m_valData.GetString();
	}

	return lex.m_strData;
}

/**
 * GETConstant
 * Get the next token as a constant
 * Return value:
 * constant
 */

ibValue ibCompileCode::GETConstant()
{
	ibLexem lex;
	int iNumRequire = 0;
	if (IsNextDelimeter('-') || IsNextDelimeter('+')) {
		iNumRequire = 1;
		if (IsNextDelimeter('-'))
			iNumRequire = -1;
		lex = GETLexem();
	}

	lex = GetLexem();

	if (lex.m_lexType != CONSTANT) {
		SetError(ERROR_CONST_DEFINE);
		return ibValue();
	}

	if (iNumRequire) {

		// check that the constant is of numeric type
		if (lex.m_valData.GetType() != ibValueTypes::TYPE_NUMBER) {
			SetError(ERROR_CONST_DEFINE);
			return ibValue();
		}

		// change sign for minus
		if (iNumRequire == -1) {
			lex.m_valData.m_fData = -lex.m_valData.m_fData;
		}
	}

	return lex.m_valData;
}

// getting the number with a string constant (to determine the method number)
const int ibCompileCode::GetConstString(const wxString& strConstName)
{
	auto iterator = std::find_if(m_listHashConst.begin(), m_listHashConst.end(),
		[strConstName](const auto pair) { return stringUtils::CompareString(strConstName, pair.first); });

	if (iterator != m_listHashConst.end())
		return iterator->second - 1;

	m_cByteCode.m_listConst.emplace_back(strConstName);
	m_listHashConst.insert_or_assign(strConstName, m_cByteCode.m_listConst.size());

	return m_cByteCode.m_listConst.size() - 1;
}

/**
 * AddVariable
 * Purpose:
 * Add the name and address of an external variable to a special array for later use
 */

void ibCompileCode::AddVariable(const wxString& strVarName, const ibValue& vObject)
{
	if (strVarName.IsEmpty())
		return;

	// take into account external variables during compilation
	m_listExternValue[strVarName] = vObject.IsReference()
		? vObject.GetRef() : const_cast<ibValue*>(&vObject);

	//set the flag for recompilation
	m_changedCode = true;
}

/**
 * AddVariable
 * Purpose:
 * Add the name and address of an external variable to a special array for later use
 */

void ibCompileCode::AddVariable(const wxString& strVarName, ibValue* pValue)
{
	if (strVarName.IsEmpty())
		return;

	// take into account external variables during compilation
	m_listExternValue[strVarName] = pValue;

	//set the flag for recompilation
	m_changedCode = true;
}

/**
 * AddContextVariable
 * Purpose:
 * Add the name and address of an external variable to a special array for later use
 */

void ibCompileCode::AddContextVariable(const wxString& strVarName, const ibValue& vObject, bool scopeContext)
{
	if (strVarName.IsEmpty())
		return;

	//adding variables from context
	m_listContextValue[strVarName] = {
		vObject.IsReference() ? vObject.GetRef() : const_cast<ibValue*>(&vObject),
		scopeContext };  // {m_value, m_scopeContext}

	//set the flag for recompilation
	m_changedCode = true;
}

/**
 * AddContextVariable
 * Purpose:
 * Add the name and address of an external variable to a special array for later use
 */

void ibCompileCode::AddContextVariable(const wxString& strVarName, ibValue* pValue, bool scopeContext)
{
	if (strVarName.IsEmpty())
		return;

	//adding variables from context
	m_listContextValue[strVarName] = { pValue, scopeContext };

	//set the flag for recompilation
	m_changedCode = true;
}

// Bound LOCAL — the name resolves to a plain frame local (kind=Local), but the
// binder fills its slot at init with pValue. The module body reads/writes it as a
// normal local (e.g. a constant's Value, backed by &m_constValue).
void ibCompileCode::AddLocalVariable(const wxString& strVarName, ibValue* pValue)
{
	if (strVarName.IsEmpty())
		return;

	m_listLocalValue[strVarName] = pValue;
	m_changedCode = true;
}

/**
 * RemoveVariable
 * Purpose:
 * Remove the name and address of an external variable
 */

void ibCompileCode::RemoveVariable(const wxString& strVarName)
{
	if (strVarName.IsEmpty())
		return;

	m_listExternValue.erase(strVarName);
	m_listContextValue.erase(strVarName);

	//set the flag for recompilation
	m_changedCode = true;
}

/**
 * Recompile
 * Purpose:
 * Translation and compilation of source code into bytecode (object code)
 * Return value:
 * true,false
 */

bool ibCompileCode::Recompile()
{
	//clear functions & variables
	Reset();

	//prepare lexem 
	if (!PrepareLexem()) {
		return false;
	}

	// prepare context variables
	PrepareModuleData();

	// compilation 
	if (CompileModule()) {
		m_changedCode = false;
		return true;
	}

	return false;
}

/**
 * Compile
 * Purpose:
 * Translation and compilation of source code into bytecode (object code)
 * Return value:
 * true,false
 */

bool ibCompileCode::Compile()
{
	//clear functions & variables
	Reset();

	//prepare lexem 
	if (!PrepareLexem()) {
		return false;
	}

	// prepare context variables
	PrepareModuleData();

	// compilation 
	if (CompileModule()) {
		m_changedCode = false;
		return true;
	}

	return false;
}

/**
 * Compile
 * Purpose:
 * Translation and compilation of source code into bytecode (object code)
 * Return value:
 * true,false
 */

bool ibCompileCode::Compile(const wxString& strCode)
{
	//clear functions & variables
	Reset();

	Load(strCode);

	//prepare lexem
	if (!PrepareLexem()) {
		return false;
	}

	// prepare context variables
	PrepareModuleData();

	// compilation 
	if (CompileModule()) {
		m_changedCode = false;
		return true;
	}

	return false;
}

bool ibCompileCode::IsTypeVar(const wxString& strType)
{
	if (!strType.IsEmpty()) {
		if (ibValue::IsRegisterCtor(strType, ibCtorObjectType::ibCtorObjectType_object_primitive))
			return true;
	}
	const ibLexem& lex = PreviewGetLexem();
	if (ibValue::IsRegisterCtor(lex.m_strData, ibCtorObjectType::ibCtorObjectType_object_primitive))
		return true;
	return false;
}

wxString ibCompileCode::GetTypeVar(const wxString& strType)
{
	if (!strType.IsEmpty()) {
		if (!ibValue::IsRegisterCtor(strType, ibCtorObjectType::ibCtorObjectType_object_primitive)) {
			SetError(ERROR_TYPE_DEF);
			return wxEmptyString;
		}
		return strType.Upper();
	}
	const ibLexem& lex = GETLexem();
	if (!ibValue::IsRegisterCtor(lex.m_strData, ibCtorObjectType::ibCtorObjectType_object_primitive)) {
		SetError(ERROR_TYPE_DEF);
		return wxEmptyString;
	}
	return stringUtils::MakeUpper(lex.m_strData);
}

/**
 * CompileDeclaration
 * Purpose:
 * Compiling explicit variable declarations
 * Return value:
 * true,false
 */

bool ibCompileCode::CompileDeclaration(ibCompileContext* context)
{
	const ibLexem& lex = PreviewGetLexem(); wxString strType;
	if (lex.m_lexType == IDENTIFIER) {
		strType = GetTypeVar(); // typed setting of variables
	}
	else {
		GETKeyWord(KEY_VAR);
	}

	while (true) {
		wxString strRealName = GETIdentifier(true);
		wxString strName = stringUtils::MakeUpper(strRealName);


		int numParent = 0;
		ibCompileContext* pCurContext = context;
		while (pCurContext) {
			numParent++;
			if (numParent > MAX_OBJECTS_LEVEL) {
				ibValueSystemFunction::Message(pCurContext->m_compileModule->GetModuleName());
				if (numParent > 2 * MAX_OBJECTS_LEVEL) {
					ibBackendCoreException::Error(_("Recursive call of modules!"));
				}
			}
			// Dup-check via direct map find — going through FindVariable
			// would also hit bytecode-fallback (parent module entries),
			// which over-reports duplicates: declaring `Var X` should
			// be allowed when a parent's bytecode has X but the parent's
			// compile-context doesn't (i.e. the parent module isn't
			// re-compiled along with this one).
			auto existing = std::find_if(pCurContext->m_listVariable.begin(), pCurContext->m_listVariable.end(),
				[&strName](const auto& pair) { return stringUtils::CompareString(strName, pair.first); });
			if (existing != pCurContext->m_listVariable.end()) {
				const auto& currentVariable = existing->second;
				if (currentVariable->m_bExport ||
					pCurContext->m_compileModule == this) {
					SetError(ERROR_DEF_VARIABLE, strRealName);
					return false;
				}
			}
			pCurContext = pCurContext->m_parentContext;
		}

		int nArrayCount = -1;
		if (IsNextDelimeter('[')) { // this is an array declaration
			nArrayCount = 0;
			GETDelimeter('[');
			if (!IsNextDelimeter(']')) {
				ibValue vConst = GETConstant();
				if (vConst.GetType() != ibValueTypes::TYPE_NUMBER ||
					vConst.GetNumber() < 0) {
					SetError(ERROR_ARRAY_SIZE_CONST);
					return false;
				}
				nArrayCount = vConst.GetInteger();
			}
			GETDelimeter(']');
		}

		// Access modifier — exactly one of Public / Private / Protected, in
		// place of the old single Export check. All optional (none = Private);
		// more than one is an error.
		bool bExport = false;
		int varAccess = ACCESS_PRIVATE;
		int numModifiers = 0;
		if (IsNextKeyWord(KEY_PUBLIC))    { GETKeyWord(KEY_PUBLIC);    bExport = true; varAccess = ACCESS_PUBLIC;    numModifiers++; }
		if (IsNextKeyWord(KEY_PRIVATE))   { GETKeyWord(KEY_PRIVATE);                   varAccess = ACCESS_PRIVATE;   numModifiers++; }
		if (IsNextKeyWord(KEY_PROTECTED)) { GETKeyWord(KEY_PROTECTED);                 varAccess = ACCESS_PROTECTED; numModifiers++; }
		if (numModifiers > 1) {
			SetError(ERROR_CODE); // only one access modifier (Public / Private / Protected) is allowed
			return false;
		}

		// there was no variable declaration yet - add
		ibParamUnit variable =
			context->AddVariable(strRealName, strType, bExport);

		// Stamp the access enum onto the just-created variable so Protected is
		// distinguishable from Private at resolve time. The parent-chain
		// visibility gate honours Protected (visible to children, not config-
		// wide); Public already rides m_bExport.
		if (varAccess != ACCESS_PRIVATE) {
			std::shared_ptr<ibCompileContext::ibVariable> declVar;
			if (context->FindVariable(strRealName, declVar) && declVar)
				declVar->m_access = varAccess;
		}

		// Tape declarator at the natural source position of the
		// declaration. Caller-side emission keeps ibCompileContext
		// pure (no bytecode reach from compile-context). Implicit
		// creates from GetVariable's fallback don't get a tape
		// declarator — m_listVar mirror at end of CompileModule
		// covers them; AOT walker prefers tape when present, falls
		// back to m_listVar otherwise. Skip RETURN_BLOCK contexts —
		// block-scope @context vars are not real frame slots.
		if (context->m_numReturn != RETURN_BLOCK) {
			ibByteUnit declLocal;
			AddLineInfo(declLocal);
			declLocal.m_numOper = OPER_FUNC_LOCAL;
			declLocal.m_param1.m_numIndex = (long)variable.m_numIndex;
			declLocal.m_param1.m_numArray = bExport ? 1 : 0;
			m_cByteCode.m_listCode.emplace_back(std::move(declLocal));
		}

		if (nArrayCount >= 0) { // write information about the arrays
			ibByteUnit code;
			AddLineInfo(code);
			code.m_numOper = OPER_SET_ARRAY_SIZE;
			code.m_param1 = variable;
			code.m_param2.m_numArray = nArrayCount;//����� ��������� � �������
			m_cByteCode.m_listCode.emplace_back(std::move(code));
		}

		AddTypeSet(variable);

		if (IsNextDelimeter('=')) { // initial initialization - works only inside the text of modules (but not re-declaring procedures and functions)

			if (nArrayCount >= 0)  GETDelimeter(','); // error!

			GETDelimeter('=');

			ibByteUnit code;
			AddLineInfo(code);
			code.m_numOper = OPER_LET;
			code.m_param1 = variable;
			code.m_param2 = GetExpression(context);
			m_cByteCode.m_listCode.emplace_back(std::move(code));
		}

		if (!IsNextDelimeter(','))
			break;

		GETDelimeter(',');
	}

	return true;
}

/**
 *CompileModule
 * Purpose:
 * Compiling all bytecode (creating object code from a set of tokens)
 * Return value:
 * true,false
*/

bool ibCompileCode::CompileModule()
{
	// set the cursor to the beginning of the token array
	m_numCurrentCompile = -1;
	ibCompileContext* mainContext = GetContext(); // context of the module itself

	while (true) {

		const ibLexem& lex = PreviewGetLexem();
		if (lex.m_lexType == ERRORTYPE) break;

		if ((KEYWORD == lex.m_lexType && lex.m_numData == KEY_VAR) || (IDENTIFIER == lex.m_lexType && IsTypeVar(lex.m_strData))) {
			if (!m_onlyFunction) {
				CompileDeclaration(mainContext); // load variable declaration
			}
			else {
				SetError(ERROR_ONLY_FUNCTION);
				return false;
			}
		}
		else if (KEYWORD == lex.m_lexType && (KEY_PROCEDURE == lex.m_numData || KEY_FUNCTION == lex.m_numData)) {
			// don't forget to restore the current module context (if necessary)...
			CompileFunction(mainContext); // load function declaration
		}
		else break;
	}

	// load the executable body of the module
	m_cByteCode.m_lStartModule = 0;
	CompileBlock(mainContext);

	mainContext->CreateLabels();

	// set the end of the program
	ibByteUnit code;
	AddLineInfo(code);
	code.m_numOper = OPER_END;

	m_cByteCode.m_listCode.emplace_back(std::move(code));
	m_cByteCode.m_lVarCount = mainContext->m_listVariable.size();

	// we finish processing procedures and functions that were called before they were declared
	// for this, at the end of the bytecode array, add new code to call such functions,
	// and for correct operation we insert GOTO statements into places of early calls
	for (auto& callFunc : m_listCallFunc) {
		m_cByteCode.m_listCode[callFunc->m_numAddLine].m_param1.m_numIndex =
			m_cByteCode.m_listCode.size(); // go to function call
		if (PushCallFunction(callFunc)) {
			// correcting labels
			ibByteUnit code;
			AddLineInfo(code);
			code.m_numOper = OPER_GOTO;
			code.m_numLine = callFunc->m_numLine;
			code.m_numString = callFunc->m_numString;
			code.m_param1.m_numIndex = callFunc->m_numAddLine + 1; // after calling the function we go back
			m_cByteCode.m_listCode.emplace_back(std::move(code));
		}
	}

	// Mirror the compile-context symbol table into the bytecode's
	// unified m_listVar (std::vector).
	//
	// Skip only temps (compiler-introduced intermediates; frame size
	// in OPER_FUNC.m_param3 covers them, no name-based access).
	//
	// All other entries land in m_listVar; the kind discriminator is
	// set by ibByteCodeVarInfo's templated copy ctor:
	//   - Pass-1 externs                 → kind=External (m_bExternal stamp)
	//   - Pass-2 top-level context bindings (Manager, ThisForm)
	//                                     → kind=Context
	//   - Pass-3 context-props (Catalogs of Manager) → kind=ContextProp
	//   - User locals / user exports     → kind=Local
	// Readers filter by m_kind (and m_bExport for cross-bc visibility
	// of locals). Binder iterates m_listVar filtering kind ∈
	// {External, Context} — that's the canonical "must-bind" set.
	//
	// Iteration order of the source map is lexicographic by name —
	// stable across recompiles and across runs of the same source,
	// which is what AOT serialization needs. Frame slots
	// (m_slotIndex) preserve declaration-order assignment from
	// AddVariable; vector position is just iteration order.
	for (auto it : mainContext->m_listVariable) {
		if (it.second->m_bTempVar) continue;
		ibByteCode::ibByteCodeVarInfo info(*it.second);
		// m_strRealName is the canonical name on the vector entry —
		// the historical map key. Ctor copies from compile-side
		// m_strRealName; if that was empty for some legacy path, fall
		// back to the map key so lookups still match by name.
		if (info.m_strRealName.IsEmpty())
			info.m_strRealName = it.first;
		m_cByteCode.m_listVar.push_back(std::move(info));
	}

	// Second pass: resolve m_parentRef for ContextProp entries.
	// `m_strContext` carries the parent binding's name (e.g. "Manager"
	// for a "Catalogs" prop); convert it to the parent's index in
	// m_listVar. Linear scan — typical bc has < 100 entries, no hot
	// path. Leaves m_parentRef = -1 if no matching parent exists.
	// (kind=ContextProp invariant: m_strContext is non-empty by
	// templated-ctor construction — no defensive check needed.)
	for (auto& v : m_cByteCode.m_listVar) {
		if (!v.IsContextProp()) continue;
		auto pIt = std::find_if(m_cByteCode.m_listVar.begin(), m_cByteCode.m_listVar.end(),
			[&](const auto& p) { return stringUtils::CompareString(v.m_strContext, p.m_strRealName); });
		if (pIt != m_cByteCode.m_listVar.end())
			v.m_parentRef = static_cast<long>(std::distance(m_cByteCode.m_listVar.begin(), pIt));
	}

	// Mirror Pass-3 context-methods (m_strContext set) into the
	// unified m_listFunc (vector) with kind=ContextMethod (set by the
	// templated ctor based on m_strContext non-empty). Own user-defined
	// functions are already mirrored per-function by CompileFunction;
	// this loop completes the table with the binding methods (e.g.
	// GetForm of Manager). lAddress = -1 marks "no own IP" — runtime
	// dispatches via OPER_CALL_METHOD on the parent binding instead of
	// OPER_CALL.
	//
	// "User funcs win" — skip the push if a function with the same
	// name already exists in m_listFunc (was achieved by try_emplace
	// when storage was a map; replicated here via find_if).
	for (auto it : mainContext->m_listFunction) {
		if (!it.second) continue;
		if (it.second->m_strContext.IsEmpty()) continue;  // own user-defined func
		const wxString& nameKey = it.first;
		auto existing = std::find_if(m_cByteCode.m_listFunc.begin(), m_cByteCode.m_listFunc.end(),
			[&](const auto& fn) { return stringUtils::CompareString(nameKey, fn.m_strRealName); });
		if (existing != m_cByteCode.m_listFunc.end()) continue;
		m_cByteCode.m_listFunc.emplace_back(/*lAddress=*/-1, *it.second);
	}

	// Second pass: resolve m_parentRef on ContextMethod entries —
	// link to the parent Context binding in m_listVar by name. Mirror
	// of the m_parentRef resolution for ContextProp vars (above).
	// (kind=ContextMethod invariant: m_strContext is non-empty.)
	for (auto& fn : m_cByteCode.m_listFunc) {
		if (!fn.IsContextMethod()) continue;
		auto pIt = std::find_if(m_cByteCode.m_listVar.begin(), m_cByteCode.m_listVar.end(),
			[&](const auto& p) { return stringUtils::CompareString(fn.m_strContext, p.m_strRealName); });
		if (pIt != m_cByteCode.m_listVar.end())
			fn.m_parentRef = static_cast<long>(std::distance(m_cByteCode.m_listVar.begin(), pIt));
	}

	if (m_numCurrentCompile + 1 < m_listLexem.size() - 1) {
		SetError(ERROR_END_PROGRAM);
		return false;
	}

	// Mark constants read-only — constants are immutable post-compile;
	// runtime writes through them would corrupt the constant pool.
	// MUST be a finalize sweep, NOT stamped at insertion (GetConstString /
	// FindConst): ibValue's move ctor resets m_bReadOnly to false
	// (value.cpp:73), so a flag set on insert is wiped when a later
	// emplace_back grows the vector and move-constructs the earlier entries.
	// Only after the pool stops reallocating is the flag stable. The eval
	// path (ibProcUnit::CompileExpression) carries its own copy of this
	// sweep for the same reason — keep them in sync.
	for (auto& c : m_cByteCode.m_listConst)
		c.m_bReadOnly = true;

	// compilation completed successfully
	m_cByteCode.m_bCompile = true;

	return true;
}

// search for function definition in the current module and all parent ones
bool ibCompileCode::GetFunction(const wxString& strName, std::shared_ptr<ibCompileContext::ibFunction>& function, int* pNumFunction)
{
	int numFunction = 0;

	// Search in the current module's compile-context. Cross-module
	// resolution goes through the bytecode chain below — the legacy
	// m_parent walk through parent compile-modules is gone in favour
	// of the dependency-driven path: ResolveFunction recurses through
	// m_dependencies + m_cByteCode.m_parent chain.
	if (!GetContext()->FindFunction(strName, function)) {
		const int fullVisDepth = m_rootContext->m_numFindLocalInParent - 1;
		if (auto found = m_cByteCode.ResolveFunction(strName, fullVisDepth)) {
			// Synthesized ibFunction from bytecode-resolved descriptor —
			// ibFunction(name, ibByteFunction) ctor copies all fields
			// including m_bCodeRet (return-kind that PushCallFunction
			// needs) and m_listParam (each via ibParamVariable ctor).
			function = std::make_shared<ibCompileContext::ibFunction>(strName, *found.fn);
			numFunction = found.depth;
		}
	}

	if (pNumFunction)
		*pNumFunction = numFunction;

	return function != nullptr;
}

// adding the bytecode of the function call to the array
bool ibCompileCode::PushCallFunction(const std::shared_ptr<ibCallFunction>& callFunction)
{
	int numModule = 0;

	// find the definition of the function
	std::shared_ptr<ibCompileContext::ibFunction> foundedFunc = nullptr;

	if (!GetFunction(callFunction->m_strName, foundedFunc, &numModule)) {
		m_numCurrentCompile = callFunction->m_numError;
		SetError(ERROR_CALL_FUNCTION, callFunction->m_strRealName);// there is no such function in the module
		return false;
	}

	if (!callFunction->m_numIsSet && !foundedFunc->m_bCodeRet) {
		m_numCurrentCompile = callFunction->m_numError;
		SetError(ERROR_USE_PROCEDURE_AS_FUNCTION, foundedFunc->m_strRealName);
		return false;
	}

	// check the match between the number of passed and declared parameters
	unsigned int numRealCount = callFunction->m_listParam.size();
	unsigned int numDefCount = foundedFunc->m_listParam.size();

	if (numRealCount > numDefCount) {
		m_numCurrentCompile = callFunction->m_numError;
		SetError(ERROR_MANY_PARAMS);// too many parameters
		return false;
	}

	ibByteUnit code;
	AddLineInfo(code);

	code.m_numString = callFunction->m_numString;
	code.m_numLine = callFunction->m_numLine;
	code.m_strModuleName = callFunction->m_strModuleName;

	if (foundedFunc->m_bContext) { // virtual function - calling replacements with the construct Context.FunctionName(...)
		code.m_numOper = OPER_CALL_METHOD;
		code.m_param1 = callFunction->m_puRetValue;		// variable into which the value is returned
		code.m_param2 = callFunction->m_puContextVal;	// variable on which the method is called
		code.m_param3.m_numIndex = GetConstString(callFunction->m_strName);	// number of the called method from the list of encountered methods
		code.m_param3.m_numArray = numDefCount;	// number of parameters
	}
	else {
		// OPER_CALL vs OPER_CALL_CLOSURE — same operand layout, different
		// runtime path. _L variant heap-allocates the callee frame
		// (shared_ptr<ibRunContext>) so inner lambdas materialised
		// during the call can capture it. m_needsHeapFrame is settled
		// by the time we get here: backward refs see the fully-compiled
		// callee directly; forward refs land in m_listCallFunc and
		// PushCallFunction reruns at finalize when all bodies are done.
		code.m_numOper = foundedFunc->m_needsHeapFrame ? OPER_CALL_CLOSURE : OPER_CALL;
		code.m_param1 = callFunction->m_puRetValue;	// variable into which the value is returned
		code.m_param2.m_numArray = numModule;		// module number
		code.m_param2.m_numIndex = foundedFunc->m_nStart;	// starting position
		code.m_param3.m_numArray = numDefCount;			// number of parameters
		code.m_param3.m_numIndex = foundedFunc->m_lVarCount;	// number of local variables
		code.m_param4 = callFunction->m_puContextVal;	// context variable
	}

	m_cByteCode.m_listCode.emplace_back(std::move(code));

	for (unsigned int i = 0; i < numDefCount; i++) {
		ibByteUnit code;
		AddLineInfo(code);
		code.m_numOper = OPER_SET; // parameters are being passed
		bool defaultValue = false;
		if (i < numRealCount) {
			code.m_param1 = callFunction->m_listParam[i];
			if (code.m_param1.m_numArray == DEF_VAR_SKIP) { // need to substitute the default value
				defaultValue = true;
			}
			else {  //��� �������� ��������
				code.m_param2.m_numIndex = foundedFunc->m_listParam[i].m_bByRef;
			}
		}
		else {
			defaultValue = true;
		}
		if (defaultValue) {
			if (foundedFunc->m_listParam[i].m_puValue.m_numArray == DEF_VAR_SKIP) {
				m_numCurrentCompile = callFunction->m_numError;
				SetError(ERROR_FEW_PARAMS);	// too few parameters
				return false;
			}
			code.m_numOper = OPER_SETCONST;	// default values
			code.m_param1 = foundedFunc->m_listParam[i].m_puValue;
		}
		m_cByteCode.m_listCode.emplace_back(std::move(code));
	}

	return true;
}

/**
 * CompileFunction
 * Purpose:
 * Creating object code for one function (procedure)
 * Algorithm:
 * - Determine the number of formal parameters
 * - Define ways to call formal parameters (by reference or by value)
 * - Define default values
 * - Determine the number of local variables
 * - Determine whether the function returns a value
 *
 * Return value:
 * true,false
 */

bool ibCompileCode::CompileFunction(ibCompileContext* context)
{
	// Thin orchestrator: parse signature → ancestor-chain dedup →
	// register in m_listFunction → emit body. Behaviour is the
	// monolithic pre-split flow exactly. The two helpers
	// (ParseFunctionSignature + EmitFunctionBody) are reused by
	// the upcoming anonymous-lambda expression path, which skips
	// the dedup + m_listFunction registration steps.
	std::shared_ptr<ibCompileContext::ibFunction> createdFunction;
	std::unique_ptr<ibCompileContext> functionContextOwner;
	int errorPlace = 0;

	if (!ParseFunctionSignature(context, createdFunction, functionContextOwner, errorPlace))
		return false;

	ibCompileContext* functionContext = functionContextOwner.get();
	const wxString& strFuncName = createdFunction->m_strName;
	const wxString& strFuncRealName = createdFunction->m_strRealName;

	// Ancestor-chain dedup: declaration cannot collide with an
	// exported function visible from any enclosing scope. Walk
	// up via m_parentContext; on collision rewind to errorPlace
	// (position right after the function name in the source) so
	// the error highlight points at the conflicting name.
	int numParent = 0;
	ibCompileContext* pCurContext = context;
	while (pCurContext != nullptr) {
		numParent++;
		if (numParent > MAX_OBJECTS_LEVEL) {
			ibValueSystemFunction::Message(pCurContext->m_compileModule->GetModuleName());
			if (numParent > 2 * MAX_OBJECTS_LEVEL) {
				ibBackendCoreException::Error(_("Recursive call of modules!"));
			}
		}

		std::shared_ptr<ibCompileContext::ibFunction> foundedFunc = nullptr;
		if (pCurContext->FindFunction(strFuncName, foundedFunc)) { // found
			if (foundedFunc != createdFunction && foundedFunc->m_bExport) {
				m_numCurrentCompile = errorPlace;
				SetError(ERROR_DEF_FUNCTION, strFuncRealName);
				return false;
			}
		}

		pCurContext = pCurContext->m_parentContext;
	}

	context->m_listFunction[strFuncName] = createdFunction;

	return EmitFunctionBody(context, createdFunction, functionContext);
}

bool ibCompileCode::ParseFunctionSignature(ibCompileContext* context,
	std::shared_ptr<ibCompileContext::ibFunction>& outFunction,
	std::unique_ptr<ibCompileContext>& outFunctionContext,
	int& outErrorPlace)
{
	// functionContext owns the function's compile-time scope (locals,
	// labels). Lifetime is bounded to the caller's frame — after
	// EmitFunctionBody stamps everything we need into the bytecode,
	// the compile-context can go away. ibFunction no longer owns it.

	// We are at the token level where FUNCTION or PROCEDURE keyword
	// is specified. The enum value chosen here fully encodes both axes
	// — anonymous-vs-named AND function-vs-procedure — so any later
	// path that needs either piece can derive it from m_numReturn
	// alone via IsReturnLambda / IsReturnFunction (no side flag).
	//
	// Anonymity is determined by the NEXT TOKEN after the keyword:
	// `Function (` is anonymous, `Function Name(` is named. No
	// external gate — context (next token) is enough to tell.
	if (IsNextKeyWord(KEY_FUNCTION)) {
		GETKeyWord(KEY_FUNCTION);
		const bool isAnon = IsNextDelimeter('(');
		outFunctionContext.reset(context->CreateContext(
			isAnon ? RETURN_LAMBDA_FUNCTION : RETURN_FUNCTION));
	}
	else if (IsNextKeyWord(KEY_PROCEDURE)) {
		GETKeyWord(KEY_PROCEDURE);
		const bool isAnon = IsNextDelimeter('(');
		outFunctionContext.reset(context->CreateContext(
			isAnon ? RETURN_LAMBDA_PROCEDURE : RETURN_PROCEDURE));
	}
	else {
		SetError(ERROR_FUNC_DEFINE);
		return false;
	}
	ibCompileContext* functionContext = outFunctionContext.get();

	// pull out the text of the function declaration
	const ibLexem& lex = PreviewGetLexem();

	wxString strShortDescription;
	const int numLine = lex.m_numLine;
	int numRes = m_strBuffer.find('\n', lex.m_numLine);
	if (numRes >= 0) {
		strShortDescription = m_strBuffer.substr(lex.m_numLine, numRes - lex.m_numLine - 1);
		numRes = strShortDescription.find_first_of('/');
		if (numRes > 0) {
			if (strShortDescription[numRes - 1] == '/') { // so this is a comment
				strShortDescription = strShortDescription.substr(numRes + 1);
			}
		}
		else {
			numRes = strShortDescription.find_first_of(')');
			strShortDescription = strShortDescription.substr(0, numRes + 1);
		}
	}

	// get the function name. Anonymous form: next token `(` directly
	// after the keyword → synthesise a guid-based label. Used purely
	// for debugger / error-message labelling — the lambda is NOT
	// registered in any compile-context's m_listFunction map
	// (CompileFunction's orchestrator path does that for declarations
	// only; CompileLambdaExpression deliberately skips it). Guid
	// guarantees zero chance of collision with any user-written
	// identifier (Variable or Function in any scope), and signals at a
	// glance "synthetic — never look this up by name".
	wxString strFuncRealName;
	if (IsNextDelimeter('(')) {
		strFuncRealName = wxT("__lambda_") + wxNewUniqueGuid.str();
	}
	else {
		strFuncRealName = GETIdentifier(true);
	}
	// Original-cased name through both the ibFunction storage and the
	// m_listFunction map key — lookups use stringUtils::CompareString
	// (case-insensitive), so storage no longer needs to upper-normalize.
	// Renderers (debugger / catalog object PrepareNames) read map keys
	// directly and now show the user's source casing.
	const wxString& strFuncName = strFuncRealName;

	outErrorPlace = m_numCurrentCompile;

	outFunction.reset(new ibCompileContext::ibFunction(strFuncName, functionContext));
	outFunction->m_strRealName = strFuncRealName;
	outFunction->m_strShortDescription = strShortDescription;
	outFunction->m_numLine = numLine;
	// Derived from the context's m_numReturn — RETURN_FUNCTION and
	// RETURN_LAMBDA_FUNCTION both carry "returns a value", the
	// procedure variants don't. No keyword bool kept; enum is the
	// single source of truth.
	outFunction->m_bCodeRet = IsReturnFunction(functionContext->m_numReturn);

	// compile the list of formal parameters + register them as local
	GETDelimeter('(');

	while (!IsNextDelimeter(')')) {

		// check for typing
		const wxString typeVar = IsTypeVar() ?
			GetTypeVar() : wxString(wxEmptyString);

		ibCompileContext::ibFunction::ibParamVariable cVariable;
		if (IsNextKeyWord(KEY_VAL)) {
			GETKeyWord(KEY_VAL);
			cVariable.m_bByRef = true;
		}

		const wxString& strRealName = GETIdentifier(true);

		cVariable.m_strName = strRealName;
		cVariable.m_strType = typeVar;

		std::shared_ptr<ibCompileContext::ibVariable> foundedVar = nullptr;

		// register this variable as local
		if (functionContext->FindVariable(strRealName, foundedVar)) { // there was an announcement + repeated announcement = error
			SetError(ERROR_IDENTIFIER_DUPLICATE, strRealName);
			return false;
		}

		if (IsNextDelimeter('[')) { // this is an array
			GETDelimeter('[');
			GETDelimeter(']');
		}
		else if (IsNextDelimeter('=')) {
			GETDelimeter('=');
			cVariable.m_puValue = FindConst(GETConstant());
		}

		functionContext->AddVariable(strRealName, typeVar);
		outFunction->m_listParam.emplace_back(std::move(cVariable));

		if (!IsNextDelimeter(',') || IsNextDelimeter(')'))
			break;

		GETDelimeter(',');
	}
	GETDelimeter(')');

	// Access modifier — exactly one of Public / Private / Protected, in place of
	// the old single Export check. All optional (none = Private, the default);
	// more than one is an error. m_access holds the enum, m_bExport is derived.
	int numModifiers = 0;
	if (IsNextKeyWord(KEY_PUBLIC))    { GETKeyWord(KEY_PUBLIC);    outFunction->m_access = ACCESS_PUBLIC;    numModifiers++; }
	if (IsNextKeyWord(KEY_PRIVATE))   { GETKeyWord(KEY_PRIVATE);   outFunction->m_access = ACCESS_PRIVATE;   numModifiers++; }
	if (IsNextKeyWord(KEY_PROTECTED)) { GETKeyWord(KEY_PROTECTED); outFunction->m_access = ACCESS_PROTECTED; numModifiers++; }
	if (numModifiers > 1) {
		SetError(ERROR_CODE); // only one access modifier (Public / Private / Protected) is allowed
		return false;
	}
	outFunction->m_bExport = (outFunction->m_access == ACCESS_PUBLIC);

	return true;
}

bool ibCompileCode::EmitFunctionBody(ibCompileContext* /*context*/,
	const std::shared_ptr<ibCompileContext::ibFunction>& createdFunction,
	ibCompileContext* functionContext)
{
	// Discriminator lives on the context — RETURN_LAMBDA_FUNCTION /
	// RETURN_LAMBDA_PROCEDURE stamped by ParseFunctionSignature for
	// anonymous bodies. No separate isLambda arg threaded through.
	const bool isLambda = IsReturnLambda(functionContext->m_numReturn);

	// Function entry — OPER_FUNC (named) or OPER_LFUNC (lambda) marks
	// the tape position. Runtime's Execute switch sets
	// cRunContext->m_currentFunction here via FindFunctionByEntry(
	// lCodeLine) — opcode-driven state update, no external lookup at
	// call sites. Both opcodes resolve through the same FindFunctionByEntry
	// path (lookup is by entry IP, not by opcode kind).
	ibByteUnit code0;
	AddLineInfo(code0);
	code0.m_numOper = isLambda ? OPER_LFUNC : OPER_FUNC;
	code0.m_param1.m_numArray = 0;
	m_cByteCode.m_listCode.emplace_back(std::move(code0));

	const long lAddress = createdFunction->m_nStart = m_cByteCode.m_listCode.size() - 1;

	// Tape declarators: emit OPER_FUNC_PARAM for each parameter so the
	// tape is fully self-describing — AOT load can reconstruct
	// ibByteFunction's m_listParam by walking the tape, no parallel
	// metadata needed. Runtime treats them as NOP. Operand layout:
	//   m_param1.m_numIndex = slot (= i, frame position)
	//   m_param1.m_numArray = byref flag (1 = ByRef, 0 = ByVal)
	//   m_param2          = m_puValue (default-value descriptor —
	//                        m_numArray = DEF_VAR_SKIP / DEF_VAR_CONST /
	//                        ..., m_numIndex = const-pool index when CONST)
	// Carries the same info that the legacy OPER_SET/SETCONST at
	// function entry carries — those remain for now as a NOP-equivalent
	// shadow; once nothing reads them we can drop them entirely
	// (next iteration).
	for (unsigned int i = 0; i < createdFunction->m_listParam.size(); i++) {
		ibByteUnit declParam;
		AddLineInfo(declParam);
		declParam.m_numOper = OPER_FUNC_PARAM;
		declParam.m_param1.m_numIndex = (long)i;
		declParam.m_param1.m_numArray = createdFunction->m_listParam[i].m_bByRef ? 1 : 0;
		declParam.m_param2 = createdFunction->m_listParam[i].m_puValue;
		m_cByteCode.m_listCode.emplace_back(std::move(declParam));
	}

	// Type-check stamping for typed params — kept separate. The legacy
	// OPER_SET/SETCONST decoration that used to sit between FUNC_PARAM
	// and AddTypeSet is gone: its full payload (m_puValue, m_bByRef)
	// now lives on OPER_FUNC_PARAM, and runtime never read those
	// OPER_SETs anyway (Execute had no case for them — fall-through NOP).
	for (unsigned int i = 0; i < createdFunction->m_listParam.size(); i++) {
		ibParamUnit variable;
		variable.m_numArray = 0;
		variable.m_numIndex = i;
		variable.m_strType = createdFunction->m_listParam[i].m_strType;
		AddTypeSet(variable);
	}

	// Save/restore enclosing-function name — handles nested lambdas
	// inside named functions (and lambda inside lambda) cleanly. Pre-
	// Phase A there was no nesting (CompileBlock rejected KEY_FUNCTION
	// inside a function body) so plain set+clear was safe; with lambdas
	// as expressions the inner emit can fire while outer is still open.
	const wxString savedCurFuncName = m_strCurFuncName;
	m_strCurFuncName                = createdFunction->m_strName;

	CompileBlock(functionContext);

	functionContext->CreateLabels();

	m_strCurFuncName = savedCurFuncName;

	if (gs_codeStyle == CODE_VES) {
		// Closer keyword matches the OPENING keyword — derived from
		// m_bCodeRet, which IsReturnFunction(m_numReturn) populated at
		// signature parse. Both named and anonymous bodies dispatch
		// the same way; the lambda enum split keeps function vs
		// procedure visible without a side flag.
		if (createdFunction->m_bCodeRet) {
			GETKeyWord(KEY_ENDFUNCTION);
		}
		else {
			GETKeyWord(KEY_ENDPROCEDURE);
		}
	}

	ibByteUnit code;
	AddLineInfo(code);
	code.m_numOper = isLambda ? OPER_ENDLFUNC : OPER_ENDFUNC;
	m_cByteCode.m_listCode.emplace_back(std::move(code));

	createdFunction->m_nFinish = m_cByteCode.m_listCode.size() - 1;
	createdFunction->m_lVarCount = functionContext->m_listVariable.size();

	m_cByteCode.m_listCode[lAddress].m_param3.m_numIndex = createdFunction->m_lVarCount;// number of local variables
	m_cByteCode.m_listCode[lAddress].m_param3.m_numArray = createdFunction->m_listParam.size();//number of formal parameters

	// Bytecode-side function entry — single write at the end when all
	// fields are settled (lVarCount + per-param info available only
	// after body compile). The ibByteFunction(long, const CompileFn&)
	// ctor copies all fields from the compile-side ibFunction.
	//
	// Lambdas land here too with kind = Lambda + synthetic name
	// "<lambda@<lAddress>>". m_listFunc lookups by name skip them
	// (`<` prefix isn't a valid identifier; IsCrossBcVisible() returns
	// false for Lambda kind so cross-bc resolvers ignore them).
	// FindFunctionByEntry sees them — that's what makes the debugger
	// stack and eval host detection work for anonymous bodies.
	// m_needsHeapFrame travels into byteFn via the templated ctor's
	// initializer list (src.m_needsHeapFrame → byteFn.m_needsHeapFrame).
	// Compile-side ibFunction was lazily stamped by GetVariable when
	// an inner lambda captured a local from this frame.
	ibByteCode::ibByteFunction byteFn(lAddress, *createdFunction);
	for (auto it : functionContext->m_listVariable) {
		if (it.second->m_bTempVar) continue;
		ibByteCode::ibByteCodeVarInfo info(*it.second);
		if (info.m_strRealName.IsEmpty())
			info.m_strRealName = it.first;
		byteFn.m_listLocals.push_back(std::move(info));
	}
	if (isLambda) {
		byteFn.m_kind        = ibFnKind::Lambda;
		byteFn.m_strRealName = wxString::Format(wxT("<lambda@%ld>"), lAddress);
	} else if (byteFn.m_strRealName.IsEmpty()) {
		byteFn.m_strRealName = createdFunction->m_strName;
	}

	m_cByteCode.m_listFunc.push_back(std::move(byteFn));

	return true;
}

ibParamUnit ibCompileCode::CompileLambdaExpression(ibCompileContext* context)
{

	// Parse signature into a context that parents into m_rootContext.
	// Closure capture (Phase A 2026-05-11+): m_parentContext rewired
	// below to the CALLER's context so the lambda body's GetVariable
	// can walk past the lambda boundary into outer fn locals (resolved
	// at depth ≥ 1 via existing m_pppArrayList chain layout —
	// the lambda boundary `break` in GetVariable was lifted at the
	// same time). Previously this line nullified m_parentContext,
	// enforcing the strict isolation discipline that has been
	// superseded by the per-frame heap-promotion design (see
	// docs/closure-capture.md). Runtime wiring still pending — Phase A
	// is compile-only; running code that captures outer locals crashes
	// until Phase B lands.
	std::shared_ptr<ibCompileContext::ibFunction> createdFunction;
	std::unique_ptr<ibCompileContext> functionContextOwner;
	int errorPlace = 0;

	if (!ParseFunctionSignature(m_rootContext, createdFunction,
		functionContextOwner, errorPlace)) {
		return ibParamUnit();
	}

	ibCompileContext* functionContext = functionContextOwner.get();
	functionContext->m_parentContext = context;

	// Closure capture marking is lazy — see ibCompileContext::GetVariable.
	// When an identifier resolves past a lambda boundary into some outer
	// ctx, GetVariable stamps that ctx's m_needsHeapFrame = true. No
	// eager walk here: ctxs that nothing captures from stay unmarked
	// regardless of how many lambdas they enclose.

	// L4-2 pushdown — the body's lexeme span starts right after the signature
	// (captured BEFORE EmitFunctionBody consumes it); the recorder re-reads
	// exactly this span once the body has compiled (see below).
	const size_t lambdaBodyFrom = m_numCurrentCompile + 1;

	// Emit OPER_LFUNC + params + body + OPER_ENDLFUNC inline.
	// EmitFunctionBody discriminates lambda vs named via
	// IsReturnLambda(functionContext->m_numReturn) — RETURN_LAMBDA_*
	// stamped by ParseFunctionSignature when the signature lookahead
	// saw `(` directly after Function/Procedure. Stamps:
	//   - createdFunction->m_nStart  = OPER_LFUNC IP
	//   - createdFunction->m_nFinish = OPER_ENDLFUNC IP
	// For lambdas, EmitFunctionBody does NOT push to m_listFunc —
	// lambda identity stays in the OPER_LFUNC operands themselves;
	// runtime BuildOrGetLambdaInfo derives the frame shape on first
	// fire.
	if (!EmitFunctionBody(context, createdFunction, functionContext)) {
		return ibParamUnit();
	}

	const long lfuncIp    = (long)createdFunction->m_nStart;
	const long endlfuncIp = (long)createdFunction->m_nFinish;

	// Allocate the dest slot in CALLER's context — that's where the
	// ibValueFunction value lands at runtime. The follow-on consumer
	// (OPER_LET / OPER_SET / OPER_CALL arg) reads from this slot.
	ibParamUnit target = context->CreateVariable();

	// Back-patch OPER_LFUNC's operands. Frame shape lives on the
	// just-pushed ibByteFunction in m_listFunc; we stamp the index here
	// so runtime can resolve the entry without a name lookup.
	//
	// Operand layout (OPER_LFUNC):
	//   m_param1                — dest slot for the resulting ibValueFunction
	//   m_param2.m_numIndex     — end IP (OPER_ENDLFUNC's position; used by
	//                              OPER_LFUNC handler at module-init walk
	//                              to skip past the body opcodes)
	//   m_param3.m_numIndex     — funcIndex into parentBc->m_listFunc
	//                              (frame shape, names, defaults — all
	//                              live there)
	const long funcIndex = (long)m_cByteCode.m_listFunc.size() - 1;
	ibByteUnit& lfuncCode = m_cByteCode.m_listCode[lfuncIp];
	lfuncCode.m_param1 = target;
	lfuncCode.m_param2.m_numIndex = endlfuncIp;
	lfuncCode.m_param3.m_numIndex = funcIndex;

	// L4-2 pushdown — record the just-compiled body as the L4 query AST when
	// it is a single-parameter, single-expression lambda in the translatable
	// subset (compiler/lambdaQueryAst.*). Null = the pipeline stays in
	// RAM; the recorder never fails the compile. The span [lambdaBodyFrom,
	// m_numCurrentCompile] covers everything EmitFunctionBody consumed,
	// including the closing fence the recorder tolerates.
	if (funcIndex >= 0 && createdFunction->m_listParam.size() == 1) {
		m_cByteCode.m_listFunc[funcIndex].m_lambdaExprAst = ibBuildLambdaQueryAst(
			m_listLexem, lambdaBodyFrom, m_numCurrentCompile + 1,
			createdFunction->m_listParam[0].m_strName);
	}

	// NOTE: ibParamUnit::m_numIndex is wxLongLong_t (8 bytes) — cast to int
	// for %d, otherwise variadic packs 8 bytes and the next arg (caller_ctx)
	// reads the upper half (zero) instead of the real pointer.

	return target;
}

// ============================================================
// LINQ block compile path — eager inline foreach + array build.
//
// Recognised at the start of any assignment RHS (CompileDeclaration's
// `=` branch hooks IsLinqBlockStart and routes here). Materialises
// the LINQ result into a temp Array slot which the caller's OPER_LET
// copies into the destination variable. Surface: `from <id> in <expr>`
// + where / let / skip / take / select / distinct / orderby / group /
// join (all extensions hang off the same shared compile-state struct).
//
// Diagnostics — previous LinqCompileLog/LinqLog `linq.log` streams
// were stripped 2026-05-12 after the LINQ surface stabilised. If
// compile-side tracing is needed again, prefer wxLogDebug at coarse
// entry points rather than per-row writes.
// ============================================================

// === LINQ compile-state types ===
// Definitions live in compileContextLinqData.h (included via
// compileContext.h) so unique_ptr<ibLinqContextData> on
// ibCompileContext can resolve to a complete type.

// Outer entry — consumes KEY_FROM itself, allocates the shared LINQ
// state on stack, sets up the RETURN_BLOCK-kind context, wires the
// back-pointer, and dives into CompileLinqBlock. Callers step back
// the lexem cursor (mirrors CompileLambdaExpression idiom) so this
// function owns the KEY_FROM consumption uniformly.
ibParamUnit ibCompileCode::CompileLinqExpression(ibCompileContext* context)
{
	GETKeyWord(KEY_FROM);

	// Allocate the fake LINQ context — child of caller, RETURN_BLOCK
	// kind so bindings register in this child's m_listVariable but
	// the actual slots land in the host frame via CreateVariable's
	// chain-delegation logic. LINQ-distinction is the non-null
	// m_linqData on this context; no RETURN_LINQ enum tag needed.
	auto linqCtxOwner = std::shared_ptr<ibCompileContext>(
		context->CreateContext(RETURN_BLOCK));
	linqCtxOwner->m_linqData = std::make_unique<ibLinqContextData>();
	ibLinqContextData& data = *linqCtxOwner->m_linqData;


	// Diagnostic: walk parent chain to see how far visibility reaches.
	int chainDepth = 0;
	for (ibCompileContext* c = linqCtxOwner->m_parentContext; c; c = c->m_parentContext) {
		++chainDepth;
		if (chainDepth > 10) break;
	}

	// Result accumulator + counters — allocate in CALLER's context
	// (not the linq scope) so slot references survive after the
	// linq context (and its m_linqData) destruct.
	data.m_resultArray = context->CreateVariable();
	{
		ibByteUnit c; AddLineInfo(c);
		c.m_numOper = OPER_NEW;
		c.m_param1 = data.m_resultArray;
		c.m_param2.m_numIndex = GetConstString(wxT("Array"));
		c.m_param2.m_numArray = 0;
		m_cByteCode.m_listCode.emplace_back(std::move(c));
	}

	data.m_skipCounter = context->CreateVariable();
	data.m_takeCounter = context->CreateVariable();
	data.m_constOne    = context->CreateVariable();
	{
		ibByteUnit c; AddLineInfo(c);
		c.m_numOper = OPER_CONSTN; c.m_param1 = data.m_skipCounter;
		c.m_param2.m_numIndex = 0;
		m_cByteCode.m_listCode.emplace_back(std::move(c));
	}
	{
		ibByteUnit c; AddLineInfo(c);
		c.m_numOper = OPER_CONSTN; c.m_param1 = data.m_takeCounter;
		c.m_param2.m_numIndex = 0;
		m_cByteCode.m_listCode.emplace_back(std::move(c));
	}
	{
		ibByteUnit c; AddLineInfo(c);
		c.m_numOper = OPER_CONSTN; c.m_param1 = data.m_constOne;
		c.m_param2.m_numIndex = 1;
		m_cByteCode.m_listCode.emplace_back(std::move(c));
	}

	// Allocate orderby parallel-keys array. Empty unless ORDERBY clause
	// fires inside CompileLinqBlock; in that case rows are pushed in
	// lock-step with __r.Add(addValue).
	data.m_orderKeys = context->CreateVariable();
	{
		ibByteUnit c; AddLineInfo(c);
		c.m_numOper = OPER_NEW;
		c.m_param1 = data.m_orderKeys;
		c.m_param2.m_numIndex = GetConstString(wxT("Array"));
		c.m_param2.m_numArray = 0;
		m_cByteCode.m_listCode.emplace_back(std::move(c));
	}

	CompileLinqBlock(linqCtxOwner.get());

	// Post-block GROUP BY expansion — runs ONCE after the outermost
	// foreach (and all its nested levels) exhausts. data.m_groupsContainer
	// has been populated by per-row Insert at whichever level the
	// `group X by K` keyword appeared.
	//
	// Two flavours:
	//   * Terminal `group X by K` (no `into`) — emit one
	//     `New Structure("Key, Values", pair.Key, pair.Value)` per
	//     pair directly into resultArray.
	//   * Non-terminal `group X by K into g` — open a new foreach
	//     over m_groupsContainer, bind `g` to the per-pair Structure,
	//     and re-enter CompileLinqBlock to parse the continuation
	//     clauses (where / select / orderby on g). The cursor is
	//     still positioned at the continuation's first token (outer
	//     CompileLinqBlock left it there).
	if (data.m_hasGroup) {

		const ibParamUnit expIn   = linqCtxOwner->GetVariable(wxT("@group_exp_in"),  true, false, false, true);
		const ibParamUnit expIt   = linqCtxOwner->GetVariable(wxT("@group_exp_it"),  true, false, false, true);
		const ibParamUnit expPair = linqCtxOwner->GetVariable(wxT("@group_exp_pair"), true, false, false, true);

		// @group_exp_in := groupsContainer.
		{
			ibByteUnit c; AddLineInfo(c);
			c.m_numOper = OPER_LET;
			c.m_param1 = expIn;
			c.m_param2 = data.m_groupsContainer;
			m_cByteCode.m_listCode.emplace_back(std::move(c));
		}

		if (data.m_hasGroupInto) {
			// Non-terminal: open new foreach, bind g to Structure(Key, Values),
			// re-enter CompileLinqBlock for the continuation clauses. The
			// continuation's leaf parser uses the same WHERE/SELECT/orderby/
			// distinct machinery as the original LINQ body, emits Add(addValue)
			// to data.m_resultArray, then NEXT_ITER + back-patches itself.

			ibLinqBinding bg;
			bg.name       = data.m_groupIntoName;
			bg.origin     = ibLinqBinding::FromGroup;
			bg.valueSlot  = linqCtxOwner->GetVariable(data.m_groupIntoName);
			bg.iterInSlot = expIn;
			bg.iterItSlot = expIt;

			// OPER_FOREACH header — iter-var is `expPair`, NOT g. g gets
			// derived from pair inside the body.
			const int expForeachIp = (int)m_cByteCode.m_listCode.size();
			{
				ibByteUnit c; AddLineInfo(c);
				c.m_numOper = OPER_FOREACH;
				c.m_param1 = expPair;
				c.m_param2 = expIn;
				c.m_param3 = expIt;
				m_cByteCode.m_listCode.emplace_back(std::move(c));
			}
			bg.foreachStartIp = expForeachIp;

			// Body prefix: extract pair.Key + pair.Value, build g = Structure.
			const ibParamUnit keySlot = context->CreateVariable();
			{
				ibByteUnit c; AddLineInfo(c);
				c.m_numOper = OPER_GET_A;
				c.m_param1 = keySlot;
				c.m_param2 = expPair;
				c.m_param3.m_numIndex = GetConstString(wxT("Key"));
				m_cByteCode.m_listCode.emplace_back(std::move(c));
			}
			const ibParamUnit valuesSlot = context->CreateVariable();
			{
				ibByteUnit c; AddLineInfo(c);
				c.m_numOper = OPER_GET_A;
				c.m_param1 = valuesSlot;
				c.m_param2 = expPair;
				c.m_param3.m_numIndex = GetConstString(wxT("Value"));
				m_cByteCode.m_listCode.emplace_back(std::move(c));
			}
			const ibParamUnit fieldsNameConst = FindConst(ibValue(wxT("Key, Values")));
			{
				ibByteUnit c; AddLineInfo(c);
				c.m_numOper = OPER_NEW;
				c.m_param1 = bg.valueSlot;
				c.m_param2.m_numIndex = GetConstString(wxT("Structure"));
				c.m_param2.m_numArray = 3;
				m_cByteCode.m_listCode.emplace_back(std::move(c));
			}
			{
				ibByteUnit c; AddLineInfo(c);
				c.m_numOper = OPER_SET;
				c.m_param1 = fieldsNameConst;
				m_cByteCode.m_listCode.emplace_back(std::move(c));
			}
			{
				ibByteUnit c; AddLineInfo(c);
				c.m_numOper = OPER_SET;
				c.m_param1 = keySlot;
				m_cByteCode.m_listCode.emplace_back(std::move(c));
			}
			{
				ibByteUnit c; AddLineInfo(c);
				c.m_numOper = OPER_SET;
				c.m_param1 = valuesSlot;
				m_cByteCode.m_listCode.emplace_back(std::move(c));
			}

			// Clear the hasGroup / hasGroupInto flags BEFORE re-entering —
			// CompileLinqBlock's leaf parser uses them to suppress SELECT /
			// Add; for the continuation we want those to fire normally
			// against g. m_groupsContainer slot stays valid (foreach reads
			// it via expIn).
			data.m_hasGroup     = false;
			data.m_hasGroupInto = false;

			// Re-enter CompileLinqBlock with the synthetic binding.
			CompileLinqBlock(linqCtxOwner.get(), bg);
		}
		else {
			// Terminal: emit one Structure{Key, Values} per pair → __r.Add.
			const int expForeachIp = (int)m_cByteCode.m_listCode.size();
			{
				ibByteUnit c; AddLineInfo(c);
				c.m_numOper = OPER_FOREACH;
				c.m_param1 = expPair;
				c.m_param2 = expIn;
				c.m_param3 = expIt;
				m_cByteCode.m_listCode.emplace_back(std::move(c));
			}

			const ibParamUnit keySlot = context->CreateVariable();
			{
				ibByteUnit c; AddLineInfo(c);
				c.m_numOper = OPER_GET_A;
				c.m_param1 = keySlot;
				c.m_param2 = expPair;
				c.m_param3.m_numIndex = GetConstString(wxT("Key"));
				m_cByteCode.m_listCode.emplace_back(std::move(c));
			}
			const ibParamUnit valuesSlot = context->CreateVariable();
			{
				ibByteUnit c; AddLineInfo(c);
				c.m_numOper = OPER_GET_A;
				c.m_param1 = valuesSlot;
				c.m_param2 = expPair;
				c.m_param3.m_numIndex = GetConstString(wxT("Value"));
				m_cByteCode.m_listCode.emplace_back(std::move(c));
			}

			const ibParamUnit groupRow         = context->CreateVariable();
			const ibParamUnit fieldsNameConst  = FindConst(ibValue(wxT("Key, Values")));
			{
				ibByteUnit c; AddLineInfo(c);
				c.m_numOper = OPER_NEW;
				c.m_param1 = groupRow;
				c.m_param2.m_numIndex = GetConstString(wxT("Structure"));
				c.m_param2.m_numArray = 3;
				m_cByteCode.m_listCode.emplace_back(std::move(c));
			}
			{
				ibByteUnit c; AddLineInfo(c);
				c.m_numOper = OPER_SET;
				c.m_param1 = fieldsNameConst;
				m_cByteCode.m_listCode.emplace_back(std::move(c));
			}
			{
				ibByteUnit c; AddLineInfo(c);
				c.m_numOper = OPER_SET;
				c.m_param1 = keySlot;
				m_cByteCode.m_listCode.emplace_back(std::move(c));
			}
			{
				ibByteUnit c; AddLineInfo(c);
				c.m_numOper = OPER_SET;
				c.m_param1 = valuesSlot;
				m_cByteCode.m_listCode.emplace_back(std::move(c));
			}

			{
				ibByteUnit c; AddLineInfo(c);
				c.m_numOper = OPER_CALL_METHOD;
				c.m_param1 = context->CreateVariable();
				c.m_param2 = data.m_resultArray;
				c.m_param3.m_numIndex = GetConstString(wxT("Add"));
				c.m_param3.m_numArray = 1;
				m_cByteCode.m_listCode.emplace_back(std::move(c));
			}
			{
				ibByteUnit c; AddLineInfo(c);
				c.m_numOper = OPER_SET;
				c.m_param1 = groupRow;
				m_cByteCode.m_listCode.emplace_back(std::move(c));
			}

			{
				ibByteUnit c; AddLineInfo(c);
				c.m_numOper = OPER_NEXT_ITER;
				c.m_param1 = expIt;
				c.m_param2.m_numIndex = expForeachIp;
				m_cByteCode.m_listCode.emplace_back(std::move(c));
			}
			m_cByteCode.m_listCode[expForeachIp].m_param4.m_numIndex =
				(long)m_cByteCode.m_listCode.size();
		}
	}

	// Post-loop ORDERBY emit — resultArray.SortByKeys(orderKeys, descending).
	// Sits AFTER all OPER_NEXT_ITER's (CompileLinqBlock's tail) so the
	// arrays are fully populated when sort runs.
	if (data.m_hasOrderBy) {
		ibParamUnit descConst = context->CreateVariable();
		{
			ibByteUnit c; AddLineInfo(c);
			c.m_numOper = OPER_CONSTN;
			c.m_param1 = descConst;
			c.m_param2.m_numIndex = data.m_orderByDescending ? 1 : 0;
			m_cByteCode.m_listCode.emplace_back(std::move(c));
		}
		// __r.SortByKeys(__keys, descending) — OPER_CALL_METHOD + 2 args.
		{
			ibByteUnit c; AddLineInfo(c);
			c.m_numOper = OPER_CALL_METHOD;
			c.m_param1 = context->CreateVariable();   // throwaway ret
			c.m_param2 = data.m_resultArray;
			c.m_param3.m_numIndex = GetConstString(wxT("SortByKeys"));
			c.m_param3.m_numArray = 2;
			m_cByteCode.m_listCode.emplace_back(std::move(c));
		}
		{
			ibByteUnit c; AddLineInfo(c);
			c.m_numOper = OPER_SET;
			c.m_param1 = data.m_orderKeys;
			m_cByteCode.m_listCode.emplace_back(std::move(c));
		}
		{
			ibByteUnit c; AddLineInfo(c);
			c.m_numOper = OPER_SET;
			c.m_param1 = descConst;
			m_cByteCode.m_listCode.emplace_back(std::move(c));
		}
	}

	const ibParamUnit resultSlot = data.m_resultArray;   // capture before scope ends
	return resultSlot;
}

// Recursive worker — one `from <id> in <expr>` clause plus either
// (a) recursive call to itself for the next `from` (nested foreach),
// or (b) tail clauses (where/skip/take/select) + Add at the deepest
// level. Each level emits its own OPER_FOREACH header on entry and
// matching OPER_NEXT_ITER + back-patches on unwind. Works for any
// nesting depth — single `from`, multi-from chain, or a tree once
// let/join/group/into bind new variables at branch points.
//
// Caller (CompileLinqExpression for outermost, or self-recursive
// call) has already consumed KEY_FROM and we start with the binding
// name.
void ibCompileCode::CompileLinqBlock(ibCompileContext* linqCtx)
{
	// linqCtx — the fake LINQ context (RETURN_BLOCK kind with non-null
	// m_linqData as the LINQ marker, allocated once by
	// CompileLinqExpression). All binding registration +
	// expression compilation goes through it; lookups walk parent
	// chain so outer-scope locals stay visible (closure capture).
	// State (m_bindings, m_resultArray, counters, ...) lives on
	// linqCtx->m_linqData — the back-pointer at the stack-allocated
	// ibLinqContextData in CompileLinqExpression.
	ibCompileContext* const context = linqCtx;

	const wxString bindRealName = GETIdentifier(true);
	const wxString bindUpper    = stringUtils::MakeUpper(bindRealName);
	GETKeyWord(KEY_IN);

	ibLinqBinding b;
	b.name       = bindRealName;
	b.origin     = ibLinqBinding::FromSource;
	b.valueSlot  = context->GetVariable(bindRealName);
	b.iterInSlot = context->GetVariable(bindUpper + wxT("@in_"), true, false, false, true);
	b.iterItSlot = context->GetVariable(bindUpper + wxT("@it_"), true, false, false, true);

	// OPER_LET @in_ := source-expression
	{
		const ibParamUnit srcSlot = GetExpression(context);
		ibByteUnit c; AddLineInfo(c);
		c.m_numOper = OPER_LET;
		c.m_param1 = b.iterInSlot;
		c.m_param2 = srcSlot;
		m_cByteCode.m_listCode.emplace_back(std::move(c));
	}

	// OPER_FOREACH header
	{
		ibByteUnit c; AddLineInfo(c);
		c.m_numOper = OPER_FOREACH;
		c.m_param1 = b.valueSlot;
		c.m_param2 = b.iterInSlot;
		c.m_param3 = b.iterItSlot;
		m_cByteCode.m_listCode.emplace_back(std::move(c));
		b.foreachStartIp = (int)m_cByteCode.m_listCode.size() - 1;
	}

	// Delegate to the pre-bound entry (which assumes OPER_LET +
	// OPER_FOREACH already emitted and `b` fully populated).
	CompileLinqBlock(linqCtx, b);
}

// Pre-bound overload — caller has emitted OPER_LET / OPER_FOREACH and
// filled `preBound` (incl. foreachStartIp). Used by `group ... into g`
// continuation to re-enter the leaf-clause / NEXT_ITER machinery with
// `g` as a synthetic binding over m_groupsContainer's pair rows.
void ibCompileCode::CompileLinqBlock(ibCompileContext* linqCtx, const ibLinqBinding& preBound)
{
	ibLinqContextData& data = *linqCtx->m_linqData;
	ibCompileContext* const context = linqCtx;

	// Per-from-level state — local to THIS CompileLinqBlock call so
	// recursion (nested from) doesn't pollute outer levels. Each
	// level owns its own pending join trampolines (emitted at THIS
	// level's NEXT_ITER, absolute-ip GOTO from body requires same
	// scope). GROUP's data.m_hasGroup + data.m_groupsContainer are linq-scope
	// (data.m_*) — expansion fires once after the outermost
	// CompileLinqBlock returns, in CompileLinqExpression.
	std::vector<ibLinqPendingJoin> pendingJoins;

	ibLinqBinding b = preBound;
	data.m_bindings.push_back(b);
	context->StartLoopList();


	// Multiple WHERE clauses are allowed (each emits its own OPER_IF);
	// all skip-targets back-patch to the same post-Add ip below.
	// JOIN-miss OPER_IFs also push here — single "skip current row,
	// continue iter" patch point for both clause kinds.
	std::vector<int> whereSkipIps;

	// JOIN clauses appear after `from`, before tail clauses (C# grammar).
	// Each emits per-iter lookup inline in outer body + records an
	// ibLinqPendingJoin for the trampoline (hash build) emitted after
	// outer NEXT_ITER. Multiple joins at the same level are allowed.
	// The "if !found, skip-to-next-iter" OPER_IF goes into whereSkipIps
	// directly so end-of-body patches both clause kinds uniformly.
	while (IsNextKeyWord(KEY_JOIN)) {
		GETKeyWord(KEY_JOIN);
		CompileLinqJoin(context, whereSkipIps, pendingJoins);
	}

	if (IsNextKeyWord(KEY_FROM)) {
		// === Recurse — nested `from` adds another foreach depth ===
		GETKeyWord(KEY_FROM);
		CompileLinqBlock(context);
	}
	else {
		// === Innermost level — process tail clauses + Add ===
		// WHERE and let-clauses (`var alias = ...` or implicit
		// `alias = ...`) can interleave freely between `from` and
		// `select` / `skip` / `take`. Unified while-loop handles any
		// order, matching C# LINQ grammar.
		while (true) {
			// WHERE clause
			if (IsNextKeyWord(KEY_WHERE)) {
				GETKeyWord(KEY_WHERE);
				const ibParamUnit whereExpr = GetExpression(context);
				ibByteUnit c; AddLineInfo(c);
				c.m_numOper = OPER_IF;
				c.m_param1 = whereExpr;
				c.m_param2.m_numIndex = 0;  // back-patched below
				m_cByteCode.m_listCode.emplace_back(std::move(c));
				whereSkipIps.push_back((int)m_cByteCode.m_listCode.size() - 1);
				continue;
			}

			// ORDERBY clause — compile key expression per-row, optional
			// ASCENDING/DESCENDING. The expression's natural result slot
			// may carry DEF_VAR_TEMP marker (for compound expressions like
			// `o * -1`) which can interact badly with subsequent
			// OPER_SET arg loads. Copy into a stable regular slot via
			// OPER_LET so __keys.Add reads it as a normal variable.
			if (IsNextKeyWord(KEY_ORDERBY)) {
				GETKeyWord(KEY_ORDERBY);
				const ibParamUnit rawKey = GetExpression(context);

				// Stable copy — allocate regular slot, OPER_LET copies
				// the per-iteration expression result into it. This slot
				// is what __keys.Add reads further down.
				data.m_orderByKeySlot = context->CreateVariable();
				{
					ibByteUnit c; AddLineInfo(c);
					c.m_numOper = OPER_LET;
					c.m_param1 = data.m_orderByKeySlot;
					c.m_param2 = rawKey;
					m_cByteCode.m_listCode.emplace_back(std::move(c));
				}
				data.m_hasOrderBy = true;
				if (IsNextKeyWord(KEY_DESCENDING)) {
					GETKeyWord(KEY_DESCENDING);
					data.m_orderByDescending = true;
				} else if (IsNextKeyWord(KEY_ASCENDING)) {
					GETKeyWord(KEY_ASCENDING);
					data.m_orderByDescending = false;
				}
				continue;
			}

			// Let-clause — `var alias = <expr>` or implicit `alias = <expr>`.
			// Both emit OPER_LET + push FromLet binding so subsequent
			// clauses see `alias` via standard context lookup.
			bool hasExplicitKw = false;
			if (IsNextKeyWord(KEY_VAR)) {
				GETKeyWord(KEY_VAR);
				hasExplicitKw = true;
			}
			else {
				// Implicit — 2-token peek <IDENTIFIER> '='. Identifier
				// must not be a clause-terminator keyword (those are
				// caught by SKIP/TAKE/SELECT branches below and never
				// reach this peek).
				const size_t pos = (size_t)m_numCurrentCompile;
				if (pos + 1 >= m_listLexem.size()) break;
				const ibLexem& l0 = m_listLexem[pos];
				const ibLexem& l1 = m_listLexem[pos + 1];
				if (l0.m_lexType != IDENTIFIER) break;
				if (l1.m_lexType != DELIMITER || l1.m_numData != '=') break;
			}

			const wxString aliasReal = GETIdentifier(true);
			GETDelimeter('=');
			const ibParamUnit aliasSlot = context->GetVariable(aliasReal);
			{
				ibByteUnit c; AddLineInfo(c);
				c.m_numOper = OPER_LET;
				c.m_param1 = aliasSlot;
				c.m_param2 = GetExpression(context);
				m_cByteCode.m_listCode.emplace_back(std::move(c));
			}
			ibLinqBinding lb;
			lb.name      = aliasReal;
			lb.origin    = ibLinqBinding::FromLet;
			lb.valueSlot = aliasSlot;
			data.m_bindings.push_back(lb);
		}

		// SKIP — counter++, if (counter <= N) goto next-iter.
		if (IsNextKeyWord(KEY_SKIP)) {
			GETKeyWord(KEY_SKIP);
			const ibParamUnit skipExpr = GetExpression(context);
			{
				ibByteUnit c; AddLineInfo(c);
				c.m_numOper = OPER_ADD;
				c.m_param1 = data.m_skipCounter;
				c.m_param2 = data.m_skipCounter;
				c.m_param3 = data.m_constOne;
				m_cByteCode.m_listCode.emplace_back(std::move(c));
			}
			const ibParamUnit tmpLE = context->CreateVariable();
			{
				ibByteUnit c; AddLineInfo(c);
				c.m_numOper = OPER_LE;
				c.m_param1 = tmpLE;
				c.m_param2 = data.m_skipCounter;
				c.m_param3 = skipExpr;
				m_cByteCode.m_listCode.emplace_back(std::move(c));
			}
			{
				ibByteUnit c; AddLineInfo(c);
				c.m_numOper = OPER_IF;
				c.m_param1 = tmpLE;
				c.m_param2.m_numIndex = 0;
				m_cByteCode.m_listCode.emplace_back(std::move(c));
			}
			const int skipIfIp = (int)m_cByteCode.m_listCode.size() - 1;
			{
				ibByteUnit c; AddLineInfo(c);
				c.m_numOper = OPER_GOTO;
				m_cByteCode.m_listCode.emplace_back(std::move(c));
				const int gotoIp = (int)m_cByteCode.m_listCode.size() - 1;
				auto* pList = context->m_listContinue[context->m_numDoNumber];
				if (pList != nullptr) pList->emplace_back(gotoIp);
			}
			m_cByteCode.m_listCode[skipIfIp].m_param2.m_numIndex =
				(long)m_cByteCode.m_listCode.size();
		}

		// TAKE — if (counter >= N) goto break-out; else counter++.
		if (IsNextKeyWord(KEY_TAKE)) {
			GETKeyWord(KEY_TAKE);
			const ibParamUnit takeExpr = GetExpression(context);
			const ibParamUnit tmpGE = context->CreateVariable();
			{
				ibByteUnit c; AddLineInfo(c);
				c.m_numOper = OPER_GE;
				c.m_param1 = tmpGE;
				c.m_param2 = data.m_takeCounter;
				c.m_param3 = takeExpr;
				m_cByteCode.m_listCode.emplace_back(std::move(c));
			}
			{
				ibByteUnit c; AddLineInfo(c);
				c.m_numOper = OPER_IF;
				c.m_param1 = tmpGE;
				c.m_param2.m_numIndex = 0;
				m_cByteCode.m_listCode.emplace_back(std::move(c));
			}
			const int takeIfIp = (int)m_cByteCode.m_listCode.size() - 1;
			{
				ibByteUnit c; AddLineInfo(c);
				c.m_numOper = OPER_GOTO;
				m_cByteCode.m_listCode.emplace_back(std::move(c));
				const int gotoIp = (int)m_cByteCode.m_listCode.size() - 1;
				auto* pList = context->m_listBreak[context->m_numDoNumber];
				if (pList != nullptr) pList->emplace_back(gotoIp);
			}
			m_cByteCode.m_listCode[takeIfIp].m_param2.m_numIndex =
				(long)m_cByteCode.m_listCode.size();
			{
				ibByteUnit c; AddLineInfo(c);
				c.m_numOper = OPER_ADD;
				c.m_param1 = data.m_takeCounter;
				c.m_param2 = data.m_takeCounter;
				c.m_param3 = data.m_constOne;
				m_cByteCode.m_listCode.emplace_back(std::move(c));
			}
		}

		// GROUP — terminal projection: `group X by K`. Mutually exclusive
		// with SELECT at this level. Aggregates rows into a Container
		// (key → Array bucket) via per-row lookup-or-create. Post-loop
		// (after outer NEXT_ITER, before foreach m_param4 patch), an
		// expansion foreach walks the Container and pushes one
		// `New Structure("Key, Values", pair.Key, pair.Value)` into
		// __r — result is Array<Structure{Key, Values:Array}>. WHERE /
		// ORDERBY before group operate on raw rows (filter / sort before
		// grouping). No `into` form yet — group is terminal, no further
		// clauses operating on groups.
		if (IsNextKeyWord(KEY_GROUP)) {
			GETKeyWord(KEY_GROUP);
			data.m_hasGroup = true;

			// Allocate persistent slots (in caller's context via
			// CreateVariable's RETURN_BLOCK chain-delegation).
			data.m_groupsContainer = context->CreateVariable();

			// First-iter init guard — mirror of JOIN's hashSlot init.
			// `tmp_isEmpty = !data.m_groupsContainer` (untyped OPER_NOT →
			// IsEmpty path: TYPE_EMPTY → true, TYPE_VALUE → false).
			// OPER_IF jumps when condition is FALSE (Container non-empty);
			// fall through emits OPER_NEW once on first iter.
			const ibParamUnit tmpIsEmpty = context->CreateVariable();
			{
				ibByteUnit c; AddLineInfo(c);
				c.m_numOper = OPER_NOT;
				c.m_param1 = tmpIsEmpty;
				c.m_param2 = data.m_groupsContainer;
				m_cByteCode.m_listCode.emplace_back(std::move(c));
			}
			const int initSkipIfIp = (int)m_cByteCode.m_listCode.size();
			{
				ibByteUnit c; AddLineInfo(c);
				c.m_numOper = OPER_IF;
				c.m_param1 = tmpIsEmpty;
				c.m_param2.m_numIndex = 0;  // back-patched after OPER_NEW
				m_cByteCode.m_listCode.emplace_back(std::move(c));
			}
			{
				ibByteUnit c; AddLineInfo(c);
				c.m_numOper = OPER_NEW;
				c.m_param1 = data.m_groupsContainer;
				c.m_param2.m_numIndex = GetConstString(wxT("Container"));
				c.m_param2.m_numArray = 0;
				m_cByteCode.m_listCode.emplace_back(std::move(c));
			}
			m_cByteCode.m_listCode[initSkipIfIp].m_param2.m_numIndex =
				(long)m_cByteCode.m_listCode.size();

			// Parse X (value to group) — eval inline per iter.
			const ibParamUnit groupValueSlot = GetExpression(context);

			GETKeyWord(KEY_BY);

			// Parse K (group key) — eval inline per iter.
			const ibParamUnit groupKeySlot = GetExpression(context);

			// Per-row lookup-or-create:
			//   bucketSlot = (uninit)
			//   found = data.m_groupsContainer.Property(K, bucketSlot)
			//   OPER_IF found, skipCreate     ← jump if found
			//   bucketSlot = New("Array")
			//   data.m_groupsContainer.Insert(K, bucketSlot)
			//   skipCreate:
			//   bucketSlot.Add(X)
			const ibParamUnit bucketSlot = context->CreateVariable();
			const ibParamUnit foundSlot  = context->CreateVariable();

			// found = data.m_groupsContainer.Property(K, bucketSlot)
			{
				ibByteUnit c; AddLineInfo(c);
				c.m_numOper = OPER_CALL_METHOD;
				c.m_param1 = foundSlot;
				c.m_param2 = data.m_groupsContainer;
				c.m_param3.m_numIndex = GetConstString(wxT("Property"));
				c.m_param3.m_numArray = 2;
				m_cByteCode.m_listCode.emplace_back(std::move(c));
			}
			{
				ibByteUnit c; AddLineInfo(c);
				c.m_numOper = OPER_SET;
				c.m_param1 = groupKeySlot;
				m_cByteCode.m_listCode.emplace_back(std::move(c));
			}
			{
				ibByteUnit c; AddLineInfo(c);
				c.m_numOper = OPER_SET;
				c.m_param1 = bucketSlot;
				m_cByteCode.m_listCode.emplace_back(std::move(c));
			}
			// OPER_IF jumps on EMPTY/falsy condition — natural for
			// "if !cond, skip body" patterns. We want the inverse here:
			// "if found, skip create". Invert via OPER_NOT so OPER_IF
			// jumps when foundSlot is TRUE (notFound is empty/false).
			const ibParamUnit notFoundSlot = context->CreateVariable();
			{
				ibByteUnit c; AddLineInfo(c);
				c.m_numOper = OPER_NOT;
				c.m_param1 = notFoundSlot;
				c.m_param2 = foundSlot;
				m_cByteCode.m_listCode.emplace_back(std::move(c));
			}
			const int createSkipIfIp = (int)m_cByteCode.m_listCode.size();
			{
				ibByteUnit c; AddLineInfo(c);
				c.m_numOper = OPER_IF;
				c.m_param1 = notFoundSlot;
				c.m_param2.m_numIndex = 0;   // back-patched below
				m_cByteCode.m_listCode.emplace_back(std::move(c));
			}
			// Create branch: bucketSlot = New("Array"); data.m_groupsContainer.Insert(K, bucketSlot).
			{
				ibByteUnit c; AddLineInfo(c);
				c.m_numOper = OPER_NEW;
				c.m_param1 = bucketSlot;
				c.m_param2.m_numIndex = GetConstString(wxT("Array"));
				c.m_param2.m_numArray = 0;
				m_cByteCode.m_listCode.emplace_back(std::move(c));
			}
			{
				ibByteUnit c; AddLineInfo(c);
				c.m_numOper = OPER_CALL_METHOD;
				c.m_param1 = context->CreateVariable();  // throwaway ret
				c.m_param2 = data.m_groupsContainer;
				c.m_param3.m_numIndex = GetConstString(wxT("Insert"));
				c.m_param3.m_numArray = 2;
				m_cByteCode.m_listCode.emplace_back(std::move(c));
			}
			{
				ibByteUnit c; AddLineInfo(c);
				c.m_numOper = OPER_SET;
				c.m_param1 = groupKeySlot;
				m_cByteCode.m_listCode.emplace_back(std::move(c));
			}
			{
				ibByteUnit c; AddLineInfo(c);
				c.m_numOper = OPER_SET;
				c.m_param1 = bucketSlot;
				m_cByteCode.m_listCode.emplace_back(std::move(c));
			}
			// skipCreate label.
			m_cByteCode.m_listCode[createSkipIfIp].m_param2.m_numIndex =
				(long)m_cByteCode.m_listCode.size();

			// bucketSlot.Add(X) — append the per-row value to the bucket.
			{
				ibByteUnit c; AddLineInfo(c);
				c.m_numOper = OPER_CALL_METHOD;
				c.m_param1 = context->CreateVariable();   // throwaway ret
				c.m_param2 = bucketSlot;
				c.m_param3.m_numIndex = GetConstString(wxT("Add"));
				c.m_param3.m_numArray = 1;
				m_cByteCode.m_listCode.emplace_back(std::move(c));
			}
			{
				ibByteUnit c; AddLineInfo(c);
				c.m_numOper = OPER_SET;
				c.m_param1 = groupValueSlot;
				m_cByteCode.m_listCode.emplace_back(std::move(c));
			}

			// `... into <g>` — non-terminal group. CompileLinqExpression's
			// post-block expansion will detect m_hasGroupInto, open a new
			// foreach over m_groupsContainer, bind <g> to Structure{Key,
			// Values} per pair, and re-enter CompileLinqBlock for the
			// continuation clauses. We do NOT parse the continuation here
			// — outer level's leaf parser falls through (hasGroup=true
			// suppresses SELECT/Add); cursor stays at the continuation's
			// first token, where post-block picks it up.
			if (IsNextKeyWord(KEY_INTO)) {
				GETKeyWord(KEY_INTO);
				data.m_groupIntoName = GETIdentifier(true);
				data.m_hasGroupInto  = true;
			}
		}

		// SELECT — projection. Three forms:
		//   select <expr>                          → single value
		//   select { name = expr, name = expr }    → anonymous row (Structure)
		//   <omitted>                              → implicit, addValue = binding
		// For the `{}` form we emit New("Structure") + a chain of
		// .Insert(name, value) calls; the structure slot becomes addValue.
		// Skipped entirely when GROUP BY took the projection slot.
		ibParamUnit addValue = b.valueSlot;
		if (!data.m_hasGroup && IsNextKeyWord(KEY_SELECT)) {
			GETKeyWord(KEY_SELECT);

			if (IsNextDelimeter('{')) {
				GETDelimeter('{');

				const ibParamUnit structSlot = context->CreateVariable();
				// structSlot := New("Structure")
				{
					ibByteUnit c; AddLineInfo(c);
					c.m_numOper = OPER_NEW;
					c.m_param1 = structSlot;
					c.m_param2.m_numIndex = GetConstString(wxT("Structure"));
					c.m_param2.m_numArray = 0;
					m_cByteCode.m_listCode.emplace_back(std::move(c));
				}

				// Field list — `name = expr` pairs separated by `,`.
				int fieldCount = 0;
				while (!IsNextDelimeter('}')) {
					const wxString fieldName = GETIdentifier(true);
					GETDelimeter('=');
					const ibParamUnit fieldValue = GetExpression(context);

					// structSlot.Insert(fieldName, fieldValue)
					// OPER_CALL_METHOD + 2 arg ops (SETCONST for name, SET for value).
					{
						ibByteUnit c; AddLineInfo(c);
						c.m_numOper = OPER_CALL_METHOD;
						c.m_param1 = context->CreateVariable();  // throwaway ret
						c.m_param2 = structSlot;
						c.m_param3.m_numIndex = GetConstString(wxT("Insert"));
						c.m_param3.m_numArray = 2;
						m_cByteCode.m_listCode.emplace_back(std::move(c));
					}
					// arg 1: field name as string constant
					{
						ibByteUnit c; AddLineInfo(c);
						c.m_numOper = OPER_SETCONST;
						c.m_param1.m_numIndex = GetConstString(fieldName);
						m_cByteCode.m_listCode.emplace_back(std::move(c));
					}
					// arg 2: field value
					{
						ibByteUnit c; AddLineInfo(c);
						c.m_numOper = OPER_SET;
						c.m_param1 = fieldValue;
						m_cByteCode.m_listCode.emplace_back(std::move(c));
					}

					++fieldCount;
					if (IsNextDelimeter(',')) GETDelimeter(',');
				}
				GETDelimeter('}');

				addValue = structSlot;
			}
			else {
				addValue = GetExpression(context);
			}
		}

		// DISTINCT modifier — `distinct` after select. Dedupes addValue
		// against already-Added rows via __r.Contains (bool). Earlier
		// Find/NOT approach broke when Find returned index 0 because
		// IsEmpty(NUMBER 0) is true (treats 0 as falsy). Contains
		// returns TYPE_BOOLEAN unambiguously: true=found, false=not.
		// shouldAdd = NOT Contains works correctly: IsEmpty(bool true)
		// is false → NOT true bool = false → IF FALSY → skip Add.
		int distinctSkipIp = -1;
		if (IsNextKeyWord(KEY_DISTINCT)) {
			GETKeyWord(KEY_DISTINCT);

			// tmpHas := __r.Contains(addValue)
			const ibParamUnit tmpHas = context->CreateVariable();
			{
				ibByteUnit c; AddLineInfo(c);
				c.m_numOper = OPER_CALL_METHOD;
				c.m_param1 = tmpHas;
				c.m_param2 = data.m_resultArray;
				c.m_param3.m_numIndex = GetConstString(wxT("Contains"));
				c.m_param3.m_numArray = 1;
				m_cByteCode.m_listCode.emplace_back(std::move(c));
			}
			{
				ibByteUnit c; AddLineInfo(c);
				c.m_numOper = OPER_SET;
				c.m_param1 = addValue;
				m_cByteCode.m_listCode.emplace_back(std::move(c));
			}
			// shouldAdd := NOT tmpHas  (true iff not present)
			const ibParamUnit shouldAdd = context->CreateVariable();
			{
				ibByteUnit c; AddLineInfo(c);
				c.m_numOper = OPER_NOT;
				c.m_param1 = shouldAdd;
				c.m_param2 = tmpHas;
				m_cByteCode.m_listCode.emplace_back(std::move(c));
			}
			// IF shouldAdd, skipAddIp  — jumps past Add when found.
			{
				ibByteUnit c; AddLineInfo(c);
				c.m_numOper = OPER_IF;
				c.m_param1 = shouldAdd;
				c.m_param2.m_numIndex = 0;  // back-patch below
				m_cByteCode.m_listCode.emplace_back(std::move(c));
			}
			distinctSkipIp = (int)m_cByteCode.m_listCode.size() - 1;
		}

		// Body: resultArray.Add(addValue).
		// Suppressed when GROUP BY took over — group-by emitted its own
		// per-row aggregation (lookup-or-create bucket + bucket.Add(X))
		// inline above; the result array gets populated post-loop by
		// the group expansion (foreach pair → __r.Add(New Structure)).
		if (!data.m_hasGroup) {
			{
				ibByteUnit c; AddLineInfo(c);
				c.m_numOper = OPER_CALL_METHOD;
				c.m_param1 = context->CreateVariable();   // throwaway ret slot
				c.m_param2 = data.m_resultArray;
				c.m_param3.m_numIndex = GetConstString(wxT("Add"));
				c.m_param3.m_numArray = 1;
				m_cByteCode.m_listCode.emplace_back(std::move(c));
			}
			{
				ibByteUnit c; AddLineInfo(c);
				c.m_numOper = OPER_SET;
				c.m_param1 = addValue;
				m_cByteCode.m_listCode.emplace_back(std::move(c));
			}
		}

		// Body: orderKeys.Add(orderByKeySlot) — lock-step with __r.Add.
		// Same skip-path: distinct/where back-patches land past both.
		if (data.m_hasOrderBy) {
			ibByteUnit c; AddLineInfo(c);
			c.m_numOper = OPER_CALL_METHOD;
			c.m_param1 = context->CreateVariable();   // throwaway ret slot
			c.m_param2 = data.m_orderKeys;
			c.m_param3.m_numIndex = GetConstString(wxT("Add"));
			c.m_param3.m_numArray = 1;
			m_cByteCode.m_listCode.emplace_back(std::move(c));

			ibByteUnit setC; AddLineInfo(setC);
			setC.m_numOper = OPER_SET;
			setC.m_param1 = data.m_orderByKeySlot;
			m_cByteCode.m_listCode.emplace_back(std::move(setC));
		}

		// Back-patch DISTINCT skip target → past the Add (= NEXT_ITER).
		if (distinctSkipIp >= 0) {
			m_cByteCode.m_listCode[distinctSkipIp].m_param2.m_numIndex =
				(long)m_cByteCode.m_listCode.size();
		}

		// Back-patch all WHERE skip targets → past the Add (= NEXT_ITER).
		for (const int ifIp : whereSkipIps) {
			m_cByteCode.m_listCode[ifIp].m_param2.m_numIndex =
				(long)m_cByteCode.m_listCode.size();
		}
	}

	// === Close this level's loop ===
	{
		ibByteUnit c; AddLineInfo(c);
		c.m_numOper = OPER_NEXT_ITER;
		c.m_param1 = b.iterItSlot;
		c.m_param2.m_numIndex = b.foreachStartIp;
		m_cByteCode.m_listCode.emplace_back(std::move(c));
	}

	// === JOIN trampolines (Phase 1.5) ===
	// Live AFTER outer NEXT_ITER and BEFORE the foreach m_param4 patch
	// (= loop-exit target). NEXT_ITER always jumps back (never falls
	// through), so trampolines are unreachable in normal flow. On loop
	// exhaustion, OPER_FOREACH's m_param4 = post-trampolines ip, so
	// they're skipped on exit too. Reached only via the per-iter
	// conditional OPER_GOTO placeholder emitted inside the body by
	// CompileLinqJoin — fires once on first iter, the trampoline
	// builds the hash + jumps back to that join's skip_label.
	for (const ibLinqPendingJoin& pj : pendingJoins) {
		const int trampolineLabel = (int)m_cByteCode.m_listCode.size();

		// Patch the body-side placeholder GOTO to land here.
		m_cByteCode.m_listCode[pj.placeholderGotoIp].m_param1.m_numIndex =
			trampolineLabel;

		// hashSlot = New("Container") — first-iter init of the dict.
		{
			ibByteUnit c; AddLineInfo(c);
			c.m_numOper = OPER_NEW;
			c.m_param1 = pj.hashSlot;
			c.m_param2.m_numIndex = GetConstString(wxT("Container"));
			c.m_param2.m_numArray = 0;
			m_cByteCode.m_listCode.emplace_back(std::move(c));
		}

		// Replay T's lex range — eval inner source ONCE into a temp.
		const int savedCursorT = m_numCurrentCompile;
		m_numCurrentCompile = pj.tLexStart;
		const ibParamUnit innerSrcSlot = GetExpression(linqCtx);
		m_numCurrentCompile = savedCursorT;

		// Inner foreach: foreach pj.bindSlot in innerSrcSlot.
		// We reuse pj.bindSlot as the inner iter-var; after hash build
		// it gets overwritten per outer-iter via Property's out-param.
		const ibParamUnit innerInSlot =
			linqCtx->GetVariable(wxString::Format(wxT("@join_in_%d"),
				(int)pendingJoins.size()), true, false, false, true);
		const ibParamUnit innerItSlot =
			linqCtx->GetVariable(wxString::Format(wxT("@join_it_%d"),
				(int)pendingJoins.size()), true, false, false, true);
		{
			ibByteUnit c; AddLineInfo(c);
			c.m_numOper = OPER_LET;
			c.m_param1 = innerInSlot;
			c.m_param2 = innerSrcSlot;
			m_cByteCode.m_listCode.emplace_back(std::move(c));
		}
		const int innerForeachIp = (int)m_cByteCode.m_listCode.size();
		{
			ibByteUnit c; AddLineInfo(c);
			c.m_numOper = OPER_FOREACH;
			c.m_param1 = pj.bindSlot;
			c.m_param2 = innerInSlot;
			c.m_param3 = innerItSlot;
			m_cByteCode.m_listCode.emplace_back(std::move(c));
		}

		// Replay K2's lex range — emit K2 reading from pj.bindSlot
		// (which is the inner iter var here).
		const int savedCursorK2 = m_numCurrentCompile;
		m_numCurrentCompile = pj.k2LexStart;
		const ibParamUnit k2Slot = GetExpression(linqCtx);
		m_numCurrentCompile = savedCursorK2;

		// hashSlot.Insert(k2Slot, bindSlot) — OPER_CALL_METHOD with 2 args.
		{
			ibByteUnit c; AddLineInfo(c);
			c.m_numOper = OPER_CALL_METHOD;
			c.m_param1 = linqCtx->CreateVariable();   // throwaway ret
			c.m_param2 = pj.hashSlot;
			c.m_param3.m_numIndex = GetConstString(wxT("Insert"));
			c.m_param3.m_numArray = 2;
			m_cByteCode.m_listCode.emplace_back(std::move(c));
		}
		{
			ibByteUnit c; AddLineInfo(c);
			c.m_numOper = OPER_SET;
			c.m_param1 = k2Slot;
			m_cByteCode.m_listCode.emplace_back(std::move(c));
		}
		{
			ibByteUnit c; AddLineInfo(c);
			c.m_numOper = OPER_SET;
			c.m_param1 = pj.bindSlot;
			m_cByteCode.m_listCode.emplace_back(std::move(c));
		}

		// Close inner foreach.
		{
			ibByteUnit c; AddLineInfo(c);
			c.m_numOper = OPER_NEXT_ITER;
			c.m_param1 = innerItSlot;
			c.m_param2.m_numIndex = innerForeachIp;
			m_cByteCode.m_listCode.emplace_back(std::move(c));
		}
		m_cByteCode.m_listCode[innerForeachIp].m_param4.m_numIndex =
			(long)m_cByteCode.m_listCode.size();

		// GOTO back to the per-iter lookup's skip_label (return into
		// outer body after first-iter build).
		{
			ibByteUnit c; AddLineInfo(c);
			c.m_numOper = OPER_GOTO;
			c.m_param1.m_numIndex = pj.skipLabelIp;
			m_cByteCode.m_listCode.emplace_back(std::move(c));
		}
	}

	m_cByteCode.m_listCode[b.foreachStartIp].m_param4.m_numIndex =
		(long)m_cByteCode.m_listCode.size();

	// GROUP BY expansion intentionally NOT emitted here — moved to
	// CompileLinqExpression's post-block emit so it fires ONCE after
	// the outermost foreach exhausts (not per inner-foreach exit).
	// Per-row aggregation (Insert into data.m_groupsContainer) is
	// emitted in the KEY_GROUP branch at whichever level the group
	// keyword appeared.

	context->FinishLoopList(m_cByteCode,
		(long)m_cByteCode.m_listCode.size() - 1,
		(long)m_cByteCode.m_listCode.size());
}

void ibCompileCode::CompileLinqJoin(ibCompileContext* linqCtx,
	std::vector<int>& whereSkipIps,
	std::vector<ibLinqPendingJoin>& pendingJoins)
{
	// KEY_JOIN already consumed by caller. Grammar:
	//   join <bindName> in <T> on <K1> equals <K2>
	//
	// Emit per-iter lookup INLINE in outer body (current ip):
	//   1. tmp_isEmpty = !hashSlot          (OPER_NOT)
	//   2. OPER_IF tmp_isEmpty, skipLabel   (jump on hashSlot non-empty)
	//   3. OPER_GOTO trampolineLabel        (placeholder, back-patched)
	//   4. skipLabel:
	//   5. found = hashSlot.Property(K1, bindSlot)
	//   6. OPER_IF found, end-of-body        (back-patched via whereSkipIps)
	//
	// T and K2 lex ranges saved by parse-and-discard so the trampoline
	// (emitted after outer NEXT_ITER) can replay them in the build
	// context (T pre-loop, K2 inside inner foreach where bindSlot is
	// the iter var).
	ibLinqContextData& data = *linqCtx->m_linqData;

	const wxString joinName = GETIdentifier(true);
	GETKeyWord(KEY_IN);

	ibLinqPendingJoin pj;

	// Save T's lex range. Parse-and-discard: GetExpression advances
	// the cursor + emits bytecode; we resize m_listCode back to drop
	// the emit (replayed later at trampoline emit time). Side effects
	// like constant-pool inserts and var auto-decl persist but are
	// idempotent across the second parse.
	pj.tLexStart = m_numCurrentCompile;
	{
		const size_t preSize = m_cByteCode.m_listCode.size();
		GetExpression(linqCtx);
		m_cByteCode.m_listCode.resize(preSize);
	}
	pj.tLexEnd = m_numCurrentCompile;

	// Detect outer-iter-referenced T: scan T's lex range for any
	// IDENTIFIER matching a binding name from m_bindings EXCEPT the
	// most-recently-pushed (= current level's from). If matched, the
	// hash dict built from T@iter-1 is stale for iter-2 — mark for
	// per-row reset at the lookup site. Constant T (or T referencing
	// only globals not in m_bindings) keeps one-shot rebuild.
	if (data.m_bindings.size() > 1) {
		// All but the last m_bindings entry are outer bindings.
		const size_t lastIdx = data.m_bindings.size() - 1;
		for (int pos = pj.tLexStart;
		     pos < pj.tLexEnd && pos < (int)m_listLexem.size();
		     ++pos)
		{
			const ibLexem& lex = m_listLexem[pos];
			if (lex.m_lexType != IDENTIFIER) continue;
			const wxString lexName = lex.m_valData.GetString();
			for (size_t i = 0; i < lastIdx; ++i) {
				if (stringUtils::CompareString(data.m_bindings[i].name, lexName)) {
					pj.m_needsReset = true;
					break;
				}
			}
			if (pj.m_needsReset) break;
		}
	}

	GETKeyWord(KEY_ON);

	// Bind b in the linq scope. Slot persists across iterations; on
	// each outer iter it's overwritten by hashSlot.Property's out-param.
	pj.bindSlot = linqCtx->GetVariable(joinName);

	// Parse K1 inline — emits at current ip (outer body, per-iter).
	// K1 reads `o` (already bound by the enclosing FROM).
	const ibParamUnit k1Slot = GetExpression(linqCtx);

	GETKeyWord(KEY_EQUALS);

	// Save K2's lex range — same parse-and-discard pattern. K2
	// references bindSlot which is bound NOW (above), so the parse
	// resolves the identifier; emit goes to the discarded buffer.
	pj.k2LexStart = m_numCurrentCompile;
	{
		const size_t preSize = m_cByteCode.m_listCode.size();
		GetExpression(linqCtx);
		m_cByteCode.m_listCode.resize(preSize);
	}
	pj.k2LexEnd = m_numCurrentCompile;

	// Persistent hash slot — survives across outer iters; first-iter
	// trampoline allocates the Container into it.
	pj.hashSlot = linqCtx->CreateVariable();

	// Per-row reset for outer-referenced T — `hashSlot = empty` so
	// the IsEmpty guard below falls through to trampoline rebuild on
	// EVERY row. This sacrifices the hash amortisation (O(M²) instead
	// of O(M+N)) for correctness when T depends on outer iter vars.
	// Hoisting reset to outer-foreach body entry (one rebuild per
	// outer iter) is deferred — requires lookahead through level-N's
	// CompileLinqBlock before emitting OPER_FOREACH header.
	if (pj.m_needsReset) {
		// OPER_LET hashSlot = (default empty ibValue from const pool).
		// We can't easily emit "set TYPE_EMPTY" inline; the cleanest
		// trigger is OPER_NEW for an empty array / undefined slot —
		// but simpler is to leverage the IsEmpty path: assign a
		// known-empty constant. Use a const-pool empty ibValue.
		const ibParamUnit emptyConst = FindConst(ibValue());
		ibByteUnit c; AddLineInfo(c);
		c.m_numOper = OPER_LET;
		c.m_param1 = pj.hashSlot;
		c.m_param2 = emptyConst;
		m_cByteCode.m_listCode.emplace_back(std::move(c));
	}

	// (1) tmp_isEmpty = !hashSlot. Untyped OPER_NOT writes BOOLEAN
	// via SetTypeBoolean(IsEmptyValue) — TYPE_EMPTY → true, otherwise
	// false. Compile-side m_strType stays empty so no +TYPE_DELTA
	// inference fires (kept on the untyped IsEmpty path).
	const ibParamUnit tmpIsEmpty = linqCtx->CreateVariable();
	{
		ibByteUnit c; AddLineInfo(c);
		c.m_numOper = OPER_NOT;
		c.m_param1 = tmpIsEmpty;
		c.m_param2 = pj.hashSlot;
		m_cByteCode.m_listCode.emplace_back(std::move(c));
	}

	// (2) OPER_IF tmp_isEmpty, skipLabel
	// OPER_IF jumps when condition is FALSE (per runtime: if !cond,
	// goto). tmpIsEmpty TRUE on first iter (empty hash) → fall through
	// to GOTO trampoline. FALSE on subsequent (built) → jump to
	// skipLabel past the trampoline GOTO.
	const int condIfIp = (int)m_cByteCode.m_listCode.size();
	{
		ibByteUnit c; AddLineInfo(c);
		c.m_numOper = OPER_IF;
		c.m_param1 = tmpIsEmpty;
		c.m_param2.m_numIndex = 0;  // back-patched after the GOTO emit
		m_cByteCode.m_listCode.emplace_back(std::move(c));
	}

	// (3) OPER_GOTO trampolineLabel (placeholder — trampoline emitted
	// after outer NEXT_ITER patches param1 with its ip).
	pj.placeholderGotoIp = (int)m_cByteCode.m_listCode.size();
	{
		ibByteUnit c; AddLineInfo(c);
		c.m_numOper = OPER_GOTO;
		c.m_param1.m_numIndex = 0;   // back-patched in trampoline emit loop
		m_cByteCode.m_listCode.emplace_back(std::move(c));
	}

	// (4) skipLabel — back-patch the conditional IF to land here on
	// subsequent iters (hashSlot non-empty path).
	pj.skipLabelIp = (int)m_cByteCode.m_listCode.size();
	m_cByteCode.m_listCode[condIfIp].m_param2.m_numIndex = pj.skipLabelIp;

	// (5) found = hashSlot.Property(K1, bindSlot)
	// Property writes the matched value into its 2nd-arg slot (bindSlot)
	// on hit; returns false on miss (bindSlot left as previous content).
	const ibParamUnit foundSlot = linqCtx->CreateVariable();
	{
		ibByteUnit c; AddLineInfo(c);
		c.m_numOper = OPER_CALL_METHOD;
		c.m_param1 = foundSlot;
		c.m_param2 = pj.hashSlot;
		c.m_param3.m_numIndex = GetConstString(wxT("Property"));
		c.m_param3.m_numArray = 2;
		m_cByteCode.m_listCode.emplace_back(std::move(c));
	}
	{
		ibByteUnit c; AddLineInfo(c);
		c.m_numOper = OPER_SET;
		c.m_param1 = k1Slot;
		m_cByteCode.m_listCode.emplace_back(std::move(c));
	}
	{
		ibByteUnit c; AddLineInfo(c);
		c.m_numOper = OPER_SET;
		c.m_param1 = pj.bindSlot;
		m_cByteCode.m_listCode.emplace_back(std::move(c));
	}

	// (6) OPER_IF found, end-of-body  — on miss (foundSlot=false),
	// jump past the Add. End-of-body back-patches via whereSkipIps,
	// shared with WHERE clauses at this level.
	const int missSkipIp = (int)m_cByteCode.m_listCode.size();
	{
		ibByteUnit c; AddLineInfo(c);
		c.m_numOper = OPER_IF;
		c.m_param1 = foundSlot;
		c.m_param2.m_numIndex = 0;   // back-patched at end of body
		m_cByteCode.m_listCode.emplace_back(std::move(c));
	}
	whereSkipIps.push_back(missSkipIp);

	// Push binding so subsequent clauses see `b` via standard linq
	// context lookup. Origin FromJoin in case future clauses (e.g.
	// `into g` group-join) need to distinguish.
	ibLinqBinding jb;
	jb.name      = joinName;
	jb.origin    = ibLinqBinding::FromJoin;
	jb.valueSlot = pj.bindSlot;
	data.m_bindings.push_back(jb);

	pendingJoins.push_back(pj);
}

/**
 * record information about the type of variable
 */

void ibCompileCode::AddTypeSet(const ibParamUnit& variable)
{
	if (!variable.m_strType.IsEmpty()) {
		ibByteUnit code;
		AddLineInfo(code);
		code.m_numOper = OPER_SET_TYPE;
		code.m_param1 = variable;
		code.m_param2.m_numArray = ibValue::GetIDObjectFromString(variable.m_strType);
		m_cByteCode.m_listCode.emplace_back(std::move(code));
	}
}

// macro checking variable Var for type Str
#define CheckTypeDef(var,type) if(wxStrlen(type) > 0)\
	{\
		if(!stringUtils::CompareString(var.m_strType, type)){\
			if (ibValue::CompareObjectName(type, ibValueTypes::TYPE_BOOLEAN)) SetError(ERROR_BAD_TYPE_EXPRESSION_B);\
			else if (ibValue::CompareObjectName(type, ibValueTypes::TYPE_NUMBER)) SetError(ERROR_BAD_TYPE_EXPRESSION_N);\
			else if (ibValue::CompareObjectName(type, ibValueTypes::TYPE_STRING)) SetError(ERROR_BAD_TYPE_EXPRESSION_S);\
			else if (ibValue::CompareObjectName(type, ibValueTypes::TYPE_DATE)) SetError(ERROR_BAD_TYPE_EXPRESSION_D);\
			else SetError(ERROR_BAD_TYPE_EXPRESSION);\
		}\
		if (ibValue::CompareObjectName(type, ibValueTypes::TYPE_NUMBER)) code.m_numOper+=TYPE_DELTA1;\
		else if (ibValue::CompareObjectName(type, ibValueTypes::TYPE_STRING)) code.m_numOper+=TYPE_DELTA2;\
		else if (ibValue::CompareObjectName(type, ibValueTypes::TYPE_DATE)) code.m_numOper+=TYPE_DELTA3;\
        else if (ibValue::CompareObjectName(type, ibValueTypes::TYPE_BOOLEAN)) code.m_numOper+=TYPE_DELTA4;\
	}

// macro for adjusting the operation by variable type
// if it is typed, then the typed operation will be performed
#define CorrectTypeDef(sKey)\
if(!sKey.m_strType.IsEmpty())\
{\
	if (ibValue::CompareObjectName(sKey.m_strType, ibValueTypes::TYPE_NUMBER)) code.m_numOper+=TYPE_DELTA1;\
	else if (ibValue::CompareObjectName(sKey.m_strType, ibValueTypes::TYPE_STRING)) code.m_numOper+=TYPE_DELTA2;\
	else if (ibValue::CompareObjectName(sKey.m_strType, ibValueTypes::TYPE_DATE)) code.m_numOper+=TYPE_DELTA3;\
    else if (ibValue::CompareObjectName(sKey.m_strType, ibValueTypes::TYPE_BOOLEAN))  code.m_numOper+=TYPE_DELTA4;\
	else SetError(ERROR_BAD_TYPE_EXPRESSION);\
}

// macro for local context 
#define CreateLocalContext(ctx) \
	std::shared_ptr<ibCompileContext>(\
	ctx->CreateContext(RETURN_BLOCK)).get()

/**
 * CompileBlock
 * Purpose:
 * Creating object code for one block (a piece of code between any
 * operator brackets like LOOP...ENDDO, IF...ENDIF, etc.
 * nIterNumber - nested block number
 * Return value:
 * true,false
 */

bool ibCompileCode::CompileBlock(ibCompileContext* context)
{

	bool bCompileBlock = false;

	// CES `{` consumption rules:
	//   * module-level (RETURN_NONE): NEVER consume `{` here — a top-level
	//     `{...}` is a free block, handled by the inner `{` branch which
	//     recurses with a fresh RETURN_BLOCK child. If we ate `{` here the
	//     module body would think the brace was its own envelope, then
	//     loop forever on the matching `}` in the `}` branch (RETURN_NONE
	//     doesn't break, and the m_numCurrentCompile-- step-back re-feeds
	//     the same `}` next iteration → 224k-line scope.log circa 2026-05-10).
	//   * function / lambda body (RETURN_PROCEDURE / FUNCTION / LAMBDA_*):
	//     require `{` — caller (CompileFunction / CompileLambdaExpression)
	//     consumed everything up to the body, so the brace is the next
	//     lexem.
	//   * block body (RETURN_BLOCK): caller did m_numCurrentCompile-- on
	//     the `{` it saw, so the brace is back in the stream — must consume.
	//   * control-structure body without braces (`if (x) stmt;`) is also
	//     RETURN_BLOCK with no `{` — guarded by IsNextDelimeter check below.
	if (gs_codeStyle == CODE_CES && context->m_numReturn != RETURN_NONE && IsNextDelimeter(wxT('{'))) {
		GETDelimeter(wxT('{'));
		bCompileBlock = true;

		// CTX_BEGIN/END are emitted only for RETURN_BLOCK — actual
		// nested block scopes. Function / lambda body envelopes use
		// the OPER_FUNC frame. Runtime bumps ibRunContext::
		// m_currentScopeDepth on CTX_BEGIN, drops on CTX_END;
		// SendLocalVariables filters Locals rows by entry depth.
		if (context->m_numReturn == RETURN_BLOCK) {
			++m_compileScopeDepth;

			ibByteUnit declCtx;
			AddLineInfo(declCtx);
			declCtx.m_numOper = OPER_CTX_BEGIN;
			m_cByteCode.m_listCode.emplace_back(std::move(declCtx));

		}

	}

	while (true) {

		const ibLexem& lex = PreviewGetLexem();

		if (lex.m_lexType == ERRORTYPE)
			break;

		if (KEYWORD == lex.m_lexType) {

			switch (lex.m_numData)
			{
			case KEY_VAR: // setting variables and arrays
				CompileDeclaration(context);
				break;
			case KEY_NEW:
				CompileNewObject(context);
				break;
			case KEY_FROM:
				// LINQ as top-level statement — result-array is emitted
				// into a temp slot and discarded. Symmetric with KEY_NEW
				// at statement-level (`New("Array")` on its own is also
				// pointless but legal). Most uses go through assignment
				// RHS where the result is captured. CompileLinqExpression
				// consumes KEY_FROM itself.
				CompileLinqExpression(context);
				break;
			case KEY_IF:
				CompileIf(context);
				break;
			case KEY_WHILE:
				CompileWhile(context);
				break;
			case KEY_FOREACH:
				CompileForeach(context);
				break;
			case KEY_FOR:
				CompileFor(context);
				break;
			case KEY_GOTO:
				CompileGoto(context);
				break;
			case KEY_TRY:
				CompileException(context);
				break;
			case KEY_RAISE:
			{
				GETKeyWord(KEY_RAISE);
				ibByteUnit code;
				AddLineInfo(code);
				if (IsNextDelimeter('(')) {
					code.m_numOper = OPER_RAISE_T;
					GETDelimeter('(');
					code.m_param1 = GetExpression(context);
					GETDelimeter(')');
				}
				else {
					code.m_numOper = OPER_RAISE;
				}
				m_cByteCode.m_listCode.emplace_back(std::move(code));
				break;
			}
			case KEY_RETURN:
			{
				GETKeyWord(KEY_RETURN);

				ibCompileContext* currContext = context;
				while (currContext->m_numReturn == RETURN_BLOCK)
					currContext = currContext->m_parentContext;

				if (currContext->m_numReturn == RETURN_NONE) {
					SetError(ERROR_USE_RETURN); // return operator cannot be used outside a procedure or function
					return false;
				}

				ibByteUnit code;
				AddLineInfo(code);
				code.m_numOper = OPER_RET;

				// Decide if Return must carry an expression. Function
				// kinds (named or anonymous) require a value; procedure
				// kinds reject one. m_numReturn fully discriminates
				// both axes — IsReturnFunction handles RETURN_FUNCTION
				// + RETURN_LAMBDA_FUNCTION uniformly.
				if (IsReturnFunction(currContext->m_numReturn)) {
					if (IsNextDelimeter(';')) {
						SetError(ERROR_EXPRESSION_REQUIRE);
						return false;
					}
					code.m_param1 = GetExpression(context);
				}
				else {
					code.m_param1.m_numArray = DEF_VAR_NORET;
					code.m_param1.m_numIndex = DEF_VAR_NORET;
				}

				m_cByteCode.m_listCode.emplace_back(std::move(code));
				break;
			}
			case KEY_CONTINUE:
			{
				GETKeyWord(KEY_CONTINUE);
				if (context->m_listContinue[context->m_numDoNumber]) {
					ibByteUnit code;
					AddLineInfo(code);
					code.m_numOper = OPER_GOTO;
					m_cByteCode.m_listCode.emplace_back(std::move(code));
					const int addrLine = m_cByteCode.m_listCode.size() - 1;
					std::vector<int>* pList = context->m_listContinue[context->m_numDoNumber];
					pList->emplace_back(addrLine);
				}
				else {
					SetError(ERROR_USE_CONTINUE); // continue statement can only be used inside a loop
					return false;
				}
				break;
			}
			case KEY_BREAK:
			{
				GETKeyWord(KEY_BREAK);
				if (context->m_listBreak[context->m_numDoNumber] != nullptr) {
					ibByteUnit code;
					AddLineInfo(code);
					code.m_numOper = OPER_GOTO;
					m_cByteCode.m_listCode.emplace_back(std::move(code));
					const int addrLine = m_cByteCode.m_listCode.size() - 1;
					std::vector<int>* pList = context->m_listBreak[context->m_numDoNumber];
					pList->emplace_back(addrLine);
				}
				else {
					SetError(ERROR_USE_BREAK); // break operator can only be used inside a loop
					return false;
				}
				break;
			}
			case KEY_FUNCTION:
			case KEY_PROCEDURE:
			{
				(void)GetLexem();
				SetError(ERROR_USE_BLOCK);
				return false;
				break;
			}
			default:
				return true;	// means the operator bracket ending this block has been encountered (for example, ENDIF, ENDDO, ENDFUNCTION, etc.)
			}
		}
		else {

			const ibLexem& nextLexem = GetLexem();
			if (IDENTIFIER == nextLexem.m_lexType) {

				if (gs_codeStyle == CODE_VES)
					context->m_numTempVar = 0;

				if (IsNextDelimeter(':')) {// this is a label task encountered
					unsigned int pLabel = context->m_listLabelDef[nextLexem.m_strData];
					if (pLabel > 0) {
						SetError(ERROR_IDENTIFIER_DUPLICATE, nextLexem.m_strData);// duplicate label definitions occurred
						return false;
					}
					// write the address of the label:
					context->m_listLabelDef[nextLexem.m_strData] = m_cByteCode.m_listCode.size() - 1;
					GETDelimeter(':');
				}
				else if (IsNextDelimeter(wxT('+')) || IsNextDelimeter(wxT('-'))) {
					// postfix ++ / -- on a bare variable: `x++;` / `x--;`  is sugar for
					// `x = x +/- 1`. Reached only when the identifier is IMMEDIATELY
					// followed by +/- (no '.' / '[' / '(' between), so a member/array
					// GET-temp can never be silently incremented. At statement level the
					// value is unused, so prefix and postfix collapse to the same store.
					const wxUniChar incOp = IsNextDelimeter(wxT('+')) ? wxT('+') : wxT('-');
					const wxString strRealName = nextLexem.m_valData.GetString();
					GETDelimeter(incOp);
					if (!IsNextDelimeter(incOp)) { // a lone +/- after a bare identifier — not valid here
						SetError(ERROR_CODE);
						return false;
					}
					GETDelimeter(incOp);

					ibParamUnit variable = context->GetVariable(strRealName, true, true); // must already exist
					ibByteUnit code;
					AddLineInfo(code);
					code.m_numOper = (incOp == wxT('+')) ? OPER_ADD : OPER_SUB;
					ibValue oneVal; oneVal.SetNumber(wxT("1"));
					code.m_param1 = variable;          // dest in place — same shape the bShortLet fold of `x = x + 1` produces
					code.m_param2 = variable;          // left operand
					code.m_param3 = FindConst(oneVal); // right operand = 1
					m_cByteCode.m_listCode.emplace_back(std::move(code));
				}
				else { //function and method calls, expression assignments are processed here

					m_numCurrentCompile--;// step back

					int numSet = 1;
					if (m_onlyFunction && context == GetContext()) {
						SetError(ERROR_ONLY_FUNCTION);
						return false;
					}

					ibParamUnit variable = GetCurrentIdentifier(context, numSet);//get the left side of the expression (before the '=' sign)
					if (numSet) { //if there is a right side, i.e. the '=' sign
						GETDelimeter('=');//this is an assignment of some expression to a variable
						ibParamUnit expression = GetExpression(context);
						ibByteUnit code;
						code.m_numOper = OPER_LET;
						AddLineInfo(code);

						CheckTypeDef(expression, variable.m_strType);
						variable.m_strType = expression.m_strType;

						bool bShortLet = false; int n = 0;

						if (DEF_VAR_TEMP == expression.m_numArray) { //reduce only temporary variables
							n = m_cByteCode.m_listCode.size() - 1;
							if (n >= 0) {
								int nOperation = m_cByteCode.m_listCode[n].m_numOper % TYPE_DELTA1;
								nOperation = nOperation % TYPE_DELTA1;
								if (OPER_MULT == nOperation ||
									OPER_DIV == nOperation ||
									OPER_ADD == nOperation ||
									OPER_SUB == nOperation ||
									OPER_MOD == nOperation ||
									OPER_GT == nOperation ||
									OPER_GE == nOperation ||
									OPER_LS == nOperation ||
									OPER_LE == nOperation ||
									OPER_NE == nOperation ||
									OPER_EQ == nOperation
									)
								{
									bShortLet = true;//shorten one assignment
								}
							}
						}

						if (bShortLet) {
							m_cByteCode.m_listCode[n].m_param1 = variable;
						}
						else {
							code.m_param1 = variable;
							code.m_param2 = expression;
							m_cByteCode.m_listCode.emplace_back(std::move(code));
						}
					}
				}
			}
			else if (nextLexem.m_lexType == DELIMITER
				&& (nextLexem.m_numData == wxT('+') || nextLexem.m_numData == wxT('-')))
			{
				// prefix ++ / -- (statement form): `++x;` / `--x;` is sugar for
				// `x = x +/- 1`. The first +/- was just consumed as nextLexem;
				// require it doubled, then a bare variable. Value is unused at
				// statement level, so this is identical to the postfix store.
				const wxUniChar incOp = nextLexem.m_numData;
				if (!IsNextDelimeter(incOp)) { // a lone leading +/- — syntax error, as before
					SetError(ERROR_CODE);
					return false;
				}
				GETDelimeter(incOp); // consume the second +/-
				const wxString strRealName = GETIdentifier(true); // target variable
				ibParamUnit variable = context->GetVariable(strRealName, true, true); // must already exist
				ibByteUnit code;
				AddLineInfo(code);
				code.m_numOper = (incOp == wxT('+')) ? OPER_ADD : OPER_SUB;
				ibValue oneVal; oneVal.SetNumber(wxT("1"));
				code.m_param1 = variable;
				code.m_param2 = variable;
				code.m_param3 = FindConst(oneVal);
				m_cByteCode.m_listCode.emplace_back(std::move(code));
			}
			else if (nextLexem.m_lexType == DELIMITER
				&& nextLexem.m_numData == wxT(';'))
			{
			}
			else if (gs_codeStyle == CODE_CES && nextLexem.m_lexType == DELIMITER
				&& nextLexem.m_numData == wxT('{'))
			{
				m_numCurrentCompile--;// step back


				const int numTempVar = context->m_numTempVar;
				CompileBlock(CreateLocalContext(context));
				context->m_numTempVar = numTempVar;

			}
			else if (gs_codeStyle == CODE_CES && nextLexem.m_lexType == DELIMITER
				&& nextLexem.m_numData == wxT('}'))
			{
				// `}` at the module-level body is a syntax error — there
				// was no opening `{` to match. Without this branch we'd
				// step-back and re-read the same `}` forever. Function /
				// lambda / block bodies step back so the caller's
				// GETDelimeter('}') consumes the brace, then break out of
				// the body loop.
				if (context->m_numReturn == RETURN_NONE) {
					SetError(ERROR_CODE);
					return false;
				}

				m_numCurrentCompile--;// step back


				break;
			}
			else if (nextLexem.m_lexType == ENDPROGRAM) {
				break;
			}
			else {
				SetError(ERROR_CODE);
				return false;
			}

			if (gs_codeStyle == CODE_CES && !bCompileBlock && context->m_numReturn == RETURN_BLOCK)
				break;
		}

	}//while

	if (gs_codeStyle == CODE_CES && bCompileBlock) {
		GETDelimeter(wxT('}'));

		// Pair with CTX_BEGIN — emit only for RETURN_BLOCK.
		if (context->m_numReturn == RETURN_BLOCK) {
			if (m_compileScopeDepth > 0) --m_compileScopeDepth;

			ibByteUnit declCtxEnd;
			AddLineInfo(declCtxEnd);
			declCtxEnd.m_numOper = OPER_CTX_END;
			m_cByteCode.m_listCode.emplace_back(std::move(declCtxEnd));

		}

	}


	return true;
}//CompileBlock

bool ibCompileCode::CompileNewObject(ibCompileContext* context)
{
	GETKeyWord(KEY_NEW);

	wxString strClassName = GETIdentifier(true);
	const int numConst = GetConstString(strClassName);

	std::vector <ibParamUnit> listParam;

	if (IsNextDelimeter('(')) { // this is a method call
		GETDelimeter('(');
		while (!IsNextDelimeter(')')) {
			if (IsNextDelimeter(',')) {
				ibParamUnit data;
				data.m_numArray = DEF_VAR_SKIP;// missing parameter
				data.m_numIndex = DEF_VAR_SKIP;
				listParam.emplace_back(std::move(data));
			}
			else {
				listParam.emplace_back(GetExpression(context));
				if (!IsNextDelimeter(',') || IsNextDelimeter(')'))
					break;
			}
			GETDelimeter(',');
		}
		GETDelimeter(')');
	}

	if (!ibValue::IsRegisterCtor(strClassName, ibCtorObjectType::ibCtorObjectType_object_value)) {
		SetError(ERROR_CALL_CONSTRUCTOR, strClassName);
		return false;
	}

	ibByteUnit code;
	AddLineInfo(code);

	code.m_numOper = OPER_NEW;
	code.m_param2.m_numIndex = numConst;//number of the called method from the list of encountered methods
	code.m_param2.m_numArray = listParam.size();// number of parameters

	ibParamUnit variable = context->CreateVariable();
	code.m_param1 = variable;// variable into which the value is returned
	m_cByteCode.m_listCode.emplace_back(std::move(code));

	for (unsigned int arg = 0; arg < listParam.size(); arg++) {

		ibByteUnit code;
		AddLineInfo(code);
		code.m_numOper = OPER_SET;
		code.m_param1 = listParam[arg];

		m_cByteCode.m_listCode.emplace_back(std::move(code));
	}

	return true;
}

/**
 * CompileGoto
 * Purpose:
 * Compiling the GOTO statement (determining the location of the jump label
 * for subsequent replacement with address and type = LABEL)
 * Return value:
 * true,false
 */

bool ibCompileCode::CompileGoto(ibCompileContext* context)
{
	GETKeyWord(KEY_GOTO);

	std::shared_ptr<ibCompileContext::ibLabel> data(new ibCompileContext::ibLabel);

	data->m_strName = GETIdentifier();
	data->m_numLine = m_cByteCode.m_listCode.size();//remember those transitions that will need to be processed later
	data->m_numError = m_numCurrentCompile;

	context->m_listLabel.emplace_back(std::move(data));

	ibByteUnit code;
	AddLineInfo(code);
	code.m_numOper = OPER_GOTO;
	m_cByteCode.m_listCode.emplace_back(std::move(code));

	return true;
}

/*
 * GetCurrentIdentifier
 * Purpose:
 * Compiling an identifier (defining its type as a variable, attribute or function, method)
 * numIsSet - at the input: 1 - a sign that an expression assignment may be expected (if the '=' sign is encountered)
 * Return value:
 * numIsSet - at the output: 1 - a sign that the assignment of the expression is exactly expected (i.e. the '=' sign must be encountered)
 * index number of the variable where the identifier value lies
*/

ibParamUnit ibCompileCode::GetCurrentIdentifier(ibCompileContext* context, int& numIsSet)
{
	ibParamUnit variable; int numPrevSet = numIsSet;

	const wxString& strRealName = GETIdentifier(true);
	const wxString& strName = stringUtils::MakeUpper(strRealName);

	if (IsNextDelimeter('(')) { // this is a function call
		std::shared_ptr<ibCompileContext::ibFunction> foundedFunc = nullptr;
		if (m_rootContext->FindFunction(strName, foundedFunc, true)) {
			const int numConst = GetConstString(strRealName);
			std::vector <ibParamUnit> listParam;
			GETDelimeter('(');
			while (!IsNextDelimeter(')')) {
				if (IsNextDelimeter(',')) {
					ibParamUnit data;
					data.m_numArray = DEF_VAR_SKIP; // missing parameter
					data.m_numIndex = DEF_VAR_SKIP;
					listParam.emplace_back(std::move(data));
				}
				else {
					listParam.emplace_back(GetExpression(context));
					if (!IsNextDelimeter(',') || IsNextDelimeter(')'))
						break;
				}
				GETDelimeter(',');
			}
			GETDelimeter(')');
			if (!numIsSet && foundedFunc != nullptr && !foundedFunc->m_bCodeRet) {
				SetError(ERROR_USE_PROCEDURE_AS_FUNCTION, foundedFunc->m_strRealName);
				return ibParamUnit();
			}
			if (listParam.size() > foundedFunc->m_listParam.size()) {
				SetError(ERROR_MANY_PARAMS); // too many parameters
				return ibParamUnit();
			}

			ibByteUnit code;
			AddLineInfo(code);
			code.m_numOper = OPER_CALL_METHOD;

			// variable on which the method is called
			code.m_param2 = context->GetVariable(foundedFunc->m_strContext, true, false, true);
			code.m_param3.m_numIndex = numConst;//number of the called method from the list of encountered methods
			code.m_param3.m_numArray = listParam.size();// number of parameters
			variable = context->CreateVariable();
			code.m_param1 = variable;// variable into which the value is returned
			m_cByteCode.m_listCode.emplace_back(std::move(code));

			for (unsigned int i = 0; i < listParam.size(); i++) {
				ibByteUnit code;
				AddLineInfo(code);
				code.m_numOper = OPER_SET;
				code.m_param1 = listParam[i];
				m_cByteCode.m_listCode.emplace_back(std::move(code));
			}
		}
		else {
			// Try as a Variable holding a callable value (lambda /
			// function-pointer / any ibValueFunction-wrapping ibValue).
			// This sits between the context-method check above and the
			// named-function fallback below: regular Local / Export
			// vars get OPER_CALL_LAMBDA on their slot; runtime then verifies
			// at dispatch that the value really wraps an ibValueFunction.
			// Excludes context-bindings (Manager, ThisForm — m_bContext)
			// and externals (m_bExternal) — those aren't user-callable
			// in Phase A. ContextProp (m_strContext non-empty) goes
			// through OPER_GET_A elsewhere.
			// `context->FindVariable` is OWN-scope only (does not walk
			// parents). For nested block scopes (RETURN_BLOCK) we MUST
			// walk the parent compile-context chain manually — otherwise
			// a lambda assigned in an outer block (e.g. `a = Function...`
			// in block-A, then `a()` called from block-A's nested
			// block-B-block-C) is not visible at the call site and the
			// emitter falls through to GetCallFunction → "Procedure or
			// function not detected (a)". Stop at the first match (own
			// scope wins over parents — same nearest-binding rule as
			// GetVariable's parent loop).
			std::shared_ptr<ibCompileContext::ibVariable> foundedCallableVar = nullptr;
			bool foundCallable = false;
			bool foundInBlockScope = false;
			for (ibCompileContext* pCur = context; pCur != nullptr; pCur = pCur->m_parentContext) {
				if (pCur->FindVariable(strRealName, foundedCallableVar)) {
					foundCallable = true;
					foundInBlockScope = (pCur->m_numReturn == RETURN_BLOCK);
					break;
				}
			}
			// Block-scope vars (var declared inside `{...}` in CES) get
			// PushVariable'd with contextVar=true so the runtime emission
			// path treats them as outer-frame slots — same routing as
			// real Context bindings (Manager / ThisForm). For dispatch
			// purposes we must still recognize them as user-callable
			// values: distinguish by the ctx they were found in
			// (RETURN_BLOCK ⇒ block-scope artifact, NOT a real context
			// binding). Real Context bindings live on m_rootContext
			// (numReturn == RETURN_NONE).
			const bool isCallableVar = foundCallable
				&& foundedCallableVar
				&& foundedCallableVar->m_strContext.IsEmpty()
				&& !foundedCallableVar->m_bExternal
				&& (!foundedCallableVar->m_bContext || foundInBlockScope);

			if (isCallableVar) {
				std::vector<ibParamUnit> listParam;
				GETDelimeter('(');
				while (!IsNextDelimeter(')')) {
					if (IsNextDelimeter(',')) {
						ibParamUnit data;
						data.m_numArray = DEF_VAR_SKIP; // missing parameter
						data.m_numIndex = DEF_VAR_SKIP;
						listParam.emplace_back(std::move(data));
					}
					else {
						listParam.emplace_back(GetExpression(context));
						if (!IsNextDelimeter(',') || IsNextDelimeter(')'))
							break;
					}
					GETDelimeter(',');
				}
				GETDelimeter(')');

				ibByteUnit code;
				AddLineInfo(code);
				code.m_numOper = OPER_CALL_LAMBDA;
				variable = context->CreateVariable();
				code.m_param1 = variable;                                  // return-value dest
				code.m_param4 = context->GetVariable(strRealName, true, false);  // source callable slot
				// m_param2.m_numIndex = caller-supplied arg count. Runtime
				// uses this as the upper bound for inline OPER_SET / SETCONST
				// consumption. Trailing params (callerArgCount..paramCount)
				// are filled from the lambda's m_listParam[i].m_defaultValue.
				code.m_param2.m_numIndex = (long)listParam.size();
				m_cByteCode.m_listCode.emplace_back(std::move(code));

				// Inline OPER_SET for each parameter — same layout as
				// OPER_CALL_METHOD / OPER_CALL. Runtime walks these via
				// lCodeLine++ inside its OPER_CALL_LAMBDA handler.
				for (unsigned int i = 0; i < listParam.size(); i++) {
					ibByteUnit code;
					AddLineInfo(code);
					code.m_numOper = OPER_SET;
					code.m_param1 = listParam[i];
					m_cByteCode.m_listCode.emplace_back(std::move(code));
				}
			}
			else {
				variable = GetCallFunction(context, strRealName, numIsSet);
			}
		}
		if (IsTypeVar(strRealName)) {
			variable.m_strType = GetTypeVar(strRealName);	// this is a type cast
		}
		numIsSet = 0;
	}
	else { //this is a variable call
		std::shared_ptr<ibCompileContext::ibVariable> foundedVar = nullptr; numIsSet = 1;

		// Kind-aware identifier resolve — promote the bind-kind to a dedicated
		// access opcode (1:1 with Bind{Context,Scope,Export}Variable):
		//   - ContextProp (Catalogs of a Manager scope) → OPER_GET_SCOPE /
		//     OPER_SET_SCOPE on the parent (scope-provider) binding + member const.
		//   - External binding (RegisterRecords / Filter / Controls / DataSource)
		//     → OPER_GET_EXTERN / OPER_SET_EXTERN on the handle's slot.
		//   - Context binding (ThisObject / ThisForm) → OPER_GET_CONTEXT /
		//     OPER_SET_CONTEXT on the handle's slot.
		//   - regular var / not found → GetVariable's frame-slot emission.
		m_rootContext->FindVariable(strRealName, foundedVar, true);
		const bool isScope   = foundedVar && !foundedVar->m_strContext.IsEmpty();
		const bool isExtern  = foundedVar && foundedVar->m_strContext.IsEmpty() && foundedVar->m_bExternal;
		const bool isContext = foundedVar && foundedVar->m_strContext.IsEmpty() && foundedVar->m_bContext && !foundedVar->m_bExternal;

		if (isScope) {
			ibByteUnit code;
			AddLineInfo(code);
			const int numConst = GetConstString(strRealName);
			if (IsNextDelimeter('=') && numPrevSet == 1) {
				GETDelimeter('='); numIsSet = 0;
				code.m_numOper = OPER_SET_SCOPE;
				code.m_param1 = context->GetVariable(foundedVar->m_strContext, true, false, true);//scope-provider binding
				code.m_param2.m_numIndex = numConst;//member name const index
				code.m_param3 = GetExpression(context);
				m_cByteCode.m_listCode.emplace_back(std::move(code));
				return variable;
			}
			else {
				code.m_numOper = OPER_GET_SCOPE;
				code.m_param2 = context->GetVariable(foundedVar->m_strContext, true, false, true);//scope-provider binding
				code.m_param3.m_numIndex = numConst;//member name const index
				variable = context->CreateVariable();
				code.m_param1 = variable;// dest temp
				m_cByteCode.m_listCode.emplace_back(std::move(code));
			}
		}
		else if (isExtern || isContext) {
			ibByteUnit code;
			AddLineInfo(code);
			const long getOp = isExtern ? OPER_GET_EXTERN : OPER_GET_CONTEXT;
			const long setOp = isExtern ? OPER_SET_EXTERN : OPER_SET_CONTEXT;
			if (IsNextDelimeter('=') && numPrevSet == 1) {
				GETDelimeter('='); numIsSet = 0;
				code.m_numOper = setOp;
				code.m_param1 = context->GetVariable(strRealName, true, false);//handle slot
				code.m_param3 = GetExpression(context);
				m_cByteCode.m_listCode.emplace_back(std::move(code));
				return variable;
			}
			else {
				code.m_numOper = getOp;
				code.m_param2 = context->GetVariable(strRealName, true, false);//handle slot
				variable = context->CreateVariable();
				code.m_param1 = variable;// dest temp
				m_cByteCode.m_listCode.emplace_back(std::move(code));
			}
		}
		else {
			bool bCheckError = !numPrevSet;
			if (IsNextDelimeter('.') || IsNextDelimeter('[')) //this variable contains a method call or array
				bCheckError = true;
			variable = context->GetVariable(strRealName, true, bCheckError);
		}
	}

loopLabel:

	if (IsNextDelimeter('[')) { // this is an array
		GETDelimeter('[');
		ibParamUnit variableKey = GetExpression(context);
		GETDelimeter(']');
		//determine the call type (i.e. is it setting or getting an array value)
		//Example:
		//Arr[10]=12; - Set
		//�=Arr[10]; - Get
		//Arr[10][2]=12; - Get,Set
		numIsSet = 0;
		if (IsNextDelimeter('[')) { //check the array variable type (multidimensional array support)
			ibByteUnit code;
			AddLineInfo(code);
			code.m_numOper = OPER_CHECK_ARRAY;
			code.m_param1 = variable;//variable is an array
			code.m_param2 = variableKey;//array index
			m_cByteCode.m_listCode.emplace_back(std::move(code));
		}
		if (IsNextDelimeter('=') && numPrevSet == 1) {
			GETDelimeter('=');
			ibByteUnit code;
			AddLineInfo(code);
			code.m_numOper = OPER_SET_ARRAY;
			code.m_param1 = variable;//variable is an array
			code.m_param2 = variableKey;//array index (more precisely, key since an associative array is used)
			code.m_param3 = GetExpression(context);

			CorrectTypeDef(variableKey);// check value type of index variable

			m_cByteCode.m_listCode.emplace_back(std::move(code));
			return variable;
		}
		else {
			ibByteUnit code;
			AddLineInfo(code);
			code.m_numOper = OPER_GET_ARRAY;
			code.m_param2 = variable;//variable - array
			code.m_param3 = variableKey;//array index (more precisely key since associative array is used)
			variable = context->CreateVariable();
			code.m_param1 = variable;// variable into which the value is returned
			CorrectTypeDef(variableKey);// check value type ��������� ����������
			m_cByteCode.m_listCode.emplace_back(std::move(code));
		}
		goto loopLabel;
	}

	if (IsNextDelimeter('.')) { // this is a method call ��� �������� ����������� �������
		GETDelimeter('.');
		// acceptKeyword=true: contextual LINQ keywords (Where/Select/...)
		// must work as method names in property-access positions.
		wxString strIdentifier = GETIdentifier(true, /*acceptKeyword*/true);
		const int numConst = GetConstString(strIdentifier);
		if (IsNextDelimeter('(')) { // this is a method call
			std::vector <ibParamUnit> listParam;
			GETDelimeter('(');
			while (!IsNextDelimeter(')')) {
				if (IsNextDelimeter(',')) {
					ibParamUnit data;
					data.m_numArray = DEF_VAR_SKIP;// missing parameter
					data.m_numIndex = DEF_VAR_SKIP;
					listParam.emplace_back(std::move(data));
				}
				else {
					listParam.emplace_back(GetExpression(context));
					if (!IsNextDelimeter(',') || IsNextDelimeter(')'))
						break;
				}
				GETDelimeter(',');
			}
			GETDelimeter(')');

			ibByteUnit code;
			AddLineInfo(code);

			// LINQ pipeline ops (Where / Select / OrderBy / ... ) detected
			// by name at emit time → OPER_CALL_LINQ carries the ibLinqMethod
			// enum value directly in m_param3.m_numIndex (no const-string
			// indirection, no runtime FindMethod walk). Per-class methods
			// (Add / Insert / Contains / ...) still go through OPER_CALL_METHOD.
			const long linqEnum = ibValue::FindLinqMethodByName(strIdentifier);
			if (linqEnum >= 0) {
				code.m_numOper = OPER_CALL_LINQ;
				code.m_param2 = variable;                          // receiver
				code.m_param3.m_numIndex = linqEnum;               // ibLinqMethod enum id (NOT a const-pool index)
				code.m_param3.m_numArray = listParam.size();       // caller arg count
			}
			else {
				code.m_numOper = OPER_CALL_METHOD;
				code.m_param2 = variable; // variable on which the method is called
				code.m_param3.m_numIndex = numConst;//number of the called method from the list of encountered methods
				code.m_param3.m_numArray = listParam.size();// number of parameters
			}
			variable = context->CreateVariable();
			code.m_param1 = variable;// variable into which the value is returned
			m_cByteCode.m_listCode.emplace_back(std::move(code));
			for (unsigned int i = 0; i < listParam.size(); i++) {
				ibByteUnit code;
				AddLineInfo(code);
				code.m_numOper = OPER_SET;
				code.m_param1 = listParam[i];
				m_cByteCode.m_listCode.emplace_back(std::move(code));
			}

			numIsSet = 0;
		}
		else { //otherwise - attribute call
			//define the call type (i.e. is it attribute setting or getting)
			//Example:
			//A=Cat.Product; - Get
			//Cat.Product=0; - Set
			//Cat.Product.Code=0; - Get,Set
			ibByteUnit code;
			AddLineInfo(code);
			if (IsNextDelimeter('=') && numPrevSet == 1) {
				GETDelimeter('='); 	numIsSet = 0;
				code.m_numOper = OPER_SET_A;
				code.m_param1 = variable;//variable for which the attribute is called
				code.m_param2.m_numIndex = numConst;//number of the called method from the list of encountered attributes and methods
				code.m_param3 = GetExpression(context);
				m_cByteCode.m_listCode.emplace_back(std::move(code));
				return variable;
			}
			else {
				code.m_numOper = OPER_GET_A;
				code.m_param2 = variable;//variable for which the attribute is called
				code.m_param3.m_numIndex = numConst;//number of the called attribute from the list of encountered attributes and methods
				variable = context->CreateVariable();
				code.m_param1 = variable;// variable into which the value is returned
				m_cByteCode.m_listCode.emplace_back(std::move(code));
			}
		}
		goto loopLabel;
	}

	return variable;
}

bool ibCompileCode::CompileIf(ibCompileContext* context)
{
	std::vector <int> listAddrLine;

	GETKeyWord(KEY_IF);

	ibByteUnit code;
	AddLineInfo(code);
	code.m_numOper = OPER_IF;

	if (gs_codeStyle == CODE_CES)
		GETDelimeter(wxT('('));

	ibParamUnit variable = GetExpression(context);
	code.m_param1 = variable;
	CorrectTypeDef(variable);// check value type

	m_cByteCode.m_listCode.emplace_back(std::move(code));

	int nLastIFLine = m_cByteCode.m_listCode.size() - 1;

	if (gs_codeStyle == CODE_VES)
		GETKeyWord(KEY_THEN);
	else
		GETDelimeter(wxT(')'));

	if (gs_codeStyle == CODE_CES)
		CompileBlock(CreateLocalContext(context));
	else
		CompileBlock(context);

	while (IsNextKeyWord(KEY_ELSEIF)) {

		ibByteUnit code1;

		// write the output from all checks for the previous block
		AddLineInfo(code1);

		code1.m_numOper = OPER_GOTO;
		m_cByteCode.m_listCode.emplace_back(std::move(code1));

		listAddrLine.emplace_back(m_cByteCode.m_listCode.size() - 1);//the parameter for the GOTO operator will be known later

		//for the previous condition, set the jump address if the condition does not match
		m_cByteCode.m_listCode[nLastIFLine].m_param2.m_numIndex = m_cByteCode.m_listCode.size();

		ibByteUnit code2;
		AddLineInfo(code2);

		GETKeyWord(KEY_ELSEIF);
		AddLineInfo(code2);
		code2.m_numOper = OPER_IF;

		if (gs_codeStyle == CODE_CES)
			GETDelimeter(wxT('('));

		variable = GetExpression(context);
		code2.m_param1 = variable;
		CorrectTypeDef(variable);// check value type

		m_cByteCode.m_listCode.emplace_back(std::move(code2));
		nLastIFLine = m_cByteCode.m_listCode.size() - 1;

		if (gs_codeStyle == CODE_VES)
			GETKeyWord(KEY_THEN);
		else
			GETDelimeter(wxT(')'));

		if (gs_codeStyle == CODE_CES)
			CompileBlock(CreateLocalContext(context));
		else
			CompileBlock(context);
	}

	if (IsNextKeyWord(KEY_ELSE)) {

		ibByteUnit code1;

		// write the output from all checks for the previous block
		AddLineInfo(code1);

		code1.m_numOper = OPER_GOTO;
		m_cByteCode.m_listCode.emplace_back(std::move(code1));

		listAddrLine.emplace_back(m_cByteCode.m_listCode.size() - 1);//the parameter for the GOTO operator will be known later

		//for the previous condition, set the jump address if the condition does not match
		m_cByteCode.m_listCode[nLastIFLine].m_param2.m_numIndex = m_cByteCode.m_listCode.size();
		nLastIFLine = 0;

		GETKeyWord(KEY_ELSE);

		if (gs_codeStyle == CODE_CES)
			CompileBlock(CreateLocalContext(context));
		else
			CompileBlock(context);
	}

	if (gs_codeStyle == CODE_VES)
		GETKeyWord(KEY_ENDIF);

	const int numCurCompile = m_cByteCode.m_listCode.size();

	//for the last condition, set the jump address if the condition does not match
	m_cByteCode.m_listCode[nLastIFLine].m_param2.m_numIndex = numCurCompile;

	//Set the parameter for the GOTO operator - exit from all local conditions
	for (unsigned int i = 0; i < listAddrLine.size(); i++) {
		m_cByteCode.m_listCode[listAddrLine[i]].m_param1.m_numIndex = numCurCompile;
	}

	return true;
}

bool ibCompileCode::CompileWhile(ibCompileContext* context)
{
	context->StartLoopList();

	const int nStartWhile = m_cByteCode.m_listCode.size();

	GETKeyWord(KEY_WHILE);
	ibByteUnit code;
	AddLineInfo(code);
	code.m_numOper = OPER_IF;

	if (gs_codeStyle == CODE_CES)
		GETDelimeter(wxT('('));

	ibParamUnit variable = GetExpression(context);
	code.m_param1 = variable;
	CorrectTypeDef(variable);// check value type

	const int numEndWhile = m_cByteCode.m_listCode.size();

	m_cByteCode.m_listCode.emplace_back(std::move(code));

	if (gs_codeStyle == CODE_VES)
		GETKeyWord(KEY_DO);
	else
		GETDelimeter(wxT(')'));

	if (gs_codeStyle == CODE_CES)
		CompileBlock(CreateLocalContext(context));
	else
		CompileBlock(context);

	if (gs_codeStyle == CODE_VES)
		GETKeyWord(KEY_ENDDO);

	ibByteUnit code2;
	AddLineInfo(code2);
	code2.m_numOper = OPER_GOTO;
	code2.m_param1.m_numIndex = nStartWhile;
	m_cByteCode.m_listCode.emplace_back(std::move(code2));

	m_cByteCode.m_listCode[numEndWhile].m_param2.m_numIndex = m_cByteCode.m_listCode.size();

	// remember the transition addresses for the Continue and Break commands
	context->FinishLoopList(m_cByteCode, m_cByteCode.m_listCode.size() - 1, m_cByteCode.m_listCode.size());

	return true;
}

bool ibCompileCode::CompileFor(ibCompileContext* context)
{
	context->StartLoopList();

	GETKeyWord(KEY_FOR);

	if (gs_codeStyle == CODE_CES)
		GETDelimeter(wxT('('));

	const wxString& strRealName = GETIdentifier(true);
	const wxString& strName = stringUtils::MakeUpper(strRealName);

	ibParamUnit variable = context->GetVariable(strRealName);

	// check variable type
	if (!variable.m_strType.IsEmpty()) {
		if (!ibValue::CompareObjectName(variable.m_strType, ibValueTypes::TYPE_NUMBER)) {
			SetError(ERROR_NUMBER_TYPE);
			return false;
		}
	}

	GETDelimeter('=');
	ibParamUnit variable2 = GetExpression(context);

	ibByteUnit code0;
	AddLineInfo(code0);
	code0.m_numOper = OPER_LET;
	code0.m_param1 = variable;
	code0.m_param2 = variable2;
	m_cByteCode.m_listCode.emplace_back(std::move(code0));

	// check value type
	if (!variable.m_strType.IsEmpty()) {
		if (!ibValue::CompareObjectName(variable2.m_strType, ibValueTypes::TYPE_NUMBER)) {
			SetError(ERROR_BAD_TYPE_EXPRESSION);
			return false;
		}
	}

	GETKeyWord(KEY_TO);

	ibParamUnit variableTo =
		context->GetVariable(strName + wxT("@to"), true, false, false, true); //loop variable

	ibByteUnit code1;
	AddLineInfo(code1);
	code1.m_numOper = OPER_LET;
	code1.m_param1 = variableTo;
	code1.m_param2 = GetExpression(context);
	m_cByteCode.m_listCode.emplace_back(std::move(code1));

	ibByteUnit code;
	AddLineInfo(code);
	code.m_numOper = OPER_FOR;
	code.m_param1 = variable;
	code.m_param2 = variableTo;
	m_cByteCode.m_listCode.emplace_back(std::move(code));

	const int nStartFOR = m_cByteCode.m_listCode.size() - 1;

	if (gs_codeStyle == CODE_VES)
		GETKeyWord(KEY_DO);
	else
		GETDelimeter(wxT(')'));

	if (gs_codeStyle == CODE_CES)
		CompileBlock(CreateLocalContext(context));
	else
		CompileBlock(context);

	if (gs_codeStyle == CODE_VES)
		GETKeyWord(KEY_ENDDO);

	ibByteUnit code2;
	AddLineInfo(code2);
	code2.m_numOper = OPER_NEXT;
	code2.m_param1 = variable;
	code2.m_param2.m_numIndex = nStartFOR;
	m_cByteCode.m_listCode.emplace_back(std::move(code2));

	m_cByteCode.m_listCode[nStartFOR].m_param3.m_numIndex = m_cByteCode.m_listCode.size();

	// remember the transition addresses for the Continue and Break commands
	context->FinishLoopList(m_cByteCode, m_cByteCode.m_listCode.size() - 1, m_cByteCode.m_listCode.size());

	return true;
}

bool ibCompileCode::CompileForeach(ibCompileContext* context)
{
	context->StartLoopList();

	GETKeyWord(KEY_FOREACH);

	if (gs_codeStyle == CODE_CES)
		GETDelimeter(wxT('('));

	const wxString& strRealName = GETIdentifier(true);
	const wxString& strName = stringUtils::MakeUpper(strRealName);

	ibParamUnit variable = context->GetVariable(strRealName);

	GETKeyWord(KEY_IN);

	ibParamUnit variableIn =
		context->GetVariable(strName + wxT("@in_"), true, false, false, true); //loop variable

	ibByteUnit code1;
	AddLineInfo(code1);
	code1.m_numOper = OPER_LET;
	code1.m_param1 = variableIn;
	code1.m_param2 = GetExpression(context);
	m_cByteCode.m_listCode.emplace_back(std::move(code1));

	ibParamUnit variableIt =
		context->GetVariable(strName + wxT("@it_"), true, false, false, true);  //storage iterpos;

	ibByteUnit code;
	AddLineInfo(code);
	code.m_numOper = OPER_FOREACH;
	code.m_param1 = variable;
	code.m_param2 = variableIn;
	code.m_param3 = variableIt; // for storage iterpos;

	m_cByteCode.m_listCode.emplace_back(std::move(code));

	const int numStartFOREACH = m_cByteCode.m_listCode.size() - 1;

	if (gs_codeStyle == CODE_VES)
		GETKeyWord(KEY_DO);
	else
		GETDelimeter(wxT(')'));

	if (gs_codeStyle == CODE_CES)
		CompileBlock(CreateLocalContext(context));
	else
		CompileBlock(context);

	if (gs_codeStyle == CODE_VES)
		GETKeyWord(KEY_ENDDO);

	ibByteUnit code2;
	AddLineInfo(code2);
	code2.m_numOper = OPER_NEXT_ITER;
	code2.m_param1 = variableIt; // for storage iterpos;
	code2.m_param2.m_numIndex = numStartFOREACH;
	m_cByteCode.m_listCode.emplace_back(std::move(code2));

	m_cByteCode.m_listCode[numStartFOREACH].m_param4.m_numIndex = m_cByteCode.m_listCode.size();

	// remember the transition addresses for the Continue and Break commands
	context->FinishLoopList(m_cByteCode, m_cByteCode.m_listCode.size() - 1, m_cByteCode.m_listCode.size());
	return true;
}

bool ibCompileCode::CompileException(ibCompileContext* context)
{
	GETKeyWord(KEY_TRY);
	ibByteUnit code1;
	AddLineInfo(code1);
	code1.m_numOper = OPER_TRY;
	m_cByteCode.m_listCode.emplace_back(std::move(code1));

	const int lineTry = m_cByteCode.m_listCode.size() - 1;

	ibByteUnit code2;
	AddLineInfo(code2);

	if (gs_codeStyle == CODE_CES)
		CompileBlock(CreateLocalContext(context));
	else
		CompileBlock(context);

	code2.m_numOper = OPER_ENDTRY;
	m_cByteCode.m_listCode.emplace_back(std::move(code2));

	const int addrLine = m_cByteCode.m_listCode.size() - 1;

	m_cByteCode.m_listCode[lineTry].m_param1.m_numIndex = m_cByteCode.m_listCode.size();

	GETKeyWord(KEY_EXCEPT);

	if (gs_codeStyle == CODE_CES)
		CompileBlock(CreateLocalContext(context));
	else
		CompileBlock(context);

	if (gs_codeStyle == CODE_VES)
		GETKeyWord(KEY_ENDTRY);

	m_cByteCode.m_listCode[addrLine].m_param1.m_numIndex = m_cByteCode.m_listCode.size();
	return true;
}

/**
 * processing a function or procedure call
 */

ibParamUnit ibCompileCode::GetCallFunction(ibCompileContext* context, const wxString& strRealName, const int& numIsSet)
{
	std::shared_ptr<ibCallFunction> callFunc(new ibCallFunction);

	callFunc->m_strName = stringUtils::MakeUpper(strRealName);
	callFunc->m_strRealName = strRealName;
	callFunc->m_numError = m_numCurrentCompile;// to display messages when errors occur

	GETDelimeter('(');

	while (!IsNextDelimeter(')')) {
		if (IsNextDelimeter(',')) {
			ibParamUnit data;
			data.m_numArray = DEF_VAR_SKIP;// missing parameter
			data.m_numIndex = DEF_VAR_SKIP;
			callFunc->m_listParam.emplace_back(std::move(data));
		}
		else {
			callFunc->m_listParam.emplace_back(GetExpression(context));
			if (!IsNextDelimeter(',') || IsNextDelimeter(')'))
				break;
		}
		GETDelimeter(',');
	}

	GETDelimeter(')');

	ibByteUnit code;
	AddLineInfo(code);

	callFunc->m_numString = code.m_numString;
	callFunc->m_numLine = code.m_numLine;
	callFunc->m_strModuleName = code.m_strModuleName;
	callFunc->m_puRetValue = context->CreateVariable();

	callFunc->m_numIsSet = numIsSet;

	std::shared_ptr<ibCompileContext::ibFunction> foundedFunc = nullptr;
	(void)GetFunction(callFunc->m_strName, foundedFunc);


	// Case-insensitive: m_strCurFuncName holds the function's source-case name
	// ("Fact"), callFunc->m_strName is upper-cased ("FACT"). A case-sensitive
	// `!=` mis-classifies a self-call as a non-recursive call, takes the
	// immediate PushCallFunction path, and emits OPER_CALL with the frame size
	// (param3 = m_lVarCount) still 0 — the count isn't known until AFTER the
	// body compiles (it includes the temps created during it). The whole point
	// of deferring a self-call to m_listCallFunc is to resolve it at finalize
	// once the count is settled, so the recursive frame is sized correctly.
	if (foundedFunc != nullptr && !stringUtils::CompareString(m_strCurFuncName, callFunc->m_strName)) {

		if (!PushCallFunction(callFunc))
			return ibParamUnit();

		return callFunc->m_puRetValue;
	}

	if (IsExpressionOnly()) {
		SetError(ERROR_CALL_FUNCTION, strRealName);
		return ibParamUnit();
	}


	code.m_numOper = OPER_GOTO;// jump to the end of the bytecode where the expanded call will be made
	m_cByteCode.m_listCode.emplace_back(std::move(code));

	ibParamUnit& puRetValue = callFunc->m_puRetValue;

	callFunc->m_numAddLine = m_cByteCode.m_listCode.size() - 1;
	m_listCallFunc.emplace_back(std::move(callFunc));

	return puRetValue;
}

/**
 * gets the constant number from a unique list of values
 * (if such a value is not in the list, it is created)
 */

ibParamUnit ibCompileCode::FindConst(const ibValue& constData)
{
	const wxString& strConstant =
		wxString::Format(wxT("%d:%s"), constData.GetType(), constData.GetString());

	ibParamUnit variable;
	variable.m_numArray = DEF_VAR_CONST;

	if (m_listHashConst.find(strConstant) != m_listHashConst.end()) {
		variable.m_numIndex = m_listHashConst.at(strConstant) - 1;
	}
	else {
		variable.m_numIndex = m_cByteCode.m_listConst.size();
		m_cByteCode.m_listConst.emplace_back(constData);
		m_listHashConst.insert_or_assign(strConstant, variable.m_numIndex + 1);
	}
	variable.m_strType = GetTypeVar(constData.GetClassName());
	return variable;
}

#define SetOper(x)	code.m_numOper=x;

/**
 * compiling an arbitrary expression (service calls from the function itself)
 */

ibParamUnit ibCompileCode::GetExpression(ibCompileContext* context, int nPriority)
{
	const ibLexem& lex = GETLexem();

	// create variable 
	ibParamUnit variable;

	// first we process Left operators
	if ((lex.m_lexType == KEYWORD && lex.m_numData == KEY_NOT) || (lex.m_lexType == DELIMITER && lex.m_numData == '!')) {

		variable = context->CreateVariable();
		variable.m_strType = ibValue::GetNameObjectFromVT(ibValueTypes::TYPE_BOOLEAN, true);

		AddTypeSet(variable);

		ibParamUnit variable2 = GetExpression(context);// , gs_operPriority['!']);

		ibByteUnit code;
		code.m_numOper = OPER_NOT;
		AddLineInfo(code);

		if (!variable2.m_strType.IsEmpty()) {
			CheckTypeDef(variable2, ibValue::GetNameObjectFromVT(ibValueTypes::TYPE_BOOLEAN));
		}

		code.m_param1 = variable;
		code.m_param2 = variable2;

		m_cByteCode.m_listCode.emplace_back(std::move(code));
	}
	else if ((lex.m_lexType == KEYWORD && lex.m_numData == KEY_NEW)) {

		const wxString strObjectName = GETIdentifier(true);
		const int numConst = GetConstString(strObjectName);

		std::vector <ibParamUnit> listParam;

		if (IsNextDelimeter('(')) { // this is a method call
			GETDelimeter('(');
			while (!IsNextDelimeter(')')) {
				if (IsNextDelimeter(',')) {
					ibParamUnit data;
					data.m_numArray = DEF_VAR_SKIP;// missing parameter
					data.m_numIndex = DEF_VAR_SKIP;
					listParam.emplace_back(std::move(data));
				}
				else {
					listParam.emplace_back(GetExpression(context));
					if (!IsNextDelimeter(',') || IsNextDelimeter(')'))
						break;
				}
				GETDelimeter(',');
			}
			GETDelimeter(')');
		}

		if (lex.m_numData == KEY_NEW && !ibValue::IsRegisterCtor(strObjectName, ibCtorObjectType::ibCtorObjectType_object_value)) {
			SetError(ERROR_CALL_CONSTRUCTOR, strObjectName);
			return ibParamUnit();
		}

		ibByteUnit code;
		AddLineInfo(code);
		code.m_numOper = OPER_NEW;

		code.m_param2.m_numIndex = numConst;//number of the called method from the list of encountered methods
		code.m_param2.m_numArray = listParam.size();// number of parameters

		variable = context->CreateVariable();
		code.m_param1 = variable;// variable into which the value is returned
		m_cByteCode.m_listCode.emplace_back(std::move(code));

		for (unsigned int arg = 0; arg < listParam.size(); arg++) {
			ibByteUnit code;
			AddLineInfo(code);
			code.m_numOper = OPER_SET;
			code.m_param1 = listParam[arg];
			m_cByteCode.m_listCode.emplace_back(std::move(code));
		}
	}
	else if (lex.m_lexType == KEYWORD && lex.m_numData == KEY_FROM) {
		// LINQ block expression — `from <id> in <expr> ... select <expr>`.
		// Step back so CompileLinqExpression's own GETKeyWord(KEY_FROM)
		// can re-consume the keyword via its standard path; mirrors the
		// CompileLambdaExpression idiom below. Returns the slot of the
		// materialised result array (eager Phase 1: inline foreach +
		// array build).
		m_numCurrentCompile--;
		variable = CompileLinqExpression(context);
	}
	else if (lex.m_lexType == KEYWORD &&
		(lex.m_numData == KEY_FUNCTION || lex.m_numData == KEY_PROCEDURE)) {
		// Anonymous Function/Procedure as expression — Phase A lambda.
		// Step back so CompileLambdaExpression's ParseFunctionSignature
		// can re-consume the keyword via its standard
		// IsNextKeyWord/GETKeyWord path; mirrors the IDENTIFIER-branch
		// step-back idiom below.
		m_numCurrentCompile--;
		variable = CompileLambdaExpression(context);
	}
	else if (lex.m_lexType == DELIMITER && lex.m_numData == '(') {
		variable = GetExpression(context);
		GETDelimeter(')');
	}
	else if (lex.m_lexType == DELIMITER && lex.m_numData == '?') {
		variable = context->CreateVariable();
		ibByteUnit code;
		AddLineInfo(code);
		code.m_numOper = OPER_ITER;
		code.m_param1 = variable;
		GETDelimeter('(');
		code.m_param2 = GetExpression(context);
		GETDelimeter(',');
		code.m_param3 = GetExpression(context);
		GETDelimeter(',');
		code.m_param4 = GetExpression(context);
		GETDelimeter(')');
		m_cByteCode.m_listCode.emplace_back(std::move(code));
	}
	else if (lex.m_lexType == IDENTIFIER) {
		// postfix ++ / -- in expression on a BARE variable: `x++` / `x--` yields
		// the OLD value, then stores x = x +/- 1. Gate: the identifier is
		// IMMEDIATELY followed by a doubled, source-ADJACENT +/- — so member /
		// array GET-temps (`obj.a++`, `arr[i]++`) fall through to a normal read,
		// and `a - -b` (spaced) is not mistaken for `a--`.
		wxUniChar incOp = 0;
		if (m_numCurrentCompile + 2 < m_listLexem.size()
			&& m_listLexem[m_numCurrentCompile + 1].m_lexType == DELIMITER
			&& m_listLexem[m_numCurrentCompile + 2].m_lexType == DELIMITER
			&& (m_listLexem[m_numCurrentCompile + 1].m_numData == '+' || m_listLexem[m_numCurrentCompile + 1].m_numData == '-')
			&& m_listLexem[m_numCurrentCompile + 1].m_numData == m_listLexem[m_numCurrentCompile + 2].m_numData
			&& m_listLexem[m_numCurrentCompile + 2].m_numString == m_listLexem[m_numCurrentCompile + 1].m_numString + 1)
			incOp = (wxUniChar)m_listLexem[m_numCurrentCompile + 1].m_numData;

		if (incOp != 0) {
			const wxString strRealName = lex.m_valData.GetString();
			GETDelimeter(incOp);
			GETDelimeter(incOp);
			ibParamUnit target = context->GetVariable(strRealName, true, true); // lvalue, must exist
			// expression value = a temp holding the OLD value (captured before the store)
			variable = context->CreateVariable();
			ibByteUnit getOld;
			AddLineInfo(getOld);
			getOld.m_numOper = OPER_LET;
			getOld.m_param1 = variable;
			getOld.m_param2 = target;
			m_cByteCode.m_listCode.emplace_back(std::move(getOld));
			// store target = target +/- 1
			ibByteUnit incCode;
			AddLineInfo(incCode);
			incCode.m_numOper = (incOp == '+') ? OPER_ADD : OPER_SUB;
			ibValue oneVal; oneVal.SetNumber(wxT("1"));
			incCode.m_param1 = target;
			incCode.m_param2 = target;
			incCode.m_param3 = FindConst(oneVal);
			m_cByteCode.m_listCode.emplace_back(std::move(incCode));
		}
		else {
			m_numCurrentCompile--;// step back
			int numSet = 0;
			variable = GetCurrentIdentifier(context, numSet);
		}
	}
	else if (lex.m_lexType == CONSTANT) {
		variable = FindConst(lex.m_valData);
	}
	else if ((lex.m_lexType == DELIMITER && lex.m_numData == '+') || (lex.m_lexType == DELIMITER && lex.m_numData == '-')) {

		// prefix ++ / -- in expression: `++x` / `--x` store x = x +/- 1, then
		// yield x (the new value). Caught BEFORE the unary-sign logic below —
		// else `++x` parses as `+(+x)` and silently no-ops. Require the two
		// signs ADJACENT in source (m_numString) so `a - -b` (spaced) stays a
		// binary minus + unary minus, not a phantom `a--`.
		if (m_numCurrentCompile + 1 < m_listLexem.size()
			&& m_listLexem[m_numCurrentCompile + 1].m_lexType == DELIMITER
			&& m_listLexem[m_numCurrentCompile + 1].m_numData == lex.m_numData
			&& m_listLexem[m_numCurrentCompile + 1].m_numString == m_listLexem[m_numCurrentCompile].m_numString + 1) {
			const wxUniChar incOp = lex.m_numData;
			GETDelimeter(incOp); // consume the second +/-
			const wxString strRealName = GETIdentifier(true); // target — a bare variable
			ibParamUnit target = context->GetVariable(strRealName, true, true); // must already exist
			ibByteUnit incCode;
			AddLineInfo(incCode);
			incCode.m_numOper = (incOp == '+') ? OPER_ADD : OPER_SUB;
			ibValue oneVal; oneVal.SetNumber(wxT("1"));
			incCode.m_param1 = target;
			incCode.m_param2 = target;
			incCode.m_param3 = FindConst(oneVal);
			m_cByteCode.m_listCode.emplace_back(std::move(incCode));
			variable = target; // prefix yields the NEW value; rejoin the operator loop
			goto delimOperation;
		}

		// Unary sign at expression-start position — always allowed,
		// regardless of caller's binary-priority context. Earlier this
		// branch compared nPriority (caller's binary op priority, e.g.
		// `*` priority when invoked from `a * -b`) against
		// gs_operPriority[lex.m_numData] (which holds the BINARY
		// priority of `-`/`+`) and errored out for `a * -b` because
		// `*` binary priority > `-` binary priority. But syntactically
		// the sign here is unary — we're at expression-start, no LHS
		// to subtract from. RHS-recursion priority is 100 (super-high)
		// to keep `- a + b` parsing as `(-a) + b`, not `-(a + b)`.

		// this is a user-defined expression sign
		if (lex.m_numData == '+') { // do nothing (ignore)
			ibByteUnit code;
			variable = GetExpression(context, 100);   // super high priority!
			if (!variable.m_strType.IsEmpty()) {
				CheckTypeDef(variable, ibValue::GetNameObjectFromVT(ibValueTypes::TYPE_NUMBER));
			}
			variable.m_strType = ibValue::GetNameObjectFromVT(ibValueTypes::TYPE_NUMBER, true);
			return variable;
		}
		else {
			variable = GetExpression(context, 100);//super high priority!
			ibByteUnit code;
			AddLineInfo(code);
			code.m_numOper = OPER_INVERT;

			if (!variable.m_strType.IsEmpty()) {
				CheckTypeDef(variable, ibValue::GetNameObjectFromVT(ibValueTypes::TYPE_NUMBER));
			}

			code.m_param2 = variable;
			variable = context->CreateVariable();
			variable.m_strType = ibValue::GetNameObjectFromVT(ibValueTypes::TYPE_NUMBER, true);
			AddTypeSet(variable);
			code.m_param1 = variable;
			m_cByteCode.m_listCode.emplace_back(std::move(code));
		}
	}
	else {
		m_numCurrentCompile--;
		SetError(ERROR_EXPRESSION);
		return ibParamUnit();
	}

	// now we process Right Operators
	// so in variable we have the first index of the expression variable

delimOperation:

	const ibLexem& prevLexem = PreviewGetLexem();

	if (prevLexem.m_lexType == DELIMITER && prevLexem.m_numData == ')')
		return variable;

	// we look to see if there are any further operators for performing actions on this variable
	if ((prevLexem.m_lexType == DELIMITER && prevLexem.m_numData != ';') || (prevLexem.m_lexType == KEYWORD && prevLexem.m_numData == KEY_AND) || (prevLexem.m_lexType == KEYWORD && prevLexem.m_numData == KEY_OR)) {
		if (prevLexem.m_numData >= 0 && prevLexem.m_numData <= 255) {
			const int numCurPriority = gs_operPriority[prevLexem.m_numData];
			if (nPriority < numCurPriority) { // �ompare the priorities of the left (previous operation) and the currently running operation

				ibByteUnit code;
				AddLineInfo(code);
				const ibLexem& next_lex = GetLexem();

				if (next_lex.m_numData == '*') {
					SetOper(OPER_MULT);
				}
				else if (next_lex.m_numData == '/') {
					SetOper(OPER_DIV);
				}
				else if (next_lex.m_numData == '+') {
					SetOper(OPER_ADD);
				}
				else if (next_lex.m_numData == '-') {
					SetOper(OPER_SUB);
				}
				else if (next_lex.m_numData == '%') {
					SetOper(OPER_MOD);
				}
				else if (next_lex.m_numData == KEY_AND) {
					SetOper(OPER_AND);
				}
				else if (next_lex.m_numData == KEY_OR) {
					SetOper(OPER_OR);
				}
				else if (next_lex.m_numData == '>') {
					SetOper(OPER_GT);
					if (IsNextDelimeter('=')) {
						GETDelimeter('=');
						SetOper(OPER_GE);
					}
				}
				else if (next_lex.m_numData == '<') {
					SetOper(OPER_LS);
					if (IsNextDelimeter('=')) {
						GETDelimeter('=');
						SetOper(OPER_LE);
					}
					else if (IsNextDelimeter('>')) {
						GETDelimeter('>');
						SetOper(OPER_NE);
					}
				}
				else if (next_lex.m_numData == '=') {
					SetOper(OPER_EQ);
				}
				else {
					SetError(ERROR_EXPRESSION);
					return ibParamUnit();
				}

				ibParamUnit puVariable1 = context->CreateVariable();
				ibParamUnit puVariable2 = variable;
				ibParamUnit puVariable3 = GetExpression(context, numCurPriority);

				if (puVariable3.m_numArray != DEF_VAR_TEMP && puVariable3.m_numArray != DEF_VAR_CONST) { // extra. checking for prohibited operations
					if (ibValue::CompareObjectName(puVariable2.m_strType, ibValueTypes::TYPE_STRING)) {
						if (OPER_DIV == code.m_numOper
							|| OPER_MOD == code.m_numOper
							|| OPER_MULT == code.m_numOper
							|| OPER_AND == code.m_numOper
							|| OPER_OR == code.m_numOper) {
							SetError(ERROR_TYPE_OPERATION);
							return ibParamUnit();
						}
					}
				}

				if (puVariable2.m_numArray != DEF_VAR_CONST && puVariable2.m_numArray != DEF_VAR_TEMP) { // constants are not checked - because they are typified by default
					CheckTypeDef(puVariable3, puVariable2.m_strType);
				}

				puVariable1.m_strType = puVariable2.m_strType;

				if (code.m_numOper >= OPER_GT && code.m_numOper <= OPER_NE) {
					puVariable1.m_strType = ibValue::GetNameObjectFromVT(ibValueTypes::TYPE_BOOLEAN, true);
				}

				code.m_param1 = puVariable1;
				code.m_param2 = puVariable2;
				code.m_param3 = puVariable3;

				m_cByteCode.m_listCode.emplace_back(std::move(code));

				variable = puVariable1;
				goto delimOperation;
			}
		}
	}

	return variable;
}

void ibCompileCode::SetParent(ibCompileCode* setParent)
{
	// Single source of truth: bytecode parent. Compile-side GetParent()
	// derives from m_cByteCode.m_parent->m_compileModule.
	m_cByteCode.m_parent = nullptr;
	m_rootContext->m_parentContext = nullptr;

	if (setParent != nullptr) {
		m_cByteCode.m_parent = &setParent->m_cByteCode;
		m_rootContext->m_parentContext = setParent->m_rootContext;
		AddDependency(&setParent->m_cByteCode);
	}

	OnSetParent(setParent);
}

#pragma warning(pop)
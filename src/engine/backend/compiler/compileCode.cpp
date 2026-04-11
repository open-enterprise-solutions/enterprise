////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, 2�-team
//	Description : compile module 
////////////////////////////////////////////////////////////////////////////

#include "compileCode.h"
#include "codeDef.h"

#include "system/systemManager.h"

#pragma warning(push)
#pragma warning(disable : 4018)

//////////////////////////////////////////////////////////////////////
//                           Constants
//////////////////////////////////////////////////////////////////////

// array of mathematical operation priorities
static std::array<int, 256> gs_operPriority = { 0 };

// set code style by file extension
static short gs_codeStyle = CODE_VBS;

//////////////////////////////////////////////////////////////////////
// Construction/Destruction ibCompileCode
//////////////////////////////////////////////////////////////////////

ibCompileCode::ibCompileCode() :
	ibTranslateCode(),
	m_parent(nullptr), m_rootContext(new ibCompileContext(this)),
	m_bExpressionOnly(false), m_changedCode(false),
	m_onlyFunction(false)
{
	InitializeCompileModule();

	// we don�t look for local variables in parent contexts!
	m_rootContext->m_numFindLocalInParent = 0;
}

ibCompileCode::ibCompileCode(const wxString& strModuleName, const wxString& strDocPath, bool onlyFunction) :
	ibTranslateCode(strModuleName, strDocPath),
	m_parent(nullptr), m_rootContext(new ibCompileContext(this)),
	m_bExpressionOnly(false), m_changedCode(false),
	m_onlyFunction(onlyFunction)
{
	InitializeCompileModule();

	// we don�t look for local variables in parent contexts!
	m_rootContext->m_numFindLocalInParent = 0;
}

ibCompileCode::ibCompileCode(const wxString& strFileName) :
	ibTranslateCode(strFileName),
	m_parent(nullptr), m_rootContext(new ibCompileContext(this)),
	m_bExpressionOnly(false), m_changedCode(false),
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
	for (auto& externValue : m_listExternValue) {
		m_rootContext->AddVariable(externValue.first, wxEmptyString, true);
		m_cByteCode.m_listExternValue.emplace_back(externValue.second);
	}

	for (auto& contextValue : m_listContextValue) {
		m_rootContext->AddVariable(contextValue.first, wxEmptyString, true, true);
		m_cByteCode.m_listExternValue.emplace_back(contextValue.second);
	}

	ibCompileContext* mainContext = GetContext();

	for (auto& pair : m_listContextValue) {

		ibValue* contextValue = pair.second;
		wxASSERT(contextValue);
		contextValue->PrepareNames();

		// adding variables from context
		for (unsigned int i = 0; i < contextValue->GetNProps(); i++) {
			// determine the number and type of the variable
			mainContext->PushVariable(
				contextValue->GetPropName(i), pair.first, i);
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

		strFileName = m_listLexem[m_numCurrentCompile].m_strFileName;
		strModuleName = m_listLexem[m_numCurrentCompile].m_strModuleName;
		strDocPath = m_listLexem[m_numCurrentCompile].m_strDocPath;

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
			code.m_strModuleName = m_listLexem[m_numCurrentCompile].m_strModuleName;
			code.m_strDocPath = m_listLexem[m_numCurrentCompile].m_strDocPath;
			code.m_strFileName = m_listLexem[m_numCurrentCompile].m_strFileName;
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

wxString ibCompileCode::GETIdentifier(bool strRealName)
{
	const ibLexem& lex = GetLexem();
	if (lex.m_lexType != IDENTIFIER) {
		m_numCurrentCompile--;
		SetError(ERROR_IDENTIFIER_DEFINE);
		return wxEmptyString;
	}

	if (strRealName) {
		return lex.m_valData.m_sData;
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
	m_listExternValue[strVarName.Upper()] = vObject.m_typeClass == ibValueTypes::TYPE_REFFER
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
	m_listExternValue[strVarName.Upper()] = pValue;

	//set the flag for recompilation
	m_changedCode = true;
}

/**
 * AddContextVariable
 * Purpose:
 * Add the name and address of an external variable to a special array for later use
 */

void ibCompileCode::AddContextVariable(const wxString& strVarName, const ibValue& vObject)
{
	if (strVarName.IsEmpty())
		return;

	//adding variables from context
	m_listContextValue[strVarName.Upper()] = vObject.m_typeClass == ibValueTypes::TYPE_REFFER ? vObject.GetRef() : const_cast<ibValue*>(&vObject);

	//set the flag for recompilation
	m_changedCode = true;
}

/**
 * AddContextVariable
 * Purpose:
 * Add the name and address of an external variable to a special array for later use
 */

void ibCompileCode::AddContextVariable(const wxString& strVarName, ibValue* pValue)
{
	if (strVarName.IsEmpty())
		return;

	//adding variables from context
	m_listContextValue[strVarName.Upper()] = pValue;

	//set the flag for recompilation
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

	m_listExternValue.erase(strVarName.Upper());
	m_listContextValue.erase(strVarName.Upper());

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
			std::shared_ptr<ibCompileContext::ibVariable> currentVariable = nullptr;
			if (pCurContext->FindVariable(strName, currentVariable)) { // found
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

		bool bExport = false;

		if (IsNextKeyWord(KEY_EXPORT)) {
			if (bExport) // there was an Export announcement
				break;
			GETKeyWord(KEY_EXPORT);
			bExport = true;
		}

		// there was no variable declaration yet - add
		ibParamUnit variable =
			context->AddVariable(strRealName, strType, bExport);

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

	// get a list of variables
	for (auto it : mainContext->m_listVariable) {
		if (it.second->m_bTempVar || it.second->m_bContext)
			continue;
		m_cByteCode.m_listVar[it.first] = it.second->m_numVariable;
		if (it.second->m_bExport) {
			m_cByteCode.m_listExportVar[it.first] = it.second->m_numVariable;
		}
	}

	if (m_numCurrentCompile + 1 < m_listLexem.size() - 1) {
		SetError(ERROR_END_PROGRAM);
		return false;
	}

	m_cByteCode.SetModule(this);

	// compilation completed successfully
	m_cByteCode.m_bCompile = true;

	return true;
}

// search for function definition in the current module and all parent ones
bool ibCompileCode::GetFunction(const wxString& strName, std::shared_ptr<ibCompileContext::ibFunction>& function, int* pNumFunction)
{
	int numCanUseLocalInParent = m_rootContext->m_numFindLocalInParent - 1;
	int numFunction = 0;

	// search in the current module
	if (!GetContext()->FindFunction(strName, function)) {
		ibCompileCode* pCurModule = m_parent;
		while (pCurModule != nullptr) {
			numFunction++;
			if (pCurModule->GetContext()->FindFunction(strName, function)) { // found
				// see if this is an export function or not
				if (numCanUseLocalInParent > 0 || function->m_bExport)
					break;//��
				function = nullptr;
			}
			numCanUseLocalInParent--;
			pCurModule = pCurModule->m_parent;
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

	if (!callFunction->m_numIsSet && foundedFunc->m_compileContext && foundedFunc->m_compileContext->m_numReturn != RETURN_FUNCTION) {
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
		code.m_numOper = OPER_CALL_M;
		code.m_param1 = callFunction->m_puRetValue;		// variable into which the value is returned
		code.m_param2 = callFunction->m_puContextVal;	// variable on which the method is called
		code.m_param3.m_numIndex = GetConstString(callFunction->m_strName);	// number of the called method from the list of encountered methods
		code.m_param3.m_numArray = numDefCount;	// number of parameters
	}
	else {
		code.m_numOper = OPER_CALL;
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
	ibCompileContext* functionContext = nullptr;

	// we are now at the token level, where the FUNCTION or PROCEDURE keyword is specified
	if (IsNextKeyWord(KEY_FUNCTION)) {
		GETKeyWord(KEY_FUNCTION);
		// create a new context in which we will compile the function body
		functionContext = context->CreateContext(RETURN_FUNCTION);
	}
	else if (IsNextKeyWord(KEY_PROCEDURE)) {
		GETKeyWord(KEY_PROCEDURE);
		// create a new context in which we will compile the body of the procedure
		functionContext = context->CreateContext(RETURN_PROCEDURE);
	}
	else {
		SetError(ERROR_FUNC_DEFINE);
		return false;
	}

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

	// get the function name
	const wxString& strFuncRealName = GETIdentifier(true);
	const wxString& strFuncName = stringUtils::MakeUpper(strFuncRealName);

	const int errorPlace = m_numCurrentCompile;

	std::shared_ptr<ibCompileContext::ibFunction> createdFunction(new ibCompileContext::ibFunction(strFuncName, functionContext));

	createdFunction->m_strRealName = strFuncRealName;
	createdFunction->m_strShortDescription = strShortDescription;
	createdFunction->m_numLine = numLine;

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
		createdFunction->m_listParam.emplace_back(std::move(cVariable));

		if (!IsNextDelimeter(',') || IsNextDelimeter(')'))
			break;

		GETDelimeter(',');
	}
	GETDelimeter(')');
	if (IsNextKeyWord(KEY_EXPORT)) {
		GETKeyWord(KEY_EXPORT);
		createdFunction->m_bExport = true;
	}

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

	// insert information about the function into the bytecode array:
	ibByteUnit code0;
	AddLineInfo(code0);
	code0.m_numOper = OPER_FUNC;
#if defined(_LP64) || defined(__LP64__) || defined(__arch64__) || defined(_WIN64)
	code0.m_param1.m_numArray = reinterpret_cast<wxLongLong_t>(functionContext);
#else
	code0.m_param1.m_numArray = reinterpret_cast<int>(functionContext);
#endif
	m_cByteCode.m_listCode.emplace_back(std::move(code0));

	const long lAddress = createdFunction->m_nStart = m_cByteCode.m_listCode.size() - 1;

	m_cByteCode.m_listFunc[strFuncName] = {
		(long)createdFunction->m_listParam.size(),
		lAddress,
		functionContext->m_numReturn == RETURN_FUNCTION
	};

	if (createdFunction->m_bExport) {
		m_cByteCode.m_listExportFunc[strFuncName] = {
			(long)createdFunction->m_listParam.size(),
			lAddress,
			functionContext->m_numReturn == RETURN_FUNCTION
		};
	}

	for (unsigned int i = 0; i < createdFunction->m_listParam.size(); i++) {
		//add set oper
		ibByteUnit code;
		AddLineInfo(code);
		if (createdFunction->m_listParam[i].m_puValue.m_numArray == DEF_VAR_CONST) {
			code.m_numOper = OPER_SETCONST;// parameters are being passed
		}
		else {
			code.m_numOper = OPER_SET;// parameters are being passed
		}
		code.m_param1 = createdFunction->m_listParam[i].m_puValue;
		code.m_param2.m_numIndex = createdFunction->m_listParam[i].m_bByRef;
		m_cByteCode.m_listCode.emplace_back(std::move(code));

		//Set type variable
		ibParamUnit variable;

		variable.m_numArray = 0;
		variable.m_numIndex = i;// index matches the number

		variable.m_strType = createdFunction->m_listParam[i].m_strType;

		AddTypeSet(variable);
	}

	m_strCurFuncName = strFuncName;

	CompileBlock(functionContext);

	functionContext->CreateLabels();

	m_strCurFuncName = wxEmptyString;

	if (gs_codeStyle == CODE_VBS) {

		if (functionContext->m_numReturn == RETURN_FUNCTION) {
			GETKeyWord(KEY_ENDFUNCTION);
		}
		else {
			GETKeyWord(KEY_ENDPROCEDURE);
		}
	}

	ibByteUnit code;
	AddLineInfo(code);
	code.m_numOper = OPER_ENDFUNC;
	m_cByteCode.m_listCode.emplace_back(std::move(code));

	createdFunction->m_nFinish = m_cByteCode.m_listCode.size() - 1;
	createdFunction->m_lVarCount = functionContext->m_listVariable.size();

	m_cByteCode.m_listCode[lAddress].m_param3.m_numIndex = createdFunction->m_lVarCount;// number of local variables
	m_cByteCode.m_listCode[lAddress].m_param3.m_numArray = createdFunction->m_listParam.size();//number of formal parameters
	return true;
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

	if (gs_codeStyle == CODE_CES && (context->m_numReturn != RETURN_NONE && context->m_numReturn != RETURN_BLOCK) || IsNextDelimeter(wxT('{'))) {
		GETDelimeter(wxT('{'));
		bCompileBlock = true;
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

				if (currContext->m_numReturn == RETURN_FUNCTION) { // some value is returned
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

				if (gs_codeStyle == CODE_VBS)
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
				m_numCurrentCompile--;// step back

				if (context->m_numReturn != RETURN_NONE)
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

	if (gs_codeStyle == CODE_CES && bCompileBlock)
		GETDelimeter(wxT('}'));

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
			if (!numIsSet && foundedFunc != nullptr &&
				foundedFunc->m_compileContext && foundedFunc->m_compileContext->m_numReturn != RETURN_FUNCTION) {
				SetError(ERROR_USE_PROCEDURE_AS_FUNCTION, foundedFunc->m_strRealName);
				return ibParamUnit();
			}
			if (listParam.size() > foundedFunc->m_listParam.size()) {
				SetError(ERROR_MANY_PARAMS); // too many parameters
				return ibParamUnit();
			}

			ibByteUnit code;
			AddLineInfo(code);
			code.m_numOper = OPER_CALL_M;

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
			variable = GetCallFunction(context, strRealName, numIsSet);
		}
		if (IsTypeVar(strRealName)) {
			variable.m_strType = GetTypeVar(strRealName);	// this is a type cast
		}
		numIsSet = 0;
	}
	else { //this is a variable call
		std::shared_ptr<ibCompileContext::ibVariable> foundedVar = nullptr; numIsSet = 1;
		if (m_rootContext->FindVariable(strRealName, foundedVar, true)) {
			ibByteUnit code;
			AddLineInfo(code);
			const int numConst = GetConstString(strRealName);
			if (IsNextDelimeter('=') && numPrevSet == 1) {
				GETDelimeter('='); numIsSet = 0;
				code.m_numOper = OPER_SET_A;
				code.m_param1 = context->GetVariable(foundedVar->m_strContext, true, false, true);//variable for which the attribute is called
				code.m_param2.m_numIndex = numConst;//number of the called method from the list of encountered attributes and methods
				code.m_param3 = GetExpression(context);
				m_cByteCode.m_listCode.emplace_back(std::move(code));
				return variable;
			}
			else {
				code.m_numOper = OPER_GET_A;
				code.m_param2 = context->GetVariable(foundedVar->m_strContext, true, false, true);//variable for which the attribute is called
				code.m_param3.m_numIndex = numConst;//number of the attribute to be called from the list of attributes and methods encountered
				variable = context->CreateVariable();
				code.m_param1 = variable;// variable into which the value is returned
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
		wxString strIdentifier = GETIdentifier(true);
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
			code.m_numOper = OPER_CALL_M;
			code.m_param2 = variable; // variable on which the method is called
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

	if (gs_codeStyle == CODE_VBS)
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

		if (gs_codeStyle == CODE_VBS)
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

	if (gs_codeStyle == CODE_VBS)
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

	if (gs_codeStyle == CODE_VBS)
		GETKeyWord(KEY_DO);
	else
		GETDelimeter(wxT(')'));

	if (gs_codeStyle == CODE_CES)
		CompileBlock(CreateLocalContext(context));
	else
		CompileBlock(context);

	if (gs_codeStyle == CODE_VBS)
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

	if (gs_codeStyle == CODE_VBS)
		GETKeyWord(KEY_DO);
	else
		GETDelimeter(wxT(')'));

	if (gs_codeStyle == CODE_CES)
		CompileBlock(CreateLocalContext(context));
	else
		CompileBlock(context);

	if (gs_codeStyle == CODE_VBS)
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

	if (gs_codeStyle == CODE_VBS)
		GETKeyWord(KEY_DO);
	else
		GETDelimeter(wxT(')'));

	if (gs_codeStyle == CODE_CES)
		CompileBlock(CreateLocalContext(context));
	else
		CompileBlock(context);

	if (gs_codeStyle == CODE_VBS)
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

	if (gs_codeStyle == CODE_VBS)
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

	if (foundedFunc != nullptr && m_strCurFuncName != callFunc->m_strName) {

		if (!PushCallFunction(callFunc))
			return ibParamUnit();

		return callFunc->m_puRetValue;
	}

	if (m_bExpressionOnly) {
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
		m_numCurrentCompile--;// step back
		int numSet = 0;
		variable = GetCurrentIdentifier(context, numSet);
	}
	else if (lex.m_lexType == CONSTANT) {
		variable = FindConst(lex.m_valData);
	}
	else if ((lex.m_lexType == DELIMITER && lex.m_numData == '+') || (lex.m_lexType == DELIMITER && lex.m_numData == '-')) {

		// check the admissibility of such assignment
		const int numCurPriority = gs_operPriority[lex.m_numData];

		if (nPriority >= numCurPriority) {
			SetError(ERROR_EXPRESSION);// �ompare the priorities of the left (previous operation) and the currently running operation
			return ibParamUnit();
		}

		// this is a user-defined expression sign
		if (lex.m_numData == '+') { // do nothing (ignore)
			ibByteUnit code;
			variable = GetExpression(context, nPriority);
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
	m_cByteCode.m_parent = nullptr;

	m_parent = setParent;
	m_rootContext->m_parentContext = nullptr;

	if (m_parent != nullptr) {
		m_cByteCode.m_parent = &m_parent->m_cByteCode;
		m_rootContext->m_parentContext = m_parent->m_rootContext;
	}

	OnSetParent(setParent);
}

#pragma warning(pop)
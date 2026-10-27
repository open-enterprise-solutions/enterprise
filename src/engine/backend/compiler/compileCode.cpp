////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : compile module
////////////////////////////////////////////////////////////////////////////

#include "compileCode.h"
#include "core/compiler/codeDef.h"
#include "lambdaQueryAST.h"   // L4-2 — lambda body -> L4 query AST (pushdown)

#include "system/systemManager.h"
#include "backend/session/session.h"       // GetCompileState — the code style lives there
#include "core/fiber/fiber.h"              // StackLow — expression nesting is native recursion

// The mark a filter writes on a comparison (procUnitLambda.h, LINQ_THREE_VALUED_NULL).
// Repeated here so the compiler does not include the interpreter. OPER_IF treats a
// NULL condition as "does not decide" when an `and` sets this on m_param4.
constexpr long kAndUnknownMark = 1;

#pragma warning(push)
#pragma warning(disable : 4018)

//////////////////////////////////////////////////////////////////////
//                           Constants
//////////////////////////////////////////////////////////////////////

// Array of mathematical operation priorities, indexed by a lexem's m_numData
// (delimiter character code, or a KEY_* id for the two word operators).
//
// It used to be filled by InitializeCompileModule(), which every ibCompileCode ctor
// called behind `if (gs_operPriority[last]) return;` — the last slot doubling as an
// "already filled" sentinel. Unguarded check-then-fill: two sessions compiling at
// once both see the sentinel clear and both write, and a third can read a slot the
// filler has not reached yet. A zero priority there does not crash, it silently
// mis-associates the expression — `a + b * c` compiled with the wrong tree. Built
// once, before main, and const from then on, that cannot happen.
// Being a constant, it can be built by the COMPILER — constexpr rather than a
// load-time initialiser, so there is no initialisation order to reason about either.
static constexpr std::array<int, 256> MakeOperPriority()
{
	std::array<int, 256> listPriority = {};

	listPriority['+'] = 10;
	listPriority['-'] = 10;
	listPriority['*'] = 30;
	listPriority['/'] = 30;
	listPriority['%'] = 30;
	// `!` / `Not` as a PREFIX takes its operand up to the next `And` / `Or` — see the KEY_NOT branch of
	// GetExpression. This entry is `!` met AFTER an operand, which the language refuses.
	listPriority['!'] = 50;

	listPriority[KEY_OR] = 1;
	listPriority[KEY_AND] = 2;
	// `Mod` binds exactly as `%` does — the same operator spelled with a word.
	listPriority[KEY_MOD] = 30;

	listPriority['>'] = 3;
	listPriority['<'] = 3;
	listPriority['='] = 3;

	// ⭐⭐ `x in (a, b, c)` — a COMPARISON, and it binds like one. It is sugar and stays sugar: the
	// compiler expands it into `(x = a) or (x = b) or (x = c)`, so nothing new reaches the runtime
	// and every reader that already understands a comparison understands this.
	//
	// 🛑 IT HAD TO BECOME PART OF THE LANGUAGE. `in` used to be understood only by the LINQ lambda
	// recorder — a second grammar, wider than the language, that read the tokens of a predicate and
	// built its own tree. When the recorder went (the tree is read off the INSTRUCTIONS now), `in`
	// went with it: `restrict s in Source where s.Code in ("A", "B")` stopped compiling, because the
	// language itself had never known the word. Either the feature exists in the language or it does
	// not exist; a grammar only one reader can see is how the two drift apart.
	listPriority[KEY_IN] = 3;

	listPriority[listPriority.size() - 1] = 1;//was the sentinel; kept so index 255 reads as it did

	return listPriority;
}

static constexpr std::array<int, 256> gs_operPriority = MakeOperPriority();

//////////////////////////////////////////////////////////////////////
// Construction/Destruction ibCompileCode
//////////////////////////////////////////////////////////////////////

// The initialiser lists below follow the DECLARATION order in compileCode.h
// (m_onlyFunction, m_translateCode, m_cByteCode, m_rootContext, m_numCurrentCompile, m_changedCode).
// Members are constructed in declaration order regardless of what the list says, so a
// list in any other order reads as a promise the language does not keep — and
// ibCompileContext's ctor takes `this`.

ibCompileCode::ibCompileCode() :
	m_onlyFunction(false),
	m_translateCode(GetCodeStyle()),
	m_rootContext(new ibCompileContext(this)),
	m_changedCode(false)
{
	// we do not look for local variables in parent contexts!
	m_rootContext->m_numFindLocalInParent = 0;
}

ibCompileCode::ibCompileCode(const ibString& strModuleName, const ibString& strDocPath, bool onlyFunction) :
	m_onlyFunction(onlyFunction),
	m_translateCode(GetCodeStyle(), strModuleName, strDocPath),
	m_rootContext(new ibCompileContext(this)),
	m_changedCode(false)
{
	// we do not look for local variables in parent contexts!
	m_rootContext->m_numFindLocalInParent = 0;
}

ibCompileCode::ibCompileCode(const ibString& strFileName) :
	m_onlyFunction(false),
	m_translateCode(GetCodeStyle(), strFileName),
	m_rootContext(new ibCompileContext(this)),
	m_changedCode(false)
{
	// we do not look for local variables in parent contexts!
	m_rootContext->m_numFindLocalInParent = 0;
}

ibCompileCode::~ibCompileCode()
{
	Reset();

	m_listExternValue.clear();
	m_listContextValue.clear();
}

// THE CODE STYLE IS THE COMPILE STATE'S (ibCompileState) — the current session's, which holds its
// configuration's; a thread without a session its own: codeRunner's syntax choice, a test. It used to be one
// static for the process, and a process may hold bases written in different syntaxes.
// CES is the default — modern brace/paren syntax with `;` terminators.
// VES (Visual Basic-style ES, a legacy business-scripting dialect) remains supported for legacy ES
// configurations migrated from a legacy business-scripting platform.
void ibCompileCode::SetCodeStyle(short codeStyle)
{
	ibSession::GetCompileState()->m_codeStyle = static_cast<ibProgramSyntax>(codeStyle);
}

short ibCompileCode::GetCodeStyle()
{
	return ibSession::GetCompileState()->m_codeStyle;
}

void ibCompileCode::Reset()
{
	m_cByteCode.Reset();

	if (m_rootContext != nullptr)
		m_rootContext->Reset();

	m_listHashConst.clear();
	m_listCallFunc.clear();

	// 🛑 AND THE COMPILE-TIME SCRATCH, because this object is REUSED — Recompile, the designer
	// rebuilding a module. A parked chain position that survived a refusal would be consumed by the
	// next module's first `foreach`, whose verbs it does not describe. The arming itself cannot leak
	// (ibLinqSourceScope disarms on every exit); this is the other half, and it costs one store.
	m_numLinqSourceEnd = -1;
	m_numLinqChainAt   = -1;
}

void ibCompileCode::PrepareModuleData()
{
	// Helper — locate the freshly-added compile-context entry by name
	// to stamp post-AddVariable flags (External / clsid / scoped).
	auto stampOnContext = [&](const ibString& name, auto&& mutate) {
		auto it = std::find_if(m_rootContext->m_listVariable.begin(), m_rootContext->m_listVariable.end(),
			[&](const auto& v) { return v && stringUtils::IsSameAs(name, v->m_strRealName); });
		if (it != m_rootContext->m_listVariable.end() && *it)
			mutate(**it);
	};

	// Pass 1: external values — kind=External on the bc mirror. Binder
	// fills the slot at runtime; pre-flight verifies clsid match.
	for (auto& externValue : m_listExternValue) {
		m_rootContext->AddVariable(externValue.first, 0, true);
		const ibClassID clsid = externValue.second ? externValue.second->GetClassType() : ibClassID(0);
		stampOnContext(externValue.first, [&](ibCompileContext::ibVariable& v) {
			v.m_kind  = ibVarKind::External;
			v.m_clsid = clsid;
		});
	}

	// Pass 1b: local binds — plain frame locals (bExport=false, NO m_bExternal /
	// m_bContext stamp → kind=Local). The binder fills the slot at runtime (no
	// required/type pre-flight); the module reads/writes it as a normal local
	// (e.g. a constant's Value backed by &m_constValue).
	for (auto& localValue : m_listLocalValue) {
		m_rootContext->AddVariable(localValue.first, 0, false);
	}

	// Pass 2: context values — currently all self-referencing.
	// Stamp scoped (per-instance "self" handles like ThisForm) and
	// clsid onto the compile-context entry; the bc mirror then carries
	// these into m_listVar with kind=Context so the binder treats them
	// as required and resolve walks them visibility-aware.
	for (auto& contextValue : m_listContextValue) {
		m_rootContext->AddVariable(contextValue.first, 0, true, true);
		ibClassID clsid = 0;
		if (contextValue.second.m_value) {
			contextValue.second.m_value->InvalidateNames();
			clsid = contextValue.second.m_value->GetClassType();
		}
		stampOnContext(contextValue.first, [&](ibCompileContext::ibVariable& v) {
			v.m_clsid = clsid;
		});
	}

	ibCompileContext* mainContext = GetContext();

	for (auto& pair : m_listContextValue) {

		ibValue* contextValue = pair.second.m_value;
		wxASSERT(contextValue);
		contextValue->InvalidateNames();

		// adding variables from context
		for (unsigned int i = 0; i < contextValue->GetNProps(); i++) {
			const ibString& propName = contextValue->GetPropName(i);
			// Skip the self-named prop (ThisForm exposes a "ThisForm"
			// prop pointing at itself; ThisObject does the same). The
			// binding entry from Pass 2 already owns this name as
			// kind=Context with a real frame slot — pushing the
			// self-prop here would overwrite that via PushVariable's
			// insert_or_assign (Pass-2 entry lost → binder skips the
			// slot at pre-flight → runtime reads garbage).
			if (stringUtils::IsSameAs(propName, pair.first))
				continue;
			mainContext->PushVariable(propName, pair.first, i);

			// ⚠ AND SCOPE-LOCALITY IS NOT COPIED ONTO THE VARIABLE, because nothing ever read it
			// back. It is asked of the VALUE, by IsPropScoped, at each of the places that care —
			// the runtime's OPER_GET_A, the debugger's property walk, autocomplete, the editor's
			// interpreter. A second copy on the compile entry travelled all the way into the AOT
			// blob and was consulted by no one; see the note in value.h, which described the
			// arrangement this replaced.
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
 * Refuse
 * Purpose:
 * Remember the translation error and throw an exception
 * Return value:
 * The method does not return control!
 */

void ibCompileCode::Refuse(int codeError, const ibString& errorDesc)
{
	ibString strFileName, strModuleName, strDocPath; int currPos = 0, currLine = 0;

	if (m_numCurrentCompile >= m_translateCode.GetLexCount()) {
		m_numCurrentCompile = m_translateCode.GetLexCount() - 1;
	}

	if (m_numCurrentCompile >= 0 && m_numCurrentCompile < m_translateCode.GetLexCount()) {

		strFileName = m_translateCode.GetLexAt(m_numCurrentCompile).GetFileName();
		strModuleName = m_translateCode.GetLexAt(m_numCurrentCompile).GetModuleName();
		strDocPath = m_translateCode.GetLexAt(m_numCurrentCompile).GetDocPath();

		if (m_translateCode.GetLexAt(m_numCurrentCompile).m_lexType != ENDPROGRAM) {
			currLine = m_translateCode.GetLexAt(m_numCurrentCompile).GetLine();
			currPos = m_translateCode.GetLexAt(m_numCurrentCompile).EndPos();
		}
		else {
			currLine = m_translateCode.GetLexAt(m_numCurrentCompile - 1).GetLine();
			currPos = m_translateCode.GetLexAt(m_numCurrentCompile - 1).EndPos();
		}
	}

	DoRefuse(codeError,
		strFileName, strModuleName, strDocPath,
		m_translateCode, currPos, currLine,
		errorDesc);
}

/**
 * another function option
 */

void ibCompileCode::Refuse(int nErr, const ibChar& c)
{
	Refuse(nErr, ibString::Format(ibT("%c"), c));
}

/**
 * DoRefuse
 * Purpose:
 * Remember the translation error and throw an exception
 * Return value:
 * The method does not return control!
 */

void ibCompileCode::DoRefuse(int codeError,
	const ibString& strFileName, const ibString& strModuleName, const ibString& strDocPath,
	const ibTranslateCode& translateCode, unsigned int currPos, unsigned int currLine,
	const ibString& strErrorDesc) const
{
	// ⭐⭐ THE SENTENCE IS KEPT EITHER WAY — this is where refusals accumulate. It used to travel on
	// the exception, so a compile that raises nothing would have lost it, and "why did that dot not
	// resolve" is precisely the question it answers. Written before the fork, so both modes record
	// the same line and only the CONSEQUENCE differs.
	m_strRefusal << ibString::Format(ibT("[%u:%u] "), currLine, currPos)
	              << ibBackendException::Format(codeError, strErrorDesc) << ibT("\n");

	// ⭐⭐ A TOLERANT COMPILE STOPS FOR NOTHING. It is READING — working out what is at a caret — and
	// a reader has nobody to raise to: no window, no subscriber, no exception. It does not raise,
	// and then there is nothing to catch either; the ~50 refusal sites that already `return` after
	// Refuse ARE the recovery, exactly as they read.
	//
	// That is how the precompiler has always worked — one refusal and zero throws in 2900 lines —
	// and it is what lets somebody edit at line one thousand of a module broken at line three
	// (Max, 2026-09-07: *"I do not want anything going into the exception when you are computing
	// autocomplete — what are you going to say with it, and to whom?"*).
	if (m_compileMode == ibCompileMode::Tolerant)
		return;

	const ibString& strCodeError =
		ibBackendException::FindErrorCodeLine(translateCode.GetBuffer(), currPos);

	ibBackendException::ProcessError(
		strFileName,
		strModuleName, strDocPath,
		currPos, currLine,
		strCodeError, codeError, strErrorDesc
	);
}

// The translator reads on past what it could not read; here each refusal becomes the compile's.
bool ibCompileCode::Translate()
{
	m_translateCode.SetCodeStyle(GetCodeStyle());

	const bool prepared = m_translateCode.Translate();
	for (size_t i = 0; i < m_translateCode.GetRefusalCount(); ++i) {
		const ibTranslateRefusal& refusal = m_translateCode.GetRefusalAt(i);
		const ibTranslateCode& translateCode = *refusal.m_translateCode;
		DoRefuse(refusal.m_codeError,
			translateCode.GetFileName(), translateCode.GetModuleName(), translateCode.GetDocPath(),
			translateCode, refusal.m_currPos, refusal.m_currLine,
			refusal.m_errorDesc);
	}
	return prepared;
}

//////////////////////////////////////////////////////////////////////
// Compiling
//////////////////////////////////////////////////////////////////////

/**
 *adding information about the current line to the byte code
 */

void ibCompileCode::StampSource(ibByteUnit& code)
{
	code.m_strModuleName = m_translateCode.GetModuleName();
	code.m_strDocPath = m_translateCode.GetDocPath();
	code.m_strFileName = m_translateCode.GetFileName();

	if (m_numCurrentCompile >= 0 && m_numCurrentCompile < m_translateCode.GetLexCount()) {
		if (m_translateCode.GetLexAt(m_numCurrentCompile).m_lexType != ENDPROGRAM) {
			code.m_strModuleName = m_translateCode.GetLexAt(m_numCurrentCompile).GetModuleName();
			code.m_strDocPath = m_translateCode.GetLexAt(m_numCurrentCompile).GetDocPath();
			code.m_strFileName = m_translateCode.GetLexAt(m_numCurrentCompile).GetFileName();
		}
		code.m_numString = m_translateCode.GetLexAt(m_numCurrentCompile).m_numString;
		code.m_numLine = m_translateCode.GetLexAt(m_numCurrentCompile).m_numLine;
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
	if (m_numCurrentCompile + 1 < m_translateCode.GetLexCount())
		return m_translateCode.GetLexAt(++m_numCurrentCompile);
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
 * ExpectLexem
 * Purpose:
 * Get the next token from the list of byte codes and increment the current position counter by 1
 * Return value:
 * no (if failure occurs, an exception is thrown)
 */

const ibLexem& ibCompileCode::ExpectLexem()
{
	const ibLexem& lex = GetLexem();
	if (lex.m_lexType == ERRORTYPE) {
		m_numCurrentCompile--;
		Refuse(ibScriptError_CodeDefine);
		return gs_nullLexem;
	}
	return lex;
}

/**
 * ExpectDelimiter
 * Purpose:
 * Get the next token as the given delimiter
 * Return value:
 * no (if failure occurs, an exception is thrown)
 */

void ibCompileCode::ExpectDelimiter(const ibChar& c)
{
	const ibLexem& lex = GetLexem();
	if (lex.m_lexType == DELIMITER && c == lex.m_numData)
		return;

	// ⚠ AND IT DOES NOT GO HUNTING FOR THE TOKEN. The precompiler's twin (ibParseCode::ExpectDelimiter)
	// does search forward, and its own call sites document what that costs: *"whose miss-path skips
	// lexems to EOF and would swallow the rest of the module"* — which is why that walker grew
	// special cases for `++` / `--` / `;` / `{` / `}` rather than let the search run. It is an
	// emergency exit there, not the mechanism, and copying it here would import the emergency.
	m_numCurrentCompile--;
	Refuse(ibScriptError_Delimiter, c);
}

/**
 * IsKeyword
 * Purpose:
 * Check if the current bytecode token is a given keyword
 * Return value:
 * true, false
 */

bool ibCompileCode::IsKeyword(int k)
{
	if (m_numCurrentCompile + 1 < m_translateCode.GetLexCount()) {
		const ibLexem& lex = m_translateCode.GetLexAt(m_numCurrentCompile);
		if (lex.m_lexType == KEYWORD && lex.m_numData == k)
			return true;
	}
	return false;
}

/**
 * IsNextKeyword
 * Purpose:
 * Check if the next bytecode token is a given keyword
 * Return value:
 * true,false
 */

bool ibCompileCode::IsNextKeyword(int k)
{
	if (m_numCurrentCompile + 1 < m_translateCode.GetLexCount()) {
		const ibLexem& lex = m_translateCode.GetLexAt(m_numCurrentCompile + 1);
		if (lex.m_lexType == KEYWORD && k == lex.m_numData)
			return true;
	}
	return false;
}

/**
 * IsDelimiter
 * Purpose:
 * Check if the current bytecode token is a given delimiter
 * Return value:
 * true,false
 */

bool ibCompileCode::IsDelimiter(const ibChar& c)
{
	if (m_numCurrentCompile + 1 < m_translateCode.GetLexCount()) {
		const ibLexem& lex = m_translateCode.GetLexAt(m_numCurrentCompile);
		if (lex.m_lexType == DELIMITER && c == lex.m_numData)
			return true;
	}
	return false;
}

/**
 * IsNextDelimiter
 * Purpose:
 * Check if the next bytecode token is a given delimiter
 * Return value:
 * true,false
 */

bool ibCompileCode::IsNextDelimiter(const ibChar& c)
{
	if (m_numCurrentCompile + 1 < m_translateCode.GetLexCount()) {
		const ibLexem& lex = m_translateCode.GetLexAt(m_numCurrentCompile + 1);
		if (lex.m_lexType == DELIMITER && c == lex.m_numData)
			return true;
	}
	return false;
}

/**
 * ExpectKeyword
 * Get the next token as the given keyword
 * Return value:
 * no (if failure occurs, an exception is thrown)
 */

void ibCompileCode::ExpectKeyword(int nKey)
{
	const ibLexem& lex = GetLexem();
	if (lex.m_lexType == KEYWORD && lex.m_numData == nKey)
		return;

	// See ExpectDelimiter on why this does not search forward either.
	m_numCurrentCompile--;
	Refuse(ibScriptError_Keyword,
		ibString::Format(ibT("%s"), s_listKeyWord[nKey].m_strKeyWord)
	);
}

/**
 * ExpectIdentifier
 * Get the next token as the given keyword
 * Return value:
 * identifier string
 */

ibString ibCompileCode::ExpectIdentifier(bool strRealName, bool acceptKeyword)
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

		// ⚠ THE STEP BACK IS FOR THE MESSAGE — it puts the cursor on the offending lexem so the
		// report points at it. A tolerant compile reports nothing and keeps reading, and handing
		// the caller back the lexem it just refused is how a loop meets it forever; the
		// precompiler's twin CONSUMES it for the same reason (ibParseCode::ExpectIdentifier takes
		// what is there and moves on).
		if (m_compileMode != ibCompileMode::Tolerant)
			m_numCurrentCompile--;

		Refuse(ibScriptError_IdentifierDefine);
		return ibEmptyString;
	}

	if (strRealName) {
		return lex.m_valData.GetString();
	}

	return lex.m_strData;
}

/**
 * ExpectConstant
 * Get the next token as a constant
 * Return value:
 * constant
 */

ibValue ibCompileCode::ExpectConstant()
{
	ibLexem lex;
	int iNumRequire = 0;
	if (IsNextDelimiter('-') || IsNextDelimiter('+')) {
		iNumRequire = 1;
		if (IsNextDelimiter('-'))
			iNumRequire = -1;
		lex = ExpectLexem();
	}

	lex = GetLexem();

	if (lex.m_lexType != CONSTANT) {
		Refuse(ibScriptError_ConstDefine);
		return ibValue();
	}

	if (iNumRequire) {

		// check that the constant is of numeric type
		if (lex.m_valData.GetType() != ibValueTypes::TYPE_NUMBER) {
			Refuse(ibScriptError_ConstDefine);
			return ibValue();
		}

		// change sign for minus
		if (iNumRequire == -1) {
			lex.m_valData = ibVariant(-lex.m_valData.GetNumber());
		}
	}

	return ibValue(lex.m_valData);
}

// getting the number with a string constant (to determine the method number)
const int ibCompileCode::GetConstString(const ibString& strConstName)
{
	// EXACT match, deliberately. The language is case-blind about NAMES, but this pool
	// stores a SPELLING, and the runtime prints that spelling back when a member is not
	// found. Folding case-insensitively made the pool keep whichever spelling arrived
	// first, so `x.ValueIsFilled()` was reported as 'VALUEISFILLED' — a name nobody in
	// the module had written, and the first thing a person doubts is their own eyes.
	// Case-blindness belongs where a name is RESOLVED (ibValue::FindMethod / FindProp
	// / the ctor registry, all at run time), not where its letters are stored.
	auto iterator = m_listHashConst.find(strConstName);

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

void ibCompileCode::AddVariable(const ibString& strVarName, const ibValue& vObject)
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

void ibCompileCode::AddVariable(const ibString& strVarName, ibValue* pValue)
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

void ibCompileCode::AddContextVariable(const ibString& strVarName, const ibValue& vObject, bool scopeContext)
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

void ibCompileCode::AddContextVariable(const ibString& strVarName, ibValue* pValue, bool scopeContext)
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
void ibCompileCode::AddLocalVariable(const ibString& strVarName, ibValue* pValue)
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

void ibCompileCode::RemoveVariable(const ibString& strVarName)
{
	if (strVarName.IsEmpty())
		return;

	m_listExternValue.erase(strVarName);
	m_listContextValue.erase(strVarName);
	m_listLocalValue.erase(strVarName);   // local binds (e.g. form attribute cells) too

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
	if (!Translate()) {
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
	if (!Translate()) {
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

bool ibCompileCode::Compile(const ibString& strCode)
{
	//clear functions & variables
	Reset();

	Load(strCode);

	//prepare lexem
	if (!Translate()) {
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

// LOOK AHEAD for a type name in a declaration position, WITHOUT consuming it.
//
// The grammar is `[modifier] Type name [= default]`, and the type may be:
//
//   Boolean value                      — a primitive, all this used to accept
//   Array rows                         — any registered value / control class
//   CatalogRef.Goods item              — a metadata type, i.e. TWO lexems and a dot
//
// The last form is why this exists: a reference type's registered name IS
// "<Kind>Ref.<Name>" (objCtor.h), so once the name is assembled the ordinary
// registry answers whether it is a type. Nothing new has to know about it.
//
// THE IDENTIFIER AFTER IT IS PART OF THE TEST, and that is not defensive
// programming — it is what keeps the extension from changing the meaning of
// existing code. `Array` alone may well be an identifier somebody uses; `Array
// rows` cannot be anything but a declaration. Deciding on the type name alone
// was safe while only five reserved primitives qualified, and stops being safe
// the moment every registered class does.
//
// `outName` receives the assembled name; `outLexemCount` how many lexems it
// spans (1 or 3), so the caller can consume exactly that many.
bool ibCompileCode::IsTypeVar(const ibString& strType)
{
	// NAMED FORM — the caller already has the word and only asks whether it names
	// a type. Any REGISTERED type counts: the narrowing to the five primitives was
	// left from when only those could be declared, and a type that exists has as
	// much right to be written down as `Number` has.
	if (!strType.IsEmpty())
		return ibValue::IsRegisterCtor(strType);

	// LOOK-AHEAD FORM — "does a declaration start here?", asked before a single
	// lexem is consumed.
	//
	// A DECLARATION IS TWO IDENTIFIERS: a registered type name, then the variable
	// name. That second identifier is what keeps the widening safe — `Array rows`
	// declares, while `Array = 5` assigns to a variable that happens to be called
	// Array. Deciding on the type name alone was safe while five reserved words
	// qualified, and stops being safe the moment every registered class does.
	const int first = m_numCurrentCompile + 1;
	const int count = static_cast<int>(m_translateCode.GetLexCount());
	if (first + 1 >= count)
		return false;

	// ⭐ THE DOTTED FORM — `CatalogRef.Goods item`, three lexems and a name. The header above promised it and the
	// code read one lexem only, so every metadata type was refused where a declaration stood ("Var is not found
	// (CatalogRef)", measured 2026-09-17). `A.B name` is nothing else in the language — two expressions do not
	// stand side by side — so the shape is a declaration, and GetTypeVar names a type the registry does not know.
	if (first + 3 < count
	 && m_translateCode.GetLexAt(first).m_lexType == IDENTIFIER
	 && m_translateCode.GetLexAt(first + 1).m_lexType == DELIMITER && m_translateCode.GetLexAt(first + 1).m_numData == '.'
	 && m_translateCode.GetLexAt(first + 2).m_lexType == IDENTIFIER
	 && m_translateCode.GetLexAt(first + 3).m_lexType == IDENTIFIER)
		return true;

	if (m_translateCode.GetLexAt(first).m_lexType != IDENTIFIER
	 || m_translateCode.GetLexAt(first + 1).m_lexType != IDENTIFIER)
		return false;

	// `AnyRef`, `CatalogRef`, `AnyControl` need no special case: they are
	// REGISTERED types like any other (typeAny.cpp, metaCtor.h), differing only in
	// that they create nothing and admit a whole family.
	return ibValue::IsRegisterCtor(m_translateCode.GetLexAt(first).m_strData);
}

ibClassID ibCompileCode::GetTypeVar(const ibString& strType)
{
	// THE ID, NOT THE NAME. Everything downstream — the gate, the typed-opcode
	// choice, the bytecode — speaks class ids; handing back a name would only mean
	// resolving it again, later, somewhere else.
	if (!strType.IsEmpty()) {
		// Unknown to the registry — that, and only that, is a bad type name.
		if (!ibValue::IsRegisterCtor(strType)) {
			Refuse(ibScriptError_TypeDef, strType);
			return 0;
		}
		return ibValue::GetIDObjectFromString(strType);
	}

	// The unnamed form follows IsTypeVar(), which already looked ahead and said
	// yes — so the next lexem IS the type name. Re-deciding here would be the same
	// question asked twice, and the two answers could drift.
	const ibLexem& lex = ExpectLexem();
	if (lex.m_lexType != IDENTIFIER) {
		Refuse(ibScriptError_TypeDef, lex.m_strData);
		return 0;
	}
	ibString name = lex.m_strData;
	ibString written = lex.m_valData.GetString();   // as the person spelled it — m_strData is folded to upper case
	if (IsNextDelimiter('.')) {   // the dotted form — see IsTypeVar
		ExpectDelimiter('.');
		const ibString member = ExpectIdentifier(true);
		name += ibT(".") + member;
		written += ibT(".") + member;
	}
	// ⚠ A CONFIGURATION'S OWN TYPE (`CatalogRef.Goods`) IS NOT IN THIS REGISTRY. The configuration registers it
	// with itself (ibMetaImage), and a compiler has no road to its module's configuration without a hook on
	// ibCompileCode — a header under commonObject.h, so a question for Max (ROADMAP). Its FAMILY is here
	// (`CatalogRef`, `AnyRef`), and the refusal names what a declaration takes.
	if (!ibValue::IsRegisterCtor(name)) {
		Refuse(ibScriptError_TypeDef, written.IsEmpty() ? name : written);
		return 0;
	}
	return ibValue::GetIDObjectFromString(name);
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
	const ibLexem& lex = PreviewGetLexem(); ibClassID typeClsid = 0;
	if (lex.m_lexType == IDENTIFIER) {
		typeClsid = GetTypeVar(); // typed setting of variables
	}
	else {
		ExpectKeyword(KEY_VAR);
	}

	while (true) {
		ibString strRealName = ExpectIdentifier(true);
		ibString strName = stringUtils::MakeUpper(strRealName);


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
				[&strName](const auto& v) { return v && stringUtils::IsSameAs(strName, v->m_strRealName); });
			if (existing != pCurContext->m_listVariable.end()) {
				const auto& currentVariable = *existing;
				if (currentVariable->IsPublic() ||
					pCurContext->m_compileModule == this) {
					Refuse(ibScriptError_DefVariable, strRealName);
					return false;
				}
			}
			pCurContext = pCurContext->m_parentContext;
		}

		int nArrayCount = -1;
		if (IsNextDelimiter('[')) { // this is an array declaration
			nArrayCount = 0;
			ExpectDelimiter('[');
			if (!IsNextDelimiter(']')) {
				ibValue vConst = ExpectConstant();
				if (vConst.GetType() != ibValueTypes::TYPE_NUMBER ||
					vConst.GetNumber() < 0) {
					Refuse(ibScriptError_ArraySizeConst);
					return false;
				}
				nArrayCount = vConst.GetInteger();
			}
			ExpectDelimiter(']');
		}

		// Access modifier — exactly one of Public / Private / Protected, in
		// place of the old single Export check. All optional (none = Private);
		// more than one is an error.
		bool bExport = false;
		int varAccess = ACCESS_PRIVATE;
		int numModifiers = 0;
		if (IsNextKeyword(KEY_PUBLIC))    { ExpectKeyword(KEY_PUBLIC);    bExport = true; varAccess = ACCESS_PUBLIC;    numModifiers++; }
		if (IsNextKeyword(KEY_PRIVATE))   { ExpectKeyword(KEY_PRIVATE);                   varAccess = ACCESS_PRIVATE;   numModifiers++; }
		if (IsNextKeyword(KEY_PROTECTED)) { ExpectKeyword(KEY_PROTECTED);                 varAccess = ACCESS_PROTECTED; numModifiers++; }
		if (numModifiers > 1) {
			Refuse(ibScriptError_Code); // only one access modifier (Public / Private / Protected) is allowed
			return false;
		}

		// there was no variable declaration yet - add
		ibParamUnit variable =
			context->AddVariable(strRealName, typeClsid, bExport);

		// Stamp the access enum onto the just-created variable so Protected is
		// distinguishable from Private at resolve time. The parent-chain
		// visibility gate honours Protected (visible to children, not config-
		// wide); Public already carries kind=Export + m_access=Public.
		if (varAccess != ACCESS_PRIVATE) {
			std::shared_ptr<ibCompileContext::ibVariable> declVar;
			if (context->FindVariable(strRealName, declVar) && declVar) {
				declVar->m_access = varAccess;
				// Protected is also a kind (the var was created kind=Local —
				// only Public sets bExport at AddVariable). Public already
				// carries kind=Export from creation, so only Protected flips.
				if (varAccess == ACCESS_PROTECTED)
					declVar->m_kind = ibVarKind::Protected;
			}
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
			StampSource(declLocal);
			declLocal.m_numOper = OPER_FUNC_LOCAL;
			declLocal.m_param1.m_numIndex = (long)variable.m_numIndex;
			declLocal.m_param1.m_numArray = bExport ? 1 : 0;
			m_cByteCode.m_listCode.emplace_back(std::move(declLocal));
		}

		if (nArrayCount >= 0) { // write information about the arrays
			ibByteUnit code;
			StampSource(code);
			code.m_numOper = OPER_SET_ARRAY_SIZE;
			code.m_param1 = variable;
			code.m_param2.m_numArray = nArrayCount;//����� ��������� � �������
			m_cByteCode.m_listCode.emplace_back(std::move(code));
		}

		AddTypeSet(variable);

		if (IsNextDelimiter('=')) { // initial initialization - works only inside the text of modules (but not re-declaring procedures and functions)

			if (nArrayCount >= 0)  ExpectDelimiter(','); // error!

			ExpectDelimiter('=');

			ibByteUnit code;
			StampSource(code);
			code.m_numOper = OPER_LET;
			code.m_param1 = variable;
			code.m_param2 = GetExpression(context);
			m_cByteCode.m_listCode.emplace_back(std::move(code));
		}

		if (!IsNextDelimiter(','))
			break;

		ExpectDelimiter(',');
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

		// IsTypeVar() with NO argument — the look-ahead form. Passing the lexem's
		// text asks the cast-position question instead ("is this word a
		// primitive"), which is what limited declarations to the five primitives:
		// a value class or a dotted metadata type never reached CompileDeclaration
		// and was parsed as an expression, failing on the missing '='.
		if ((KEYWORD == lex.m_lexType && lex.m_numData == KEY_VAR) || (IDENTIFIER == lex.m_lexType && IsTypeVar())) {
			if (!m_onlyFunction) {
				CompileDeclaration(mainContext); // load variable declaration
			}
			else {
				// ⚠ THE CURSOR IS STILL BEHIND THE WORD THAT IS WRONG. The lexem above came from
				// PreviewGetLexem, which looks ahead WITHOUT advancing, so an unqualified Refuse
				// reports wherever the previous construct ended — and, when the module opens with
				// the declaration, m_numCurrentCompile is still -1 and the message carries NO LINE
				// AT ALL. Measured 2026-09-04 on a common module: `Var calls;` on line 1 was
				// reported at line 5 (the end of the file), which reads as a broken last line and
				// sends the reader to the wrong end of the module.
				m_numCurrentCompile++;
				Refuse(ibScriptError_OnlyFunction);
				return false;
			}
		}
		else if (KEYWORD == lex.m_lexType && (KEY_PROCEDURE == lex.m_numData || KEY_FUNCTION == lex.m_numData)) {

			// don't forget to restore the current module context (if necessary)...
			//
			// ⚠ NO GUARD HERE, AND THAT IS THE POINT. A tolerant compile does not raise
			// (DoRefuse), so there is nothing to catch and nothing to skip: a refusal is reported
			// and the parse walks on by itself. A runtime compile raises and the caller ends.
			CompileFunction(mainContext); // load function declaration
		}
		else break;
	}

	// load the executable body of the module
	m_cByteCode.m_lStartModule = 0;

	// The module's own span starts where its BODY does — after the declarations, which own theirs.
	// That is what keeps the two from claiming the same caret (see NoteCaret).
	const long bodyStart = CaretCursor();

	CompileBlock(mainContext);

	NoteCaret(bodyStart, kCaretAtModule);

	mainContext->CreateLabels();

	// set the end of the program
	ibByteUnit code;
	StampSource(code);
	code.m_numOper = OPER_END;

	m_cByteCode.m_listCode.emplace_back(std::move(code));
	m_cByteCode.m_lVarCount = mainContext->m_listVariable.size();

	// we finish processing procedures and functions that were called before they were declared
	// for this, at the end of the bytecode array, add new code to call such functions,
	// and for correct operation we insert GOTO statements into places of early calls
	// ⚠ THE CURSOR IS A REPORTING DEVICE HERE, NOT A PARSE POSITION. The whole token stream has
	// already been read by the time this loop runs, and PushCallFunction REWINDS
	// (`m_numCurrentCompile = callFunc->m_numError`) so a refusal points at the call rather than at
	// the end of the module. With a raise that never mattered — nothing looked at the cursor again.
	// Surviving the refusal, something does: the end-of-stream check below then sees a cursor
	// standing in the middle of the text and reports "unexpected program code termination" about a
	// module that was read to its end (measured 2026-09-07 — three unresolved calls answered with
	// four diagnostics).
	const int cursorAtStreamEnd = m_numCurrentCompile;

	for (auto& callFunc : m_listCallFunc) {
		m_cByteCode.m_listCode[callFunc->m_numAddLine].m_param1.m_numIndex =
			m_cByteCode.m_listCode.size(); // go to function call

		// ⭐ HERE THE UNIT OF FAILURE IS THE CALL — finer than a declaration, and the loop was
		// already written for it: PushCallFunction returns false and the GOTO is simply not
		// emitted. Only the throw stood in the way, so one unresolved name hid every other in the
		// module. A tolerant compile answers about all of them; a runtime compile still stops at
		// the first, because half a module is not worth executing.
		if (m_compileMode == ibCompileMode::Tolerant) {
			bool resolved = false;
			try { resolved = PushCallFunction(callFunc); }
			catch (const ibCoreException&) { resolved = false; }
			if (!resolved)
				continue;
		}
		else if (!PushCallFunction(callFunc)) {
			continue;
		}

		{
			// correcting labels
			ibByteUnit gotoCode;
			StampSource(gotoCode);
			gotoCode.m_numOper = OPER_GOTO;
			gotoCode.m_numLine = callFunc->m_numLine;
			gotoCode.m_numString = callFunc->m_numString;
			gotoCode.m_param1.m_numIndex = callFunc->m_numAddLine + 1; // after calling the function we go back
			m_cByteCode.m_listCode.emplace_back(std::move(gotoCode));
		}
	}

	m_numCurrentCompile = cursorAtStreamEnd;   // see the note above the loop

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
	for (const auto& v : mainContext->m_listVariable) {
		if (!v || v->m_bTempVar) continue;
		ibByteCode::ibByteCodeVarInfo info(*v);
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
			[&](const auto& p) { return stringUtils::IsSameAs(v.m_strContext, p.m_strRealName); });
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
	for (const auto& fnPtr : mainContext->m_listFunction) {
		if (!fnPtr) continue;
		if (fnPtr->m_strContext.IsEmpty()) continue;  // own user-defined func
		const ibString& nameKey = fnPtr->m_strRealName;
		auto existing = std::find_if(m_cByteCode.m_listFunc.begin(), m_cByteCode.m_listFunc.end(),
			[&](const auto& fn) { return stringUtils::IsSameAs(nameKey, fn.m_strRealName); });
		if (existing != m_cByteCode.m_listFunc.end()) continue;
		m_cByteCode.m_listFunc.emplace_back(/*lAddress=*/-1, *fnPtr);
	}

	// Second pass: resolve m_parentRef on ContextMethod entries —
	// link to the parent Context binding in m_listVar by name. Mirror
	// of the m_parentRef resolution for ContextProp vars (above).
	// (kind=ContextMethod invariant: m_strContext is non-empty.)
	for (auto& fn : m_cByteCode.m_listFunc) {
		if (!fn.IsContextMethod()) continue;
		auto pIt = std::find_if(m_cByteCode.m_listVar.begin(), m_cByteCode.m_listVar.end(),
			[&](const auto& p) { return stringUtils::IsSameAs(fn.m_strContext, p.m_strRealName); });
		if (pIt != m_cByteCode.m_listVar.end())
			fn.m_parentRef = static_cast<long>(std::distance(m_cByteCode.m_listVar.begin(), pIt));
	}

	if (m_numCurrentCompile + 1 < m_translateCode.GetLexCount() - 1) {
		Refuse(ibScriptError_EndProgram);
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

	// ⭐⭐ AND THE TREE IS NOT THROWN AWAY HERE. The slice is the BASE TYPE (byteCode.h): everything
	// below the compiler is typed on `ibByteCode`, so what the runtime and the AOT cache hold cannot
	// reach the LINQ section whatever this module does with it. Which leaves the question of who
	// still WANTS it — and the answer is the third reader: IntelliSense runs on the compiler's side,
	// on this very object, and a query's bindings are exactly what it needs to answer inside one.
	// Max, 2026-09-08: *"you needn't clear it — IntelliSense will get it later."*

	// compilation completed successfully
	m_cByteCode.m_bCompile = true;

	return true;
}

// search for function definition in the current module and all parent ones
bool ibCompileCode::GetFunction(const ibString& strName, std::shared_ptr<ibCompileContext::ibFunction>& function, int* pNumFunction)
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
		Refuse(ibScriptError_CallFunction, callFunction->m_strRealName);// there is no such function in the module
		return false;
	}

	if (!callFunction->m_numIsSet && !foundedFunc->m_bCodeRet) {
		m_numCurrentCompile = callFunction->m_numError;
		Refuse(ibScriptError_UseProcedureAsFunction, foundedFunc->m_strRealName);
		return false;
	}

	// check the match between the number of passed and declared parameters
	unsigned int numRealCount = callFunction->m_listParam.size();
	unsigned int numDefCount = foundedFunc->m_listParam.size();

	if (foundedFunc->m_valueVariadic) {
		// It takes what it is given. The declared list is empty BY CONSTRUCTION
		// for a negative arity, so it can neither bound the call nor say how many
		// slots to emit — the caller's own count is both answers.
		numDefCount = numRealCount;
	}
	else if (numRealCount > numDefCount) {
		m_numCurrentCompile = callFunction->m_numError;
		Refuse(ibScriptError_ManyParams, foundedFunc->m_strRealName);
		return false;
	}

	ibByteUnit code;
	StampSource(code);

	code.m_numString = callFunction->m_numString;
	code.m_numLine = callFunction->m_numLine;
	code.m_strModuleName = callFunction->m_strModuleName;

	if (foundedFunc->IsContextMethod()) { // virtual function - calling replacements with the construct Context.FunctionName(...)
		code.m_numOper = OPER_CALL_METHOD;
		code.m_param1 = callFunction->m_puRetValue;		// variable into which the value is returned
		code.m_param2 = callFunction->m_puContextVal;	// variable on which the method is called
		// m_strRealName, not m_strName: the pair is (upper form for LOOKUP, spelling as WRITTEN),
		// and what goes into the constant pool is read back by a person — the runtime prints it
		// when the member is not found. The upper form leaked out that way as 'VALUEISFILLED',
		// a spelling nobody typed. Resolution stays case-blind at run time (FindMethod).
		code.m_param3.m_numIndex = GetConstString(callFunction->m_strRealName);	// number of the called method from the list of encountered methods
		code.m_param3.m_numArray = numDefCount;	// number of parameters
	}
	else {
		// OPER_CALL vs OPER_CALL_CLOSURE — same operand layout, different
		// runtime path. The closure variant builds the callee frame as an
		// ibRunCaptureContext (it counts its own holders) so inner lambdas
		// materialised during the call can take it. m_needsHeapFrame is settled
		// by the time we get here: backward refs see the fully-compiled
		// callee directly; forward refs land in m_listCallFunc and
		// PushCallFunction reruns at finalize when all bodies are done.
		// `Cached` is NOT a call opcode. The modifier is applied at the callee's
		// entry opcode, where every road into a body arrives — a direct call, a
		// call through a module value, a handler fired from C++ — so the caller
		// emits the ordinary call and the callee decides. An opcode here would
		// have covered only the calls this emitter can see.
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
		ibByteUnit paramCode;
		StampSource(paramCode);
		paramCode.m_numOper = OPER_SET; // parameters are being passed
		bool defaultValue = false;
		if (i < numRealCount) {
			paramCode.m_param1 = callFunction->m_listParam[i];
			if (paramCode.m_param1.m_numArray == DEF_VAR_SKIP) { // need to substitute the default value
				defaultValue = true;
			}
			else {  //��� �������� ��������
				paramCode.m_param2.m_numIndex = foundedFunc->m_listParam[i].m_bByValue;
			}
		}
		else {
			defaultValue = true;
		}
		if (defaultValue) {
			if (foundedFunc->m_listParam[i].m_puValue.m_numArray == DEF_VAR_SKIP) {
				m_numCurrentCompile = callFunction->m_numError;
				Refuse(ibScriptError_FewParams, foundedFunc->m_strRealName);
				return false;
			}
			paramCode.m_numOper = OPER_SETCONST;	// default values
			paramCode.m_param1 = foundedFunc->m_listParam[i].m_puValue;
		}
		m_cByteCode.m_listCode.emplace_back(std::move(paramCode));
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
	const ibString& strFuncName = createdFunction->m_strRealName;
	const ibString& strFuncRealName = createdFunction->m_strRealName;

	// A second declaration in THIS module collides. A function of the same
	// name in a parent module does not: an object module and its manager are
	// different methods, and the object's own declaration shadows the
	// manager's export. The call inside this module sees this declaration.
	// The highlight stays on the name (errorPlace is the token after it).
	{
		std::shared_ptr<ibCompileContext::ibFunction> foundedFunc = nullptr;
		if (context->FindFunction(strFuncName, foundedFunc)) {
			if (foundedFunc != createdFunction && foundedFunc->IsCrossBcVisible()) {
				m_numCurrentCompile = errorPlace;
				Refuse(ibScriptError_DefFunction, strFuncRealName);
				return false;
			}
		}
	}

	// Upsert into the vector (was map subscript assign): replace a same-named
	// entry, else append.
	auto fit = std::find_if(context->m_listFunction.begin(), context->m_listFunction.end(),
		[&](const auto& f) { return f && stringUtils::IsSameAs(strFuncName, f->m_strRealName); });
	if (fit != context->m_listFunction.end())
		*fit = createdFunction;
	else
		context->m_listFunction.push_back(createdFunction);

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
	if (IsNextKeyword(KEY_FUNCTION)) {
		ExpectKeyword(KEY_FUNCTION);
		const bool isAnon = IsNextDelimiter('(');
		outFunctionContext.reset(context->CreateContext(
			isAnon ? RETURN_LAMBDA_FUNCTION : RETURN_FUNCTION));
	}
	else if (IsNextKeyword(KEY_PROCEDURE)) {
		ExpectKeyword(KEY_PROCEDURE);
		const bool isAnon = IsNextDelimiter('(');
		outFunctionContext.reset(context->CreateContext(
			isAnon ? RETURN_LAMBDA_PROCEDURE : RETURN_PROCEDURE));
	}
	else {
		Refuse(ibScriptError_FuncDefine);
		return false;
	}
	ibCompileContext* functionContext = outFunctionContext.get();

	// pull out the text of the function declaration
	const ibLexem& lex = PreviewGetLexem();

	ibString strShortDescription;
	const int numLine = lex.m_numLine;
	const ibString strBuffer = m_translateCode.GetBuffer();
	int numRes = strBuffer.find('\n', lex.m_numLine);
	if (numRes >= 0) {
		strShortDescription = strBuffer.substr(lex.m_numLine, numRes - lex.m_numLine - 1);
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
	ibString strFuncRealName;
	if (IsNextDelimiter('(')) {
		strFuncRealName = ibT("__lambda_") + wxNewUniqueGuid.str();
	}
	else {
		strFuncRealName = ExpectIdentifier(true);
	}
	// Original-cased name through both the ibFunction storage and the
	// m_listFunction map key — lookups use stringUtils::IsSameAs
	// (case-insensitive), so storage no longer needs to upper-normalize.
	// Renderers (debugger / catalog object PrepareNames) read map keys
	// directly and now show the user's source casing.
	const ibString& strFuncName = strFuncRealName;

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
	ExpectDelimiter('(');

	while (!IsNextDelimiter(')')) {

		// `[Val] [Type] name` — the passing mode first, then the type, then the name: `Val CatalogRef.Goods item`.
		// The type was asked for first, and a type's look-ahead wants two identifiers, so with `Val` in front it
		// never saw one, and with `Val` between it read the type as the parameter's name — neither order compiled
		// (measured 2026-09-17, both refused with "Symbol expected ')'").
		ibCompileContext::ibFunction::ibParamVariable cVariable;
		if (IsNextKeyword(KEY_VAL)) {
			ExpectKeyword(KEY_VAL);
			cVariable.m_bByValue = true;
		}

		// check for typing
		const ibClassID typeVar = IsTypeVar() ? GetTypeVar() : 0;

		const ibString& strRealName = ExpectIdentifier(true);

		cVariable.m_strName = strRealName;
		cVariable.m_clsid = typeVar;

		std::shared_ptr<ibCompileContext::ibVariable> foundedVar = nullptr;

		// register this variable as local
		if (functionContext->FindVariable(strRealName, foundedVar)) { // there was an announcement + repeated announcement = error
			Refuse(ibScriptError_IdentifierDuplicate, strRealName);
			return false;
		}

		if (IsNextDelimiter('[')) { // this is an array
			ExpectDelimiter('[');
			ExpectDelimiter(']');
		}
		else if (IsNextDelimiter('=')) {
			ExpectDelimiter('=');
			cVariable.m_puValue = FindConst(ExpectConstant());
		}

		functionContext->AddVariable(strRealName, typeVar);
		outFunction->m_listParam.emplace_back(std::move(cVariable));

		if (!IsNextDelimiter(',') || IsNextDelimiter(')'))
			break;

		ExpectDelimiter(',');
	}
	ExpectDelimiter(')');

	// Trailing modifiers — TWO INDEPENDENT AXES, read in one pass so neither
	// owns a position. Access (Public / Private / Protected) answers who sees
	// the function; Cached answers when it is evaluated. Both optional, both
	// at most once; `Private Cached` and `Cached Private` are the same
	// declaration. Repeating an axis is the error — two accesses, or Cached
	// twice.
	int numModifiers = 0;
	for (;;) {
		if (IsNextKeyword(KEY_PUBLIC))         { ExpectKeyword(KEY_PUBLIC);    outFunction->m_access = ACCESS_PUBLIC;    numModifiers++; }
		else if (IsNextKeyword(KEY_PRIVATE))   { ExpectKeyword(KEY_PRIVATE);   outFunction->m_access = ACCESS_PRIVATE;   numModifiers++; }
		else if (IsNextKeyword(KEY_PROTECTED)) { ExpectKeyword(KEY_PROTECTED); outFunction->m_access = ACCESS_PROTECTED; numModifiers++; }
		else if (IsNextKeyword(KEY_CACHED)) {
			ExpectKeyword(KEY_CACHED);
			if (outFunction->m_valueCached) {
				Refuse(ibScriptError_Code); // `Cached` stated twice
				return false;
			}
			outFunction->m_valueCached = true;
		}
		else
			break;
	}
	if (numModifiers > 1) {
		Refuse(ibScriptError_Code); // only one access modifier (Public / Private / Protected) is allowed
		return false;
	}
	// A PROCEDURE has no result to keep, so the modifier has nothing to mean
	// there — refused rather than ignored, because a silently-dropped Cached
	// looks exactly like a cache that never helps.
	if (outFunction->m_valueCached && !outFunction->m_bCodeRet) {
		Refuse(ibScriptError_Code); // `Cached` applies to a Function, not a Procedure
		return false;
	}
	// Kind from the access modifier — user-declared functions only (a context
	// method's kind is set by PushFunction). Public→Export, Protected→Protected.
	outFunction->m_kind = (outFunction->m_access == ACCESS_PUBLIC)    ? ibFnKind::Export
	                    : (outFunction->m_access == ACCESS_PROTECTED) ? ibFnKind::Protected
	                                                                  : ibFnKind::Local;

	return true;
}

bool ibCompileCode::EmitFunctionBody(ibCompileContext* /*context*/,
	const std::shared_ptr<ibCompileContext::ibFunction>& createdFunction,
	ibCompileContext* functionContext, bool bareExprBody)
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
	StampSource(code0);
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
		StampSource(declParam);
		declParam.m_numOper = OPER_FUNC_PARAM;
		declParam.m_param1.m_numIndex = (long)i;
		declParam.m_param1.m_numArray = createdFunction->m_listParam[i].m_bByValue ? 1 : 0;
		declParam.m_param2 = createdFunction->m_listParam[i].m_puValue;
		m_cByteCode.m_listCode.emplace_back(std::move(declParam));
	}

	// Type-check stamping for typed params — kept separate. The legacy
	// OPER_SET/SETCONST decoration that used to sit between FUNC_PARAM
	// and AddTypeSet is gone: its full payload (m_puValue, m_bByValue)
	// now lives on OPER_FUNC_PARAM, and runtime never read those
	// OPER_SETs anyway (Execute had no case for them — fall-through NOP).
	for (unsigned int i = 0; i < createdFunction->m_listParam.size(); i++) {
		ibParamUnit variable;
		variable.m_numArray = 0;
		variable.m_numIndex = i;
		variable.m_clsid = createdFunction->m_listParam[i].m_clsid;
		AddTypeSet(variable);
	}

	// Save/restore enclosing-function name — handles nested lambdas
	// inside named functions (and lambda inside lambda) cleanly. Pre-
	// Phase A there was no nesting (CompileBlock rejected KEY_FUNCTION
	// inside a function body) so plain set+clear was safe; with lambdas
	// as expressions the inner emit can fire while outer is still open.
	const ibString savedCurFuncName = m_strCurFuncName;
	m_strCurFuncName                = createdFunction->m_strRealName;

	if (bareExprBody) {
		// A `restrict` clause body: a bare `Return <expr>` (no `{ … }` block, no closing keyword).
		// The whole clause expression is read — the join ON is one `s.k <op> a.k` Compare the
		// decorator later splits, so nothing needs to stop early.
		ibByteUnit ret;
		StampSource(ret);
		ret.m_numOper = OPER_RET;
		ret.m_param1 = GetExpression(functionContext);
		m_cByteCode.m_listCode.emplace_back(std::move(ret));
	}
	// ⭐⭐ A BODY THAT IS NOT FINISHED YET IS NOT A FAILED DECLARATION — it is a declaration somebody
	// is in the middle of writing, and in a tolerant compile that is the ordinary state of the text.
	// The refusal is still SAID (Refuse publishes before it throws, so a check still lists it);
	// what changes is that the declaration is still CLOSED and REGISTERED below. That matters
	// because its name, its parameters and the locals it already has are exactly what a reader
	// standing INSIDE it is asking about — and by this point they exist. Same rule as the dangling
	// dot in GetCurrentIdentifier: keep what the compiler already has rather than unwind past it.
	// Where this body's span begins — see NoteCaret, which closes it at the end.
	const long bodyStart = CaretCursor();

	if (!bareExprBody)
		CompileBlock(functionContext);

	functionContext->CreateLabels();

	m_strCurFuncName = savedCurFuncName;

	if (!bareExprBody && GetCodeStyle() == CODE_VES) {
		// Closer keyword matches the OPENING keyword — derived from
		// m_bCodeRet, which IsReturnFunction(m_numReturn) populated at
		// signature parse. Both named and anonymous bodies dispatch
		// the same way; the lambda enum split keeps function vs
		// procedure visible without a side flag.
		if (createdFunction->m_bCodeRet) {
			ExpectKeyword(KEY_ENDFUNCTION);
		}
		else {
			ExpectKeyword(KEY_ENDPROCEDURE);
		}
	}

	ibByteUnit code;
	StampSource(code);
	code.m_numOper = isLambda ? OPER_ENDLFUNC : OPER_ENDFUNC;

	m_cByteCode.m_listCode.emplace_back(std::move(code));

	// ⭐⭐ THE DECLARATION THE CARET IS STANDING IN, SAID HERE. See NoteCaret: this is the moment
	// both ends of this body's span are known, and the parser is standing on the token that closed
	// it. Nothing about it is written into the tape.
	NoteCaret(bodyStart, lAddress);

	// ⭐⭐ …AND A BODY NOBODY CLOSED REACHES THE END OF THE TEXT. NoteCaret measures a span by the
	// token that closed it, which is right for every body that HAS one. A body being typed into has
	// not got there yet — `Where(Function(o) { return |` — and the caret then sat past the last
	// token, outside every span, so the lambda's own parameter `o` went unoffered while all 170
	// module names were listed (measured 2026-09-08).
	//
	// ⚠ THE BLOCK DEPTH DOES NOT ANSWER THIS, and it was the first thing tried: CompileBlock raises
	// it for RETURN_BLOCK scopes only, while a function or lambda body lives in the OPER_FUNC frame
	// and never moves it. What separates the two cases is the token the parse came to rest on —
	// CompileBlock CONSUMES the `}` that closes a body, so resting anywhere else means the text ran
	// out first.
	const auto bodyWasClosed = [this]() {
		if (m_numCurrentCompile < 0 || m_numCurrentCompile >= (int)m_translateCode.GetLexCount())
			return false;
		const ibLexem& last = m_translateCode.GetLexAt((size_t)m_numCurrentCompile);
		if (last.m_lexType == DELIMITER)
			return last.m_numData == ibT('}');
		return last.m_lexType == KEYWORD
			&& (last.m_numData == KEY_ENDFUNCTION || last.m_numData == KEY_ENDPROCEDURE);
	};

	if (m_caretOwner == kCaretNowhere && m_caretPos >= bodyStart && !bodyWasClosed())
		m_caretOwner = lAddress;

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
	for (const auto& v : functionContext->m_listVariable) {
		if (!v || v->m_bTempVar) continue;
		ibByteCode::ibByteCodeVarInfo info(*v);
		byteFn.m_listLocals.push_back(std::move(info));
	}
	if (isLambda) {
		byteFn.m_kind        = ibFnKind::Lambda;
		byteFn.m_strRealName = ibString::Format(ibT("<lambda@%ld>"), lAddress);
	} else if (byteFn.m_strRealName.IsEmpty()) {
		byteFn.m_strRealName = createdFunction->m_strRealName;
	}

	// The entry's own place in m_listFunc, so a call reaches this function by index instead of walking the list
	// for the entry line (OPER_FUNC, procUnit.cpp) — the index OPER_LFUNC carries in m_param3, where a named
	// function's entry holds its frame shape instead.
	if (!isLambda)
		m_cByteCode.m_listCode[lAddress].m_param4.m_numIndex = (long)m_cByteCode.m_listFunc.size();

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
	// docs/private/closure-capture.md). Phase B (runtime frame capture) landed
	// alongside — see procUnit.cpp OPER_LFUNC / OPER_CALL_LAMBDA, which build the
	// frame as an ibRunCaptureContext and hand the lambda its link to it.
	std::shared_ptr<ibCompileContext::ibFunction> createdFunction;
	std::unique_ptr<ibCompileContext> functionContextOwner;
	int errorPlace = 0;

	if (!ParseFunctionSignature(GetContext(), createdFunction,
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

	// ⭐⭐ THE QUERY TREE, READ OFF THE INSTRUCTIONS THIS BODY JUST EMITTED.
	//
	// There was a second reader here until today: it re-read the LEXEMES with a recursive descent of
	// its own, and its verdict was recorded beside this one so the two could be compared on the same
	// lambdas. They agreed on the same TREE for every shape the language can express (32 of 32,
	// measured 2026-09-08), and the one place they differed was the lexeme reader accepting
	// `x.Code in (…)` — which the compiler answers with "Symbol expected ')'", because there is no
	// `in` operator. A reader whose grammar is WIDER than the language can record predicates the
	// program does not contain, and that is why it is gone rather than kept as a second opinion.
	//
	// One journal line per lambda, at COMPILE time, so "did this predicate push down" is answered
	// without instrumenting anything — and it says WHAT the tree is, not merely that there is one.
	if (funcIndex >= 0 && createdFunction->m_listParam.size() == 1) {
		// ⭐ THE ONE FACT THE INSTRUCTIONS CANNOT ANSWER ON THEIR OWN is a captured outer local's
		// NAME: it lives in a function that is still being compiled, so its symbol table is in no
		// bytecode yet. The compile context IS that table.
		const auto nameOfOuter = [context](long framesOut, long slot) -> ibString {
			const ibCompileContext* ctx = context;
			for (long hop = 1; hop < framesOut && ctx != nullptr; ++hop)
				ctx = ctx->m_parentContext;
			if (ctx == nullptr)
				return ibString();
			for (const std::shared_ptr<ibCompileContext::ibVariable>& var : ctx->m_listVariable)
				if (var && !var->m_bTempVar && (long)var->m_numVariable == slot)
					return var->m_strRealName;
			return ibString();
		};

		ibString refused;
		const std::shared_ptr<ibQueryAstExpr> tree =
			ibBuildLambdaQueryAstFromCode(m_cByteCode, funcIndex, &refused, nameOfOuter);

		// 🛑 AT INFO, NOT WARNING. A warning in this engine reaches wxLogGui and comes back as a
		// MODAL dialog — measured: one line on a half-typed lambda put a 'Designer Warning' box on
		// screen and every request after it was refused because the designer was waiting on it.
		// ⚠ ASCII only in this line: a non-ASCII dash in a wxT() literal reaches the journal as
		// mojibake, and a diagnostic nobody can read is worse than a plainer one.
		ibJournalInfo(ibT("linq"), ibT("lambda `%s` at %d: %s"),
			createdFunction->m_listParam[0].m_strName,
			(int)m_translateCode.GetLexAt(lambdaBodyFrom).m_numString,
			tree ? ibT("query tree recorded: ") + ibDescribeQueryAst(tree)
				: ibString::Format(ibT("runs in RAM: %s"),
					refused.IsEmpty() ? ibString(ibT("no query tree")) : refused));
	}
		else if (funcIndex >= 0) {
		// The other silent gate, said out loud: only a ONE-parameter lambda is ever recorded, so
		// WhereIndexed / SelectIndexed / an Aggregate reducer / a Join result-selector never push
		// down — not because they cannot be translated, but because nobody looked.
		ibJournalInfo(ibT("linq"), ibT("lambda with %d parameters: not recorded (only one-parameter "
			"bodies are), so this step will run in RAM"),
			(int)createdFunction->m_listParam.size());
	}

	// NOTE: ibParamUnit::m_numIndex is wxLongLong_t (8 bytes) — cast to int
	// for %d, otherwise variadic packs 8 bytes and the next arg (caller_ctx)
	// reads the upper half (zero) instead of the real pointer.

	return target;
}

// ⚠ THE LINQ HALF OF THIS COMPILER LIVES IN compileCodeLINQ.cpp — the block road (`from … select`),
// the chain road (`src.Where(…)…`), the fold into a foreach somebody wrote, and `restrict`. Same
// class, same single pass, same members; a translation unit is a unit of BUILDING and this one had
// grown to seven and a half thousand lines. Nothing was made public to split it: the helpers that
// went with it had no caller on this side.

/**
 * record information about the type of variable
 */

void ibCompileCode::AddTypeSet(const ibParamUnit& variable)
{
	if (variable.m_clsid != 0) {
		ibByteUnit code;
		StampSource(code);
		code.m_numOper = OPER_SET_TYPE;
		code.m_param1 = variable;
		code.m_param2.m_numArray = variable.m_clsid;
		m_cByteCode.m_listCode.emplace_back(std::move(code));
	}
}

// ⭐ ONLY FOUR TYPES HAVE A TIER OF INSTRUCTIONS — Number, String, Date, Boolean (`OPER_… + TYPE_DELTAn`). A declared
// type of any other registered class (`Array rows`, `CatalogRef.Goods item`, since 2026-08-04) has no typed
// instruction to choose and no class the compiler can know an expression by, so it is not compared here: it is
// the runtime gate's (OPER_SET_TYPE, AllowValue) — at the declaration, at a parameter's entry, and after every
// assignment to it. Compared here it refused every use: `Array rows; rows = New Array;` was "Bad value type".
static inline bool HasTypedTier(const ibClassID& clsid)
{
	return clsid == g_valueNumberCLSID || clsid == g_valueStringCLSID
		|| clsid == g_valueDateCLSID || clsid == g_valueBooleanCLSID;
}

// macro checking variable Var against an expected type (by CLASS ID)
#define CheckTypeDef(var,typeClsid) if((typeClsid) != 0 && HasTypedTier(typeClsid))\
	{\
		if(var.m_clsid != (typeClsid)){\
			if ((typeClsid) == g_valueBooleanCLSID) Refuse(ibScriptError_BadTypeExpressionBoolean);\
			else if ((typeClsid) == g_valueNumberCLSID) Refuse(ibScriptError_BadTypeExpressionNumber);\
			else if ((typeClsid) == g_valueStringCLSID) Refuse(ibScriptError_BadTypeExpressionString);\
			else if ((typeClsid) == g_valueDateCLSID) Refuse(ibScriptError_BadTypeExpressionDate);\
			else Refuse(ibScriptError_BadTypeExpression);\
		}\
		if ((typeClsid) == g_valueNumberCLSID) code.m_numOper+=TYPE_DELTA1;\
		else if ((typeClsid) == g_valueStringCLSID) code.m_numOper+=TYPE_DELTA2;\
		else if ((typeClsid) == g_valueDateCLSID) code.m_numOper+=TYPE_DELTA3;\
        else if ((typeClsid) == g_valueBooleanCLSID) code.m_numOper+=TYPE_DELTA4;\
	}

// macro for adjusting the operation by variable type
// if it is typed, then the typed operation will be performed
#define CorrectTypeDef(sKey)\
if(sKey.m_clsid != 0 && HasTypedTier(sKey.m_clsid))\
{\
	if (sKey.m_clsid == g_valueNumberCLSID) code.m_numOper+=TYPE_DELTA1;\
	else if (sKey.m_clsid == g_valueStringCLSID) code.m_numOper+=TYPE_DELTA2;\
	else if (sKey.m_clsid == g_valueDateCLSID) code.m_numOper+=TYPE_DELTA3;\
    else if (sKey.m_clsid == g_valueBooleanCLSID)  code.m_numOper+=TYPE_DELTA4;\
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
	//     RETURN_BLOCK with no `{` — guarded by IsNextDelimiter check below.
	if (GetCodeStyle() == CODE_CES && context->m_numReturn != RETURN_NONE && IsNextDelimiter(ibT('{'))) {
		ExpectDelimiter(ibT('{'));
		bCompileBlock = true;

		// CTX_BEGIN/END are emitted only for RETURN_BLOCK — actual
		// nested block scopes. Function / lambda body envelopes use
		// the OPER_FUNC frame. Runtime bumps ibRunContext::
		// m_currentScopeDepth on CTX_BEGIN, drops on CTX_END;
		// SendLocalVariables filters Locals rows by entry depth.
		if (context->m_numReturn == RETURN_BLOCK) {
			++m_compileScopeDepth;

			ibByteUnit declCtx;
			StampSource(declCtx);
			declCtx.m_numOper = OPER_CTX_BEGIN;
			m_cByteCode.m_listCode.emplace_back(std::move(declCtx));

		}

	}

	// ⭐ A BODY WITHOUT BRACES IS ONE STATEMENT — OF ANY KIND. It used to stop after an assignment or a
	// call only: a statement opened by a keyword leaves the switch below with a `break` that ends the
	// switch, not the loop, so `else if (c) { … } n = n + 1;` put the increment inside the else
	// (2026-09-30: a `while` over it never ended). The loop condition says it for every kind at once.
	const bool oneStatement = GetCodeStyle() == CODE_CES && !bCompileBlock && context->m_numReturn == RETURN_BLOCK;

	do {

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
			case KEY_RESTRICT:
				CompileRestrictExpression(context);
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
				ExpectKeyword(KEY_RAISE);
				ibByteUnit code;
				StampSource(code);
				if (IsNextDelimiter('(')) {
					code.m_numOper = OPER_RAISE_T;
					ExpectDelimiter('(');
					code.m_param1 = GetExpression(context);
					ExpectDelimiter(')');
				}
				else {
					code.m_numOper = OPER_RAISE;
				}
				m_cByteCode.m_listCode.emplace_back(std::move(code));
				break;
			}
			case KEY_RETURN:
			{
				ExpectKeyword(KEY_RETURN);

				// ⭐⭐ A FOLDED LAMBDA BODY ANSWERS WITH A CELL, NOT BY LEAVING A FRAME. When a
				// chain's lambda became instructions inside the caller's own loop, this `return`
				// belongs to the BODY — the frame an OPER_RET would leave is the person's
				// procedure. See ibReturnCapture (compileContext.h): the value goes into the cell
				// the fold is waiting on and the jump past the body is recorded for it to patch.
				if (ibReturnCapture* capture = context->FindReturnCapture()) {
					if (IsNextDelimiter(';')) {
						Refuse(ibScriptError_ExpressionRequire);
						return false;
					}
					const ibParamUnit value = GetExpression(context);
					{
						ibByteUnit let; StampSource(let);
						let.m_numOper = OPER_LET;
						let.m_param1  = capture->m_valueCell;
						let.m_param2  = value;
						m_cByteCode.m_listCode.emplace_back(std::move(let));
					}
					// Close every block this `return` is standing inside — see ibReturnCapture.
					for (int depth = m_compileScopeDepth; depth > capture->m_scopeDepth; --depth) {
						ibByteUnit close; StampSource(close);
						close.m_numOper = OPER_CTX_END;
						m_cByteCode.m_listCode.emplace_back(std::move(close));
					}
					{
						ibByteUnit jump; StampSource(jump);
						jump.m_numOper = OPER_GOTO;   // target written when the body closes
						m_cByteCode.m_listCode.emplace_back(std::move(jump));
						capture->m_jumps.push_back((int)m_cByteCode.m_listCode.size() - 1);
					}
					break;
				}

				ibCompileContext* currContext = context;
				while (currContext->m_numReturn == RETURN_BLOCK)
					currContext = currContext->m_parentContext;

				if (currContext->m_numReturn == RETURN_NONE) {
					Refuse(ibScriptError_UseReturn); // return operator cannot be used outside a procedure or function
					return false;
				}

				ibByteUnit code;
				StampSource(code);
				code.m_numOper = OPER_RET;

				// Decide if Return must carry an expression. Function
				// kinds (named or anonymous) require a value; procedure
				// kinds reject one. m_numReturn fully discriminates
				// both axes — IsReturnFunction handles RETURN_FUNCTION
				// + RETURN_LAMBDA_FUNCTION uniformly.
				if (IsReturnFunction(currContext->m_numReturn)) {
					if (IsNextDelimiter(';')) {
						Refuse(ibScriptError_ExpressionRequire);
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
				ExpectKeyword(KEY_CONTINUE);
				// THE LOOP MAY BE A CONTEXT ABOVE — see ibCompileContext::FindLoopContext.
				ibCompileContext* loopContext = context->FindLoopContext();
				if (loopContext != nullptr) {
					ibByteUnit code;
					StampSource(code);
					code.m_numOper = OPER_GOTO;
					m_cByteCode.m_listCode.emplace_back(std::move(code));
					const int addrLine = m_cByteCode.m_listCode.size() - 1;
					const auto& pList = loopContext->m_listContinue[loopContext->m_numDoNumber];
					pList->emplace_back(addrLine);
				}
				else {
					Refuse(ibScriptError_UseContinue); // continue statement can only be used inside a loop
					return false;
				}
				break;
			}
			case KEY_BREAK:
			{
				ExpectKeyword(KEY_BREAK);
				ibCompileContext* loopContext = context->FindLoopContext();
				if (loopContext == nullptr || !loopContext->m_listBreak[loopContext->m_numDoNumber]) {
					Refuse(ibScriptError_UseBreak); // break operator can only be used inside a loop
					return false;
				}
				const auto& pList = loopContext->m_listBreak[loopContext->m_numDoNumber];
				ibByteUnit code;
				StampSource(code);
				code.m_numOper = OPER_GOTO;
				m_cByteCode.m_listCode.emplace_back(std::move(code));
				const int addrLine = m_cByteCode.m_listCode.size() - 1;
				pList->emplace_back(addrLine);
				break;
			}
			case KEY_FUNCTION:
			case KEY_PROCEDURE:
			{
				(void)GetLexem();
				Refuse(ibScriptError_UseBlock);
				return false;
				break;
			}
			default:
				return true;	// means the operator bracket ending this block has been encountered (for example, ENDIF, ENDDO, ENDFUNCTION, etc.)
			}
		}
		else {

			// A TYPED DECLARATION INSIDE A BODY. Only `Var x` used to be caught
			// here (the KEY_VAR case above), so `Boolean cancel;` in the middle of
			// a procedure was parsed as an expression and failed on the missing
			// '='. The look-ahead requires an identifier after the type, so an
			// ordinary statement that starts with a variable of the same name is
			// unaffected.
			if (IDENTIFIER == lex.m_lexType && IsTypeVar()) {
				if (!CompileDeclaration(context))
					return false;
				continue;
			}

			const ibLexem& nextLexem = GetLexem();
			if (IDENTIFIER == nextLexem.m_lexType) {

				if (GetCodeStyle() == CODE_VES)
					context->m_numTempVar = 0;

				if (IsNextDelimiter(':')) {// this is a label task encountered
					// PRESENCE, NOT VALUE — the same trap as in CreateLabels. Address
					// 0 is legitimate (a label as the first statement of a body), so
					// `> 0` would miss a genuine duplicate there, and operator[] would
					// insert the key while asking. Ask whether it is declared.
					if (context->m_listLabelDef.find(nextLexem.m_strData) != context->m_listLabelDef.end()) {
						Refuse(ibScriptError_IdentifierDuplicate, nextLexem.m_strData);// duplicate label definitions occurred
						return false;
					}
					// write the address of the label:
					context->m_listLabelDef[nextLexem.m_strData] = m_cByteCode.m_listCode.size() - 1;
					ExpectDelimiter(':');
				}
				else if (IsNextDelimiter(ibT('+')) || IsNextDelimiter(ibT('-'))
				      || IsNextDelimiter(ibT('*')) || IsNextDelimiter(ibT('/')) || IsNextDelimiter(ibT('%'))) {
					// Compound assignment / increment on a BARE variable. Reached only when the
					// identifier is IMMEDIATELY followed by an arithmetic op (no '.' / '[' / '('
					// between), so a member/array GET-temp can never be silently mutated:
					//   x++ / x--                                   -> x = x +/- 1
					//   x += e / x -= e / x *= e / x /= e / x %= e  -> x = x <op> e
					// At statement level the result is unused, so postfix == prefix store. All emit
					// the in-place shape (param1 == param2 == variable) the `x = x <op> e` fold makes.
					const ibChar op = IsNextDelimiter(ibT('+')) ? ibT('+')
					                   : IsNextDelimiter(ibT('-')) ? ibT('-')
					                   : IsNextDelimiter(ibT('*')) ? ibT('*')
					                   : IsNextDelimiter(ibT('/')) ? ibT('/') : ibT('%');
					const ibString strRealName = nextLexem.m_valData.GetString();
					ExpectDelimiter(op); // consume the operator

					ibParamUnit variable = context->GetVariable(strRealName, true, true); // must already exist
					ibByteUnit code;
					StampSource(code);
					code.m_param1 = variable; // dest in place
					code.m_param2 = variable; // left operand

					if ((op == ibT('+') || op == ibT('-')) && IsNextDelimiter(op)) {
						// ++ / -- : right operand = 1
						ExpectDelimiter(op);
						code.m_numOper = (op == ibT('+')) ? OPER_ADD : OPER_SUB;
						ibValue oneVal; oneVal.SetNumber(ibT("1"));
						code.m_param3 = FindConst(oneVal);
					}
					else if (IsNextDelimiter(ibT('='))) {
						// += / -= / *= / /= / %= : right operand = expression
						ExpectDelimiter(ibT('='));
						code.m_param3 = GetExpression(context);
						code.m_numOper = (op == ibT('+')) ? OPER_ADD
						               : (op == ibT('-')) ? OPER_SUB
						               : (op == ibT('*')) ? OPER_MULT
						               : (op == ibT('/')) ? OPER_DIV : OPER_MOD;
					}
					else { // a lone +/-/*// after a bare identifier — not valid here
						Refuse(ibScriptError_Code);
						return false;
					}
					m_cByteCode.m_listCode.emplace_back(std::move(code));
				}
				else { //function and method calls, expression assignments are processed here

					m_numCurrentCompile--;// step back

					int numSet = 1;
					if (m_onlyFunction && context == GetContext()) {
						Refuse(ibScriptError_OnlyFunction);
						return false;
					}

					ibParamUnit variable = GetCurrentIdentifier(context, numSet);//get the left side of the expression (before the '=' sign)
					if (numSet) { //if there is a right side, i.e. the '=' sign
						ExpectDelimiter('=');//this is an assignment of some expression to a variable
						ibParamUnit expression = GetExpression(context);
						ibByteUnit code;
						code.m_numOper = OPER_LET;
						StampSource(code);

						CheckTypeDef(expression, variable.m_clsid);
						// A declared type with no typed tier is checked where the value arrives — see HasTypedTier.
						ibParamUnit gated;
						if (variable.m_clsid != 0 && !HasTypedTier(variable.m_clsid))
							gated = variable;
						variable.m_clsid = expression.m_clsid;

						bool bShortLet = false; int n = 0;

						// THE LAST INSTRUCTION MUST BE THE ONE THAT PRODUCED THIS TEMP.
						//
						// The fold rewrites that instruction's destination so it writes
						// straight into the target, skipping a copy. It asked only two
						// things — is the result a temp, is the last opcode arithmetic —
						// and ASSUMED the last instruction was the one that filled the
						// temp. When it is not, the fold redirects a write meant for
						// somewhere else.
						//
						// `taken = i++` is that case: the expression's value is a temp
						// filled by an OPER_LET, and the last instruction is the
						// increment `ADD i, i, 1`. The fold pointed that ADD at `taken`,
						// so `taken` got 6 (the incremented value, not the old one) and
						// `i` was never stored at all — both halves of `x++` wrong from
						// one missing question.
						if (DEF_VAR_TEMP == expression.m_numArray) { //reduce only temporary variables
							n = m_cByteCode.m_listCode.size() - 1;
							if (n >= 0
							 && m_cByteCode.m_listCode[n].m_param1.m_numArray == expression.m_numArray
							 && m_cByteCode.m_listCode[n].m_param1.m_numIndex == expression.m_numIndex) {
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
						AddTypeSet(gated);   // nothing when the variable's type has a tier or none was declared

					}
				}
			}
			else if (nextLexem.m_lexType == DELIMITER
				&& (nextLexem.m_numData == ibT('+') || nextLexem.m_numData == ibT('-')))
			{
				// prefix ++ / -- (statement form): `++x;` / `--x;` is sugar for
				// `x = x +/- 1`. The first +/- was just consumed as nextLexem;
				// require it doubled, then a bare variable. Value is unused at
				// statement level, so this is identical to the postfix store.
				const ibChar incOp = nextLexem.m_numData;
				if (!IsNextDelimiter(incOp)) { // a lone leading +/- — syntax error, as before
					Refuse(ibScriptError_Code);
					return false;
				}
				ExpectDelimiter(incOp); // consume the second +/-
				const ibString strRealName = ExpectIdentifier(true); // target variable
				ibParamUnit variable = context->GetVariable(strRealName, true, true); // must already exist
				ibByteUnit code;
				StampSource(code);
				code.m_numOper = (incOp == ibT('+')) ? OPER_ADD : OPER_SUB;
				ibValue oneVal; oneVal.SetNumber(ibT("1"));
				code.m_param1 = variable;
				code.m_param2 = variable;
				code.m_param3 = FindConst(oneVal);
				m_cByteCode.m_listCode.emplace_back(std::move(code));
			}
			else if (nextLexem.m_lexType == DELIMITER
				&& nextLexem.m_numData == ibT(';'))
			{
			}
			else if (GetCodeStyle() == CODE_CES && nextLexem.m_lexType == DELIMITER
				&& nextLexem.m_numData == ibT('{'))
			{
				m_numCurrentCompile--;// step back


				const int numTempVar = context->m_numTempVar;
				CompileBlock(CreateLocalContext(context));
				context->m_numTempVar = numTempVar;

			}
			else if (GetCodeStyle() == CODE_CES && nextLexem.m_lexType == DELIMITER
				&& nextLexem.m_numData == ibT('}'))
			{
				// `}` at the module-level body is a syntax error — there
				// was no opening `{` to match. Without this branch we'd
				// step-back and re-read the same `}` forever. Function /
				// lambda / block bodies step back so the caller's
				// ExpectDelimiter('}') consumes the brace, then break out of
				// the body loop.
				if (context->m_numReturn == RETURN_NONE) {
					Refuse(ibScriptError_Code);
					return false;
				}

				m_numCurrentCompile--;// step back


				break;
			}
			else if (nextLexem.m_lexType == ENDPROGRAM) {
				break;
			}
			else {
				Refuse(ibScriptError_Code);
				return false;
			}
		}

	} while (!oneStatement);

	// …and the `;` that closed it is stepped over, as the next statement's look-ahead did before, so
	// `if (c) raise; else …` still finds its else.
	if (oneStatement)
		PreviewGetLexem();

	if (GetCodeStyle() == CODE_CES && bCompileBlock) {
		ExpectDelimiter(ibT('}'));

		// Pair with CTX_BEGIN — emit only for RETURN_BLOCK.
		if (context->m_numReturn == RETURN_BLOCK) {
			if (m_compileScopeDepth > 0) --m_compileScopeDepth;

			ibByteUnit declCtxEnd;
			StampSource(declCtxEnd);
			declCtxEnd.m_numOper = OPER_CTX_END;
			m_cByteCode.m_listCode.emplace_back(std::move(declCtxEnd));

		}

	}


	return true;
}//CompileBlock

bool ibCompileCode::CompileNewObject(ibCompileContext* context)
{
	ExpectKeyword(KEY_NEW);

	ibString strClassName = ExpectIdentifier(true);
	const int numConst = GetConstString(strClassName);

	std::vector <ibParamUnit> listParam;

	if (IsNextDelimiter('(')) { // this is a method call
		ExpectDelimiter('(');
		while (!IsNextDelimiter(')')) {
			if (IsNextDelimiter(',')) {
				ibParamUnit data;
				data.m_numArray = DEF_VAR_SKIP;// missing parameter
				data.m_numIndex = DEF_VAR_SKIP;
				listParam.emplace_back(std::move(data));
			}
			else {
				listParam.emplace_back(GetExpression(context));
				if (!IsNextDelimiter(',') || IsNextDelimiter(')'))
					break;
			}
			ExpectDelimiter(',');
		}
		ExpectDelimiter(')');
	}

	if (!ibValue::IsRegisterCtor(strClassName, ibCtorObjectType::ibCtorObjectType_object_value)) {
		Refuse(ibScriptError_CallConstructor, strClassName);
		return false;
	}

	ibByteUnit code;
	StampSource(code);

	code.m_numOper = OPER_NEW;
	code.m_param2.m_numIndex = numConst;//number of the called method from the list of encountered methods
	code.m_param2.m_numArray = listParam.size();// number of parameters

	// ⭐⭐ THE CLASS ID, DECIDED HERE — the guard above has just PROVED this name is a registered
	// value ctor, and that answer used to be thrown away: the instruction carried the NAME and the
	// runtime looked it up again on every execution (`CreateObject(className, …)` is literally
	// `GetIDObjectFromString` followed by `CreateObject(clsid, …)`). GetTypeVar's own comment states
	// the rule this restores: *"THE ID, NOT THE NAME. Everything downstream speaks class ids;
	// handing back a name would only mean resolving it again, later, somewhere else."*
	//
	// The name stays in the const pool: a refusal has to be able to say which class it was.
	code.m_param3.m_numIndex = (long long)ibValue::GetIDObjectFromString(strClassName);

	ibParamUnit variable = context->CreateVariable();
	code.m_param1 = variable;// variable into which the value is returned
	m_cByteCode.m_listCode.emplace_back(std::move(code));

	for (unsigned int arg = 0; arg < listParam.size(); arg++) {

		ibByteUnit argCode;
		StampSource(argCode);
		argCode.m_numOper = OPER_SET;
		argCode.m_param1 = listParam[arg];

		// The ARGUMENT's instruction — `code` was moved into the list above, so pushing it again put an empty
		// instruction where each argument belonged: `New Structure("a", 1);` as a statement reached the
		// constructor with no arguments at all (measured 2026-09-17). The expression form below was right.
		m_cByteCode.m_listCode.emplace_back(std::move(argCode));
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
	ExpectKeyword(KEY_GOTO);

	std::shared_ptr<ibCompileContext::ibLabel> data(new ibCompileContext::ibLabel);

	data->m_strName = ExpectIdentifier();
	data->m_numLine = m_cByteCode.m_listCode.size();//remember those transitions that will need to be processed later
	data->m_numError = m_numCurrentCompile;

	context->m_listLabel.emplace_back(std::move(data));

	ibByteUnit code;
	StampSource(code);
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

	const ibString& strRealName = ExpectIdentifier(true);
	const ibString& strName = stringUtils::MakeUpper(strRealName);

	if (IsNextDelimiter('(')) { // this is a function call
		std::shared_ptr<ibCompileContext::ibFunction> foundedFunc = nullptr;
		if (m_rootContext->FindFunction(strName, foundedFunc, true)) {
			const int numConst = GetConstString(strRealName);
			std::vector <ibParamUnit> listParam;
			ExpectDelimiter('(');
			while (!IsNextDelimiter(')')) {
				if (IsNextDelimiter(',')) {
					ibParamUnit data;
					data.m_numArray = DEF_VAR_SKIP; // missing parameter
					data.m_numIndex = DEF_VAR_SKIP;
					listParam.emplace_back(std::move(data));
				}
				else {
					listParam.emplace_back(GetExpression(context));
					if (!IsNextDelimiter(',') || IsNextDelimiter(')'))
						break;
				}
				ExpectDelimiter(',');
			}
			ExpectDelimiter(')');
			if (!numIsSet && foundedFunc != nullptr && !foundedFunc->m_bCodeRet) {
				Refuse(ibScriptError_UseProcedureAsFunction, foundedFunc->m_strRealName);
				return ibParamUnit();
			}
			// A variadic built-in (negative declared arity) has an empty declared
			// list, so this bound would reject its first argument. See
			// ibFunction::m_valueVariadic.
			if (!foundedFunc->m_valueVariadic && listParam.size() > foundedFunc->m_listParam.size()) {
				Refuse(ibScriptError_ManyParams, foundedFunc->m_strRealName);
				return ibParamUnit();
			}

			ibByteUnit code;
			StampSource(code);
			code.m_numOper = OPER_CALL_METHOD;

			// variable on which the method is called
			code.m_param2 = context->GetVariable(foundedFunc->m_strContext, true, false, true);
			code.m_param3.m_numIndex = numConst;//number of the called method from the list of encountered methods
			code.m_param3.m_numArray = listParam.size();// number of parameters
			variable = context->CreateVariable();
			code.m_param1 = variable;// variable into which the value is returned
			m_cByteCode.m_listCode.emplace_back(std::move(code));

			for (unsigned int i = 0; i < listParam.size(); i++) {
				ibByteUnit argCode;
				StampSource(argCode);
				argCode.m_numOper = OPER_SET;
				argCode.m_param1 = listParam[i];
				m_cByteCode.m_listCode.emplace_back(std::move(argCode));
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
				&& !foundedCallableVar->IsExternal()
				&& (!foundedCallableVar->IsContext() || foundInBlockScope);

			if (isCallableVar) {
				std::vector<ibParamUnit> listParam;
				ExpectDelimiter('(');
				while (!IsNextDelimiter(')')) {
					if (IsNextDelimiter(',')) {
						ibParamUnit data;
						data.m_numArray = DEF_VAR_SKIP; // missing parameter
						data.m_numIndex = DEF_VAR_SKIP;
						listParam.emplace_back(std::move(data));
					}
					else {
						listParam.emplace_back(GetExpression(context));
						if (!IsNextDelimiter(',') || IsNextDelimiter(')'))
							break;
					}
					ExpectDelimiter(',');
				}
				ExpectDelimiter(')');

				ibByteUnit code;
				StampSource(code);
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
					ibByteUnit argCode;
					StampSource(argCode);
					argCode.m_numOper = OPER_SET;
					argCode.m_param1 = listParam[i];
					m_cByteCode.m_listCode.emplace_back(std::move(argCode));
				}
			}
			else {
				variable = GetCallFunction(context, strRealName, numIsSet);
			}
		}
		if (IsTypeVar(strRealName)) {
			variable.m_clsid = GetTypeVar(strRealName);	// this is a type cast
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
		context->FindVariable(strRealName, foundedVar, true);
		const bool isScope   = foundedVar && foundedVar->IsContextProp();
		const bool isExtern  = foundedVar && foundedVar->IsExternal();
		const bool isContext = foundedVar && foundedVar->IsContext();

		if (isScope) {
			ibByteUnit code;
			StampSource(code);
			const int numConst = GetConstString(strRealName);
			if (IsNextDelimiter('=') && numPrevSet == 1) {
				ExpectDelimiter('='); numIsSet = 0;
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
			StampSource(code);
			const long getOp = isExtern ? OPER_GET_EXTERN : OPER_GET_CONTEXT;
			const long setOp = isExtern ? OPER_SET_EXTERN : OPER_SET_CONTEXT;
			if (IsNextDelimiter('=') && numPrevSet == 1) {
				ExpectDelimiter('='); numIsSet = 0;
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
			if (IsNextDelimiter('.') || IsNextDelimiter('[')) //this variable contains a method call or array
				bCheckError = true;
			variable = context->GetVariable(strRealName, true, bCheckError);
		}
	}

loopLabel:

	if (IsNextDelimiter('[')) { // this is an array
		ExpectDelimiter('[');
		ibParamUnit variableKey = GetExpression(context);
		ExpectDelimiter(']');
		//determine the call type (i.e. is it setting or getting an array value)
		//Example:
		//Arr[10]=12; - Set
		//�=Arr[10]; - Get
		//Arr[10][2]=12; - Get,Set
		numIsSet = 0;
		if (IsNextDelimiter('[')) { //check the array variable type (multidimensional array support)
			ibByteUnit code;
			StampSource(code);
			code.m_numOper = OPER_CHECK_ARRAY;
			code.m_param1 = variable;//variable is an array
			code.m_param2 = variableKey;//array index
			m_cByteCode.m_listCode.emplace_back(std::move(code));
		}
		if (IsNextDelimiter('=') && numPrevSet == 1) {
			ExpectDelimiter('=');
			ibByteUnit code;
			StampSource(code);
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
			StampSource(code);
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

	if (IsNextDelimiter('.')) { // this is a method call ��� �������� ����������� �������

		// ⭐⭐ A PIPELINE WRITTEN AND CONSUMED IN ONE EXPRESSION IS A LOOP, AND COMPILES AS ONE.
		// Asked BEFORE the dot is consumed, because the answer needs the whole chain and the
		// receiver as it stands. Refuses everything outside its slice, and then the ordinary
		// OPER_CALL_LINQ road below takes it — which is always correct. (docs/private/linq.md §0.2g)
		{
			ibParamUnit inlined;
			if (CompileLinqChain(context, variable, inlined)) {
				variable = inlined;
				goto loopLabel;
			}
			// …and the same fold with no terminal to end on: the SOURCE of a foreach. The loop
			// belongs to the foreach, so the verbs are parked here and emitted into its body.
			if (TryFoldLinqChainSource(context, variable))
				goto loopLabel;
		}

		ExpectDelimiter('.');

		// ⭐⭐ THE TEXT ENDED ON THE DOT, and refusing here would throw away the one thing that was
		// asked about. The compiler has the RECEIVER resolved and in hand; what is missing is a name
		// nobody has typed yet. In a runtime compile that is still an error — an unfinished module
		// is broken — but a tolerant compile was asked "tell me what you can about this text", and
		// what it can tell is exactly this: the chain got this far, to this value.
		//
		// So the step is emitted with NO attribute name, and it costs no new notion: a reader that
		// walks the instructions finds a member step whose name is empty and answers with the
		// parent, which is what standing on a dot means (scriptComplete.cpp). The instruction is never
		// executed — a tolerant compile produces no runtime artefact.
		if (m_compileMode == ibCompileMode::Tolerant && IsEndOfProgram()) {

			// The refusal is still SAID — in this mode Refuse reports and returns, so a check
			// asked about a finished text still hears about the dangling dot.
			Refuse(ibScriptError_IdentifierDefine);

			ibByteUnit code;
			StampSource(code);
			code.m_numOper = OPER_GET_A;
			code.m_param2 = variable;                                 // the receiver, still in hand
			code.m_param3.m_numIndex = GetConstString(ibEmptyString);  // the name nobody typed yet
			variable = context->CreateVariable();
			code.m_param1 = variable;

			// ⭐⭐ AND THE COMPILER SAYS WHICH INSTRUCTION IT IS, rather than leaving a reader to find
			// it by position. What follows this step on the tape is the compiler CLOSING what the
			// typist left open — a block's `OPER_CTX_END`, a query's `OPER_LINQ_RESULT`, a loop's
			// `OPER_NEXT_ITER` — and every one of them carries this same offset, because the parser
			// has read no further token to stamp them with. See ibByteCode::m_numCaretInstruction.
			//
			// Guarded by the caret's own position: the same gate fires in an ordinary tolerant check
			// (script_check) where nobody asked about a caret, and in the full-text compile the name
			// door does, where the caret sits BEFORE this dot and must not be dragged to it.
			const long emittedAt = (long)code.m_numString;
			m_cByteCode.m_listCode.emplace_back(std::move(code));

			if (m_caretPos >= 0 && emittedAt <= m_caretPos)
				m_cByteCode.m_numCaretInstruction = (long)m_cByteCode.m_listCode.size() - 1;

			return variable;
		}

		// acceptKeyword=true: contextual LINQ keywords (Where/Select/...)
		// must work as method names in property-access positions.
		ibString strIdentifier = ExpectIdentifier(true, /*acceptKeyword*/true);
		const int numConst = GetConstString(strIdentifier);
		if (IsNextDelimiter('(')) { // this is a method call
			std::vector <ibParamUnit> listParam;
			ExpectDelimiter('(');
			while (!IsNextDelimiter(')')) {
				if (IsNextDelimiter(',')) {
					ibParamUnit data;
					data.m_numArray = DEF_VAR_SKIP;// missing parameter
					data.m_numIndex = DEF_VAR_SKIP;
					listParam.emplace_back(std::move(data));
				}
				else {
					listParam.emplace_back(GetExpression(context));
					if (!IsNextDelimiter(',') || IsNextDelimiter(')'))
						break;
				}
				ExpectDelimiter(',');
			}
			ExpectDelimiter(')');

			ibByteUnit code;
			StampSource(code);

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
				ibByteUnit argCode;
				StampSource(argCode);
				argCode.m_numOper = OPER_SET;
				argCode.m_param1 = listParam[i];
				m_cByteCode.m_listCode.emplace_back(std::move(argCode));
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
			StampSource(code);
			if (IsNextDelimiter('=') && numPrevSet == 1) {
				ExpectDelimiter('='); 	numIsSet = 0;
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

	ExpectKeyword(KEY_IF);

	ibByteUnit code;
	StampSource(code);
	code.m_numOper = OPER_IF;

	if (GetCodeStyle() == CODE_CES)
		ExpectDelimiter(ibT('('));

	ibParamUnit variable = GetExpression(context);
	code.m_param1 = variable;
	CorrectTypeDef(variable);// check value type

	m_cByteCode.m_listCode.emplace_back(std::move(code));

	int nLastIFLine = m_cByteCode.m_listCode.size() - 1;

	if (GetCodeStyle() == CODE_VES)
		ExpectKeyword(KEY_THEN);
	else
		ExpectDelimiter(ibT(')'));

	if (GetCodeStyle() == CODE_CES)
		CompileBlock(CreateLocalContext(context));
	else
		CompileBlock(context);

	while (IsNextKeyword(KEY_ELSEIF)) {

		ibByteUnit code1;

		// write the output from all checks for the previous block
		StampSource(code1);

		code1.m_numOper = OPER_GOTO;
		m_cByteCode.m_listCode.emplace_back(std::move(code1));

		listAddrLine.emplace_back(m_cByteCode.m_listCode.size() - 1);//the parameter for the GOTO operator will be known later

		//for the previous condition, set the jump address if the condition does not match
		m_cByteCode.m_listCode[nLastIFLine].m_param2.m_numIndex = m_cByteCode.m_listCode.size();

		ibByteUnit code2;
		StampSource(code2);

		ExpectKeyword(KEY_ELSEIF);
		StampSource(code2);
		code2.m_numOper = OPER_IF;

		if (GetCodeStyle() == CODE_CES)
			ExpectDelimiter(ibT('('));

		variable = GetExpression(context);
		code2.m_param1 = variable;
		CorrectTypeDef(variable);// check value type

		m_cByteCode.m_listCode.emplace_back(std::move(code2));
		nLastIFLine = m_cByteCode.m_listCode.size() - 1;

		if (GetCodeStyle() == CODE_VES)
			ExpectKeyword(KEY_THEN);
		else
			ExpectDelimiter(ibT(')'));

		if (GetCodeStyle() == CODE_CES)
			CompileBlock(CreateLocalContext(context));
		else
			CompileBlock(context);
	}

	if (IsNextKeyword(KEY_ELSE)) {

		ibByteUnit code1;

		// write the output from all checks for the previous block
		StampSource(code1);

		code1.m_numOper = OPER_GOTO;
		m_cByteCode.m_listCode.emplace_back(std::move(code1));

		listAddrLine.emplace_back(m_cByteCode.m_listCode.size() - 1);//the parameter for the GOTO operator will be known later

		//for the previous condition, set the jump address if the condition does not match
		m_cByteCode.m_listCode[nLastIFLine].m_param2.m_numIndex = m_cByteCode.m_listCode.size();

		// ⚠ "THERE IS NO LONGER AN OPEN CONDITION", and it must not be sayable as an
		// ADDRESS. This was `nLastIFLine = 0`, and zero is a perfectly good instruction
		// index — the FIRST one — so the unconditional write at the end of this function
		// stamped the length of the bytecode into the operand of instruction 0.
		//
		// An `else` therefore corrupted the module's opening instruction. Silently,
		// because what it overwrote is m_param2.m_numIndex — an operand's slot number,
		// which stays a plausible number: `Message("x" + CommonModule.Method())` on the
		// first line began resolving CommonModule to slot 116 of a frame holding 69, and
		// the engine answered "a variable is not an aggregate object" about a name that
		// is a common module. Where the first instruction's second operand happened not
		// to matter, the corruption did nothing at all and waited.
		nLastIFLine = wxNOT_FOUND;

		ExpectKeyword(KEY_ELSE);

		if (GetCodeStyle() == CODE_CES)
			CompileBlock(CreateLocalContext(context));
		else
			CompileBlock(context);
	}

	if (GetCodeStyle() == CODE_VES)
		ExpectKeyword(KEY_ENDIF);

	const int numCurCompile = m_cByteCode.m_listCode.size();

	//for the last condition, set the jump address if the condition does not match
	// — unless an `else` closed the last one already, in which case there is nothing
	// to point anywhere and the marker says so.
	if (nLastIFLine != wxNOT_FOUND)
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

	ExpectKeyword(KEY_WHILE);
	ibByteUnit code;
	StampSource(code);
	code.m_numOper = OPER_IF;

	if (GetCodeStyle() == CODE_CES)
		ExpectDelimiter(ibT('('));

	ibParamUnit variable = GetExpression(context);
	code.m_param1 = variable;
	CorrectTypeDef(variable);// check value type

	const int numEndWhile = m_cByteCode.m_listCode.size();

	m_cByteCode.m_listCode.emplace_back(std::move(code));

	if (GetCodeStyle() == CODE_VES)
		ExpectKeyword(KEY_DO);
	else
		ExpectDelimiter(ibT(')'));

	if (GetCodeStyle() == CODE_CES)
		CompileBlock(CreateLocalContext(context));
	else
		CompileBlock(context);

	if (GetCodeStyle() == CODE_VES)
		ExpectKeyword(KEY_ENDDO);

	ibByteUnit code2;
	StampSource(code2);
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

	ExpectKeyword(KEY_FOR);

	if (GetCodeStyle() == CODE_CES)
		ExpectDelimiter(ibT('('));

	const ibString& strRealName = ExpectIdentifier(true);
	const ibString& strName = stringUtils::MakeUpper(strRealName);

	ibParamUnit variable = context->GetVariable(strRealName);

	// check variable type
	if (variable.m_clsid != 0) {
		if (variable.m_clsid != g_valueNumberCLSID) {
			Refuse(ibScriptError_NumberType);
			return false;
		}
	}

	ExpectDelimiter('=');
	ibParamUnit variable2 = GetExpression(context);

	ibByteUnit code0;
	StampSource(code0);
	code0.m_numOper = OPER_LET;
	code0.m_param1 = variable;
	code0.m_param2 = variable2;
	m_cByteCode.m_listCode.emplace_back(std::move(code0));

	// check value type
	if (variable.m_clsid != 0) {
		if (variable2.m_clsid != g_valueNumberCLSID) {
			Refuse(ibScriptError_BadTypeExpression);
			return false;
		}
	}

	ExpectKeyword(KEY_TO);

	ibParamUnit variableTo =
		context->GetVariable(strName + ibT("@to"), true, false, false, true); //loop variable

	ibByteUnit code1;
	StampSource(code1);
	code1.m_numOper = OPER_LET;
	code1.m_param1 = variableTo;
	code1.m_param2 = GetExpression(context);
	m_cByteCode.m_listCode.emplace_back(std::move(code1));

	ibByteUnit code;
	StampSource(code);
	code.m_numOper = OPER_FOR;
	code.m_param1 = variable;
	code.m_param2 = variableTo;
	m_cByteCode.m_listCode.emplace_back(std::move(code));

	const int nStartFOR = m_cByteCode.m_listCode.size() - 1;

	if (GetCodeStyle() == CODE_VES)
		ExpectKeyword(KEY_DO);
	else
		ExpectDelimiter(ibT(')'));

	if (GetCodeStyle() == CODE_CES)
		CompileBlock(CreateLocalContext(context));
	else
		CompileBlock(context);

	if (GetCodeStyle() == CODE_VES)
		ExpectKeyword(KEY_ENDDO);

	ibByteUnit code2;
	StampSource(code2);
	code2.m_numOper = OPER_NEXT;
	code2.m_param1 = variable;
	code2.m_param2.m_numIndex = nStartFOR;
	m_cByteCode.m_listCode.emplace_back(std::move(code2));

	m_cByteCode.m_listCode[nStartFOR].m_param3.m_numIndex = m_cByteCode.m_listCode.size();

	// remember the transition addresses for the Continue and Break commands
	context->FinishLoopList(m_cByteCode, m_cByteCode.m_listCode.size() - 1, m_cByteCode.m_listCode.size());

	return true;
}

// Where a `foreach` header closes, read off the tokens with brackets balanced — see the declaration.
// A pure look: nothing is consumed and nothing is emitted, so it can be asked before the source is
// read, which is the only moment its answer is of any use.
int ibCompileCode::FindForeachHeaderEnd(int at) const
{
	// ⚠ A NEGATIVE CURSOR IS THE ORDINARY "BEFORE THE FIRST TOKEN" (m_numCurrentCompile starts at
	// wxNOT_FOUND), and `(size_t)(-1) + 1` is 0 — a valid index. So the scan would silently start at
	// the top of the module instead of refusing. One comparison closes that whole question.
	if (at < 0)
		return wxNOT_FOUND;

	const auto isDelim = [&](size_t i, ibChar c) {
		return i < m_translateCode.GetLexCount() && m_translateCode.GetLexAt(i).m_lexType == DELIMITER
			&& (ibChar)m_translateCode.GetLexAt(i).m_numData == c;
	};

	int depth = 0;
	for (size_t i = (size_t)at + 1; i < m_translateCode.GetLexCount(); ++i) {

		if (m_translateCode.GetLexAt(i).m_lexType == ENDPROGRAM)
			break;

		if (isDelim(i, ibT('(')) || isDelim(i, ibT('['))) { ++depth; continue; }
		if (isDelim(i, ibT(')')) || isDelim(i, ibT(']'))) {
			// The header's own closer is the first one the source did not open.
			if (depth == 0)
				return GetCodeStyle() == CODE_CES ? (int)i : wxNOT_FOUND;
			--depth;
			continue;
		}
		if (depth == 0 && GetCodeStyle() != CODE_CES
			&& m_translateCode.GetLexAt(i).m_lexType == KEYWORD && m_translateCode.GetLexAt(i).m_numData == KEY_DO)
			return (int)i;
	}
	return wxNOT_FOUND;
}

bool ibCompileCode::CompileForeach(ibCompileContext* context)
{
	context->StartLoopList();

	ExpectKeyword(KEY_FOREACH);

	if (GetCodeStyle() == CODE_CES)
		ExpectDelimiter(ibT('('));

	const ibString& strRealName = ExpectIdentifier(true);
	const ibString& strName = stringUtils::MakeUpper(strRealName);

	ibParamUnit variable = context->GetVariable(strRealName);

	ExpectKeyword(KEY_IN);

	ibParamUnit variableIn =
		context->GetVariable(strName + ibT("@in_"), true, false, false, true); //loop variable

	ibByteUnit code1;
	StampSource(code1);
	code1.m_numOper = OPER_LET;
	code1.m_param1 = variableIn;
	// ⭐ `foreach (r in src.Where(λ))` — the verbs are recognised WHILE the source is read and parked
	// (TryFoldLinqChainSource); what comes back here is the source itself, and the filters and
	// projections are emitted into the body below.
	//
	// The scope says WHICH header is being read, by the position of its closer, and disarms itself on
	// every way out — see ibLinqSourceScope. A source that is not a chain costs the scan below and
	// nothing else.
	{
		const ibLinqSourceScope readingTheSource(this, FindForeachHeaderEnd(m_numCurrentCompile));
		code1.m_param2 = GetExpression(context);
	}
	m_cByteCode.m_listCode.emplace_back(std::move(code1));

	ibParamUnit variableIt =
		context->GetVariable(strName + ibT("@it_"), true, false, false, true);  //storage iterpos;

	ibByteUnit code;
	StampSource(code);
	code.m_numOper = OPER_FOREACH;
	code.m_param1 = variable;
	code.m_param2 = variableIn;
	code.m_param3 = variableIt; // for storage iterpos;

	m_cByteCode.m_listCode.emplace_back(std::move(code));

	const int numStartFOREACH = m_cByteCode.m_listCode.size() - 1;

	// The folded verbs open the body: a `Where` becomes a branch to the next row, a `Select` writes
	// the projection back into the loop variable. Emitted BEFORE the person's own body, which then
	// runs only for rows that survived and sees exactly what the chain said it would.
	std::vector<int> chainSkipIps;
	EmitLinqChainClauses(context, variable, chainSkipIps);

	if (GetCodeStyle() == CODE_VES)
		ExpectKeyword(KEY_DO);
	else
		ExpectDelimiter(ibT(')'));

	if (GetCodeStyle() == CODE_CES)
		CompileBlock(CreateLocalContext(context));
	else
		CompileBlock(context);

	if (GetCodeStyle() == CODE_VES)
		ExpectKeyword(KEY_ENDDO);

	const int nextIterIp = (int)m_cByteCode.m_listCode.size();
	ibByteUnit code2;
	StampSource(code2);
	code2.m_numOper = OPER_NEXT_ITER;
	code2.m_param1 = variableIt; // for storage iterpos;
	code2.m_param2.m_numIndex = numStartFOREACH;
	m_cByteCode.m_listCode.emplace_back(std::move(code2));

	// A row the chain rejected goes straight to the next one — the same address `continue` uses.
	for (const int ip : chainSkipIps)
		m_cByteCode.m_listCode[ip].m_param2.m_numIndex = nextIterIp;

	m_cByteCode.m_listCode[numStartFOREACH].m_param4.m_numIndex = m_cByteCode.m_listCode.size();

	// remember the transition addresses for the Continue and Break commands
	context->FinishLoopList(m_cByteCode, m_cByteCode.m_listCode.size() - 1, m_cByteCode.m_listCode.size());
	return true;
}

bool ibCompileCode::CompileException(ibCompileContext* context)
{
	// A Try inside a Try is another compile frame. The same ceiling an
	// expression already has: the text is refused, the stack is not.
	if (ibFiber::StackLow()) {
		Refuse(ibScriptError_Expression,
			_("it is nested deeper than the stack allows - split it into steps"));
		return false;
	}
	ExpectKeyword(KEY_TRY);
	ibByteUnit code1;
	StampSource(code1);
	code1.m_numOper = OPER_TRY;
	m_cByteCode.m_listCode.emplace_back(std::move(code1));

	const int lineTry = m_cByteCode.m_listCode.size() - 1;

	ibByteUnit code2;
	StampSource(code2);

	if (GetCodeStyle() == CODE_CES)
		CompileBlock(CreateLocalContext(context));
	else
		CompileBlock(context);

	code2.m_numOper = OPER_ENDTRY;
	m_cByteCode.m_listCode.emplace_back(std::move(code2));

	const int addrLine = m_cByteCode.m_listCode.size() - 1;

	m_cByteCode.m_listCode[lineTry].m_param1.m_numIndex = m_cByteCode.m_listCode.size();

	ExpectKeyword(KEY_EXCEPT);

	if (GetCodeStyle() == CODE_CES)
		CompileBlock(CreateLocalContext(context));
	else
		CompileBlock(context);

	if (GetCodeStyle() == CODE_VES)
		ExpectKeyword(KEY_ENDTRY);

	m_cByteCode.m_listCode[addrLine].m_param1.m_numIndex = m_cByteCode.m_listCode.size();
	return true;
}

/**
 * processing a function or procedure call
 */

ibParamUnit ibCompileCode::GetCallFunction(ibCompileContext* context, const ibString& strRealName, const int& numIsSet)
{
	std::shared_ptr<ibCallFunction> callFunc(new ibCallFunction);

	callFunc->m_strName = stringUtils::MakeUpper(strRealName);
	callFunc->m_strRealName = strRealName;
	callFunc->m_numError = m_numCurrentCompile;// to display messages when errors occur

	ExpectDelimiter('(');

	while (!IsNextDelimiter(')')) {
		if (IsNextDelimiter(',')) {
			ibParamUnit data;
			data.m_numArray = DEF_VAR_SKIP;// missing parameter
			data.m_numIndex = DEF_VAR_SKIP;
			callFunc->m_listParam.emplace_back(std::move(data));
		}
		else {
			callFunc->m_listParam.emplace_back(GetExpression(context));
			if (!IsNextDelimiter(',') || IsNextDelimiter(')'))
				break;
		}
		ExpectDelimiter(',');
	}

	ExpectDelimiter(')');

	ibByteUnit code;
	StampSource(code);

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
	if (foundedFunc != nullptr && !stringUtils::IsSameAs(m_strCurFuncName, callFunc->m_strName)) {

		if (!PushCallFunction(callFunc))
			return ibParamUnit();

		return callFunc->m_puRetValue;
	}

	if (IsExpressionOnly()) {
		Refuse(ibScriptError_CallFunction, strRealName);
		return ibParamUnit();
	}


	code.m_numOper = OPER_GOTO;// jump to the end of the bytecode where the expanded call will be made
	// The call itself is appended at the end of the module, so this jump leaves
	// whatever try contains it. A jump to a line outside the try drops the handler
	// (procUnit, OPER_GOTO). The return jump comes back in, but the handler is
	// already gone, and an exception from a function declared later is not caught.
	// param2 stays 0 on every other goto. The interpreter keeps the handler when
	// this one is set. No new opcode: the operand was already stored, and it was 0.
	code.m_param2.m_numIndex = 1;
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
	const ibString& strConstant =
		ibString::Format(ibT("%d:%s"), constData.GetType(), constData.GetString());

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
	variable.m_clsid = GetTypeVar(constData.GetClassName());
	return variable;
}

#define SetOper(x)	code.m_numOper=x;

/**
 * compiling an arbitrary expression (service calls from the function itself)
 */

// ⚠ A NESTED EXPRESSION IS A NESTED CALL. Each level of `((((…` is one more GetExpression frame on
// the native stack, and with no ceiling a pathological or broken module overflowed it: the process
// went down without a word. What runs out is the THREAD's stack, so the count is the thread's — a
// compile started inside another (an eval during a run) spends the same stack. Past the ceiling
// the expression is refused like any other bad expression.
static thread_local int ts_expressionDepth = 0;
static constexpr int gs_maxExpressionDepth = 400;

ibParamUnit ibCompileCode::GetExpression(ibCompileContext* context, int nPriority)
{
	struct ibExpressionDepthGuard {
		ibExpressionDepthGuard() { ++ts_expressionDepth; }
		~ibExpressionDepthGuard() { --ts_expressionDepth; }
	} depthGuard;
	if (ts_expressionDepth > gs_maxExpressionDepth) {
		Refuse(ibScriptError_Expression, ibString::Format(
			_("it is nested deeper than %d levels - split it into steps"), gs_maxExpressionDepth));
		return ibParamUnit();
	}
	// The ceiling above is what a module sees. This is the overflow past
	// it: the frame is already on the stack, and another level would not fit.
	if (ibFiber::StackLow()) {
		Refuse(ibScriptError_Expression,
			_("it is nested deeper than the stack allows - split it into steps"));
		return ibParamUnit();
	}

	const ibLexem& lex = ExpectLexem();

	// create variable 
	ibParamUnit variable;

	// first we process Left operators
	if ((lex.m_lexType == KEYWORD && lex.m_numData == KEY_NOT) || (lex.m_lexType == DELIMITER && lex.m_numData == '!')) {

		variable = context->CreateVariable();
		variable.m_clsid = g_valueBooleanCLSID;

		// ⚠ NO GATE ON THE OPERATOR'S OWN CELL. OPER_SET_TYPE used to MAKE the cell Boolean; it is a gate now (it
		// checks the value standing there), and emitted before OPER_NOT it checked what the cell held from the
		// previous pass. In a LINQ filter NOT over an unknown answers UNKNOWN (LINQ_THREE_VALUED_NULL), so the
		// second row met the first row's NULL and raised "Type mismatch ... 'Boolean'" (2026-09-17). OPER_NOT
		// types its result itself; the class id above is what the compiler reads the expression as.

		// ⭐⭐ `Not` IS TIGHTER THAN `And` / `Or` AND LOOSER THAN A COMPARISON (Max, 2026-09-17): `Not x = 5` is
		// `Not (x = 5)`, `Not a And b` is `(Not a) And b`. The operand is read at And's priority, so it takes
		// comparisons, `In`, arithmetic, and stops at the first `And` / `Or`; what follows continues in the
		// caller's loop with `Not a` as its left side. The query language reads NOT the same way
		// (queryParser.cpp: OR < AND < NOT < comparison).
		//
		// 🛑 It used to take the WHOLE rest — `Not a And b` was `Not (a And b)` — with `gs_operPriority['!']`
		// (50) written beside the call and commented out; uncommenting that would have made `Not x = 5` read
		// `(Not x) = 5`. Pinned for a week by LambdaRecorderCES as a question, not an answer.
		ibParamUnit variable2 = GetExpression(context, gs_operPriority[KEY_AND]);

		ibByteUnit code;
		code.m_numOper = OPER_NOT;
		StampSource(code);

		if (variable2.m_clsid != 0) {
			CheckTypeDef(variable2, g_valueBooleanCLSID);
		}

		code.m_param1 = variable;
		code.m_param2 = variable2;

		m_cByteCode.m_listCode.emplace_back(std::move(code));
	}
	else if ((lex.m_lexType == KEYWORD && lex.m_numData == KEY_NEW)) {

		const ibString strObjectName = ExpectIdentifier(true);
		const int numConst = GetConstString(strObjectName);

		std::vector <ibParamUnit> listParam;

		if (IsNextDelimiter('(')) { // this is a method call
			ExpectDelimiter('(');
			while (!IsNextDelimiter(')')) {
				if (IsNextDelimiter(',')) {
					ibParamUnit data;
					data.m_numArray = DEF_VAR_SKIP;// missing parameter
					data.m_numIndex = DEF_VAR_SKIP;
					listParam.emplace_back(std::move(data));
				}
				else {
					listParam.emplace_back(GetExpression(context));
					if (!IsNextDelimiter(',') || IsNextDelimiter(')'))
						break;
				}
				ExpectDelimiter(',');
			}
			ExpectDelimiter(')');
		}

		if (lex.m_numData == KEY_NEW && !ibValue::IsRegisterCtor(strObjectName, ibCtorObjectType::ibCtorObjectType_object_value)) {
			Refuse(ibScriptError_CallConstructor, strObjectName);
			return ibParamUnit();
		}

		ibByteUnit code;
		StampSource(code);
		code.m_numOper = OPER_NEW;

		code.m_param2.m_numIndex = numConst;//number of the called method from the list of encountered methods
		code.m_param2.m_numArray = listParam.size();// number of parameters
		// The id, decided here — see the twin emitter above for why.
		code.m_param3.m_numIndex = (long long)ibValue::GetIDObjectFromString(strObjectName);

		variable = context->CreateVariable();
		code.m_param1 = variable;// variable into which the value is returned
		m_cByteCode.m_listCode.emplace_back(std::move(code));

		for (unsigned int arg = 0; arg < listParam.size(); arg++) {
			ibByteUnit argCode;
			StampSource(argCode);
			argCode.m_numOper = OPER_SET;
			argCode.m_param1 = listParam[arg];
			m_cByteCode.m_listCode.emplace_back(std::move(argCode));
		}
	}
	else if (lex.m_lexType == KEYWORD && lex.m_numData == KEY_FROM) {
		// LINQ block expression — `from <id> in <expr> ... select <expr>`.
		// Step back so CompileLinqExpression's own ExpectKeyword(KEY_FROM)
		// can re-consume the keyword via its standard path; mirrors the
		// CompileLambdaExpression idiom below. Returns the slot of the
		// materialised result array (eager Phase 1: inline foreach +
		// array build).
		m_numCurrentCompile--;
		variable = CompileLinqExpression(context);
	}
	else if (lex.m_lexType == KEYWORD && lex.m_numData == KEY_RESTRICT) {
		// Access-policy restriction — `restrict <id> in <src> [join ...] [where ...]`.
		// Step back so CompileRestrictExpression's own ExpectKeyword(KEY_RESTRICT)
		// re-consumes the keyword via its standard path (mirrors the KEY_FROM ->
		// CompileLinqExpression idiom above).
		m_numCurrentCompile--;
		variable = CompileRestrictExpression(context);
	}
	else if (lex.m_lexType == KEYWORD &&
		(lex.m_numData == KEY_FUNCTION || lex.m_numData == KEY_PROCEDURE)) {
		// Anonymous Function/Procedure as expression — Phase A lambda.
		// Step back so CompileLambdaExpression's ParseFunctionSignature
		// can re-consume the keyword via its standard
		// IsNextKeyword/ExpectKeyword path; mirrors the IDENTIFIER-branch
		// step-back idiom below.
		m_numCurrentCompile--;
		variable = CompileLambdaExpression(context);
	}
	else if (lex.m_lexType == DELIMITER && lex.m_numData == '(') {
		variable = GetExpression(context);
		ExpectDelimiter(')');
	}
	else if (lex.m_lexType == DELIMITER && lex.m_numData == '?') {
		// ?(cond, whenTrue, whenFalse) — an If that answers with a value.
		//
		// Both branches used to be compiled, and run, before OPER_ITER copied
		// one of the two results. The branch the condition did not pick still
		// raised and still called: `?(Ref <> Undefined, Ref.Description, "")`
		// died on Undefined. The shape below is CompileIf's: test the
		// condition, write the chosen value into the result, jump over the
		// other branch. The branch not taken is in the tape and is not executed.
		variable = context->CreateVariable();

		ExpectDelimiter('(');

		ibByteUnit code;
		StampSource(code);
		code.m_numOper = OPER_IF;
		ibParamUnit condition = GetExpression(context);
		code.m_param1 = condition;
		CorrectTypeDef(condition);
		m_cByteCode.m_listCode.emplace_back(std::move(code));
		const int ifLine = m_cByteCode.m_listCode.size() - 1;

		ExpectDelimiter(',');
		{
			ibByteUnit letTrue;
			StampSource(letTrue);
			letTrue.m_numOper = OPER_LET;
			letTrue.m_param1 = variable;
			letTrue.m_param2 = GetExpression(context);
			m_cByteCode.m_listCode.emplace_back(std::move(letTrue));
		}

		ibByteUnit jumpOver;
		StampSource(jumpOver);
		jumpOver.m_numOper = OPER_GOTO;
		m_cByteCode.m_listCode.emplace_back(std::move(jumpOver));
		const int gotoLine = m_cByteCode.m_listCode.size() - 1;

		// Where If sends a condition that does not hold: the other branch.
		m_cByteCode.m_listCode[ifLine].m_param2.m_numIndex = m_cByteCode.m_listCode.size();

		ExpectDelimiter(',');
		{
			ibByteUnit letFalse;
			StampSource(letFalse);
			letFalse.m_numOper = OPER_LET;
			letFalse.m_param1 = variable;
			letFalse.m_param2 = GetExpression(context);
			m_cByteCode.m_listCode.emplace_back(std::move(letFalse));
		}
		ExpectDelimiter(')');

		m_cByteCode.m_listCode[gotoLine].m_param1.m_numIndex = m_cByteCode.m_listCode.size();
	}
	else if (lex.m_lexType == IDENTIFIER) {
		// postfix ++ / -- in expression on a BARE variable: `x++` / `x--` yields
		// the OLD value, then stores x = x +/- 1. Gate: the identifier is
		// IMMEDIATELY followed by a doubled, source-ADJACENT +/- — so member /
		// array GET-temps (`obj.a++`, `arr[i]++`) fall through to a normal read,
		// and `a - -b` (spaced) is not mistaken for `a--`.
		ibChar incOp = 0;
		if (m_numCurrentCompile + 2 < m_translateCode.GetLexCount()
			&& m_translateCode.GetLexAt(m_numCurrentCompile + 1).m_lexType == DELIMITER
			&& m_translateCode.GetLexAt(m_numCurrentCompile + 2).m_lexType == DELIMITER
			&& (m_translateCode.GetLexAt(m_numCurrentCompile + 1).m_numData == '+' || m_translateCode.GetLexAt(m_numCurrentCompile + 1).m_numData == '-')
			&& m_translateCode.GetLexAt(m_numCurrentCompile + 1).m_numData == m_translateCode.GetLexAt(m_numCurrentCompile + 2).m_numData
			&& m_translateCode.GetLexAt(m_numCurrentCompile + 2).m_numString == m_translateCode.GetLexAt(m_numCurrentCompile + 1).m_numString + 1)
			incOp = (ibChar)m_translateCode.GetLexAt(m_numCurrentCompile + 1).m_numData;

		if (incOp != 0) {
			const ibString strRealName = lex.m_valData.GetString();
			ExpectDelimiter(incOp);
			ExpectDelimiter(incOp);
			ibParamUnit target = context->GetVariable(strRealName, true, true); // lvalue, must exist
			// expression value = a temp holding the OLD value (captured before the store)
			variable = context->CreateVariable();
			ibByteUnit getOld;
			StampSource(getOld);
			getOld.m_numOper = OPER_LET;
			getOld.m_param1 = variable;
			getOld.m_param2 = target;
			m_cByteCode.m_listCode.emplace_back(std::move(getOld));
			// store target = target +/- 1
			ibByteUnit incCode;
			StampSource(incCode);
			incCode.m_numOper = (incOp == '+') ? OPER_ADD : OPER_SUB;
			ibValue oneVal; oneVal.SetNumber(ibT("1"));
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
		variable = FindConst(ibValue(lex.m_valData));
	}
	else if ((lex.m_lexType == DELIMITER && lex.m_numData == '+') || (lex.m_lexType == DELIMITER && lex.m_numData == '-')) {

		// prefix ++ / -- in expression: `++x` / `--x` store x = x +/- 1, then
		// yield x (the new value). Caught BEFORE the unary-sign logic below —
		// else `++x` parses as `+(+x)` and silently no-ops. Require the two
		// signs ADJACENT in source (m_numString) so `a - -b` (spaced) stays a
		// binary minus + unary minus, not a phantom `a--`.
		if (m_numCurrentCompile + 1 < m_translateCode.GetLexCount()
			&& m_translateCode.GetLexAt(m_numCurrentCompile + 1).m_lexType == DELIMITER
			&& m_translateCode.GetLexAt(m_numCurrentCompile + 1).m_numData == lex.m_numData
			&& m_translateCode.GetLexAt(m_numCurrentCompile + 1).m_numString == m_translateCode.GetLexAt(m_numCurrentCompile).m_numString + 1) {
			const ibChar incOp = lex.m_numData;
			ExpectDelimiter(incOp); // consume the second +/-
			const ibString strRealName = ExpectIdentifier(true); // target — a bare variable
			ibParamUnit target = context->GetVariable(strRealName, true, true); // must already exist
			ibByteUnit incCode;
			StampSource(incCode);
			incCode.m_numOper = (incOp == '+') ? OPER_ADD : OPER_SUB;
			ibValue oneVal; oneVal.SetNumber(ibT("1"));
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
			if (variable.m_clsid != 0) {
				CheckTypeDef(variable, g_valueNumberCLSID);
			}
			variable.m_clsid = g_valueNumberCLSID;
			return variable;
		}
		else {
			variable = GetExpression(context, 100);//super high priority!
			ibByteUnit code;
			StampSource(code);
			code.m_numOper = OPER_INVERT;

			if (variable.m_clsid != 0) {
				CheckTypeDef(variable, g_valueNumberCLSID);
			}

			code.m_param2 = variable;
			variable = context->CreateVariable();
			variable.m_clsid = g_valueNumberCLSID;
			AddTypeSet(variable);
			code.m_param1 = variable;
			m_cByteCode.m_listCode.emplace_back(std::move(code));
		}
	}
	else {
		m_numCurrentCompile--;
		// …and the same here: the token that could not start an expression, named. An identifier
		// carries its own text; a delimiter is the character itself.
		Refuse(ibScriptError_Expression, lex.m_strData.IsEmpty()
			? ibString::Format(ibT("%c"), ibChar(lex.m_numData)) : lex.m_strData);
		return ibParamUnit();
	}

	// now we process Right Operators
	// so in variable we have the first index of the expression variable

delimOperation:

	const ibLexem& prevLexem = PreviewGetLexem();

	if (prevLexem.m_lexType == DELIMITER && prevLexem.m_numData == ')')
		return variable;

	// we look to see if there are any further operators for performing actions on this variable
	// THE WORD OPERATORS ARE NAMED HERE, and this list is the gate: a KEYWORD not
	// in it stops the expression, whatever gs_operPriority holds for its id. That
	// is also what makes the table's shared index space (delimiter codes AND
	// keyword ids in one 256-entry array) harmless for every other keyword.
	if ((prevLexem.m_lexType == DELIMITER && prevLexem.m_numData != ';')
	 || (prevLexem.m_lexType == KEYWORD && prevLexem.m_numData == KEY_AND)
	 || (prevLexem.m_lexType == KEYWORD && prevLexem.m_numData == KEY_OR)
	 || (prevLexem.m_lexType == KEYWORD && prevLexem.m_numData == KEY_MOD)
	 || (prevLexem.m_lexType == KEYWORD && prevLexem.m_numData == KEY_IN)) {
		if (prevLexem.m_numData >= 0 && prevLexem.m_numData <= 255) {
			const int numCurPriority = gs_operPriority[prevLexem.m_numData];
			if (nPriority < numCurPriority) { // �ompare the priorities of the left (previous operation) and the currently running operation

				ibByteUnit code;
				StampSource(code);
				const ibLexem& next_lex = GetLexem();

				// ⭐⭐ `x in (a, b, c)` — the one operator whose right side is a LIST, so it is read
				// here rather than through the shared "one right operand" road below. It expands into
				// comparisons joined by `or`: nothing new reaches the runtime, and every reader that
				// understands `=` and `or` understands this without being taught a third thing.
				if (next_lex.m_lexType == KEYWORD && next_lex.m_numData == KEY_IN) {

					ExpectDelimiter('(');

					ibParamUnit answer;
					bool any = false;
					while (!IsNextDelimiter(')')) {
						if (any)
							ExpectDelimiter(',');

						const ibParamUnit item = GetExpression(context, numCurPriority);

						ibParamUnit same = context->CreateVariable();
						same.m_clsid = g_valueBooleanCLSID;
						{
							ibByteUnit c; StampSource(c);
							c.m_numOper = OPER_EQ;
							c.m_param1  = same;
							c.m_param2  = variable;
							c.m_param3  = item;
							m_cByteCode.m_listCode.emplace_back(std::move(c));
						}

						if (!any) {
							answer = same;
							any = true;
							continue;
						}

						ibParamUnit either = context->CreateVariable();
						either.m_clsid = g_valueBooleanCLSID;
						{
							ibByteUnit c; StampSource(c);
							c.m_numOper = OPER_OR;
							c.m_param1  = either;
							c.m_param2  = answer;
							c.m_param3  = same;
							m_cByteCode.m_listCode.emplace_back(std::move(c));
						}
						answer = either;
					}
					ExpectDelimiter(')');

					// ⚠ AN EMPTY LIST IS A QUESTION WITH NO ANSWER, not `false` by default: `x in ()`
					// is far more likely a hand slipping than an intention, and a silent `false`
					// filters every row away without saying why.
					if (!any) {
						Refuse(ibScriptError_Expression, ibString(
							_("'in' needs something to look in - the list is empty")));
						return ibParamUnit();
					}

					variable = answer;
					goto delimOperation;
				}

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
				// The word form of the same operator, beside And / Or which are
				// already read here as keywords rather than delimiters.
				else if (next_lex.m_numData == KEY_MOD) {
					SetOper(OPER_MOD);
				}
				// ⭐⭐ `and` / `or` DECIDE BEFORE THE RIGHT SIDE RUNS. Both operands used to be
				// compiled, and run, and only then combined: `u <> Undefined and u.Property("x")`
				// raised on Undefined, which is the guard written everywhere. Same shape as
				// `?(cond, a, b)`: test the left, jump over the right when it cannot change the
				// answer. AND skips a false left; OR skips a true left.
				//
				// The join is still one OPER_AND / OPER_OR writing the result. A query reads that
				// instruction back as the logical operator. The cell it reads on the right is filled
				// with the answer a skipped side would have given (false for AND, true for OR) and
				// overwritten only by the path that actually evaluates the right operand — so the
				// last writer of that cell, which is what the query walk finds, is the real operand.
				else if (next_lex.m_numData == KEY_AND || next_lex.m_numData == KEY_OR) {
					const bool isOr = next_lex.m_numData == KEY_OR;

					ibParamUnit result = context->CreateVariable();
					result.m_clsid = g_valueBooleanCLSID;
					ibParamUnit rhsSlot = context->CreateVariable();

					const ibValue decisive(isOr);
					{
						ibByteUnit letDef;
						StampSource(letDef);
						letDef.m_numOper = OPER_LET;
						letDef.m_param1 = rhsSlot;
						letDef.m_param2 = FindConst(decisive);
						m_cByteCode.m_listCode.emplace_back(std::move(letDef));
					}

					code.m_numOper = OPER_IF;
					code.m_param1 = variable;
					CorrectTypeDef(variable);
					// A definite false skips the right side of `and`. SQL NULL does not: UNKNOWN
					// AND x is not FALSE, and `Not (Null = a And Null = b)` must stay UNKNOWN.
					// The mark is LINQ_THREE_VALUED_NULL (procUnitLambda.h); OPER_IF reads it
					// and falls through on NULL. `or` already falls through only on a true left,
					// and NULL is not true, so its test needs no mark.
					if (!isOr) {
						code.m_param4.m_numArray = DEF_VAR_SKIP;
						code.m_param4.m_numIndex = kAndUnknownMark;
					}
					m_cByteCode.m_listCode.emplace_back(std::move(code));
					const int ifLine = (int)m_cByteCode.m_listCode.size() - 1;

					int gotoLine = -1;
					if (isOr) {
						// A true left never enters the right side. A false left jumps to it.
						ibByteUnit jumpOver;
						StampSource(jumpOver);
						jumpOver.m_numOper = OPER_GOTO;
						m_cByteCode.m_listCode.emplace_back(std::move(jumpOver));
						gotoLine = (int)m_cByteCode.m_listCode.size() - 1;
						m_cByteCode.m_listCode[ifLine].m_param2.m_numIndex =
							(long)m_cByteCode.m_listCode.size();
					}

					const ibParamUnit right = GetExpression(context, numCurPriority);
					if (right.m_numArray != DEF_VAR_TEMP && right.m_numArray != DEF_VAR_CONST
						&& variable.m_clsid == g_valueStringCLSID) {
						Refuse(ibScriptError_TypeOperation);
						return ibParamUnit();
					}
					{
						ibByteUnit letRhs;
						StampSource(letRhs);
						letRhs.m_numOper = OPER_LET;
						letRhs.m_param1 = rhsSlot;
						letRhs.m_param2 = right;
						m_cByteCode.m_listCode.emplace_back(std::move(letRhs));
					}

					if (isOr)
						m_cByteCode.m_listCode[gotoLine].m_param1.m_numIndex =
							(long)m_cByteCode.m_listCode.size();
					else
						m_cByteCode.m_listCode[ifLine].m_param2.m_numIndex =
							(long)m_cByteCode.m_listCode.size();

					ibByteUnit join;
					StampSource(join);
					join.m_numOper = isOr ? OPER_OR : OPER_AND;
					join.m_param1 = result;
					join.m_param2 = variable;
					join.m_param3 = rhsSlot;
					m_cByteCode.m_listCode.emplace_back(std::move(join));

					variable = result;
					goto delimOperation;
				}
				else if (next_lex.m_numData == '>') {
					SetOper(OPER_GT);
					if (IsNextDelimiter('=')) {
						ExpectDelimiter('=');
						SetOper(OPER_GE);
					}
				}
				else if (next_lex.m_numData == '<') {
					SetOper(OPER_LS);
					if (IsNextDelimiter('=')) {
						ExpectDelimiter('=');
						SetOper(OPER_LE);
					}
					else if (IsNextDelimiter('>')) {
						ExpectDelimiter('>');
						SetOper(OPER_NE);
					}
				}
				else if (next_lex.m_numData == '=') {
					SetOper(OPER_EQ);

					// ⭐⭐ `==` IS THE ONE SLIP EVERY C-TRAINED HAND MAKES HERE, and this is the only
					// place that KNOWS it was made: the first `=` has just been read as the comparison,
					// so a second one against it can be nothing else. Left to fall through, the doubled
					// sign reaches the far end of the expression parser as "a token that cannot start
					// an expression" and is reported as a bare `=` — true, and no help at all: the
					// author is looking at the line they wrote and the message names a character that
					// is in it twice.
					//
					// Braces and semicolons say C to a reader, so the assumption arrives with them; the
					// language compares with a single `=`. Say WHICH spelling is meant, not merely that
					// something is wrong (measured over MCP, 2026-09-02 — it cost a full round of
					// bisecting a five-line block to find).
					if (IsNextDelimiter('=')) {
						Refuse(ibScriptError_Expression, ibString(
							_("'==' is not an operator here - comparison is written with a single '='")));
						return ibParamUnit();
					}
				}
				else {
					// ⭐ SAY WHAT WAS FOUND. The message is "Error in expression:\n%s" and both raises
					// of it passed nothing, so an author read a sentence that ends in a colon —
					// promising the detail and then withholding it. Here the detail is the whole
					// answer: `a == b` reaches this branch because comparison in this language is a
					// single `=`, and a caller who writes the other spelling gets a compile error
					// that names no operator, no token and nothing to change (measured 2026-09-02).
					Refuse(ibScriptError_Expression, ibChar(next_lex.m_numData));
					return ibParamUnit();
				}

				ibParamUnit puVariable1 = context->CreateVariable();
				ibParamUnit puVariable2 = variable;
				ibParamUnit puVariable3 = GetExpression(context, numCurPriority);

				if (puVariable3.m_numArray != DEF_VAR_TEMP && puVariable3.m_numArray != DEF_VAR_CONST) { // extra. checking for prohibited operations
					if (puVariable2.m_clsid == g_valueStringCLSID) {
						if (OPER_DIV == code.m_numOper
							|| OPER_MOD == code.m_numOper
							|| OPER_MULT == code.m_numOper
							|| OPER_AND == code.m_numOper
							|| OPER_OR == code.m_numOper) {
							Refuse(ibScriptError_TypeOperation);
							return ibParamUnit();
						}
					}
				}

				// ⭐ WHAT THIS OPERATOR ANSWERS, ASKED BEFORE THE TIER MOVES ITS NUMBER. CheckTypeDef
				// below adds TYPE_DELTA1..4 to the opcode when the left side has a declared type, and
				// the test for "is this a comparison" is a RANGE over the operator numbers - so asked
				// afterwards it answered no about every typed comparison, and the answer's type came
				// out as the operand's rather than Boolean. And / Or are asked here as well, for the
				// same reason and with the same consequence: they sit outside that range, so `if (a And
				// b)` over two typed Numbers read a number field nobody had written.
				//
				// What that cost: the If over it then took the operand's tier and read the operand's
				// FIELD - m_fData for a number, m_bData for a boolean - which is a different field, not
				// a different name for one (value.h: only the boolean, date, string and reference share
				// a union; a number sits outside it). It worked only while the comparison wrote that
				// same field and nothing else, which is exactly what the commit before this one had to
				// stop doing to make a comparison's answer readable as a value.
				const bool bAnswersBoolean = (code.m_numOper >= OPER_GT && code.m_numOper <= OPER_NE)
					|| code.m_numOper == OPER_AND || code.m_numOper == OPER_OR;

				if (puVariable2.m_numArray != DEF_VAR_CONST && puVariable2.m_numArray != DEF_VAR_TEMP) { // constants are not checked - because they are typified by default
					CheckTypeDef(puVariable3, puVariable2.m_clsid);
				}

				puVariable1.m_clsid = bAnswersBoolean ? g_valueBooleanCLSID : puVariable2.m_clsid;

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
		m_rootContext->m_parentContext = setParent->GetContext();
		AddDependency(&setParent->m_cByteCode);
	}

	m_translateCode.OnSetParent(setParent != nullptr ? &setParent->m_translateCode : nullptr);
}

#pragma warning(pop)
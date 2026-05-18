////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : calculate compile value
////////////////////////////////////////////////////////////////////////////

#include "codeEditorInterpreter.h"
#include "backend/metaData.h"
#include "backend/session/session.h"


#pragma warning(push)
#pragma warning(disable : 4018)

//array of mathematical operation priorities
static std::array<int, 256> gs_operPriority = { 0 };

// IntelliSense mirrors the backend compiler's syntax mode — process-
// global flag set on ibCompileCode. CES (`if (cond) { … }`) and VES
// (`If cond Then … EndIf`) compile to identical bytecode at the
// backend, so the editor walks the lexem stream with the matching
// closing-token expectations and brace handling.
static inline bool IsCES() {
	return ibCompileCode::GetCodeStyle() == CODE_CES;
}

ibPrecompileCode::ibPrecompileCode(ibValueMetaObjectModuleBase* moduleObject)
	: ibTranslateCode(moduleObject ? moduleObject->GetFullName() : wxString(),
	                  moduleObject ? moduleObject->GetDocPath()  : wxString()),
	  m_moduleObject(moduleObject)
{
	// Wire the root context's back-pointer to this module once at
	// construction. Used to live as a side effect inside GetContext()
	// (mutated state on every read); single setter call up front keeps
	// the same effect with const-correct getter.
	m_rootContext.SetModule(this);

	if (!gs_operPriority[gs_operPriority.size() - 1]) {

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

		gs_operPriority[gs_operPriority.size() - 1] = true;
	}

	// Sessionless / no-document hosts (codeRunner) construct the
	// precompiler without a backing module — names + initial Load
	// stay empty; OnTextChange will Load() the live editor text.
	if (m_moduleObject != nullptr) {
		m_strModuleName = m_moduleObject->GetFullName();
		m_strDocPath = m_moduleObject->GetDocPath();
		m_strFileName = m_moduleObject->GetFileName();
		Load(m_moduleObject->GetModuleText());
	}
}

ibPrecompileCode::~ibPrecompileCode() {}

void ibPrecompileCode::Clear()
{
	m_cursorContext = nullptr;
	if (m_defineList != nullptr) m_defineList->Clear();
#ifdef UTF8_LEXEM_TRANSLATE
	m_bufferSize = m_currentLine = m_currentPos = m_currentUtf8Pos = 0;
#else 
	m_bufferSize = m_currentLine = m_currentPos = 0;
#endif
	for (auto& function : m_rootContext.m_functions) wxDELETE(function.second);
	m_cursor = wxNOT_FOUND;
	m_rootContext.m_variables.clear();
	m_rootContext.m_functions.clear();

	m_valObject.Reset();
}

#include "backend/compiler/enumFactory.h"
#include "codeEditorParser.h"

void ibPrecompileCode::PrepareModuleData()
{
	ibRuntimeModuleDataObject* contextVariable = nullptr;

	// Sessionless / no-metadata hosts (codeRunner) skip the cache
	// lookup entirely — autocomplete just falls back to local
	// declarations parsed out of the live editor text.
	if (m_moduleObject) {
		ibMetaData* metaData = m_moduleObject->GetMetaData();
		wxASSERT(metaData);
		ibSession* session = ibSession::Current();
		ibValueModuleManager* moduleManager = session ? session->GetManagerModule() : nullptr;
		auto* cc = metaData ? metaData->GetCompileCache() : nullptr;
		if (cc == nullptr || moduleManager == nullptr ||
			!cc->FindCompileModule(m_moduleObject, contextVariable)) {
			return;
		}
		for (auto pair : moduleManager->GetContextVariables()) {

			ibValue* managerVariable = pair.second;
			managerVariable->PrepareNames();
			for (unsigned int i = 0; i < managerVariable->GetNProps(); i++) {
				const wxString& strAttributeName = managerVariable->GetPropName(i);
				//determine the m_number and type of the variable
				ibPrecompileVariable variables;
				variables.m_name = strAttributeName;
				variables.m_realName = strAttributeName;

				variables.m_number = i;
				variables.m_isContext = true;
				variables.m_isExport = true;

				variables.m_valContext = managerVariable;

				GetContext()->m_variables[stringUtils::MakeUpper(strAttributeName)] = variables;
			}

			for (unsigned int i = 0; i < managerVariable->GetNMethods(); i++) {
				const wxString& strMethodName = managerVariable->GetMethodName(i);

				ibPrecompileContext* compileContext = new ibPrecompileContext();
				compileContext->m_returnKind = managerVariable->HasRetVal(i) ? RETURN_FUNCTION : RETURN_PROCEDURE;
				compileContext->m_module = this;

				ibPrecompileFunction* pFunction = new ibPrecompileFunction(strMethodName, compileContext);
				pFunction->m_realName = strMethodName;
				pFunction->m_shortDescription = managerVariable->GetMethodHelper(i);
				pFunction->m_isContext = true;
				pFunction->m_isExport = true;

				pFunction->m_valContext = managerVariable;

				// check for typing
				GetContext()->m_functions[stringUtils::MakeUpper(strMethodName)] = pFunction;
			}
		}
		unsigned int nNumberAttr = 0;
		unsigned int nNumberFunc = 0;
		for (auto module : moduleManager->GetCommonModules()) {
			if (module->IsGlobalModule()) {
				ibParserModule cParser;
				if (cParser.ParseModule(module->GetModuleText())) {
					for (auto code : cParser.GetAllContent()) {
						if (code.m_eType == eExportVariable) {
							wxString strAttributeName = code.m_name;
							if (m_rootContext.FindVariable(strAttributeName))
								continue;
							//determine the m_number and type of the variable
							ibPrecompileVariable variables;
							variables.m_name = strAttributeName;
							variables.m_realName = strAttributeName;

							variables.m_number = nNumberAttr;
							variables.m_isContext = true;
							variables.m_isExport = true;

							variables.m_valContext = module;

							GetContext()->m_variables[stringUtils::MakeUpper(strAttributeName)] = variables;	nNumberAttr++;
						}
						else if (code.m_eType == eExportProcedure) {
							wxString strMethodName = code.m_name;
							if (m_rootContext.FindFunction(strMethodName))
								continue;
							ibPrecompileContext* procContext = new ibPrecompileContext(GetContext());
							procContext->SetModule(this);
							procContext->m_returnKind = RETURN_PROCEDURE;

							ibPrecompileFunction* pFunction = new ibPrecompileFunction(strMethodName, procContext);
							pFunction->m_realName = strMethodName;
							pFunction->m_shortDescription = code.m_shortDescription;
							pFunction->m_isContext = true;
							pFunction->m_isExport = true;

							pFunction->m_valContext = module;

							// check for typing
							GetContext()->m_functions[stringUtils::MakeUpper(strMethodName)] = pFunction; nNumberFunc++;
						}
						else if (code.m_eType == eExportFunction) {
							wxString strMethodName = code.m_name;
							if (m_rootContext.FindFunction(strMethodName)) continue;

							ibPrecompileContext* procContext = new ibPrecompileContext(GetContext());
							procContext->SetModule(this);
							procContext->m_returnKind = RETURN_FUNCTION;

							ibPrecompileFunction* pFunction = new ibPrecompileFunction(strMethodName, procContext);
							pFunction->m_realName = strMethodName;
							pFunction->m_shortDescription = code.m_shortDescription;
							pFunction->m_isContext = true;
							pFunction->m_isExport = true;

							pFunction->m_valContext = module;

							// check for typing
							GetContext()->m_functions[stringUtils::MakeUpper(strMethodName)] = pFunction; nNumberFunc++;
						}
					}
				}
			}
		}
	}

	if (contextVariable != nullptr) {
		ibValue* pRefData = nullptr;
		ibCompileModule* compileModule = contextVariable->GetCompileModule();
		while (compileModule != nullptr) {

			// Surface compile-side context bindings (ThisForm, ThisObject, …)
			// — bound via BindContextVariable into the compile module's
			// m_listContextValue. They aren't enumerated as props of any
			// cached value (the self-named props are IsPropScoped-skipped),
			// so without this loop the runtime compiler resolves them but
			// IntelliSense doesn't see them.
			for (auto& kv : compileModule->m_listContextValue) {
				if (m_rootContext.FindVariable(kv.first))
					continue;
				ibPrecompileVariable variable;
				variable.m_name = kv.first;
				variable.m_realName = kv.first;
				variable.m_isContext = true;
				variable.m_isExport = true;
				variable.m_valContext = kv.second;
				GetContext()->m_variables[stringUtils::MakeUpper(kv.first)] = variable;
			}

			const ibValueMetaObjectModuleBase* moduleObject = compileModule->GetObjectModule();
			if (moduleObject != nullptr) {
				ibMetaData* metaData = moduleObject->GetMetaData();
				wxASSERT(metaData);
				auto* cc = metaData->GetCompileCache();
				if (cc && cc->FindCompileModule(moduleObject, pRefData)) {
					//adding variables from context
					for (long i = 0; i < pRefData->GetNProps(); i++) {
						// Scope-local props (ThisObject / ThisForm /
						// RegisterRecords) are bc-internal — autocomplete
						// must not surface them when walking another bc's
						// context value.
						if (pRefData->IsPropScoped(i))
							continue;
						wxString strAttributeName = pRefData->GetPropName(i);
						if (m_rootContext.FindVariable(strAttributeName))
							continue;

						//determine the m_number and type of the variable
						ibPrecompileVariable variables;
						variables.m_name = strAttributeName;
						variables.m_realName = strAttributeName;

						variables.m_number = i;
						variables.m_isContext = true;
						variables.m_isExport = true;

						variables.m_valContext = pRefData;

						GetContext()->m_variables[stringUtils::MakeUpper(strAttributeName)] = variables;
					}

					// add methods from context
					for (long i = 0; i < pRefData->GetNMethods(); i++) {
						wxString strMethodName = pRefData->GetMethodName(i);
						if (m_rootContext.FindFunction(strMethodName))
							continue;

						ibPrecompileContext* procContext = new ibPrecompileContext(GetContext());
						procContext->SetModule(this);

						if (pRefData->HasRetVal(i))
							procContext->m_returnKind = RETURN_FUNCTION;
						else
							procContext->m_returnKind = RETURN_PROCEDURE;

						ibPrecompileFunction* pFunction = new ibPrecompileFunction(strMethodName, procContext);
						pFunction->m_realName = strMethodName;
						pFunction->m_shortDescription = pRefData->GetMethodHelper(i);
						pFunction->m_isContext = true;
						pFunction->m_isExport = true;

						pFunction->m_valContext = pRefData;

						// check for typing
						GetContext()->m_functions[stringUtils::MakeUpper(strMethodName)] = pFunction;
					}

					if (moduleObject != nullptr) {
						ibParserModule cParser;
						if (cParser.ParseModule(moduleObject->GetModuleText())) {
							unsigned int nNumberAttr = pRefData->GetNProps() + 1;
							unsigned int nNumberFunc = pRefData->GetNMethods() + 1;
							for (auto code : cParser.GetAllContent()) {
								if (code.m_eType == eExportVariable) {
									const wxString& strAttributeName = code.m_name;
									if (m_rootContext.FindVariable(strAttributeName))
										continue;
									//determine the m_number and type of the variable
									ibPrecompileVariable cVariable;
									cVariable.m_name = strAttributeName;
									cVariable.m_realName = strAttributeName;

									cVariable.m_number = nNumberAttr;
									cVariable.m_isContext = true;
									cVariable.m_isExport = true;

									cVariable.m_valContext = pRefData;

									GetContext()->m_variables[stringUtils::MakeUpper(strAttributeName)] = cVariable;	nNumberAttr++;
								}
								else if (code.m_eType == eExportProcedure) {
									const wxString& strMethodName = code.m_name;
									if (m_rootContext.FindFunction(strMethodName))
										continue;

									ibPrecompileContext* procContext = new ibPrecompileContext(GetContext());
									procContext->SetModule(this);
									procContext->m_returnKind = RETURN_PROCEDURE;

									ibPrecompileFunction* pFunction = new ibPrecompileFunction(strMethodName, procContext);
									pFunction->m_realName = strMethodName;
									pFunction->m_shortDescription = code.m_shortDescription;
									pFunction->m_isContext = true;
									pFunction->m_isExport = true;

									pFunction->m_valContext = pRefData;

									// check for typing
									GetContext()->m_functions[stringUtils::MakeUpper(strMethodName)] = pFunction; nNumberFunc++;
								}
								else if (code.m_eType == eExportFunction) {
									const wxString& strMethodName = code.m_name;
									if (m_rootContext.FindFunction(strMethodName))
										continue;

									ibPrecompileContext* procContext = new ibPrecompileContext(GetContext());
									procContext->SetModule(this);
									procContext->m_returnKind = RETURN_FUNCTION;

									ibPrecompileFunction* pFunction = new ibPrecompileFunction(strMethodName, procContext);
									pFunction->m_realName = strMethodName;
									pFunction->m_shortDescription = code.m_shortDescription;
									pFunction->m_isContext = true;
									pFunction->m_isExport = true;

									pFunction->m_valContext = pRefData;

									// check for typing
									GetContext()->m_functions[stringUtils::MakeUpper(strMethodName)] = pFunction; nNumberFunc++;
								}
							}
						}
					}
				}
			}
			compileModule = compileModule->GetParent();
		}
	}
}

bool ibPrecompileCode::PrepareLexem()
{
	wxString s;
	m_listLexem.clear();

	// Line on which a preprocessor directive (#define / #undef / #ifdef /
	// #ifndef / #region / etc.) was last seen. Every token tokenised on
	// the same line is dropped — the directive's parameter (an ident
	// after #define, the symbol after #ifdef, the region name) would
	// otherwise leak into the lexem stream and the parser would try to
	// interpret it as an expression. -1 = no directive in flight.
	int directiveLine = -1;

	while (!IsEnd()) {

		// m_translateCode bound at ctor time, kept through moves.

		m_current_lex.m_numLine = m_currentLine;
		m_current_lex.m_numString = m_currentPos;
#ifdef UTF8_LEXEM_TRANSLATE
		m_current_lex.m_numUtf8String = m_currentUtf8Pos;
#endif // UTF8_LEXEM_TRANSLATE	

		if (IsWord()) {

			wxString strOrig;

			if (GetWord(s, strOrig)) {

				const int k = IsKeyWord(s);

				//undefined
				if (k == KEY_UNDEFINED) {
					m_current_lex.m_lexType = CONSTANT;
					m_current_lex.m_valData.SetType(ibValueTypes::TYPE_EMPTY);
				}
				//boolean
				else if (k == KEY_TRUE || k == KEY_FALSE) {
					m_current_lex.m_lexType = CONSTANT;
					m_current_lex.m_valData.SetBoolean(s);
				}
				//null
				else if (k == KEY_NULL) {
					m_current_lex.m_lexType = CONSTANT;
					m_current_lex.m_valData.SetType(ibValueTypes::TYPE_NULL);
				}
				else {

					if (k >= 0) {
						m_current_lex.m_lexType = KEYWORD;
						m_current_lex.m_numData = k;
					}
					else {
						m_current_lex.m_lexType = IDENTIFIER;
					}

					m_current_lex.m_valData = strOrig;
				}
			}
		}
		else if (IsNumber() || IsString() || IsDate()) {
			m_current_lex.m_lexType = CONSTANT;
			if (IsNumber()) {
				GetNumber(s);
				m_current_lex.m_valData.SetNumber(s);
				int n = m_listLexem.size() - 1;
				if (n >= 0) {
					if (m_listLexem[n].m_lexType == DELIMITER && (m_listLexem[n].m_numData == '-' || m_listLexem[n].m_numData == '+')) {
						n--;
						if (n >= 0) {
							if (m_listLexem[n].m_lexType == DELIMITER && (m_listLexem[n].m_numData == '[' || m_listLexem[n].m_numData == '(' || m_listLexem[n].m_numData == ',' || m_listLexem[n].m_numData == '<' || m_listLexem[n].m_numData == '>' || m_listLexem[n].m_numData == '='))
							{
								n++;
								if (m_listLexem[n].m_numData == '-')
									m_current_lex.m_valData.m_fData = -m_current_lex.m_valData.m_fData;
								m_listLexem[n] = m_current_lex;
								continue;
							}
						}
					}
				}
			}
			else {
				if (IsString()) {
					GetString(s);
					m_current_lex.m_valData.SetString(s);
				}
				else if (IsDate()) {
					GetDate(s);
					m_current_lex.m_valData.SetDate(s);
				}
			}

			// Directive-line guard — same rationale as the bottom-of-loop
			// check: a string / number / date literal on a `#define X = 5`
			// line must not enter the lexem stream as a free token.
			if (directiveLine >= 0 && (int)m_current_lex.m_numLine == directiveLine)
				continue;
			m_listLexem.emplace_back(std::move(m_current_lex));
			continue;
		}
		else if (IsByte('~')) {
			s.clear();

			GetByte();
			continue;
		}
		else {

			s.clear();

			m_current_lex.m_lexType = DELIMITER;
			m_current_lex.m_lexType = DELIMITER;
			wxUniChar byte; GetByte(byte);
			m_current_lex.m_numData = byte;

			if (m_current_lex.m_numData <= 13) continue;
		}
		m_current_lex.m_strData = s;
		if (m_current_lex.m_lexType == KEYWORD)
		{
			if (m_current_lex.m_numData == KEY_DEFINE
				|| m_current_lex.m_numData == KEY_UNDEF
				|| m_current_lex.m_numData == KEY_IFDEF
				|| m_current_lex.m_numData == KEY_IFNDEF
				|| m_current_lex.m_numData == KEY_ENDIFDEF
				|| m_current_lex.m_numData == KEY_ELSEDEF
				|| m_current_lex.m_numData == KEY_REGION
				|| m_current_lex.m_numData == KEY_ENDREGION) {
				directiveLine = m_current_lex.m_numLine;
				continue;
			}
		}
		// Drop everything on the directive's line — the parameter token
		// (`#define <NAME>`, `#region <Name>`, …) would otherwise enter
		// the parser as a stray identifier.
		if (directiveLine >= 0 && m_current_lex.m_numLine == directiveLine)
			continue;
		m_listLexem.emplace_back(std::move(m_current_lex));
	}

	m_current_lex.m_lexType = ENDPROGRAM;
	m_current_lex.m_numData = 0;
	m_current_lex.m_numString = m_currentPos;
#ifdef UTF8_LEXEM_TRANSLATE
	m_current_lex.m_numUtf8String = m_currentUtf8Pos;
#endif // UTF8_LEXEM_TRANSLATE	

	m_listLexem.emplace_back(std::move(m_current_lex));
	return true;
}

#ifdef UTF8_LEXEM_TRANSLATE
void ibPrecompileCode::PrepareLexem(unsigned int line, int line_offset, const int& pos_offset, const int& pos_offset_utf8)
#else 
void ibPrecompileCode::PrepareLexem(unsigned int line, int line_offset, const int& pos_offset)
#endif
{
	// Defensive: a fresh ibPrecompileCode without a prior full PrepareLexem
	// would have an empty m_listLexem; the index math below assumes at
	// least the trailing ENDPROGRAM marker is present.
	if (m_listLexem.empty())
		return;

	m_currentLine = m_currentPos = 0;

	// Rewind the tokenizer to one lexem BEFORE the first lexem at or
	// past `line` (or before the ENDPROGRAM marker if the edit is past
	// the last real lexem). Re-tokenization restarts at that lexem's
	// start position — the erase pass below drops it together with the
	// edited region, and the tokenizer then re-emits it as the first
	// new lexem. This minimises the rewind to a single lexem of replay.
	unsigned int lexem_idx = 0;
	bool insert_after = false;
	auto hint = m_listLexem.begin();

	for (size_t i = 0; i < m_listLexem.size(); ++i) {

		const bool atTriggerLine = m_listLexem[i].m_numLine >= line;
		const bool atEndProgram  = m_listLexem[i].m_lexType == ENDPROGRAM;

		if (!atTriggerLine && !atEndProgram)
			continue;

		if (i > 0) {
			m_currentLine = m_listLexem[i - 1].m_numLine;
			m_currentPos  = m_listLexem[i - 1].m_numString;
#ifdef UTF8_LEXEM_TRANSLATE
			m_currentUtf8Pos = m_listLexem[i - 1].m_numUtf8String;
#endif
			lexem_idx = (unsigned int)(i - 1);
			if (lexem_idx > 0) std::advance(hint, lexem_idx - 1);
			insert_after = atEndProgram ? true : (lexem_idx > 0);
		}
		break;
	}

	wxString s;

	const bool insert_text = pos_offset > 0;
	const bool delete_text = pos_offset < 0;

	m_listLexem.erase(
		std::remove_if(m_listLexem.begin() + lexem_idx, m_listLexem.end() - 1,
			[&](const auto& e) {
				if (insert_text) return e.m_numLine <= line;
				if (delete_text) return e.m_numLine <= (line - line_offset);
				return false;
			}),
		m_listLexem.end() - 1
	);

	if (m_listLexem.size() <= 1) {
		hint = m_listLexem.begin();
		insert_after = false;
	}

	// Mirror the directive-line skip from the full PrepareLexem path
	// so incremental re-tokenisation drops `#define <NAME>` etc. as a
	// whole-line unit too. -1 = no directive in flight.
	int directiveLine = -1;

	while (!IsEnd()) {

		if (insert_text && m_currentLine > (line + line_offset)) break;
		else if (delete_text && (m_currentLine > line)) break;

		// m_translateCode bound at ctor time, kept through moves.

		m_current_lex.m_numLine = m_currentLine;
		m_current_lex.m_numString = m_currentPos;
#ifdef UTF8_LEXEM_TRANSLATE
		m_current_lex.m_numUtf8String = m_currentUtf8Pos;
#endif // UTF8_LEXEM_TRANSLATE	

		if (IsWord()) {

			wxString strOrig;

			if (GetWord(s, strOrig)) {

				const int k = IsKeyWord(s);

				//undefined
				if (k == KEY_UNDEFINED) {
					m_current_lex.m_lexType = CONSTANT;
					m_current_lex.m_valData.SetType(ibValueTypes::TYPE_EMPTY);
				}
				//boolean
				else if (k == KEY_TRUE || k == KEY_FALSE) {
					m_current_lex.m_lexType = CONSTANT;
					m_current_lex.m_valData.SetBoolean(s);
				}
				//null
				else if (k == KEY_NULL) {
					m_current_lex.m_lexType = CONSTANT;
					m_current_lex.m_valData.SetType(ibValueTypes::TYPE_NULL);
				}
				else {

					if (k >= 0) {
						m_current_lex.m_lexType = KEYWORD;
						m_current_lex.m_numData = k;
					}
					else {
						m_current_lex.m_lexType = IDENTIFIER;
					}

					m_current_lex.m_valData = strOrig;
				}
			}

			m_current_lex.m_strData = s;
		}
		else if (IsNumber() || IsString() || IsDate()) {
			m_current_lex.m_lexType = CONSTANT;
			if (IsNumber()) {

				GetNumber(s); m_current_lex.m_valData.SetNumber(s);

				if (hint != m_listLexem.begin() && hint->m_lexType == DELIMITER && (hint->m_numData == '-' || hint->m_numData == '+')) {
					auto prev = std::prev(hint, 1);
					if (prev != m_listLexem.begin() && prev->m_lexType == DELIMITER && (prev->m_numData == '[' || prev->m_numData == '(' || prev->m_numData == ',' || prev->m_numData == '<' || prev->m_numData == '>' || prev->m_numData == '=')) {
						if (hint->m_numData == '-')
							m_current_lex.m_valData.m_fData = -m_current_lex.m_valData.m_fData;
						*hint = std::move(m_current_lex);
						continue;
					}
				}
			}
			else {
				if (IsString()) {
					GetString(s); m_current_lex.m_valData.SetString(s);
				}
				else if (IsDate()) {
					GetDate(s); m_current_lex.m_valData.SetDate(s);
				}
			}

			// Directive-line guard for the incremental tokenizer's
			// CONSTANT path — see the full PrepareLexem version.
			if (directiveLine >= 0 && (int)m_current_lex.m_numLine == directiveLine) {
				continue;
			}
			if (insert_after) {
				hint = m_listLexem.emplace(
					std::next(hint, 1), std::move(m_current_lex));
			}
			else {
				hint = m_listLexem.emplace(
					hint, std::move(m_current_lex));
				insert_after = true;
			}

			continue;
		}
		else if (IsByte('~')) {
			s.clear();
			GetByte();
			continue;
		}
		else {

			s.clear();

			m_current_lex.m_lexType = DELIMITER;
			wxUniChar byte; GetByte(byte);
			m_current_lex.m_numData = byte;

			if (m_current_lex.m_numData <= 13) continue;
		}
		m_current_lex.m_strData = s;
		if (m_current_lex.m_lexType == KEYWORD) {
			if (
				m_current_lex.m_numData == KEY_DEFINE
				|| m_current_lex.m_numData == KEY_UNDEF
				|| (m_current_lex.m_numData == KEY_IFDEF || m_current_lex.m_numData == KEY_IFNDEF)
				|| m_current_lex.m_numData == KEY_ENDIFDEF
				|| m_current_lex.m_numData == KEY_ELSEDEF
				|| m_current_lex.m_numData == KEY_REGION
				|| m_current_lex.m_numData == KEY_ENDREGION
				)
			{
				directiveLine = m_current_lex.m_numLine;
				continue;
			}
		}
		// Drop tokens on the directive's line — see full-PrepareLexem
		// counterpart for rationale.
		if (directiveLine >= 0 && (int)m_current_lex.m_numLine == directiveLine)
			continue;

		if (insert_after) {
			hint = m_listLexem.emplace(
				std::next(hint, 1), std::move(m_current_lex));
		}
		else {
			hint = m_listLexem.emplace(
				hint, std::move(m_current_lex));
			insert_after = true;
		}
	}

	const size_t lex_size = m_listLexem.size() - 1;

	if (lex_size > 0) {

		const size_t lex_distance = std::distance(m_listLexem.begin(), insert_after ? hint + 1 : hint);

		for (unsigned int i = lex_distance; i < lex_size; i++) {
			m_listLexem[i].m_numLine += line_offset;
			m_listLexem[i].m_numString += pos_offset;
#ifdef UTF8_LEXEM_TRANSLATE
			m_listLexem[i].m_numUtf8String += pos_offset_utf8;
#endif // UTF8_LEXEM_TRANSLATE
		}
	}

	m_listLexem[lex_size].m_numString += pos_offset;
#ifdef UTF8_LEXEM_TRANSLATE
	m_listLexem[lex_size].m_numUtf8String += pos_offset_utf8;
#endif

}

bool ibPrecompileCode::Compile()
{
	Clear();

	// Sessionless / no-module hosts (codeRunner) skip the moduleManager
	// global-var seeding — there's no metadata configuration to pull
	// Catalogs / Documents / EnumManager from. Local declarations parsed
	// from the live editor text still feed autocomplete via the lexem
	// pass below.
	if (m_moduleObject != nullptr) {
		ibMetaData* metaData = m_moduleObject->GetMetaData();
		wxASSERT(metaData);
		ibSession* session = ibSession::Current();
		ibValueModuleManager* moduleManager = session ? session->GetManagerModule() : nullptr;
		if (moduleManager != nullptr) {
			for (auto variable : moduleManager->GetGlobalVariables()) {
				AddVariable(variable.first, variable.second);
			}
		}
	}

	PrepareModuleData();

	return CompileModule();
}

bool ibPrecompileCode::CompileModule()
{
	m_activeContext = GetContext();// context of the module itself

	ibLexem lex;

	while ((lex = PreviewGetLexem()).m_lexType != ERRORTYPE)
	{
		if ((KEYWORD == lex.m_lexType && KEY_VAR == lex.m_numData) || (IDENTIFIER == lex.m_lexType && IsTypeVar(lex.m_strData)))
		{
			CompileDeclaration();// load variable declaration
		}
		else if (KEYWORD == lex.m_lexType && (KEY_PROCEDURE == lex.m_numData || KEY_FUNCTION == lex.m_numData))
		{
			CompileFunction();// load function declaration
		}
		else
		{
			break;
		}
	}

	int nStartContext = m_cursor >= 0 ? m_listLexem[m_cursor].m_numString : 0;

	// load the executable body of the module
	m_activeContext = GetContext();// context of the module itself

	CompileBlock(/*allowSingleStmt=*/false);

	if (m_cursor + 1 < m_listLexem.size() - 1)
		return false;

	if (m_caretPos >= nStartContext && m_caretPos <= m_listLexem[m_cursor].m_numString)
		m_cursorContext = m_activeContext;

	return true;
}

bool ibPrecompileCode::CompileFunction()
{
	// we are now at the token level, where the FUNCTION or PROCEDURE keyword is specified.
	// Track opening keyword + lambda-ness for downstream decisions (synthetic-name
	// generation, m_returnKind tag, closing-keyword expectation).
	ibLexem lex;
	bool isFunctionKeyword = false;
	bool isLambda = false;

	if (IsNextKeyWord(KEY_FUNCTION))
	{
		ExpectKeyword(KEY_FUNCTION);
		isFunctionKeyword = true;
		isLambda = IsNextDelimeter('(');

		m_activeContext = new ibPrecompileContext(GetContext());
		m_activeContext->SetModule(this);
		// Enum value encodes both axes (anonymous-vs-named and
		// function-vs-procedure) — symmetric with the backend
		// compiler's ibCompileContext::m_numReturn axis.
		m_activeContext->m_returnKind = isLambda ? RETURN_LAMBDA_FUNCTION : RETURN_FUNCTION;
	}
	else if (IsNextKeyWord(KEY_PROCEDURE))
	{
		ExpectKeyword(KEY_PROCEDURE);
		isFunctionKeyword = false;
		isLambda = IsNextDelimeter('(');

		m_activeContext = new ibPrecompileContext(GetContext());
		m_activeContext->SetModule(this);
		m_activeContext->m_returnKind = isLambda ? RETURN_LAMBDA_PROCEDURE : RETURN_PROCEDURE;
	}
	else
	{
		/*SetError(ERROR_FUNC_DEFINE);*/
		return false;
	}

	// pull out the text of the function declaration
	lex = PreviewGetLexem();
	wxString shortDescription;
	int      numLine = lex.m_numLine;
	int nRes = m_strBuffer.find('\n', lex.m_numString);
	if (nRes >= 0)
	{
		shortDescription = m_strBuffer.substr(lex.m_numString, nRes - lex.m_numString - 1);
		nRes = shortDescription.find_first_of('/');
		if (nRes > 0)
		{
			if (shortDescription[nRes - 1] == '/')// so this is a comment
			{
				shortDescription = shortDescription.substr(nRes + 1);
			}
		}
		else
		{
			nRes = shortDescription.find_first_of(')');
			shortDescription = shortDescription.substr(0, nRes + 1);
		}
	}

	// get the function name. Anonymous lambda — no identifier follows
	// the keyword (`(` comes directly), so synthesise a label tied to
	// the source line. The synthetic name is stable per-line within a
	// compilation pass and unique enough for the m_functions map key
	// (anonymous bodies are never looked up by name from script anyway).
	wxString csFuncName0;
	if (isLambda) {
		csFuncName0 = wxString::Format(wxT("<lambda@%d>"), numLine);
	} else {
		csFuncName0 = ExpectIdentifier(true);
	}
	wxString funcName = stringUtils::MakeUpper(csFuncName0);
	int nError = m_cursor;

	ibPrecompileFunction* pFunction = new ibPrecompileFunction(funcName, m_activeContext);

	pFunction->m_realName = csFuncName0;
	pFunction->m_shortDescription = shortDescription;
	// compile the list of formal parameters + register them as local
	ExpectDelimeter('(');
	while (m_cursor + 1 < m_listLexem.size()
		&& !IsNextDelimeter(')'))
	{
		while (m_cursor + 1 < m_listLexem.size())
		{
			wxString type;
			// check for typing
			if (IsTypeVar())
			{
				type = GetTypeVar();
			}

			ibParamValue variable;

			if (IsNextKeyWord(KEY_VAL))
			{
				ExpectKeyword(KEY_VAL);
			}

			wxString realName = ExpectIdentifier(true);
			variable.m_paramName = stringUtils::MakeUpper(realName);

			// register this variable as local
			if (m_activeContext->FindVariable(variable.m_paramName)) return false;

			if (IsNextDelimeter('[')) { // this is an array
				ExpectDelimeter('[');
				ExpectDelimeter(']');
			}
			else if (IsNextDelimeter('=')) {
				ExpectDelimeter('=');
				ExpectConstant();
			}

			ibValue valObject;

			if (!type.IsEmpty()) {
				try {
					valObject = ibValue::CreateObject(type);
				}
				catch (...)
				{
				}
			}

			m_activeContext->AddVariable(realName, type, false, false, valObject);
			variable.m_paramType = type;

			pFunction->m_params.push_back(variable);

			if (IsNextDelimeter(')'))
				break;

			ExpectDelimeter(',');
		}
	}

	ExpectDelimeter(')');

	if (IsNextKeyWord(KEY_EXPORT)) {
		ExpectKeyword(KEY_EXPORT);
		pFunction->m_isExport = true;
	}

	// check for typing
	GetContext()->m_functions[funcName] = pFunction;

	int nStartContext = m_listLexem[m_cursor].m_numString;

	GetContext()->m_currentFunctionName = funcName;
	// Function/procedure body is multi-statement (terminator = EndFunction /
	// EndProcedure in VES, matching `}` in CES). The default
	// allowSingleStmt=true would make the body exit after the first parsed
	// statement and leave the terminator unconsumed — cascade-fail when an
	// inline lambda's `EndFunction` is what appears next.
	CompileBlock(/*allowSingleStmt=*/false);
	GetContext()->m_currentFunctionName = wxEmptyString;

	// VES — closing keyword matches the OPENING keyword (Function →
	// EndFunction, Procedure → EndProcedure). isFunctionKeyword captured
	// at signature-parse drives the decision uniformly for named and
	// anonymous bodies. CES uses brace-fenced bodies; the trailing `}`
	// is consumed by the body's CompileBlock.
	if (!IsCES()) {
		if (isFunctionKeyword) ExpectKeyword(KEY_ENDFUNCTION);
		else ExpectKeyword(KEY_ENDPROCEDURE);
	}

	if (m_caretPos >= nStartContext && m_caretPos <= m_listLexem[m_cursor].m_numString) m_cursorContext = m_activeContext;
	return true;
}

bool ibPrecompileCode::CompileDeclaration()
{
	wxString type;
	const ibLexem& lex = PreviewGetLexem();

	if (IDENTIFIER == lex.m_lexType) type = GetTypeVar(); // typed setting of variables
	else ExpectKeyword(KEY_VAR);

	while (m_cursor + 1 < m_listLexem.size())
	{
		wxString name = ExpectIdentifier(true);

		int nArrayCount = wxNOT_FOUND;
		if (IsNextDelimeter('['))// this is an array declaration
		{
			nArrayCount = 0;
			ExpectDelimeter('[');
			if (!IsNextDelimeter(']')) {
				ibValue vConst = ExpectConstant();
				if (vConst.GetType() != ibValueTypes::TYPE_NUMBER || vConst.GetNumber() < 0)
					return false;
				nArrayCount = vConst.GetInteger();
			}
			ExpectDelimeter(']');
		}

		bool isExport = false;
		if (IsNextKeyWord(KEY_EXPORT)) {
			ExpectKeyword(KEY_EXPORT);
			isExport = true;
		}

		ibParamValue inferred;
		bool         hasInit = false;

		if (IsNextDelimeter('='))// initial initialization - works only inside the text of modules (but not re-declaring procedures and functions)
		{
			if (nArrayCount >= 0) ExpectDelimeter(',');//Error!
			ExpectDelimeter('=');
			// Consume the initialiser expression. Without this, CES code
			// like `var x = Catalogs.Items;` left the chain expression in
			// the lexem stream, the outer CompileBlock then read it as a
			// fresh statement and failed at the `.` (no leading identifier
			// state was preserved). Walking through GetExpression also
			// captures the inferred type so subsequent `x.` autocomplete
			// resolves the chain.
			inferred = GetExpression();
			hasInit  = true;
		}

		// Variable registration goes AFTER the initialiser parse so the
		// inferred type carries through to AddVariable. Original-cased
		// `name` lands in the context; case-insensitive lookup handles
		// the rest. declPos is captured at the start of the declaration
		// so autocomplete only surfaces this var when caret is past it.
		const wxString effectiveType = type.IsEmpty() && hasInit
			? inferred.m_paramType : type;
		const int declPos = (m_cursor >= 0 && m_cursor < (int)m_listLexem.size())
			? m_listLexem[m_cursor].m_numString : 0;
		m_activeContext->AddVariable(name, effectiveType, isExport, /*context=*/false,
			hasInit ? inferred.m_paramObject : ibValue(), declPos);

		if (!IsNextDelimeter(','))
			break;

		ExpectDelimeter(',');
	}

	return true;
}

bool ibPrecompileCode::CompileBlock(bool allowSingleStmt)
{
	ibLexem lex;

	// CES — consume the opener `{` if present. Function bodies and the
	// per-control-structure call-sites both invoke CompileBlock with `{`
	// as the next token (mirroring ibCompileCode::CompileBlock's entry
	// gate). Set bCompileBlock so the matching `}` is consumed at exit.
	//
	// CES also supports the brace-less single-statement body
	// (`if (cond) stmt;` / `while (cond) stmt;`), where the body is
	// exactly one statement and the surrounding control structure
	// resumes parsing immediately after. singleStmtMode below mirrors
	// the backend compiler's `RETURN_BLOCK` exit condition; IntelliSense
	// lacks that context kind, so we track it locally via parsedOne.
	// Module-level body callsite passes allowSingleStmt=false — no `{`
	// is expected there but the body is multi-statement.
	bool bCompileBlock  = false;
	bool singleStmtMode = false;
	bool parsedOne      = false;
	if (IsCES()) {
		if (IsNextDelimeter('{')) {
			ExpectDelimeter('{');
			bCompileBlock = true;
		}
		else if (allowSingleStmt) {
			singleStmtMode = true;
		}
	}

	while ((lex = PreviewGetLexem()).m_lexType != ERRORTYPE)
	{
		if (singleStmtMode && parsedOne)
			goto blockExit;

		if (IDENTIFIER == lex.m_lexType && IsTypeVar(lex.m_strData)) CompileDeclaration();

		if (KEYWORD == lex.m_lexType)
		{
			switch (lex.m_numData)
			{
			case KEY_VAR:// setting variables and arrays
				CompileDeclaration();
				break;
			case KEY_NEW:
				CompileNewObject();
				break;
			case KEY_IF:
				CompileIf();
				break;
			case KEY_WHILE:
				CompileWhile();
				break;
			case KEY_FOREACH:
				CompileForeach();
				break;
			case KEY_FOR:
				CompileFor();
				break;
			case KEY_GOTO:
				CompileGoto();
				break;
			case KEY_RETURN:
			{
				ExpectKeyword(KEY_RETURN);

				if (m_activeContext->m_returnKind == RETURN_NONE)
					return false;

				if (IsReturnFunction(m_activeContext->m_returnKind))
				{
					if (IsNextDelimeter(';')) return false;

					ibParamValue returnValue = GetExpression();

					if (!m_rootContext.m_currentFunctionName.IsEmpty())
					{
						ibPrecompileFunction* function = m_rootContext.m_functions[m_rootContext.m_currentFunctionName];
						if (function)
							function->m_returnValue = returnValue;
					}
				}
				break;
			}
			case KEY_TRY:
			{
				ExpectKeyword(KEY_TRY);
				CompileBlock();

				ExpectKeyword(KEY_EXCEPT);
				CompileBlock();
				// VES terminates with EndTry; CES closes via the body
				// braces and falls through directly.
				if (!IsCES())
					ExpectKeyword(KEY_ENDTRY);

				break;
			}

			case KEY_RAISE: ExpectKeyword(KEY_RAISE); break;
			case KEY_CONTINUE: ExpectKeyword(KEY_CONTINUE); break;
			case KEY_BREAK: ExpectKeyword(KEY_BREAK); break;

			case KEY_FROM:
				// Statement-level LINQ — rare but legal at compile time
				// (`from o in arr select o;` discarded result, side-effecty
				// only via lambda invocations). Walker registers bindings
				// so caret-inside-query autocomplete works.
				CompileLinqExpression();
				break;

			case KEY_FUNCTION:
			case KEY_PROCEDURE:
			{
				// Anonymous Function/Procedure expression inside a body
				// (lambda — `var f = Function(x) ... EndFunction`). For
				// Phase A IntelliSense we skip the body via depth-tracking
				// rather than recursively parsing it: anonymous bodies
				// aren't navigable by name, and walking them with the
				// existing m_activeContext would clobber the enclosing
				// scope. A future iteration can invoke CompileFunction
				// here with proper save/restore around m_activeContext to
				// expose the lambda's locals for autocomplete.
				GetLexem();  // consume the opening Function/Procedure keyword
				int depth = 1;
				while (m_cursor + 1 < m_listLexem.size()) {
					const ibLexem& nl = m_listLexem[m_cursor + 1];
					if (nl.m_lexType == KEYWORD) {
						if (nl.m_numData == KEY_FUNCTION || nl.m_numData == KEY_PROCEDURE) {
							depth++;
						}
						else if (nl.m_numData == KEY_ENDFUNCTION || nl.m_numData == KEY_ENDPROCEDURE) {
							if (--depth == 0) {
								GetLexem();  // consume matching end
								break;
							}
						}
					}
					GetLexem();
				}
				break;
			}

			default:
				// means the operator bracket ending this block has been encountered (for example, ENDIF, ENDDO, ENDFUNCTION, etc.)
				goto blockExit;
			}
			// One control-keyword statement consumed (KEY_VAR / KEY_IF /
			// KEY_WHILE / KEY_FOR{,EACH} / KEY_GOTO / KEY_RETURN /
			// KEY_TRY / KEY_RAISE / KEY_CONTINUE / KEY_BREAK / lambda
			// skip). Trips singleStmtMode so a brace-less CES body
			// `if (cond) stmt;` exits after exactly this statement.
			parsedOne = true;
		}
		else
		{
			lex = GetLexem();
			if (IDENTIFIER == lex.m_lexType)
			{
				if (IsNextDelimeter(':'))// this is a label task encountered
				{
					// write the address of the label:
					ExpectDelimeter(':');
				}
				else
				{
					m_cursor--;// step back

					int nSet = 1;
					ibParamValue variable = GetCurrentIdentifier(nSet);
					if (nSet)
					{
						ExpectDelimeter('=');

						ibParamValue expression = GetExpression();
						variable.m_paramType = expression.m_paramType;
						variable.m_paramObject = expression.m_paramObject;

						if (m_activeContext->FindVariable(variable.m_paramName)) {
							m_activeContext->m_variables[variable.m_paramName].m_valObject = expression.m_paramObject;
						}
						else
						{
							const int implDeclPos = (m_cursor >= 0 && m_cursor < (int)m_listLexem.size())
								? m_listLexem[m_cursor].m_numString : 0;
							m_activeContext->AddVariable(variable.m_paramName, expression.m_paramType, false, false, expression.m_paramObject, implDeclPos);
						}
					}
				}
				parsedOne = true;
			}
			else if (DELIMITER == lex.m_lexType && ';' == lex.m_numData) {
				// VES legacy break-on-`;` was a bug for CES — `;` is just
				// a statement terminator there; the block continues.
				// In single-stmt mode the trailing `;` after the body is
				// the body's own terminator; ate-it-and-exit.
				if (IsCES()) {
					if (singleStmtMode && parsedOne)
						goto blockExit;
					continue;
				}
				break;
			}
			else if (IsCES() && DELIMITER == lex.m_lexType && '{' == lex.m_numData) {
				// Nested `{ … }` block — recurse into CompileBlock; it
				// will consume the matching `}` itself.
				m_cursor--;  // step back so CompileBlock sees the `{`
				CompileBlock();
				parsedOne = true;
			}
			else if (IsCES() && DELIMITER == lex.m_lexType && '}' == lex.m_numData) {
				// Block close — step back and let the surrounding caller
				// (or our own bCompileBlock branch below) consume `}`.
				m_cursor--;
				break;
			}
			else if (ENDPROGRAM == lex.m_lexType) break;
			else return false;
		}
	}//while

blockExit:
	if (IsCES() && bCompileBlock)
		ExpectDelimeter('}');

	return true;
}

bool ibPrecompileCode::CompileNewObject()
{
	ExpectKeyword(KEY_NEW);

	wxString objectName = ExpectIdentifier(true);
	(void)GetConstString(objectName);

	std::vector <ibParamValue> listParam;

	if (IsNextDelimeter('('))// this is a method call
	{
		ExpectDelimeter('(');

		while (m_cursor + 1 < m_listLexem.size()
			&& !IsNextDelimeter(')'))
		{
			if (IsNextDelimeter(','))
			{
				ibParamValue data; // missing parameter
				listParam.push_back(data);
			}
			else
			{
				listParam.emplace_back(GetExpression());

				if (IsNextDelimeter(')')) break;
			}
			ExpectDelimeter(',');
		}

		ExpectDelimeter(')');
	}

	return true;
}

bool ibPrecompileCode::CompileGoto()
{
	ExpectKeyword(KEY_GOTO);
	return true;
}

bool ibPrecompileCode::CompileIf()
{
	ExpectKeyword(KEY_IF);

	if (IsCES())
		ExpectDelimeter('(');
	GetExpression();
	if (IsCES())
		ExpectDelimeter(')');
	else
		ExpectKeyword(KEY_THEN);

	CompileBlock();

	while (IsNextKeyWord(KEY_ELSEIF))
	{
		// write the output from all checks for the previous block
		ExpectKeyword(KEY_ELSEIF);

		if (IsCES())
			ExpectDelimeter('(');
		GetExpression();
		if (IsCES())
			ExpectDelimeter(')');
		else
			ExpectKeyword(KEY_THEN);

		CompileBlock();
	}

	if (IsNextKeyWord(KEY_ELSE))
	{
		// write the output from all checks for the previous block
		ExpectKeyword(KEY_ELSE);
		CompileBlock();
	}

	// VES terminates with EndIf; CES closes via the trailing `}` already
	// consumed by the last CompileBlock.
	if (!IsCES())
		ExpectKeyword(KEY_ENDIF);
	return true;
}

bool ibPrecompileCode::CompileWhile()
{
	ExpectKeyword(KEY_WHILE);

	if (IsCES())
		ExpectDelimeter('(');
	GetExpression();
	if (IsCES())
		ExpectDelimeter(')');
	else
		ExpectKeyword(KEY_DO);

	CompileBlock();

	if (!IsCES())
		ExpectKeyword(KEY_ENDDO);

	return true;
}

bool ibPrecompileCode::CompileFor()
{
	ExpectKeyword(KEY_FOR);

	if (IsCES())
		ExpectDelimeter('(');

	int nStartPos = m_listLexem[m_cursor].m_numString;

	wxString realName = ExpectIdentifier(true);

	GetVariable(realName);  // ensures loop var is registered as a local

	if (IsNextDelimeter('='))
		ExpectDelimeter('=');

	GetExpression();

	ExpectKeyword(KEY_TO);
	GetExpression();

	if (IsCES())
		ExpectDelimeter(')');
	else
		ExpectKeyword(KEY_DO);

	CompileBlock();

	if (!IsCES())
		ExpectKeyword(KEY_ENDDO);

	if (!(nStartPos < m_caretPos && m_listLexem[m_cursor].m_numString > m_caretPos))
		m_activeContext->RemoveVariable(realName);

	return true;
}

bool ibPrecompileCode::CompileForeach()
{
	ExpectKeyword(KEY_FOREACH);

	if (IsCES())
		ExpectDelimeter('(');

	int nStartPos = m_listLexem[m_cursor].m_numString;

	wxString realName = ExpectIdentifier(true);
	wxString name = stringUtils::MakeUpper(realName);

	GetVariable(realName);  // ensures iteration var is registered as a local

	ExpectKeyword(KEY_IN);

	ibParamValue collection = GetExpression();
	{
		ibValue skeleton;
		auto state = collection.m_paramObject.CreateIterator();
		if (state) state->PeekSample(skeleton);
		m_activeContext->m_variables[name].m_valObject = skeleton;
	}

	if (IsCES())
		ExpectDelimeter(')');
	else
		ExpectKeyword(KEY_DO);

	CompileBlock();

	if (!IsCES())
		ExpectKeyword(KEY_ENDDO);

	if (!(nStartPos < m_caretPos && m_listLexem[m_cursor].m_numString > m_caretPos))
		m_activeContext->RemoveVariable(realName);

	return true;
}

// Block-syntax LINQ precompile walker — see header. Minimal pass:
//   - Each `from <id> in <expr>` / `join <id> in <expr> on K1 equals K2`
//     registers <id> as a binding in m_activeContext (untyped).
//   - `let <id> = <expr>` (KEY_VAR form) registers <id>.
//   - `group ... into <id>` registers <id>.
//   - All other clauses (where/skip/take/orderby/select/distinct/group
//     terminal) are parsed enough to advance the cursor past them so
//     the host CompileBlock loop resumes at the next statement.
//
// Bindings live in m_activeContext for the duration of the walk. After
// the walker returns, only those active when the caret was inside the
// query's lex range survive (mirror of CompileForeach's
// nStartPos/m_caretPos discipline at line ~1465). Real LINQ scoping is
// per-block with RETURN_BLOCK chain delegation; precompile flattens
// everything onto the active context for IntelliSense convenience.
ibParamValue ibPrecompileCode::CompileLinqExpression()
{
	const int nStartPos = (m_cursor + 1 < (int)m_listLexem.size())
		? m_listLexem[m_cursor + 1].m_numString
		: 0;

	std::vector<wxString> introducedBindings;
	auto registerBinding = [&](const wxString& realName) {
		if (realName.IsEmpty()) return;
		m_activeContext->AddVariable(realName, wxEmptyString, false, false, ibValue());
		introducedBindings.push_back(realName);
	};

	// ---- Outer from-clauses. At least one is required (caller ensured
	//      KEY_FROM is next), and subsequent `from` chains additional
	//      bindings (multi-source LINQ).
	while (IsNextKeyWord(KEY_FROM)) {
		ExpectKeyword(KEY_FROM);
		const wxString bindingName = ExpectIdentifier(true);
		if (!IsNextKeyWord(KEY_IN)) break;
		ExpectKeyword(KEY_IN);
		GetExpression();   // source expression; element type unknown
		registerBinding(bindingName);
	}

	// ---- Tail clauses. Loop until we hit a terminator (`;`, `,`, `)`)
	//      or an unrecognised keyword.
	bool sawSelectOrGroupTerminal = false;
	while (m_cursor + 1 < (int)m_listLexem.size() && !sawSelectOrGroupTerminal) {
		const ibLexem& lex = m_listLexem[m_cursor + 1];

		if (lex.m_lexType == DELIMITER && (lex.m_numData == ';'
			|| lex.m_numData == ',' || lex.m_numData == ')'
			|| lex.m_numData == '}'))
			break;

		if (lex.m_lexType != KEYWORD) break;

		switch (lex.m_numData) {

			case KEY_WHERE:
				ExpectKeyword(KEY_WHERE);
				GetExpression();
				break;

			case KEY_VAR: {
				// let-clause: `var <id> = <expr>` introduces a new
				// binding (LET in C# LINQ; KEY_LET enum was retired,
				// KEY_VAR reused per linq.md grammar).
				ExpectKeyword(KEY_VAR);
				const wxString letName = ExpectIdentifier(true);
				if (IsNextDelimeter('=')) {
					ExpectDelimeter('=');
					GetExpression();
				}
				registerBinding(letName);
				break;
			}

			case KEY_SKIP:
				ExpectKeyword(KEY_SKIP);
				GetExpression();
				break;

			case KEY_TAKE:
				ExpectKeyword(KEY_TAKE);
				GetExpression();
				break;

			case KEY_ORDERBY:
				ExpectKeyword(KEY_ORDERBY);
				GetExpression();
				if (IsNextKeyWord(KEY_ASCENDING))  ExpectKeyword(KEY_ASCENDING);
				if (IsNextKeyWord(KEY_DESCENDING)) ExpectKeyword(KEY_DESCENDING);
				break;

			case KEY_JOIN: {
				// `join <id> in <T> on <K1> equals <K2> [into <g>]`
				ExpectKeyword(KEY_JOIN);
				const wxString joinName = ExpectIdentifier(true);
				if (IsNextKeyWord(KEY_IN)) ExpectKeyword(KEY_IN);
				GetExpression();   // T
				if (IsNextKeyWord(KEY_ON)) ExpectKeyword(KEY_ON);
				GetExpression();   // K1
				if (IsNextKeyWord(KEY_EQUALS)) ExpectKeyword(KEY_EQUALS);
				GetExpression();   // K2
				registerBinding(joinName);
				if (IsNextKeyWord(KEY_INTO)) {
					ExpectKeyword(KEY_INTO);
					const wxString groupName = ExpectIdentifier(true);
					registerBinding(groupName);
				}
				break;
			}

			case KEY_GROUP: {
				// `group <X> by <K> [into <g>]`. Terminal when no `into`.
				ExpectKeyword(KEY_GROUP);
				GetExpression();   // X
				if (IsNextKeyWord(KEY_BY)) ExpectKeyword(KEY_BY);
				GetExpression();   // K
				if (IsNextKeyWord(KEY_INTO)) {
					ExpectKeyword(KEY_INTO);
					const wxString groupName = ExpectIdentifier(true);
					registerBinding(groupName);
					// continue parsing the tail — `into g` is non-terminal
				} else {
					sawSelectOrGroupTerminal = true;   // terminal group
				}
				break;
			}

			case KEY_SELECT: {
				ExpectKeyword(KEY_SELECT);
				if (IsNextDelimeter('{')) {
					// Anonymous structure: `{ name = expr, name = expr }`
					ExpectDelimeter('{');
					while (m_cursor + 1 < (int)m_listLexem.size()
						&& !IsNextDelimeter('}')) {
						ExpectIdentifier(true);
						if (IsNextDelimeter('=')) ExpectDelimeter('=');
						GetExpression();
						if (IsNextDelimeter(',')) ExpectDelimeter(',');
						else break;
					}
					if (IsNextDelimeter('}')) ExpectDelimeter('}');
				} else {
					GetExpression();
				}
				sawSelectOrGroupTerminal = true;
				break;
			}

			case KEY_DISTINCT:
				// Modifier on select — consume but don't terminate (in case
				// distinct appears as the very last keyword without an
				// explicit select).
				ExpectKeyword(KEY_DISTINCT);
				break;

			case KEY_FROM: {
				// Additional from-clause inside the tail (rare — usually
				// at the start, but grammar allows it after where/let).
				ExpectKeyword(KEY_FROM);
				const wxString bindingName = ExpectIdentifier(true);
				if (IsNextKeyWord(KEY_IN)) {
					ExpectKeyword(KEY_IN);
					GetExpression();
				}
				registerBinding(bindingName);
				break;
			}

			default:
				// Unknown keyword — bail to host parser.
				goto done;
		}
	}

done:
	// Bindings stay visible only if the caret landed inside the query's
	// lex range; otherwise drop them to avoid leaking into surrounding
	// statements (mirrors CompileForeach line ~1465).
	const int nEndPos = (m_cursor < (int)m_listLexem.size())
		? m_listLexem[m_cursor].m_numString
		: 0;
	const bool caretInsideBlock = (nStartPos < (int)m_caretPos && nEndPos > (int)m_caretPos);
	if (!caretInsideBlock) {
		for (const wxString& name : introducedBindings)
			m_activeContext->RemoveVariable(name);
	}

	ibParamValue result;
	result.m_paramType = wxT("Array");   // LINQ block evaluates to an Array
	return result;
}

//////////////////////////////////////////////////////////////////////
// Compiling
//////////////////////////////////////////////////////////////////////

/**
 * GetLexem
 *   Advance to the next lexem and return it. Returns gs_nullLexem if the
 *   cursor is past the end of the list.
 */
const ibLexem& ibPrecompileCode::GetLexem()
{
	if (m_cursor + 1 < m_listLexem.size())
		return m_listLexem[++m_cursor];
	return gs_nullLexem;
}

/**
 * PreviewGetLexem
 *   Peek the next non-trivial lexem without advancing the cursor.
 *   Designer-side skips both ';' and '\n' delimiters so IntelliSense
 *   walk-ahead steps over whitespace tokens; production ibCompileCode
 *   skips only ';' (statement separator).
 */
const ibLexem& ibPrecompileCode::PreviewGetLexem()
{
	while (true) {
		const ibLexem& lex = GetLexem();
		if (!(lex.m_lexType == DELIMITER && (lex.m_numData == ';' || lex.m_numData == '\n'))) {
			m_cursor--;
			return lex;
		}
	}
	return gs_nullLexem;
}

/**
 * ExpectLexem
 *   Same as GetLexem but treats ERRORTYPE silently — designer's
 *   IntelliSense walk doesn't surface compile errors, the caller simply
 *   accepts incomplete results.
 */
const ibLexem& ibPrecompileCode::ExpectLexem()
{
	return GetLexem();
}

/**
 * ExpectDelimeter
 *   Consume lexems until the matching delimiter is found. If the very
 *   first lexem already matches, append it to the IntelliSense
 *   "last expression" buffer. The append-only-on-first-match behaviour
 *   mirrors the original code.
 */
void ibPrecompileCode::ExpectDelimeter(const wxUniChar& c)
{
	const ibLexem& first = ExpectLexem();
	if (first.m_lexType == DELIMITER && c == first.m_numData) {
		m_lastExpression += c;
		return;
	}

	while (m_cursor + 1 < m_listLexem.size()) {
		const ibLexem& lex = ExpectLexem();
		if (lex.m_lexType == DELIMITER && c == lex.m_numData)
			return;
	}
}
/**
 * IsNextDelimeter
 *   Predicate: is the next lexem the given delimiter? Does not advance.
 */
bool ibPrecompileCode::IsNextDelimeter(const wxUniChar& c)
{
	if (m_cursor + 1 < m_listLexem.size()) {
		ibLexem lex = m_listLexem[m_cursor + 1];
		if (lex.m_lexType == DELIMITER && c == lex.m_numData)
			return true;
	}

	return false;
}

/**
 * IsNextKeyWord
 *   Predicate: is the next lexem the given keyword? Does not advance.
 */
bool ibPrecompileCode::IsNextKeyWord(int keyword)
{
	if (m_cursor + 1 < m_listLexem.size()) {
		const ibLexem& lex = m_listLexem[m_cursor + 1];
		if (lex.m_lexType == KEYWORD && lex.m_numData == keyword)
			return true;

	}
	return false;
}

/**
 * ExpectKeyword
 *   Consume lexems until the given keyword is matched, or the lexem
 *   list is exhausted.
 */
void ibPrecompileCode::ExpectKeyword(int keyword)
{
	ibLexem lex = ExpectLexem();
	while (!(lex.m_lexType == KEYWORD && lex.m_numData == keyword)) {
		if (m_cursor + 1 >= m_listLexem.size())
			break;
		lex = ExpectLexem();
	}
}

/**
 * ExpectIdentifier
 *   Consume the next lexem as an identifier. With realName=true the
 *   original-case spelling is returned; otherwise the upper-cased form.
 *   Keywords are accepted in real-name mode (designer-side autocomplete
 *   still wants to surface them).
 */
wxString ibPrecompileCode::ExpectIdentifier(bool realName)
{
	const ibLexem& lex = ExpectLexem();
	if (lex.m_lexType != IDENTIFIER) {
		if (realName && lex.m_lexType == KEYWORD)
			return lex.m_strData;
		return wxEmptyString;
	}

	if (realName) return lex.m_valData.m_sData;
	else return lex.m_strData;
}

/**
 * ExpectConstant
 *   Consume the next lexem as a (possibly signed) numeric constant.
 *   Handles unary +/- prefix and flips the sign on negation.
 */
ibValue ibPrecompileCode::ExpectConstant()
{
	ibLexem lex;
	int sign = 0;
	if (IsNextDelimeter('-') || IsNextDelimeter('+')) {
		sign = 1;
		if (IsNextDelimeter('-'))
			sign = wxNOT_FOUND;
		lex = ExpectLexem();
	}

	lex = ExpectLexem();

	if (sign) {
		// Flip the sign on numeric negation. Non-numeric constants with a
		// leading +/- silently fall through unchanged — designer-side
		// IntelliSense doesn't report grammar errors here.
		if (sign == wxNOT_FOUND)
			lex.m_valData.m_fData = -lex.m_valData.m_fData;
	}
	return lex.m_valData;
}

// getting the m_number with a string constant (to determine the method m_number)
int ibPrecompileCode::GetConstString(const wxString& method)
{
	if (!m_constHashes[method])
	{
		m_constHashes[method] = m_constHashes.size();
	}
	return ((int)m_constHashes[method]) - 1;
}

int ibPrecompileCode::IsTypeVar(const wxString& type)
{
	if (!type.IsEmpty()) {
		if (ibValue::IsRegisterCtor(type, ibCtorObjectType::ibCtorObjectType_object_primitive))
			return true;
	}
	else {
		const ibLexem& lex = PreviewGetLexem();
		if (ibValue::IsRegisterCtor(lex.m_strData, ibCtorObjectType::ibCtorObjectType_object_primitive))
			return true;
	}

	return false;
}

wxString ibPrecompileCode::GetTypeVar(const wxString& type)
{
	if (!type.IsEmpty()) {
		if (ibValue::IsRegisterCtor(type, ibCtorObjectType::ibCtorObjectType_object_primitive))
			return type.Upper();
	}
	else {
		const ibLexem& lex = ExpectLexem();
		if (ibValue::IsRegisterCtor(lex.m_strData, ibCtorObjectType::ibCtorObjectType_object_primitive)) {
			return stringUtils::MakeUpper(lex.m_strData);
		}
	}

	return wxEmptyString;
}

#define SetOper(x) nOper=x;

/**
 *    (    )
 */
ibParamValue ibPrecompileCode::GetExpression(int priority)
{
	ibParamValue variable;
	ibLexem lex = ExpectLexem();

	// first we process Left operators
	if ((lex.m_lexType == KEYWORD && lex.m_numData == KEY_NOT) ||
		(lex.m_lexType == DELIMITER && lex.m_numData == '!')) {
		variable = GetVariable();
		ibParamValue sVariable2 = GetExpression(gs_operPriority['!']);
		variable.m_paramType = wxT("NUMBER");
	}
	else if (lex.m_lexType == KEYWORD &&
		(lex.m_numData == KEY_FUNCTION || lex.m_numData == KEY_PROCEDURE)) {
		// Anonymous Function/Procedure expression (lambda).
		// Push a child precompile context with parent = root so the body
		// parses with params + locals visible to autocomplete. Lambda
		// scope rule mirrors runtime: own params/locals + root-level
		// Context bindings (Catalogs / Documents / Manager / system fns).
		// The enclosing function's locals are intentionally invisible
		// (matches CompileLambdaExpression's m_parentContext = nullptr
		// discipline on the backend).
		const long openKey = lex.m_numData;

		ibPrecompileContext* const prevActive = m_activeContext;
		m_activeContext = new ibPrecompileContext(GetContext());
		m_activeContext->SetModule(this);
		m_activeContext->m_returnKind = (openKey == KEY_FUNCTION)
			? RETURN_LAMBDA_FUNCTION : RETURN_LAMBDA_PROCEDURE;

		// Parse the parameter list — register each as a local in the
		// lambda's context. Mirrors CompileFunction's signature loop;
		// optional default values are consumed but their type isn't
		// inferred (defaults are literal primitives, fine to ignore for
		// autocomplete typing).
		if (IsNextDelimeter('(')) {
			ExpectDelimeter('(');
			while (m_cursor + 1 < m_listLexem.size() && !IsNextDelimeter(')')) {
				wxString type;
				if (IsTypeVar())            type = GetTypeVar();
				if (IsNextKeyWord(KEY_VAL)) ExpectKeyword(KEY_VAL);

				wxString realName = ExpectIdentifier(true);

				if (IsNextDelimeter('[')) {
					ExpectDelimeter('[');
					ExpectDelimeter(']');
				}
				else if (IsNextDelimeter('=')) {
					ExpectDelimeter('=');
					ExpectConstant();
				}

				ibValue valObject;
				if (!type.IsEmpty()) {
					try { valObject = ibValue::CreateObject(type); }
					catch (...) {}
				}
				m_activeContext->AddVariable(realName, type, false, false, valObject);

				if (IsNextDelimeter(')')) break;
				ExpectDelimeter(',');
			}
			ExpectDelimeter(')');
		}

		// Body — CompileBlock handles both CES `{ … }` and VES
		// keyword-fenced fall-through. Capture body span for cursor-
		// inside check. allowSingleStmt=false: lambda body is multi-
		// statement up to its EndFunction / EndProcedure / `}`, same as a
		// named function body.
		const int nStartContext = (m_cursor < (int)m_listLexem.size())
			? m_listLexem[m_cursor].m_numString : 0;

		CompileBlock(/*allowSingleStmt=*/false);

		if (!IsCES()) {
			if (openKey == KEY_FUNCTION) ExpectKeyword(KEY_ENDFUNCTION);
			else                          ExpectKeyword(KEY_ENDPROCEDURE);
		}

		// Cursor inside lambda body → expose its context to autocomplete.
		// Same pattern as CompileFunction's tail (line ~982).
		if (m_cursor < (int)m_listLexem.size() &&
			m_caretPos >= nStartContext &&
			m_caretPos <= m_listLexem[m_cursor].m_numString) {
			m_cursorContext = m_activeContext;
		}

		// Restore caller's context. Lambda's context is referenced by
		// m_cursorContext (when caret hit it) and by AddVariable's parent
		// chain — leaking into a member would shadow the next CompileBlock
		// iteration's local-scope pushes.
		m_activeContext = prevActive;

		variable.m_paramType = wxT("FUNCTION");
		return variable;
	}
	else if (lex.m_lexType == KEYWORD && lex.m_numData == KEY_FROM) {
		// Block-syntax LINQ expression entry. Walker consumes the whole
		// query (from / where / let / skip / take / orderby / group /
		// join / select / distinct) and registers bindings so the user's
		// caret-inside-block sees them in autocomplete. Result type is
		// "Array" — the LINQ block always evaluates to an Array result
		// (or after .Add via __r at the runtime end).
		m_cursor--;   // step back so the walker sees KEY_FROM
		return CompileLinqExpression();
	}
	else if ((lex.m_lexType == KEYWORD && lex.m_numData == KEY_NEW)) {

		const wxString& objectName = ExpectIdentifier();
		std::vector <ibParamValue> listParam;


		if (IsNextDelimeter('(')) { // this is a method call	
			ExpectDelimeter('(');
			while (m_cursor + 1 < m_listLexem.size()
				&& !IsNextDelimeter(')')) {
				if (IsNextDelimeter(',')) {
					ibParamValue data;
					//data.nArray = DEF_VAR_SKIP;// missing parameter
					//data.nIndex = DEF_VAR_SKIP;
					listParam.push_back(data);
				}
				else {
					listParam.emplace_back(GetExpression());
					if (IsNextDelimeter(')')) break;
				}
				ExpectDelimeter(',');
			}
			ExpectDelimeter(')');
		}

		ibValue** pRefLocVars = listParam.size() ? 
			new ibValue * [listParam.size()] : nullptr;
		
		for (unsigned int i = 0; i < listParam.size(); i++) 
			pRefLocVars[i] = &listParam[i].m_paramObject;

		try {
			variable.m_paramObject = ibValue::CreateObject(objectName,
				pRefLocVars, listParam.size()
			);
		}
		catch (...) {
		}

		if (pRefLocVars != nullptr) 
			wxDELETEA(pRefLocVars);
	
		return variable;
	}
	else if (lex.m_lexType == DELIMITER && lex.m_numData == '(')
	{
		variable = GetExpression();
		ExpectDelimeter(')');
	}
	else if (lex.m_lexType == DELIMITER && lex.m_numData == '?')
	{
		variable = GetVariable();
		//ibByteUnit code;
		//AddLineInfo(code);
		//code.nOper = OPER_ITER;
		/*code.Param1 = variable;*/
		ExpectDelimeter('(');
		/*code.Param2 =*/ GetExpression();
		ExpectDelimeter(',');
		/*code.Param3 *=*/ GetExpression();
		ExpectDelimeter(',');
		/*code.Param4 = */GetExpression();
		ExpectDelimeter(')');
		//cByteCode.CodeList.push_back(code);
	}
	else if (lex.m_lexType == IDENTIFIER)
	{
		m_cursor--;// step back
		int nSet = 0;
		variable = GetCurrentIdentifier(nSet);
	}
	else if (lex.m_lexType == CONSTANT)
	{
		variable = FindConst(lex.m_valData);
	}
	else if ((lex.m_lexType == DELIMITER && lex.m_numData == '+') || (lex.m_lexType == DELIMITER && lex.m_numData == '-'))
	{

		int nCurPriority = gs_operPriority[lex.m_numData];

		if (priority >= nCurPriority)
			return variable;


		if (lex.m_numData == '+')// do nothing (ignore)
		{
			variable = GetExpression(priority);
			variable.m_paramType = wxT("NUMBER");
			return variable;
		}
		else
		{
			variable = GetExpression(100);//super high priority!
			variable = GetVariable();
			variable.m_paramType = wxT("NUMBER");
		}
	}




MOperation:

	lex = PreviewGetLexem();

	if (lex.m_lexType == DELIMITER && lex.m_numData == ')') return variable;


	if ((lex.m_lexType == DELIMITER && lex.m_numData != ';') || (lex.m_lexType == KEYWORD && lex.m_numData == KEY_AND) || (lex.m_lexType == KEYWORD && lex.m_numData == KEY_OR))
	{
		if (lex.m_numData >= 0 && lex.m_numData <= 255)
		{
			int nCurPriority = gs_operPriority[lex.m_numData]; int nOper = 0;

			if (priority < nCurPriority)
			{
				lex = GetLexem();

				if (lex.m_numData == '*')
				{
					SetOper(OPER_MULT);
				}
				else if (lex.m_numData == '/')
				{
					SetOper(OPER_DIV);
				}
				else if (lex.m_numData == '+')
				{
					SetOper(OPER_ADD);
				}
				else if (lex.m_numData == '-')
				{
					SetOper(OPER_SUB);
				}
				else if (lex.m_numData == '%')
				{
					SetOper(OPER_MOD);
				}
				else if (lex.m_numData == KEY_AND)
				{
					SetOper(OPER_AND);
				}
				else if (lex.m_numData == KEY_OR)
				{
					SetOper(OPER_OR);
				}
				else if (lex.m_numData == '>')
				{
					SetOper(OPER_GT);

					if (IsNextDelimeter('='))
					{
						ExpectDelimeter('=');
						SetOper(OPER_GE);
					}
				}
				else if (lex.m_numData == '<')
				{
					SetOper(OPER_LS);
					if (IsNextDelimeter('='))
					{
						ExpectDelimeter('=');
						SetOper(OPER_LE);
					}
					else if (IsNextDelimeter('>'))
					{
						ExpectDelimeter('>');
						SetOper(OPER_NE);
					}

				}
				else if (lex.m_numData == '=')
				{
					SetOper(OPER_EQ);
				}
				else return variable;

				ibParamValue sVariable1 = GetVariable();
				ibParamValue sVariable2 = variable;
				ibParamValue sVariable3 = GetExpression(nCurPriority);


				if (sVariable2.m_paramType == wxT("STRING")) {
					if (OPER_DIV == nOper ||
						OPER_MOD == nOper ||
						OPER_MULT == nOper ||
						OPER_AND == nOper ||
						OPER_OR == nOper)
						return variable;
				}

				sVariable1.m_paramType = sVariable2.m_paramType;

				if (nOper >= OPER_GT && nOper <= OPER_NE) {
					sVariable1.m_paramType = wxT("NUMBER");
				}

				variable = sVariable1;
				goto MOperation;
			}
		}
	}
	return variable;
}

/*
 * GetCurrentIdentifier
 * Purpose:
 * Compiling an identifier (defining its type as a variable, attribute or function, method)
 * numIsSet - at the input: 1 - a sign that an expression assignment may be expected (if the '=' sign is encountered)
 * Return value:
 * numIsSet - at the output: 1 - a sign that the assignment of the expression is exactly expected (i.e. the '=' sign must be encountered)
 * index m_number of the variable where the identifier value lies
*/

ibParamValue ibPrecompileCode::GetCurrentIdentifier(int& isSet)
{
	int nPrevSet = isSet;

	ibParamValue variable = GetVariable();

	const wxString& realName = ExpectIdentifier(true);
	const wxString& name = stringUtils::MakeUpper(realName);

	// Identifier's source-text position — used both for the caret-after-
	// identifier teleport check below and as the declPos seed when
	// GetVariable auto-declares this name (`a = expr` first-occurrence).
	// declPos propagation keeps autocomplete from surfacing implicit
	// locals whose first reference is below the caret.
	const int nStartPos = m_listLexem[m_cursor].m_numString;

	if (!m_calcValue && (nStartPos + realName.length() == m_caretPos ||
		nStartPos + realName.length() == m_caretPos - 1)) {
		unsigned int endContext = 0;
		for (unsigned int i = m_cursor; i < m_listLexem.size(); i++) {
			if (m_listLexem[i].m_lexType == KEYWORD && (m_listLexem[i].m_numData == KEY_ENDPROCEDURE || m_listLexem[i].m_numData == KEY_ENDFUNCTION))
				endContext = i;
			if (m_listLexem[i].m_lexType == ENDPROGRAM)
				endContext = i;
		}
		isSet = 0; m_cursor = endContext; return variable;
	}

	m_lastExpression = realName;

	if (IsNextDelimeter('('))// this is a function call
	{
		ibValue valContext;
		if (m_rootContext.FindFunction(realName, &valContext, true))
		{
			std::vector <ibParamValue> listParam;
			ExpectDelimeter('(');
			while (m_cursor + 1 < m_listLexem.size()
				&& !IsNextDelimeter(')'))
			{
				if (IsNextDelimeter(','))
				{
					ibParamValue data;
					listParam.push_back(data);
				}
				else
				{
					listParam.emplace_back(GetExpression());
					if (IsNextDelimeter(')')) break;
				}
				ExpectDelimeter(',');
			}

			ExpectDelimeter(')');

			const long lMethodNum = valContext.FindMethod(name);
			if (lMethodNum != wxNOT_FOUND && valContext.HasRetVal(lMethodNum)) {
				
				size_t paramCount = valContext.GetNParams(lMethodNum) > 0 ?
					valContext.GetNParams(lMethodNum) : 1;
				paramCount = listParam.size() > 0 ? listParam.size() : paramCount;

				ibValue** pRefLocVars = new ibValue * [paramCount];
				for (unsigned int i = 0; i < paramCount; i++) {
					if (i < listParam.size())
						pRefLocVars[i] = &listParam[i].m_paramObject;
					else
						pRefLocVars[i] = new ibValue;
				}

				try {
					valContext.CallAsFunc(lMethodNum, variable.m_paramObject, pRefLocVars, listParam.size());
				}
				catch (...) {
				}
				
				SetVariable(variable.m_paramName, variable.m_paramObject);

				for (unsigned int i = listParam.size(); i < paramCount; i++)
					wxDELETE(pRefLocVars[i]);
				
				wxDELETEA(pRefLocVars);
			}
		}
		else
		{
			variable = GetCallFunction(name);
		}

		if (IsTypeVar(name)) { // this is a type cast
			variable.m_paramObject = GetTypeVar(name);
		}

		isSet = 0;
	}
	else
	{
		m_lastParentKeyword = realName;

		bool checkError = !nPrevSet;

		if (IsNextDelimeter('.'))
			checkError = true;

		ibValue valContext;
		if (m_rootContext.FindVariable(realName, &valContext, true)) {
			isSet = 0;
			if (IsNextDelimeter('=') && nPrevSet == 1) {
				ExpectDelimeter('=');
				ibParamValue sParam = GetExpression();
				variable.m_paramObject = sParam.m_paramObject;
				return variable;
			}
			else {
				const long lPropNum = valContext.FindProp(name);
				if (lPropNum != wxNOT_FOUND) {
					try {
						valContext.GetPropVal(lPropNum, variable.m_paramObject);
					}
					catch (...) {
					}
					SetVariable(variable.m_paramName, variable.m_paramObject);
				}
			}
		}
		else {
			isSet = 1;
			variable = GetVariable(realName, checkError, nStartPos);
		}
	}

loopLabel:

	if (IsNextDelimeter('['))// this is an array
	{
		ExpectDelimeter('[');
		ibParamValue sKey = GetExpression();
		ExpectDelimeter(']');







		isSet = 0;

		if (IsNextDelimeter('=') && nPrevSet == 1)
		{
			ExpectDelimeter('=');

			ibParamValue sData = GetExpression();
			return variable;
		}
		else variable = GetVariable();

		goto loopLabel;
	}

	if (IsNextDelimeter('.'))
	{
		wxString sTempExpression = m_lastExpression;

		ExpectDelimeter('.');

		wxString strRealMethod = ExpectIdentifier(true);
		wxString method = stringUtils::MakeUpper(strRealMethod);

		if (m_listLexem[m_cursor].m_numString > m_caretPos
			|| m_listLexem[m_cursor].m_lexType == KEYWORD) {
			strRealMethod = method = wxEmptyString;
		}

		m_lastExpression += strRealMethod;

		if (m_listLexem[m_cursor].m_numString > (m_caretPos - strRealMethod.length() - 1))
		{
			m_lastExpression = sTempExpression; m_lastPosition = m_cursor; m_lastKeyword = strRealMethod;
			m_valObject = variable.m_paramObject; m_cursor = m_listLexem.size() - 1; isSet = 0;
			return variable;
		}
		else if (m_listLexem[m_cursor].m_lexType == ENDPROGRAM)
		{
			m_lastExpression = sTempExpression; m_lastPosition = m_cursor; m_lastKeyword = strRealMethod;
			m_valObject = variable.m_paramObject; m_cursor = m_listLexem.size() - 1; isSet = 0;
			return variable;
		}

		if (IsNextDelimeter('('))// this is a method call
		{
			std::vector <ibParamValue> listParam;
			ExpectDelimeter('(');
			while (m_cursor + 1 < m_listLexem.size()
				&& !IsNextDelimeter(')'))
			{
				if (IsNextDelimeter(','))
				{
					ibParamValue data;
					//data.nArray = DEF_VAR_SKIP;// missing parameter
					//data.nIndex = DEF_VAR_SKIP;
					listParam.push_back(data);
				}
				else
				{
					listParam.emplace_back(GetExpression());
					if (IsNextDelimeter(')')) break;
				}
				ExpectDelimeter(',');
			}

			ExpectDelimeter(')');

			ibValue parentValueObject = variable.m_paramObject;

			variable = GetVariable();

			const long lMethodNum = parentValueObject.FindMethod(method);
			if (lMethodNum != wxNOT_FOUND && parentValueObject.HasRetVal(lMethodNum)) {

				size_t paramCount = parentValueObject.GetNParams(lMethodNum) > 0 ?
					parentValueObject.GetNParams(lMethodNum) : 1;

				paramCount = listParam.size() > 0 ? listParam.size() : paramCount;

				ibValue** pRefLocVars = new ibValue * [paramCount];

				for (unsigned int i = 0; i < paramCount; i++) {
					if (i < listParam.size())
						pRefLocVars[i] = &listParam[i].m_paramObject;
					else 
						pRefLocVars[i] = new ibValue;
				}

				try {
					parentValueObject.CallAsFunc(lMethodNum, variable.m_paramObject, pRefLocVars, listParam.size());
				}
				catch (...) {
				}

				SetVariable(variable.m_paramName, variable.m_paramObject);

				for (unsigned int i = listParam.size(); i < paramCount; i++)
					wxDELETE(pRefLocVars[i]);

				wxDELETEA(pRefLocVars);
			}

			isSet = 0;
		}
		else
		{






			isSet = 0;

			if (IsNextDelimeter('=') && nPrevSet == 1) {
				ExpectDelimeter('=');
				ibValue parentValueObject = variable.m_paramObject;
				ibParamValue sParam = GetExpression();
				const long lPropNum = parentValueObject.FindProp(strRealMethod);
				if (lPropNum != wxNOT_FOUND) {
					try {
						parentValueObject.SetPropVal(lPropNum, sParam.m_paramObject);
					}
					catch (...) {
					}
				}
				return variable;
			}
			else {
				ibValue parentValueObject = variable.m_paramObject;
				variable = GetVariable();
				const long lPropNum = parentValueObject.FindProp(method);
				if (lPropNum != wxNOT_FOUND) {
					try {
						parentValueObject.GetPropVal(lPropNum, variable.m_paramObject);
					}
					catch (...)
					{
					}
				}
				SetVariable(variable.m_paramName, variable.m_paramObject);
			}
		}
		goto loopLabel;
	}

	return variable;
}//GetCurrentIdentifier

/**
 *     
 */
ibParamValue ibPrecompileCode::GetCallFunction(const wxString& name)
{
	std::vector<ibParamValue> listParam;

	ExpectDelimeter('(');

	while (m_cursor + 1 < m_listLexem.size()
		&& !IsNextDelimeter(')'))
	{
		if (IsNextDelimeter(','))
		{
			ibParamValue data;
			//data.nArray = DEF_VAR_SKIP;// missing parameter
			//data.nIndex = DEF_VAR_SKIP;
			listParam.push_back(data);
		}
		else
		{
			listParam.emplace_back(GetExpression());

			if (IsNextDelimeter(')')) break;
		}
		ExpectDelimeter(',');
	}
	ExpectDelimeter(')');

	ibValue retValue;

	if (m_rootContext.m_functions.find(name) != m_rootContext.m_functions.end()) {
		ibPrecompileFunction* pDefFunction = m_rootContext.m_functions[name];
		pDefFunction->m_params = listParam;
		retValue = pDefFunction->m_returnValue.m_paramObject;
	}

	ibParamValue variable = GetVariable();
	variable.m_paramObject = retValue;
	return variable;
}

/**
 * AddVariable
 *   Register a variable in the root context (module scope). Empty
 *   names are ignored. Used to surface externally-supplied locals
 *   (e.g. parent module's exports) before compilation starts.
 */
void ibPrecompileCode::AddVariable(const wxString& varName, const ibValue& value)
{
	if (varName.IsEmpty())
		return;

	m_rootContext.GetVariable(varName, false, false, value);
}

/**
 * GetVariable
 *   Resolve a variable by name in the active context, walking parent
 *   scopes. checkError=true suppresses auto-declaration on miss.
 */
ibParamValue ibPrecompileCode::GetVariable(const wxString& name, bool checkError, int declPos)
{
	return m_activeContext->GetVariable(name, true, checkError, ibValue(), declPos);
}

/**
 * GetVariable (anonymous)
 *   Allocate the next temporary `@N` slot in the active context.
 */
ibParamValue ibPrecompileCode::GetVariable()
{
	const wxString& varName = wxString::Format("@%d", m_activeContext->m_tempVarCounter);
	ibParamValue variable = m_activeContext->GetVariable(varName, false);
	m_activeContext->m_tempVarCounter++;
	return variable;
}

void ibPrecompileCode::SetVariable(const wxString& varName, const ibValue& value)
{
	m_activeContext->SetVariable(varName, value);
}

/**
 * FindConst
 *   Wrap a literal ibValue as an ibParamValue with its type tag set.
 */
ibParamValue ibPrecompileCode::FindConst(ibValue& vData)
{
	ibParamValue Const;
	wxString type = vData.GetClassName();
	Const.m_paramType = GetTypeVar(type);
	Const.m_paramObject = vData;
	return Const;
}


#pragma warning(pop)
////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : calculate compile value
////////////////////////////////////////////////////////////////////////////

#include "codeEditorInterpreter.h"
#include "backend/metaData.h"


#pragma warning(push)
#pragma warning(disable : 4018)

//array of mathematical operation priorities
static std::array<int, 256> gs_operPriority = { 0 };

ibPrecompileCode::ibPrecompileCode(ibValueMetaObjectModuleBase* moduleObject) :
	ibTranslateCode(moduleObject->GetFullName(), moduleObject->GetDocPath()),
	m_moduleObject(moduleObject), m_pContext(nullptr), m_pCurrentContext(nullptr),
	m_numCurrentCompile(wxNOT_FOUND), m_nCurrentPos(0), nLastPosition(0),
	m_bCalcValue(false)
{
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

	m_strModuleName = m_moduleObject->GetFullName();
	m_strDocPath = m_moduleObject->GetDocPath();
	m_strFileName = m_moduleObject->GetFileName();

	Load(m_moduleObject->GetModuleText());
}

ibPrecompileCode::~ibPrecompileCode() {}

void ibPrecompileCode::Clear() //����� ������ ��� ���������� ������������� �������
{
	m_pCurrentContext = nullptr;
	if (m_defineList != nullptr) m_defineList->Clear();
#ifdef UTF8_LEXEM_TRANSLATE
	m_bufferSize = m_currentLine = m_currentPos = m_currentUtf8Pos = 0;
#else 
	m_bufferSize = m_currentLine = m_currentPos = 0;
#endif
	for (auto& function : m_cContext.cFunctions) wxDELETE(function.second);
	m_numCurrentCompile = wxNOT_FOUND;
	m_cContext.cVariables.clear();
	m_cContext.cFunctions.clear();

	m_valObject.Reset();
}

#include "backend/compiler/enumFactory.h"
#include "codeEditorParser.h"

void ibPrecompileCode::PrepareModuleData()
{
	ibModuleDataObject* contextVariable = nullptr;

	if (m_moduleObject) {
		ibMetaData* metaData = m_moduleObject->GetMetaData();
		wxASSERT(metaData);
		ibValueModuleManager* moduleManager = metaData->GetModuleManager();
		wxASSERT(moduleManager);
		if (!moduleManager->FindCompileModule(m_moduleObject, contextVariable)) {
			wxASSERT_MSG(false, "ibPrecompileCode::PrepareModuleData");
		}
		for (auto pair : moduleManager->GetContextVariables()) {
			//��������� ���������� �� ���������
			ibValue* managerVariable = pair.second;
			managerVariable->PrepareNames();
			for (unsigned int i = 0; i < managerVariable->GetNProps(); i++) {
				const wxString& strAttributeName = managerVariable->GetPropName(i);
				//determine the number and type of the variable
				CPrecompileVariable cVariables;
				cVariables.strName = strAttributeName;
				cVariables.strRealName = strAttributeName;

				cVariables.nNumber = i;
				cVariables.bContext = true;
				cVariables.bExport = true;

				cVariables.m_valContext = managerVariable;

				GetContext()->cVariables[stringUtils::MakeUpper(strAttributeName)] = cVariables;
			}
			//��������� ������ �� ���������
			for (unsigned int i = 0; i < managerVariable->GetNMethods(); i++) {
				const wxString& strMethodName = managerVariable->GetMethodName(i);

				CPrecompileContext* compileContext = new CPrecompileContext();
				compileContext->nReturn = managerVariable->HasRetVal(i) ? RETURN_FUNCTION : RETURN_PROCEDURE;
				compileContext->pModule = this;

				CPrecompileFunction* pFunction = new CPrecompileFunction(strMethodName, compileContext);
				pFunction->strRealName = strMethodName;
				pFunction->strShortDescription = managerVariable->GetMethodHelper(i);
				pFunction->nStart = i;
				pFunction->bContext = true;
				pFunction->bExport = true;

				pFunction->m_valContext = managerVariable;

				// check for typing
				GetContext()->cFunctions[stringUtils::MakeUpper(strMethodName)] = pFunction;
			}
		}
		unsigned int nNumberAttr = 0;
		unsigned int nNumberFunc = 0;
		for (auto module : moduleManager->GetCommonModules()) {
			if (module->IsGlobalModule()) {
				ibParserModule cParser;
				if (cParser.ParseModule(module->GetModuleText())) {
					for (auto code : cParser.GetAllContent()) {
						if (code.eType == eExportVariable) {
							wxString strAttributeName = code.strName;
							if (m_cContext.FindVariable(strAttributeName))
								continue;
							//determine the number and type of the variable
							CPrecompileVariable cVariables;
							cVariables.strName = strAttributeName;
							cVariables.strRealName = strAttributeName;

							cVariables.nNumber = nNumberAttr;
							cVariables.bContext = true;
							cVariables.bExport = true;

							cVariables.m_valContext = module;

							GetContext()->cVariables[stringUtils::MakeUpper(strAttributeName)] = cVariables;	nNumberAttr++;
						}
						else if (code.eType == eExportProcedure) {
							wxString strMethodName = code.strName;
							if (m_cContext.FindFunction(strMethodName))
								continue;
							CPrecompileContext* procContext = new CPrecompileContext(GetContext());//������� ����� ��������, � ������� ����� ������������� ���� �������
							procContext->SetModule(this);
							procContext->nReturn = RETURN_PROCEDURE;

							CPrecompileFunction* pFunction = new CPrecompileFunction(strMethodName, procContext);
							pFunction->strRealName = strMethodName;
							pFunction->strShortDescription = code.strShortDescription;

							pFunction->nStart = nNumberFunc;
							pFunction->bContext = true;
							pFunction->bExport = true;

							pFunction->m_valContext = module;

							// check for typing
							GetContext()->cFunctions[stringUtils::MakeUpper(strMethodName)] = pFunction; nNumberFunc++;
						}
						else if (code.eType == eExportFunction) {
							wxString strMethodName = code.strName;
							if (m_cContext.FindFunction(strMethodName)) continue;

							CPrecompileContext* procContext = new CPrecompileContext(GetContext());//������� ����� ��������, � ������� ����� ������������� ���� �������
							procContext->SetModule(this);
							procContext->nReturn = RETURN_FUNCTION;

							CPrecompileFunction* pFunction = new CPrecompileFunction(strMethodName, procContext);
							pFunction->strRealName = strMethodName;
							pFunction->strShortDescription = code.strShortDescription;

							pFunction->nStart = nNumberFunc;
							pFunction->bContext = true;
							pFunction->bExport = true;

							pFunction->m_valContext = module;

							// check for typing
							GetContext()->cFunctions[stringUtils::MakeUpper(strMethodName)] = pFunction; nNumberFunc++;
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
			const ibValueMetaObjectModuleBase* moduleObject = compileModule->GetModuleObject();
			if (moduleObject != nullptr) {
				ibMetaData* metaData = moduleObject->GetMetaData();
				wxASSERT(metaData);
				ibValueModuleManager* moduleManager = metaData->GetModuleManager();
				if (moduleManager->FindCompileModule(moduleObject, pRefData)) {
					//adding variables from context
					for (long i = 0; i < pRefData->GetNProps(); i++) {
						wxString strAttributeName = pRefData->GetPropName(i);
						if (m_cContext.FindVariable(strAttributeName))
							continue;

						//determine the number and type of the variable
						CPrecompileVariable cVariables;
						cVariables.strName = strAttributeName;
						cVariables.strRealName = strAttributeName;

						cVariables.nNumber = i;
						cVariables.bContext = true;
						cVariables.bExport = true;

						cVariables.m_valContext = pRefData;

						GetContext()->cVariables[stringUtils::MakeUpper(strAttributeName)] = cVariables;
					}

					// add methods from context
					for (long i = 0; i < pRefData->GetNMethods(); i++) {
						wxString strMethodName = pRefData->GetMethodName(i);
						if (m_cContext.FindFunction(strMethodName))
							continue;

						CPrecompileContext* procContext = new CPrecompileContext(GetContext());//������� ����� ��������, � ������� ����� ������������� ���� �������
						procContext->SetModule(this);

						if (pRefData->HasRetVal(i))
							procContext->nReturn = RETURN_FUNCTION;
						else
							procContext->nReturn = RETURN_PROCEDURE;

						CPrecompileFunction* pFunction = new CPrecompileFunction(strMethodName, procContext);
						pFunction->strRealName = strMethodName;
						pFunction->strShortDescription = pRefData->GetMethodHelper(i);
						pFunction->nStart = i;
						pFunction->bContext = true;
						pFunction->bExport = true;

						pFunction->m_valContext = pRefData;

						// check for typing
						GetContext()->cFunctions[stringUtils::MakeUpper(strMethodName)] = pFunction;
					}

					if (moduleObject != nullptr) {
						ibParserModule cParser;
						if (cParser.ParseModule(moduleObject->GetModuleText())) {
							unsigned int nNumberAttr = pRefData->GetNProps() + 1;
							unsigned int nNumberFunc = pRefData->GetNMethods() + 1;
							for (auto code : cParser.GetAllContent()) {
								if (code.eType == eExportVariable) {
									const wxString& strAttributeName = code.strName;
									if (m_cContext.FindVariable(strAttributeName))
										continue;
									//determine the number and type of the variable
									CPrecompileVariable cVariable;
									cVariable.strName = strAttributeName;
									cVariable.strRealName = strAttributeName;

									cVariable.nNumber = nNumberAttr;
									cVariable.bContext = true;
									cVariable.bExport = true;

									cVariable.m_valContext = pRefData;

									GetContext()->cVariables[stringUtils::MakeUpper(strAttributeName)] = cVariable;	nNumberAttr++;
								}
								else if (code.eType == eExportProcedure) {
									const wxString& strMethodName = code.strName;
									if (m_cContext.FindFunction(strMethodName))
										continue;

									CPrecompileContext* procContext = new CPrecompileContext(GetContext());//������� ����� ��������, � ������� ����� ������������� ���� �������
									procContext->SetModule(this);
									procContext->nReturn = RETURN_PROCEDURE;

									CPrecompileFunction* pFunction = new CPrecompileFunction(strMethodName, procContext);
									pFunction->strRealName = strMethodName;
									pFunction->strShortDescription = code.strShortDescription;

									pFunction->nStart = nNumberFunc;
									pFunction->bContext = true;
									pFunction->bExport = true;

									pFunction->m_valContext = pRefData;

									// check for typing
									GetContext()->cFunctions[stringUtils::MakeUpper(strMethodName)] = pFunction; nNumberFunc++;
								}
								else if (code.eType == eExportFunction) {
									const wxString& strMethodName = code.strName;
									if (m_cContext.FindFunction(strMethodName))
										continue;

									CPrecompileContext* procContext = new CPrecompileContext(GetContext());//������� ����� ��������, � ������� ����� ������������� ���� �������
									procContext->SetModule(this);
									procContext->nReturn = RETURN_FUNCTION;

									CPrecompileFunction* pFunction = new CPrecompileFunction(strMethodName, procContext);
									pFunction->strRealName = strMethodName;
									pFunction->strShortDescription = code.strShortDescription;

									pFunction->nStart = nNumberFunc;
									pFunction->bContext = true;
									pFunction->bExport = true;

									pFunction->m_valContext = pRefData;

									// check for typing
									GetContext()->cFunctions[stringUtils::MakeUpper(strMethodName)] = pFunction; nNumberFunc++;
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

	while (!IsEnd()) {

		m_current_lex.m_strModuleName = m_strModuleName;

		m_current_lex.m_numLine = m_currentLine;
		m_current_lex.m_numString = m_currentPos;//���� � ���������� ���������� ������, �� ������ ��� ������ ����� ������ ������������
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

			m_listLexem.emplace_back(std::move(m_current_lex));
			continue;
		}
		else if (IsByte('~')) {
			s.clear();

			GetByte();//���������� ����������� � �������. ������ ����� (��� ������)
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
			if (m_current_lex.m_numData == KEY_DEFINE)continue; //������� ������������� ��������������
			else if (m_current_lex.m_numData == KEY_UNDEF) continue; //�������� ��������������
			else if (m_current_lex.m_numData == KEY_IFDEF || m_current_lex.m_numData == KEY_IFNDEF) continue; //�������� ��������������
			else if (m_current_lex.m_numData == KEY_ENDIFDEF) continue; //����� ��������� ��������������
			else if (m_current_lex.m_numData == KEY_ELSEDEF) continue; //"�����" ��������� ��������������
			else if (m_current_lex.m_numData == KEY_REGION) continue;
			else if (m_current_lex.m_numData == KEY_ENDREGION) continue;
		}
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
	m_currentLine = m_currentPos = 0;

	unsigned int lexem_idx = 0, lexem_line = 0, lexem_start_idx = 0;
	bool insert_after = false;
	auto hint = m_listLexem.begin();

	for (unsigned int i = 0; i <= m_listLexem.size() - 1; i++) {

		if (m_listLexem[i].m_numLine != lexem_start_idx) {
			lexem_line = m_listLexem[i].m_numLine; lexem_start_idx = i;
		}

		if (m_listLexem[i].m_numLine >= line) {
			if (i > 0) {
				m_currentLine = m_listLexem[lexem_start_idx - 1].m_numLine;
				m_currentPos = m_listLexem[lexem_start_idx - 1].m_numString;
#ifdef UTF8_LEXEM_TRANSLATE
				m_currentUtf8Pos = m_listLexem[lexem_start_idx - 1].m_numUtf8String;
#endif
				lexem_idx = lexem_start_idx - 1;
				if (lexem_idx > 0) std::advance(hint, lexem_idx - 1);
				insert_after = lexem_idx > 0;
			}
			break;
		}
		else if (m_listLexem[i].m_lexType == ENDPROGRAM) {
			if (i > 0) {
				m_currentLine = m_listLexem[i - 1].m_numLine;
				m_currentPos = m_listLexem[i - 1].m_numString;
#ifdef UTF8_LEXEM_TRANSLATE
				m_currentUtf8Pos = m_listLexem[i - 1].m_numUtf8String;
#endif
				lexem_idx = i - 1;
				if (lexem_idx > 0) std::advance(hint, lexem_idx - 1);
				insert_after = true;
			}
			break;
		}
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

	while (!IsEnd()) {

		if (insert_text && m_currentLine > (line + line_offset)) break;
		else if (delete_text && (m_currentLine > line)) break;

		m_current_lex.m_strModuleName = m_strModuleName;

		m_current_lex.m_numLine = m_currentLine;
		m_current_lex.m_numString = m_currentPos; //���� � ���������� ���������� ������, �� ������ ��� ������ ����� ������ ������������
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
			GetByte();//���������� ����������� � �������. ������ ����� (��� ������)
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
				m_current_lex.m_numData == KEY_DEFINE //������� ������������� ��������������
				|| m_current_lex.m_numData == KEY_UNDEF //�������� ��������������
				|| (m_current_lex.m_numData == KEY_IFDEF || m_current_lex.m_numData == KEY_IFNDEF)  //�������� ��������������
				|| m_current_lex.m_numData == KEY_ENDIFDEF  //����� ��������� ��������������
				|| m_current_lex.m_numData == KEY_ELSEDEF  //"�����" ��������� ��������������
				|| m_current_lex.m_numData == KEY_REGION
				|| m_current_lex.m_numData == KEY_ENDREGION
				)
			{
				continue;
			}
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

	//���������� ���������� ��������
	ibMetaData* metaData = m_moduleObject->GetMetaData();
	wxASSERT(metaData);
	ibValueModuleManager* moduleManager = metaData->GetModuleManager();
	wxASSERT(moduleManager);

	for (auto variable : moduleManager->GetGlobalVariables()) {
		AddVariable(variable.first, variable.second);
	}

	PrepareModuleData();

	return CompileModule();
}

bool ibPrecompileCode::CompileModule()
{
	m_pContext = GetContext();// context of the module itself

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
			// don't forget to restore the current module context (if necessary)...
		}
		else
		{
			break;
		}
	}

	int nStartContext = m_numCurrentCompile >= 0 ? m_listLexem[m_numCurrentCompile].m_numString : 0;

	// load the executable body of the module
	m_pContext = GetContext();// context of the module itself

	CompileBlock();

	if (m_numCurrentCompile + 1 < m_listLexem.size() - 1) return false;

	if (m_nCurrentPos >= nStartContext && m_nCurrentPos <= m_listLexem[m_numCurrentCompile].m_numString)
	{
		m_pCurrentContext = m_pContext;
	}

	return true;
}

bool ibPrecompileCode::CompileFunction()
{
	// we are now at the token level, where the FUNCTION or PROCEDURE keyword is specified
	ibLexem lex;
	if (IsNextKeyWord(KEY_FUNCTION))
	{
		GETKeyWord(KEY_FUNCTION);

		m_pContext = new CPrecompileContext(GetContext());//������� ����� ��������, � ������� ����� ������������� ���� �������
		m_pContext->SetModule(this);
		m_pContext->nReturn = RETURN_FUNCTION;
	}
	else if (IsNextKeyWord(KEY_PROCEDURE))
	{
		GETKeyWord(KEY_PROCEDURE);

		m_pContext = new CPrecompileContext(GetContext());//������� ����� ��������, � ������� ����� ������������� ���� ���������
		m_pContext->SetModule(this);
		m_pContext->nReturn = RETURN_PROCEDURE;
	}
	else
	{
		/*SetError(ERROR_FUNC_DEFINE);*/
		return false;
	}

	// pull out the text of the function declaration
	lex = PreviewGetLexem();
	wxString strShortDescription;
	int m_numLine = lex.m_numLine;
	int nRes = m_strBuffer.find('\n', lex.m_numString);
	if (nRes >= 0)
	{
		strShortDescription = m_strBuffer.substr(lex.m_numString, nRes - lex.m_numString - 1);
		nRes = strShortDescription.find_first_of('/');
		if (nRes > 0)
		{
			if (strShortDescription[nRes - 1] == '/')// so this is a comment
			{
				strShortDescription = strShortDescription.substr(nRes + 1);
			}
		}
		else
		{
			nRes = strShortDescription.find_first_of(')');
			strShortDescription = strShortDescription.substr(0, nRes + 1);
		}
	}

	// get the function name
	wxString csFuncName0 = GETIdentifier(true);
	wxString strFuncName = stringUtils::MakeUpper(csFuncName0);
	int nError = m_numCurrentCompile;

	CPrecompileFunction* pFunction = new CPrecompileFunction(strFuncName, m_pContext);

	pFunction->strRealName = csFuncName0;
	pFunction->strShortDescription = strShortDescription;
	pFunction->nNumberLine = m_numLine;

	// compile the list of formal parameters + register them as local
	GETDelimeter('(');
	while (m_numCurrentCompile + 1 < m_listLexem.size()
		&& !IsNextDelimeter(')'))
	{
		while (m_numCurrentCompile + 1 < m_listLexem.size())
		{
			wxString strType;
			// check for typing
			if (IsTypeVar())
			{
				strType = GetTypeVar();
			}

			ibParamValue variable;

			if (IsNextKeyWord(KEY_VAL))
			{
				GETKeyWord(KEY_VAL);
			}

			wxString strRealName = GETIdentifier(true);
			variable.m_paramName = stringUtils::MakeUpper(strRealName);

			// register this variable as local
			if (m_pContext->FindVariable(variable.m_paramName)) return false;//���� ���������� + ��������� ���������� = ������

			if (IsNextDelimeter('[')) { // this is an array
				GETDelimeter('[');
				GETDelimeter(']');
			}
			else if (IsNextDelimeter('=')) {
				GETDelimeter('=');
				GETConstant();
			}

			ibValue valObject;

			if (!strType.IsEmpty()) {
				try {
					valObject = ibValue::CreateObject(strType);
				}
				catch (...)
				{
				}
			}

			m_pContext->AddVariable(strRealName, strType, false, false, valObject);
			variable.m_paramType = strType;

			pFunction->aParamList.push_back(variable);

			if (IsNextDelimeter(')'))
				break;

			GETDelimeter(',');
		}
	}

	GETDelimeter(')');

	if (IsNextKeyWord(KEY_EXPORT)) {
		GETKeyWord(KEY_EXPORT);
		pFunction->bExport = true;
	}

	// check for typing
	GetContext()->cFunctions[strFuncName] = pFunction;

	int nStartContext = m_listLexem[m_numCurrentCompile].m_numString;

	GetContext()->sCurFuncName = strFuncName;
	CompileBlock();
	GetContext()->sCurFuncName = wxEmptyString;

	if (m_pContext->nReturn == RETURN_FUNCTION) GETKeyWord(KEY_ENDFUNCTION);
	else GETKeyWord(KEY_ENDPROCEDURE);

	if (m_nCurrentPos >= nStartContext && m_nCurrentPos <= m_listLexem[m_numCurrentCompile].m_numString) m_pCurrentContext = m_pContext;
	return true;
}

bool ibPrecompileCode::CompileDeclaration()
{
	wxString strType;
	const ibLexem& lex = PreviewGetLexem();

	if (IDENTIFIER == lex.m_lexType) strType = GetTypeVar(); // typed setting of variables
	else GETKeyWord(KEY_VAR);

	while (m_numCurrentCompile + 1 < m_listLexem.size())
	{
		wxString strName = GETIdentifier(true);

		int nArrayCount = wxNOT_FOUND;
		if (IsNextDelimeter('['))// this is an array declaration
		{
			nArrayCount = 0;
			GETDelimeter('[');
			if (!IsNextDelimeter(']')) {
				ibValue vConst = GETConstant();
				if (vConst.GetType() != ibValueTypes::TYPE_NUMBER || vConst.GetNumber() < 0)
					return false;
				nArrayCount = vConst.GetInteger();
			}
			GETDelimeter(']');
		}

		bool bExport = false;

		if (IsNextKeyWord(KEY_EXPORT)) {
			if (bExport) break;// there was an Export announcement
			GETKeyWord(KEY_EXPORT);
			bExport = true;
		}

		// there was no variable declaration yet - add
		m_pContext->AddVariable(strName, strType, bExport);

		if (IsNextDelimeter('='))// initial initialization - works only inside the text of modules (but not re-declaring procedures and functions)
		{
			if (nArrayCount >= 0) GETDelimeter(',');//Error!
			GETDelimeter('=');
		}

		if (!IsNextDelimeter(','))
			break;

		GETDelimeter(',');
	}

	return true;
}

bool ibPrecompileCode::CompileBlock()
{
	ibLexem lex;

	while ((lex = PreviewGetLexem()).m_lexType != ERRORTYPE)
	{
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
				GETKeyWord(KEY_RETURN);

				if (m_pContext->nReturn == RETURN_NONE)
					return false;

				if (m_pContext->nReturn == RETURN_FUNCTION)//������������ �����-�� ��������
				{
					if (IsNextDelimeter(';')) return false;

					ibParamValue returnValue = GetExpression();

					if (!m_cContext.sCurFuncName.IsEmpty())
					{
						CPrecompileFunction* m_precompile = static_cast<CPrecompileFunction*>(m_cContext.cFunctions[m_cContext.sCurFuncName]);
						m_precompile->RealRetValue = returnValue;
					}
				}
				break;
			}
			case KEY_TRY:
			{
				GETKeyWord(KEY_TRY);
				CompileBlock();

				GETKeyWord(KEY_EXCEPT);
				CompileBlock();
				GETKeyWord(KEY_ENDTRY);

				break;
			}

			case KEY_RAISE: GETKeyWord(KEY_RAISE); break;
			case KEY_CONTINUE: GETKeyWord(KEY_CONTINUE); break;
			case KEY_BREAK: GETKeyWord(KEY_BREAK); break;

			case KEY_FUNCTION:
			case KEY_PROCEDURE:
				GetLexem();
				break;

			default:
				// means the operator bracket ending this block has been encountered (for example, ENDIF, ENDDO, ENDFUNCTION, etc.)
				return true;
			}
		}
		else
		{
			lex = GetLexem();
			if (IDENTIFIER == lex.m_lexType)
			{
				if (IsNextDelimeter(':'))// this is a label task encountered
				{
					// write the address of the label:
					GETDelimeter(':');
				}
				else//����� �������������� ������ �������, �������, ������������� ���������
				{
					m_numCurrentCompile--;// step back

					int nSet = 1;
					ibParamValue variable = GetCurrentIdentifier(nSet);//�������� ����� ����� ��������� (�� ����� '=')
					if (nSet)//���� ���� ������ �����, �.�. ���� '='
					{
						GETDelimeter('=');//��� ������������ ���������� ������-�� ���������

						ibParamValue expression = GetExpression();
						variable.m_paramType = expression.m_paramType;
						variable.m_paramObject = expression.m_paramObject;

						if (m_pContext->FindVariable(variable.m_paramName)) {
							m_pContext->cVariables[variable.m_paramName].m_valObject = expression.m_paramObject;
						}
						else
						{
							m_pContext->AddVariable(variable.m_paramName, expression.m_paramType, false, false, expression.m_paramObject);
						}
					}
				}
			}
			else if (DELIMITER == lex.m_lexType && ';' == lex.m_numData) break;
			else if (ENDPROGRAM == lex.m_lexType) break;
			else return false;
		}
	}//while

	return true;
}

bool ibPrecompileCode::CompileNewObject()
{
	GETKeyWord(KEY_NEW);

	wxString objectName = GETIdentifier(true);
	int nNumber = GetConstString(objectName);

	std::vector <ibParamValue> listParam;

	if (IsNextDelimeter('('))// this is a method call
	{
		GETDelimeter('(');

		while (m_numCurrentCompile + 1 < m_listLexem.size()
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
			GETDelimeter(',');
		}

		GETDelimeter(')');
	}

	return true;
}

bool ibPrecompileCode::CompileGoto()
{
	GETKeyWord(KEY_GOTO);
	return true;
}

bool ibPrecompileCode::CompileIf()
{
	GETKeyWord(KEY_IF);

	GetExpression();

	GETKeyWord(KEY_THEN);
	CompileBlock();

	while (IsNextKeyWord(KEY_ELSEIF))
	{
		// write the output from all checks for the previous block
		GETKeyWord(KEY_ELSEIF);

		GetExpression();

		GETKeyWord(KEY_THEN);
		CompileBlock();
	}

	if (IsNextKeyWord(KEY_ELSE))
	{
		// write the output from all checks for the previous block
		GETKeyWord(KEY_ELSE);
		CompileBlock();
	}

	GETKeyWord(KEY_ENDIF);
	return true;
}

bool ibPrecompileCode::CompileWhile()
{
	GETKeyWord(KEY_WHILE);

	GetExpression();

	GETKeyWord(KEY_DO);
	CompileBlock();
	GETKeyWord(KEY_ENDDO);

	return true;
}

bool ibPrecompileCode::CompileFor()
{
	GETKeyWord(KEY_FOR);

	int nStartPos = m_listLexem[m_numCurrentCompile].m_numString;

	wxString strRealName = GETIdentifier(true);
	//wxString strName = stringUtils::MakeUpper(strRealName);

	ibParamValue variable = GetVariable(strRealName);

	if (IsNextDelimeter('='))
		GETDelimeter('=');

	GetExpression();

	GETKeyWord(KEY_TO);
	ibParamValue VariableTo = GetExpression();

	GETKeyWord(KEY_DO);
	CompileBlock();
	GETKeyWord(KEY_ENDDO);

	if (!(nStartPos < m_nCurrentPos && m_listLexem[m_numCurrentCompile].m_numString > m_nCurrentPos))
		m_pContext->RemoveVariable(strRealName);

	return true;
}

bool ibPrecompileCode::CompileForeach()
{
	GETKeyWord(KEY_FOREACH);

	int nStartPos = m_listLexem[m_numCurrentCompile].m_numString;

	wxString strRealName = GETIdentifier(true);
	wxString strName = stringUtils::MakeUpper(strRealName);

	ibParamValue variable = GetVariable(strRealName);

	GETKeyWord(KEY_IN);

	ibParamValue VariableTo = GetExpression();
	m_pContext->cVariables[strName].m_valObject = VariableTo.m_paramObject.GetIteratorEmpty();

	GETKeyWord(KEY_DO);
	CompileBlock();
	GETKeyWord(KEY_ENDDO);

	if (!(nStartPos < m_nCurrentPos && m_listLexem[m_numCurrentCompile].m_numString > m_nCurrentPos))
		m_pContext->RemoveVariable(strRealName);

	return true;
}

//////////////////////////////////////////////////////////////////////
// Compiling
//////////////////////////////////////////////////////////////////////

/**
 * GetLexem
 * ����������:
 * �������� ��������� ������� �� ������ ���� ���� � �������� ������� ������� ������� �� 1
 * ������������ ��������:
 * 0 ��� ��������� �� �������
 */
ibLexem ibPrecompileCode::GetLexem()
{
	ibLexem lex;
	if (m_numCurrentCompile + 1 < m_listLexem.size()) {
		lex = m_listLexem[++m_numCurrentCompile];
	}
	return lex;
}

//�������� ��������� ������� �� ������ ���� ���� ��� ���������� �������� ������� �������
ibLexem ibPrecompileCode::PreviewGetLexem()
{
	ibLexem lex;
	while (true) {
		lex = GetLexem();
		if (!(lex.m_lexType == DELIMITER && (lex.m_numData == ';' || lex.m_numData == '\n')))
			break;
	}
	m_numCurrentCompile--;
	return lex;
}

/**
 * GETLexem
 * ����������:
 * �������� ��������� ������� �� ������ ���� ���� � �������� ������� ������� ������� �� 1
 * ������������ ��������:
 * ��� (� ������ ������� ��������� ����������)
 */
ibLexem ibPrecompileCode::GETLexem()
{
	const ibLexem& lex = GetLexem();
	if (lex.m_lexType == ERRORTYPE) {}
	return lex;
}
/**
 * GETDELIMITER
 * ����������:
 * �������� ��������� ������� ��� �������� �����������
 * ������������ ��������:
 * ��� (� ������ ������� ��������� ����������)
 */
void ibPrecompileCode::GETDelimeter(const wxUniChar& c)
{
	ibLexem lex = GETLexem();

	if (lex.m_lexType == DELIMITER && c == lex.m_numData)
		m_strLastExpression += c;

	while (!(lex.m_lexType == DELIMITER && c == lex.m_numData)) {
		if (m_numCurrentCompile + 1 >= m_listLexem.size()) break;
		lex = GETLexem();
	}
}
/**
 * IsNextDELIMITER
 * ����������:
 * ��������� �������� �� ��������� ������� ����-���� �������� ������������
 * ������������ ��������:
 * true,false
 */
bool ibPrecompileCode::IsNextDelimeter(const wxUniChar& c)
{
	if (m_numCurrentCompile + 1 < m_listLexem.size()) {
		ibLexem lex = m_listLexem[m_numCurrentCompile + 1];
		if (lex.m_lexType == DELIMITER && c == lex.m_numData)
			return true;
	}

	return false;
}

/**
 * IsNextKeyWord
 * ����������:
 * ��������� �������� �� ��������� ������� ����-���� �������� �������� ������
 * ������������ ��������:
 * true,false
 */
bool ibPrecompileCode::IsNextKeyWord(int nKey)
{
	if (m_numCurrentCompile + 1 < m_listLexem.size()) {
		const ibLexem& lex = m_listLexem[m_numCurrentCompile + 1];
		if (lex.m_lexType == KEYWORD && lex.m_numData == nKey)
			return true;

	}
	return false;
}

/**
 * GETKeyWord
 * �������� ��������� ������� ��� �������� �������� �����
 * ������������ ��������:
 * ��� (� ������ ������� ��������� ����������)
 */
void ibPrecompileCode::GETKeyWord(int nKey)
{
	ibLexem lex = GETLexem();
	while (!(lex.m_lexType == KEYWORD && lex.m_numData == nKey)) {
		if (m_numCurrentCompile + 1 >= m_listLexem.size())
			break;
		lex = GETLexem();
	}
}

/**
 * GETIdentifier
 * �������� ��������� ������� ��� �������� �������� �����
 * ������������ ��������:
 * ������-�������������
 */
wxString ibPrecompileCode::GETIdentifier(bool strRealName)
{
	const ibLexem& lex = GETLexem();
	if (lex.m_lexType != IDENTIFIER) {
		if (strRealName && lex.m_lexType == KEYWORD)
			return lex.m_strData;
		return wxEmptyString;
	}

	if (strRealName) return lex.m_valData.m_sData;
	else return lex.m_strData;
}

/**
 * GETConstant
 * �������� ��������� ������� ��� ���������
 * ������������ ��������:
 * ���������
 */
ibValue ibPrecompileCode::GETConstant()
{
	ibLexem lex;
	int iNumRequire = 0;
	if (IsNextDelimeter('-') || IsNextDelimeter('+')) {
		iNumRequire = 1;
		if (IsNextDelimeter('-'))
			iNumRequire = wxNOT_FOUND;
		lex = GETLexem();
	}

	lex = GETLexem();

	if (iNumRequire) {
		// check that the constant is of numeric type	
		if (lex.m_valData.GetType() != ibValueTypes::TYPE_NUMBER) {}
		// change sign for minus
		if (iNumRequire == wxNOT_FOUND)
			lex.m_valData.m_fData = -lex.m_valData.m_fData;
	}
	return lex.m_valData;
}

// getting the number with a string constant (to determine the method number)
int ibPrecompileCode::GetConstString(const wxString& sMethod)
{
	if (!m_aHashConstList[sMethod])
	{
		m_aHashConstList[sMethod] = m_aHashConstList.size();
	}
	return ((int)m_aHashConstList[sMethod]) - 1;
}

int ibPrecompileCode::IsTypeVar(const wxString& strType)
{
	if (!strType.IsEmpty()) {
		if (ibValue::IsRegisterCtor(strType, ibCtorObjectType::ibCtorObjectType_object_primitive))
			return true;
	}
	else {
		const ibLexem& lex = PreviewGetLexem();
		if (ibValue::IsRegisterCtor(lex.m_strData, ibCtorObjectType::ibCtorObjectType_object_primitive))
			return true;
	}

	return false;
}

wxString ibPrecompileCode::GetTypeVar(const wxString& strType)
{
	if (!strType.IsEmpty()) {
		if (ibValue::IsRegisterCtor(strType, ibCtorObjectType::ibCtorObjectType_object_primitive))
			return strType.Upper();
	}
	else {
		const ibLexem& lex = GETLexem();
		if (ibValue::IsRegisterCtor(lex.m_strData, ibCtorObjectType::ibCtorObjectType_object_primitive)) {
			return stringUtils::MakeUpper(lex.m_strData);
		}
	}

	return wxEmptyString;
}

#define SetOper(x) nOper=x;

/**
 * ���������� ������������� ��������� (��������� ������ �� ����� �������)
 */
ibParamValue ibPrecompileCode::GetExpression(int nPriority)
{
	ibParamValue variable;
	ibLexem lex = GETLexem();

	// first we process Left operators
	if ((lex.m_lexType == KEYWORD && lex.m_numData == KEY_NOT) ||
		(lex.m_lexType == DELIMITER && lex.m_numData == '!')) {
		variable = GetVariable();
		ibParamValue sVariable2 = GetExpression(gs_operPriority['!']);
		variable.m_paramType = wxT("NUMBER");
	}
	else if ((lex.m_lexType == KEYWORD && lex.m_numData == KEY_NEW)) {

		const wxString& objectName = GETIdentifier();
		std::vector <ibParamValue> listParam;


		if (IsNextDelimeter('(')) { // this is a method call	
			GETDelimeter('(');
			while (m_numCurrentCompile + 1 < m_listLexem.size()
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
				GETDelimeter(',');
			}
			GETDelimeter(')');
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
		GETDelimeter(')');
	}
	else if (lex.m_lexType == DELIMITER && lex.m_numData == '?')
	{
		variable = GetVariable();
		//ibByteUnit code;
		//AddLineInfo(code);
		//code.nOper = OPER_ITER;
		/*code.Param1 = variable;*/
		GETDelimeter('(');
		/*code.Param2 =*/ GetExpression();
		GETDelimeter(',');
		/*code.Param3 *=*/ GetExpression();
		GETDelimeter(',');
		/*code.Param4 = */GetExpression();
		GETDelimeter(')');
		//cByteCode.CodeList.push_back(code);
	}
	else if (lex.m_lexType == IDENTIFIER)
	{
		m_numCurrentCompile--;// step back
		int nSet = 0;
		variable = GetCurrentIdentifier(nSet);
	}
	else if (lex.m_lexType == CONSTANT)
	{
		variable = FindConst(lex.m_valData);
	}
	else if ((lex.m_lexType == DELIMITER && lex.m_numData == '+') || (lex.m_lexType == DELIMITER && lex.m_numData == '-'))
	{
		//��������� ������������ ������ // check the admissibility of such assignment
		int nCurPriority = gs_operPriority[lex.m_numData];

		if (nPriority >= nCurPriority)
			return variable; //���������� ���������� ����� (���������� ��������) � ������� ����������� ��������

		//��� ������� ������������� ����� ���������
		if (lex.m_numData == '+')// do nothing (ignore)
		{
			variable = GetExpression(nPriority);
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

	//������ ������������ ������ ���������
	//���� � variable ����� ������ ������ ���������� ���������

MOperation:

	lex = PreviewGetLexem();

	if (lex.m_lexType == DELIMITER && lex.m_numData == ')') return variable;

	//������� ���� �� ����� ��������� ���������� �������� ��� ������ ����������
	if ((lex.m_lexType == DELIMITER && lex.m_numData != ';') || (lex.m_lexType == KEYWORD && lex.m_numData == KEY_AND) || (lex.m_lexType == KEYWORD && lex.m_numData == KEY_OR))
	{
		if (lex.m_numData >= 0 && lex.m_numData <= 255)
		{
			int nCurPriority = gs_operPriority[lex.m_numData]; int nOper = 0;

			if (nPriority < nCurPriority)//���������� ���������� ����� (���������� ��������) � ������� ����������� ��������
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
						GETDelimeter('=');
						SetOper(OPER_GE);
					}
				}
				else if (lex.m_numData == '<')
				{
					SetOper(OPER_LS);
					if (IsNextDelimeter('='))
					{
						GETDelimeter('=');
						SetOper(OPER_LE);
					}
					else if (IsNextDelimeter('>'))
					{
						GETDelimeter('>');
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

				//���. �������� �� ����������� ��������
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
 * index number of the variable where the identifier value lies
*/

ibParamValue ibPrecompileCode::GetCurrentIdentifier(int& nIsSet)
{
	int nPrevSet = nIsSet;

	ibParamValue variable = GetVariable();

	const wxString& strRealName = GETIdentifier(true);
	const wxString& strName = stringUtils::MakeUpper(strRealName);

	const int nStartPos = m_listLexem[m_numCurrentCompile].m_numString;

	if (!m_bCalcValue && (nStartPos + strRealName.length() == m_nCurrentPos ||
		nStartPos + strRealName.length() == m_nCurrentPos - 1)) {
		unsigned int endContext = 0;
		for (unsigned int i = m_numCurrentCompile; i < m_listLexem.size(); i++) {
			if (m_listLexem[i].m_lexType == KEYWORD && (m_listLexem[i].m_numData == KEY_ENDPROCEDURE || m_listLexem[i].m_numData == KEY_ENDFUNCTION))
				endContext = i;
			if (m_listLexem[i].m_lexType == ENDPROGRAM)
				endContext = i;
		}
		nIsSet = 0; m_numCurrentCompile = endContext; return variable;
	}

	m_strLastExpression = strRealName;

	if (IsNextDelimeter('('))// this is a function call
	{
		ibValue valContext;
		if (m_cContext.FindFunction(strRealName, valContext, true))
		{
			std::vector <ibParamValue> listParam;
			GETDelimeter('(');
			while (m_numCurrentCompile + 1 < m_listLexem.size()
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
				GETDelimeter(',');
			}

			GETDelimeter(')');

			const long lMethodNum = valContext.FindMethod(strName);
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
			variable = GetCallFunction(strName);
		}

		if (IsTypeVar(strName)) { // this is a type cast
			variable.m_paramObject = GetTypeVar(strName);
		}

		nIsSet = 0;
	}
	else//��� ����� ����������
	{
		m_strLastParentKeyword = strRealName;

		bool bCheckError = !nPrevSet;

		if (IsNextDelimeter('.'))//��� ���������� �������� ����� ������
			bCheckError = true;

		ibValue valContext;
		if (m_cContext.FindVariable(strRealName, valContext, true)) {
			nIsSet = 0;
			if (IsNextDelimeter('=') && nPrevSet == 1) {
				GETDelimeter('=');
				ibParamValue sParam = GetExpression();
				variable.m_paramObject = sParam.m_paramObject;
				return variable;
			}
			else {
				const long lPropNum = valContext.FindProp(strName);
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
			nIsSet = 1;
			variable = GetVariable(strRealName, bCheckError);
		}
	}

loopLabel:

	if (IsNextDelimeter('['))// this is an array
	{
		GETDelimeter('[');
		ibParamValue sKey = GetExpression();
		GETDelimeter(']');

		//���������� ��� ������ (�.�. ��� ��������� �������� ������� ��� ���������)
		//������:
		//���[10]=12; - Set
		//�=���[10]; - Get
		//���[10][2]=12; - Get,Set

		nIsSet = 0;

		if (IsNextDelimeter('[')) {}//�������� ���� ���������� ������� (��������� ����������� ��������)

		if (IsNextDelimeter('=') && nPrevSet == 1)
		{
			GETDelimeter('=');

			ibParamValue sData = GetExpression();
			return variable;
		}
		else variable = GetVariable();

		goto loopLabel;
	}

	if (IsNextDelimeter('.'))// this is a method call ��� �������� ����������� �������
	{
		wxString sTempExpression = m_strLastExpression;

		GETDelimeter('.');

		wxString strRealMethod = GETIdentifier(true);
		wxString sMethod = stringUtils::MakeUpper(strRealMethod);

		if (m_listLexem[m_numCurrentCompile].m_numString > m_nCurrentPos
			|| m_listLexem[m_numCurrentCompile].m_lexType == KEYWORD) {
			strRealMethod = sMethod = wxEmptyString;
		}

		m_strLastExpression += strRealMethod;

		if (m_listLexem[m_numCurrentCompile].m_numString > (m_nCurrentPos - strRealMethod.length() - 1))
		{
			m_strLastExpression = sTempExpression; nLastPosition = m_numCurrentCompile; m_strLastKeyword = strRealMethod;
			m_valObject = variable.m_paramObject; m_numCurrentCompile = m_listLexem.size() - 1; nIsSet = 0;
			return variable;
		}
		else if (m_listLexem[m_numCurrentCompile].m_lexType == ENDPROGRAM)
		{
			m_strLastExpression = sTempExpression; nLastPosition = m_numCurrentCompile; m_strLastKeyword = strRealMethod;
			m_valObject = variable.m_paramObject; m_numCurrentCompile = m_listLexem.size() - 1; nIsSet = 0;
			return variable;
		}

		if (IsNextDelimeter('('))// this is a method call
		{
			std::vector <ibParamValue> listParam;
			GETDelimeter('(');
			while (m_numCurrentCompile + 1 < m_listLexem.size()
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
				GETDelimeter(',');
			}

			GETDelimeter(')');

			ibValue parentValueObject = variable.m_paramObject;

			variable = GetVariable();

			const long lMethodNum = parentValueObject.FindMethod(sMethod);
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

			nIsSet = 0;
		}
		else//����� - ����� ��������
		{
			//���������� ��� ������ (�.�. ��� ��������� �������� ��� ���������)
			//������:
			//�=���.�����; - Get
			//���.�����=0; - Set
			//���.�����.���=0;  - Get,Set

			nIsSet = 0;

			if (IsNextDelimeter('=') && nPrevSet == 1) {
				GETDelimeter('=');
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
				const long lPropNum = parentValueObject.FindProp(sMethod);
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
 * ��������� ������ ������� ��� ���������
 */
ibParamValue ibPrecompileCode::GetCallFunction(const wxString& strName)
{
	std::vector<ibParamValue> listParam;

	GETDelimeter('(');

	while (m_numCurrentCompile + 1 < m_listLexem.size()
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
		GETDelimeter(',');
	}
	GETDelimeter(')');

	ibValue retValue;

	if (m_cContext.cFunctions.find(strName) != m_cContext.cFunctions.end()) {
		CPrecompileFunction* pDefFunction = m_cContext.cFunctions[strName];
		pDefFunction->aParamList = listParam;
		retValue = pDefFunction->RealRetValue.m_paramObject;
	}

	ibParamValue variable = GetVariable();
	variable.m_paramObject = retValue;
	return variable;
}

/**
 * AddVariable
 * ����������:
 * �������� ��� � ����� ������� ��������� � ����������� ������ ��� ����������� �������������
 */
void ibPrecompileCode::AddVariable(const wxString& strVarName, const ibValue& varVal)
{
	if (strVarName.IsEmpty())
		return;

	// take into account external variables during compilation
	m_cContext.GetVariable(strVarName, false, false, varVal);
}

/**
 * ������� ���������� ����� ���������� �� ���������� �����
 */
ibParamValue ibPrecompileCode::GetVariable(const wxString& strName, bool bCheckError)
{
	return m_pContext->GetVariable(strName, true, bCheckError);
}

/**
 * C������ ����� ������������� ����������
 */
ibParamValue ibPrecompileCode::GetVariable()
{
	const wxString& strVarName = wxString::Format("@%d", m_pContext->nTempVar); //@ - ��� �������� ������������ �����
	ibParamValue variable = m_pContext->GetVariable(strVarName, false);//��������� ���������� ���� ������ � ��������� ���������
	m_pContext->nTempVar++;
	return variable;
}

void ibPrecompileCode::SetVariable(const wxString& strVarName, const ibValue& varVal)
{
	m_pContext->SetVariable(strVarName, varVal);
}

/**
 * �������� ����� ��������� �� ����������� ������ ��������
 * (���� ������ �������� � ������ ���, �� ��� ���������)
 */
ibParamValue ibPrecompileCode::FindConst(ibValue& vData)
{
	ibParamValue Const;
	wxString strType = vData.GetClassName();
	Const.m_paramType = GetTypeVar(strType);
	Const.m_paramObject = vData;
	return Const;
}


#pragma warning(pop)
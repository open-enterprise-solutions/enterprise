////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : autoComplete loader  
////////////////////////////////////////////////////////////////////////////

#include "codeEditor.h"
#include "backend/debugger/debugClient.h"
#include "frontend/docView/docView.h"
#include "backend/wrapper/managerInfo.h"

#include "codeEditorParser.h"
#include "backend/metaCollection/metaModuleObject.h"

void CCodeEditor::AddKeywordFromObject(const CValue& vObject)
{
	if (vObject.GetType() != eValueTypes::TYPE_EMPTY &&
		vObject.GetType() != eValueTypes::TYPE_OLE) {
		for (long i = 0; i < vObject.GetNMethods(); i++) {
			if (vObject.HasRetVal(i)) {
				ac.Append(
					eContentType::eFunction,
					vObject.GetMethodName(i),
					vObject.GetMethodHelper(i)
				);
			}
			else {
				ac.Append(
					eContentType::eProcedure,
					vObject.GetMethodName(i),
					vObject.GetMethodHelper(i)
				);
			}
		}
		for (long i = 0; i < vObject.GetNProps(); i++) {
			ac.Append(
				eContentType::eVariable,
				vObject.GetPropName(i),
				wxEmptyString
			);
		}
		IModuleInfo* moduleInfo = dynamic_cast<IModuleInfo*>(vObject.GetRef());
		if (moduleInfo != nullptr) {
			CMetaObjectModule* computeModuleObject = moduleInfo->GetMetaObject();
			if (computeModuleObject != nullptr) {
				CParserModule cParser;
				if (cParser.ParseModule(computeModuleObject->GetModuleText())) {
					for (auto code : cParser.GetAllContent()) {
						if (code.eType == eExportVariable) {
							ac.Append(
								eContentType::eExportVariable,
								code.strName,
								wxEmptyString
							);
						}
						else if (code.eType == eExportProcedure) {
							ac.Append(
								eContentType::eExportFunction,
								code.strName,
								code.strShortDescription
							);
						}
						else if (code.eType == eExportFunction) {
							ac.Append(
								eContentType::eExportFunction,
								code.strName,
								code.strShortDescription
							);
						}
					}
				}
			}
		}
		IMetaManagerInfo* metaManager = dynamic_cast<IMetaManagerInfo*>(vObject.GetRef());
		if (metaManager != nullptr) {
			CMetaObjectCommonModule* computeManagerModule = metaManager->GetModuleManager();
			if (computeManagerModule != nullptr) {
				CParserModule cParser;
				if (cParser.ParseModule(computeManagerModule->GetModuleText())) {
					for (auto code : cParser.GetAllContent()) {
						if (code.eType == eExportVariable) {
							ac.Append(eContentType::eExportVariable, code.strName, wxEmptyString);
						}
						else if (code.eType == eExportProcedure) {
							ac.Append(eContentType::eExportFunction, code.strName, code.strShortDescription);
						}
						else if (code.eType == eExportFunction) {
							ac.Append(eContentType::eExportFunction, code.strName, code.strShortDescription);
						}
					}
				}
			}
		}
	}
	else if (debugClient->IsEnterLoop()) {
		CPrecompileContext* currContext = m_precompileModule->GetCurrentContext();
		if (currContext && currContext->FindVariable(m_precompileModule->sLastParentKeyword)) {
			ac.Cancel();
			IMetaObject *metaObject = m_document->GetMetaObject(); 
			wxASSERT(metaObject);
			debugClient->EvaluateAutocomplete(
				metaObject->GetFileName(), 
				metaObject->GetDocPath(), 
				m_precompileModule->sLastExpression, 
				m_precompileModule->sLastKeyword, 
				GetCurrentPos()
			);
		}
	}
}

bool CCodeEditor::PrepareExpression(unsigned int currPos, wxString& strExpression, wxString& strKeyWord, wxString& sCurrWord, bool& hPoint)
{
	bool hasPoint = false, hasKeyword = false;
	for (unsigned int i = 0; i < m_precompileModule->m_listLexem.size(); i++)
	{
		if (m_precompileModule->m_listLexem[i].m_nType == IDENTIFIER)
		{
			if (hasPoint) strExpression += m_precompileModule->m_listLexem[i].m_vData.GetString();
			else strExpression = m_precompileModule->m_listLexem[i].m_vData.GetString();

			sCurrWord = m_precompileModule->m_listLexem[i].m_vData.GetString();

			if (i < m_precompileModule->m_listLexem.size() - 1) {
				if (m_precompileModule->m_listLexem[i + 1].m_nNumberString >= currPos)
					break;
				const lexem_t& lex = m_precompileModule->m_listLexem[i + 1];
				if (lex.m_nType == DELIMITER && lex.m_nData == '(')
					strExpression = wxEmptyString;
				if (lex.m_nType == DELIMITER && lex.m_nData == '(' && !hasPoint)
					strKeyWord = sCurrWord;

				if (lex.m_nType != ENDPROGRAM)
					hasPoint = lex.m_nType == DELIMITER && lex.m_nData == '.';
			}

			hasKeyword = hasKeyword ? i == m_precompileModule->m_listLexem.size() - 1 : false;
		}
		else if (m_precompileModule->m_listLexem[i].m_nType == KEYWORD && m_precompileModule->m_listLexem[i].m_nData == KEY_NEW)
		{
			strExpression = wxEmptyString; sCurrWord = wxEmptyString;
			strKeyWord = m_precompileModule->m_listLexem[i].m_vData.GetString(); hasKeyword = true;
		}
		else if (m_precompileModule->m_listLexem[i].m_nType == CONSTANT)
		{
			sCurrWord = m_precompileModule->m_listLexem[i].m_vData.GetString();
			hasKeyword = stringUtils::CompareString(strKeyWord, wxT("type")) || stringUtils::CompareString(strKeyWord, wxT("showCommonForm")) || stringUtils::CompareString(strKeyWord, wxT("getCommonForm")) && !hasPoint;
		}
		else if (m_precompileModule->m_listLexem[i].m_nType == DELIMITER
			&& m_precompileModule->m_listLexem[i].m_nData == '.')
		{
			if (!strExpression.IsEmpty())
				strExpression += '.';

			sCurrWord = wxEmptyString; hasPoint = true; hasKeyword = false;
		}
		else
		{
			if (m_precompileModule->m_listLexem[i].m_nType != ENDPROGRAM) {
				strExpression = wxEmptyString; sCurrWord = wxEmptyString;
			}

			hasKeyword = false;
		}

		if (i < m_precompileModule->m_listLexem.size() - 1 &&
			m_precompileModule->m_listLexem[i + 1].m_nNumberString >= currPos) break;
	}

	hPoint = hasPoint; return hasKeyword;
}

void CCodeEditor::PrepareTooTipExpression(unsigned int currPos, wxString& strExpression, wxString& sCurrWord, bool& hPoint)
{
	bool hasPoint = false;

	for (unsigned int i = 0; i < m_precompileModule->m_listLexem.size(); i++)
	{
		if (m_precompileModule->m_listLexem[i].m_nNumberString > currPos
			&& !hasPoint) break;

		if (m_precompileModule->m_listLexem[i].m_nType == IDENTIFIER)
		{
			if (hasPoint) strExpression += m_precompileModule->m_listLexem[i].m_vData.GetString();
			else strExpression = m_precompileModule->m_listLexem[i].m_vData.GetString();

			sCurrWord = m_precompileModule->m_listLexem[i].m_vData.GetString();

			if (i < m_precompileModule->m_listLexem.size() - 1) {
				const lexem_t& lex = m_precompileModule->m_listLexem[i + 1];
				if (lex.m_nType == DELIMITER && lex.m_nData == '(')
					strExpression = wxEmptyString;
				hasPoint = lex.m_nType == DELIMITER && lex.m_nData == '.';
			}
			else hasPoint = false;
		}
		else if (m_precompileModule->m_listLexem[i].m_nType == DELIMITER
			&& m_precompileModule->m_listLexem[i].m_nData == '.')
		{
			if (!strExpression.IsEmpty())
				strExpression += '.';

			sCurrWord = wxEmptyString; hasPoint = true;
		}
		else
		{
			strExpression = wxEmptyString; sCurrWord = wxEmptyString;
		}
	}

	hPoint = hasPoint;
}

void CCodeEditor::PrepareTABs()
{
	const int curr_position = CCodeEditor::GetCurrentPos();
	const int curr_line =
		CCodeEditor::LineFromPosition(curr_position);

	const int level = CCodeEditor::GetFoldLevel(curr_line);
	int fold_level = level ^ wxSTC_FOLDLEVELBASE_FLAG;

	if ((level & wxSTC_FOLDLEVELHEADER_FLAG) != 0) {
		fold_level = (fold_level ^ wxSTC_FOLDLEVELHEADER_FLAG);
		const int start_line_pos = CCodeEditor::PositionFromLine(curr_line);
		if (start_line_pos + fold_level != curr_position) {
			fold_level = fold_level + 1;
		}
	}
	else if ((level & wxSTC_FOLDLEVELELSE_FLAG) != 0) {
		fold_level = (fold_level ^ wxSTC_FOLDLEVELELSE_FLAG);
		if (fold_level >= 0) {
			const int start_line_pos = CCodeEditor::PositionFromLine(curr_line);
			int currFold = 0; unsigned int toPos = 0;
			std::string strBuffer(CCodeEditor::GetLineRaw(curr_line));
			const int length = curr_position - start_line_pos;
			for (unsigned int i = 0; i < length; i++) {
				if (strBuffer[i] == '\t' || strBuffer[i] == ' ') {
					currFold++; toPos = i + 1;
				}
				else break;
			};
			if (currFold != (fold_level - 1)) {
				(void)strBuffer.replace(0, toPos, fold_level - 1, '\t');
				(void)strBuffer.append(toPos > (fold_level - 1) ? toPos - (fold_level - 1) : 0, ' ');
				CCodeEditor::Replace(
					start_line_pos,
					start_line_pos + strBuffer.length(),
					strBuffer
				);
			}

			if (start_line_pos + fold_level - 1 == curr_position) {
				fold_level = fold_level - 1;
			}
		}
	}
	else if ((level & wxSTC_FOLDLEVELWHITE_FLAG) != 0) {
		fold_level = (fold_level ^ wxSTC_FOLDLEVELWHITE_FLAG) - 1;
		if (fold_level >= 0) {
			const int start_line_pos = CCodeEditor::PositionFromLine(curr_line);
			int currFold = 0; unsigned int toPos = 0;
			std::string strBuffer(CCodeEditor::GetLineRaw(curr_line));
			const int length = curr_position - start_line_pos;
			for (unsigned int i = 0; i < length; i++) {
				if (strBuffer[i] == '\t' || strBuffer[i] == ' ') {
					currFold++; toPos = i + 1;
				}
				else break;
			};
			if (currFold != fold_level) {
				(void)strBuffer.replace(0, toPos, fold_level, '\t');
				(void)strBuffer.append(toPos > fold_level ? toPos - fold_level : 0, ' ');
				CCodeEditor::Replace(
					start_line_pos,
					start_line_pos + strBuffer.length(),
					strBuffer
				);
			}
		}
	}
	std::string strBuffer;
	strBuffer.append("\r\n");
	for (int i = 0; i < fold_level; i++) strBuffer.push_back('\t');
	CCodeEditor::InsertText(curr_position, strBuffer);
	CCodeEditor::GotoLine(CCodeEditor::LineFromPosition(curr_position + strBuffer.length()));
	CCodeEditor::SetEmptySelection(curr_position + strBuffer.length());
}

void CCodeEditor::LoadAutoComplete()
{
	int realPos = GetRealPosition();

	// Find the word start
	int currentPos = GetCurrentPos();

	int wordStartPos = WordStartPosition(currentPos, true);
	int wordEndPos = WordEndPosition(currentPos, false);

	// Display the autocompletion list
	int lenEntered = currentPos - wordStartPos;

	wxString strExpression, strKeyWord, strCurWord; bool hasPoint = true;

	if (ct.Active())
		ct.Cancel();

	if (!PrepareExpression(realPos, strExpression, strKeyWord, strCurWord, hasPoint)) {
		ac.Start(strCurWord, currentPos, lenEntered, TextHeight(GetCurrentLine()));
		if (hasPoint) {
			LoadIntelliList();
		}
		else {
			LoadSysKeyword();
		}
	}
	else {
		ac.Start(strCurWord, currentPos, lenEntered, TextHeight(GetCurrentLine()));
		LoadFromKeyWord(strKeyWord);
	}

	wxPoint position = PointFromPosition(wordStartPos);
	position.y += TextHeight(GetCurrentLine());
	ac.Show(position);
}

void CCodeEditor::LoadToolTip(const wxPoint& pos)
{
	if (debugClient->IsEnterLoop()) {

		int currentPos = GetRealPositionFromPoint(pos);
		wxString strExpression, strCurWord; bool hasPoint = false;
		PrepareTooTipExpression(currentPos, strExpression, strCurWord, hasPoint);

		strExpression.Trim(true).Trim(false);

		if (strExpression.IsEmpty()) {
			SetToolTip(nullptr); return; 
		}

		auto& it = std::find_if(m_expressions.begin(), m_expressions.end(),
			[strExpression](const std::pair<wxString, wxString>& p) {
				return stringUtils::CompareString(strExpression, p.first);
			}
		);

		if (it == m_expressions.end()) {
			IMetaObject* metaObject = m_document->GetMetaObject();
			wxASSERT(metaObject);
			debugClient->EvaluateToolTip(
				metaObject->GetFileName(),
				metaObject->GetDocPath(),
				strExpression
			);
		}
		else {
			SetToolTip(it->second);
		}
	}
}

void CCodeEditor::LoadCallTip()
{
	// Find the word start
	int currentPos = GetRealPosition();

	wxString strExpression, strKeyWord, strCurWord, sDescription; bool hasPoint = true;

	if (!PrepareExpression(currentPos, strExpression, strKeyWord, strCurWord, hasPoint)) {
		if (hasPoint) {
			m_precompileModule->m_nCurrentPos = GetRealPosition();
			//Cобираем текст
			if (m_precompileModule->Compile()) {
				CValue vObject = m_precompileModule->GetComputeValue();
				for (long i = 0; i < vObject.GetNMethods(); i++) {
					wxString sMethod = vObject.GetMethodName(i);
					if (stringUtils::CompareString(sMethod, strCurWord)) {
						sDescription = vObject.GetMethodHelper(i);
						break;
					}
				}

				IModuleInfo* moduleInfo = dynamic_cast<IModuleInfo*>(vObject.GetRef());
				if (moduleInfo) {
					CMetaObjectModule* computeModuleObject = moduleInfo->GetMetaObject();
					if (computeModuleObject) {
						CParserModule cParser;
						if (cParser.ParseModule(computeModuleObject->GetModuleText())) {
							for (auto code : cParser.GetAllContent()) {
								if (code.eType == eExportProcedure || code.eType == eExportFunction) {
									if (stringUtils::CompareString(code.strName, strCurWord)) {
										sDescription = code.strShortDescription;
										break;
									}
								}
							}
						}
					}
				}

				IMetaManagerInfo* metaManager = dynamic_cast<IMetaManagerInfo*>(vObject.GetRef());
				if (metaManager) {
					CMetaObjectCommonModule* computeManagerModule = metaManager->GetModuleManager();
					if (computeManagerModule) {
						CParserModule cParser;
						if (cParser.ParseModule(computeManagerModule->GetModuleText())) {
							for (auto code : cParser.GetAllContent()) {
								if (stringUtils::CompareString(code.strName, strCurWord)) {
									sDescription = code.strShortDescription;
									break;
								}
							}
						}
					}
				}
			}
		}
		else
		{
			//Cобираем текст
			m_precompileModule->m_nCurrentPos = GetRealPosition();

			if (m_precompileModule->Compile()) {
				CPrecompileContext* m_pContext = m_precompileModule->GetContext();
				for (auto function : m_pContext->cFunctions) {
					CPrecompileFunction* m_functionContext = function.second;
					if (stringUtils::CompareString(function.first, strCurWord)) {
						sDescription = m_functionContext->strShortDescription;
						break;
					}
				}
			}
		}
	}
	else {

		if (stringUtils::CompareString(strKeyWord, wxT("new"))) {
			if (CValue::IsRegisterCtor(strExpression)) {
				IAbstractTypeCtor* objectValueAbstract =
					CValue::GetAvailableCtor(strExpression);
				CValue* newObject = objectValueAbstract->CreateObject();
				CValue::CMethodHelper* methodHelper = newObject->GetPMethods();
				if (methodHelper != nullptr) {
					for (long idx = 0; idx < methodHelper->GetNConstructors(); idx++) {
						sDescription = methodHelper->GetConstructorHelper(idx);
					}
				}
				wxDELETE(newObject);
			}
		}
	}

	if (!sDescription.IsEmpty()) {
		ct.Show(currentPos, sDescription);
	}

	m_precompileModule->Clear();
}

void CCodeEditor::LoadSysKeyword()
{
	m_precompileModule->m_nCurrentPos = GetRealPosition();

	for (int i = 0; i < LastKeyWord; i++) {
		ac.Append(eContentType::eVariable,
			s_aKeyWords[i].Eng,
			s_aKeyWords[i].strShortDescription
		);
	}

	if (m_precompileModule->Compile()) {
		CPrecompileContext* m_pContext = m_precompileModule->GetContext();
		for (auto variable : m_pContext->cVariables) {
			CPrecompileVariable m_variable = variable.second;
			if (m_variable.bTempVar)
				continue;
			ac.Append(m_variable.bExport ?
				eContentType::eExportVariable : eContentType::eVariable, m_variable.strRealName, wxEmptyString
			);
		}

		for (auto function : m_pContext->cFunctions) {
			CPrecompileFunction* m_functionContext = function.second;
			if (m_functionContext->m_pContext) {
				if (m_functionContext->m_pContext->nReturn == RETURN_FUNCTION) {
					ac.Append(m_functionContext->bExport ? eContentType::eExportFunction : eContentType::eFunction, m_functionContext->strRealName, m_functionContext->strShortDescription);
				}
				else {
					ac.Append(m_functionContext->bExport ? eContentType::eExportProcedure : eContentType::eProcedure, m_functionContext->strRealName, m_functionContext->strShortDescription);
				}
			}
			else {
				ac.Append(m_functionContext->bExport ? eContentType::eExportFunction : eContentType::eFunction, m_functionContext->strRealName, m_functionContext->strShortDescription);
			}

			if (m_precompileModule->m_pCurrentContext && m_precompileModule->m_pCurrentContext == m_functionContext->m_pContext) {
				for (auto variable : m_precompileModule->m_pCurrentContext->cVariables) {
					CPrecompileVariable m_variable = variable.second;
					if (m_variable.bTempVar)
						continue;
					ac.Append(m_variable.bExport ?
						eContentType::eExportVariable : eContentType::eVariable, m_variable.strRealName, wxEmptyString
					);
				}
			}
		}
	}

	m_precompileModule->Clear();
}

void CCodeEditor::LoadIntelliList()
{
	m_precompileModule->m_nCurrentPos = GetRealPosition();
	m_precompileModule->m_bCalcValue = true;

	//Cобираем текст
	if (m_precompileModule->Compile()) {
		AddKeywordFromObject(m_precompileModule->GetComputeValue());
	}

	m_precompileModule->m_bCalcValue = false;
	m_precompileModule->Clear();
}

#include "backend/metaData.h"
#include "backend/objCtor.h"

void CCodeEditor::LoadFromKeyWord(const wxString& strKeyWord)
{
	if (stringUtils::CompareString(strKeyWord, wxT("new"))) {
		for (auto class_obj : CValue::GetListCtorsByType(eCtorObjectType::eCtorObjectType_object_value))
			ac.Append(eContentType::eVariable, class_obj->GetClassName(), wxEmptyString);
	}
	else if (stringUtils::CompareString(strKeyWord, wxT("type")))
	{
		for (auto class_obj : CValue::GetListCtorsByType(eCtorObjectType::eCtorObjectType_object_primitive))
			ac.Append(eContentType::eVariable, class_obj->GetClassName(), wxEmptyString);

		for (auto class_obj : CValue::GetListCtorsByType(eCtorObjectType::eCtorObjectType_object_value))
			ac.Append(eContentType::eVariable, class_obj->GetClassName(), wxEmptyString);

		for (auto class_obj : CValue::GetListCtorsByType(eCtorObjectType::eCtorObjectType_object_control))
			ac.Append(eContentType::eVariable, class_obj->GetClassName(), wxEmptyString);

		for (auto class_obj : CValue::GetListCtorsByType(eCtorObjectType::eCtorObjectType_object_system))
			ac.Append(eContentType::eVariable, class_obj->GetClassName(), wxEmptyString);

		for (auto class_obj : CValue::GetListCtorsByType(eCtorObjectType::eCtorObjectType_object_enum))
			ac.Append(eContentType::eVariable, class_obj->GetClassName(), wxEmptyString);

		if (m_document) {
			IMetaObject* metaObject = m_document->GetMetaObject();
			if (metaObject) {
				IMetaData* metaData = metaObject->GetMetaData();
				wxASSERT(metaData);

				for (auto class_obj : metaData->GetListCtorsByType(eCtorMetaType::eCtorMetaType_Object))
					ac.Append(eContentType::eVariable, class_obj->GetClassName(), wxEmptyString);
				for (auto class_obj : metaData->GetListCtorsByType(eCtorMetaType::eCtorMetaType_Reference))
					ac.Append(eContentType::eVariable, class_obj->GetClassName(), wxEmptyString);
				for (auto class_obj : metaData->GetListCtorsByType(eCtorMetaType::eCtorMetaType_List))
					ac.Append(eContentType::eVariable, class_obj->GetClassName(), wxEmptyString);
				for (auto class_obj : metaData->GetListCtorsByType(eCtorMetaType::eCtorMetaType_Manager))
					ac.Append(eContentType::eVariable, class_obj->GetClassName(), wxEmptyString);
				for (auto class_obj : metaData->GetListCtorsByType(eCtorMetaType::eCtorMetaType_Selection))
					ac.Append(eContentType::eVariable, class_obj->GetClassName(), wxEmptyString);
			}
		}
	}
	else if (stringUtils::CompareString(strKeyWord, wxT("showCommonForm"))
		|| stringUtils::CompareString(strKeyWord, wxT("getCommonForm")))
	{
		IMetaObject* metaObject = m_document->GetMetaObject();
		wxASSERT(metaObject);
		IMetaData* metaData = metaObject->GetMetaData();
		wxASSERT(metaData);

		for (auto& obj : metaData->GetMetaObject(g_metaCommonFormCLSID))
			ac.Append(eContentType::eVariable, obj->GetName(), wxEmptyString);
	}
}

#include "backend/fileSystem/fs.h"

void CCodeEditor::ShowAutoComplete(const debugAutoCompleteData_t& autoCompleteData)
{
	ac.Cancel();

	for (unsigned int i = 0; i < autoCompleteData.m_arrVar.size(); i++) {
		ac.Append(eContentType::eVariable, autoCompleteData.m_arrVar[i].m_variableName, wxEmptyString);
	}

	for (unsigned int i = 0; i < autoCompleteData.m_arrMeth.size(); i++) {
		ac.Append(autoCompleteData.m_arrMeth[i].m_methodRet ? eContentType::eFunction : eContentType::eProcedure,
			autoCompleteData.m_arrMeth[i].m_methodName, 
			autoCompleteData.m_arrMeth[i].m_methodHelper
		);
	}
	
	ac.Start(autoCompleteData.m_keyword,
		autoCompleteData.m_currentPos,
		autoCompleteData.m_keyword.Length(),
		TextHeight(GetCurrentLine())
	);

	wxPoint position = PointFromPosition(autoCompleteData.m_currentPos);
	position.y += TextHeight(GetCurrentLine());
	ac.Show(position);
}
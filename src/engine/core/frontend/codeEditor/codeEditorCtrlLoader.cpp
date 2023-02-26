////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : autoComplete loader  
////////////////////////////////////////////////////////////////////////////

#include "codeEditorCtrl.h"
#include "core/compiler/definition.h"
#include "core/compiler/debugger/debugClient.h"
#include "frontend/docView/docView.h"
#include "core/common/managerInfo.h"
#include "utils/stringUtils.h"

#include "codeEditorParser.h"
#include "core/metadata/metaObjects/metaModuleObject.h"

void CCodeEditorCtrl::AddKeywordFromObject(const CValue& vObject)
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
		if (moduleInfo != NULL) {
			CMetaModuleObject* computeModuleObject = moduleInfo->GetMetaObject();
			if (computeModuleObject != NULL) {
				CParserModule cParser;
				if (cParser.ParseModule(computeModuleObject->GetModuleText())) {
					for (auto code : cParser.GetAllContent()) {
						if (code.eType == eExportVariable) {
							ac.Append(
								eContentType::eExportVariable,
								code.sName,
								wxEmptyString
							);
						}
						else if (code.eType == eExportProcedure) {
							ac.Append(
								eContentType::eExportFunction,
								code.sName,
								code.sShortDescription
							);
						}
						else if (code.eType == eExportFunction) {
							ac.Append(
								eContentType::eExportFunction,
								code.sName,
								code.sShortDescription
							);
						}
					}
				}
			}
		}
		IMetaManagerInfo* metaManager = dynamic_cast<IMetaManagerInfo*>(vObject.GetRef());
		if (metaManager != NULL) {
			CMetaCommonModuleObject* computeManagerModule = metaManager->GetModuleManager();
			if (computeManagerModule != NULL) {
				CParserModule cParser;
				if (cParser.ParseModule(computeManagerModule->GetModuleText())) {
					for (auto code : cParser.GetAllContent()) {
						if (code.eType == eExportVariable) {
							ac.Append(eContentType::eExportVariable, code.sName, wxEmptyString);
						}
						else if (code.eType == eExportProcedure) {
							ac.Append(eContentType::eExportFunction, code.sName, code.sShortDescription);
						}
						else if (code.eType == eExportFunction) {
							ac.Append(eContentType::eExportFunction, code.sName, code.sShortDescription);
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

bool CCodeEditorCtrl::PrepareExpression(unsigned int currPos, wxString& sExpression, wxString& sKeyWord, wxString& sCurrWord, bool& hPoint)
{
	bool hasPoint = false, hasKeyword = false;
	for (unsigned int i = 0; i < m_precompileModule->m_aLexemList.size(); i++)
	{
		if (m_precompileModule->m_aLexemList[i].m_nType == IDENTIFIER)
		{
			if (hasPoint) sExpression += m_precompileModule->m_aLexemList[i].m_vData.GetString();
			else sExpression = m_precompileModule->m_aLexemList[i].m_vData.GetString();

			sCurrWord = m_precompileModule->m_aLexemList[i].m_vData.GetString();

			if (i < m_precompileModule->m_aLexemList.size() - 1) {
				if (m_precompileModule->m_aLexemList[i + 1].m_nNumberString >= currPos)
					break;
				const lexem_t& lex = m_precompileModule->m_aLexemList[i + 1];
				if (lex.m_nType == DELIMITER && lex.m_nData == '(')
					sExpression = wxEmptyString;
				if (lex.m_nType == DELIMITER && lex.m_nData == '(' && !hasPoint)
					sKeyWord = sCurrWord;

				if (lex.m_nType != ENDPROGRAM)
					hasPoint = lex.m_nType == DELIMITER && lex.m_nData == '.';
			}

			hasKeyword = hasKeyword ? i == m_precompileModule->m_aLexemList.size() - 1 : false;
		}
		else if (m_precompileModule->m_aLexemList[i].m_nType == KEYWORD && m_precompileModule->m_aLexemList[i].m_nData == KEY_NEW)
		{
			sExpression = wxEmptyString; sCurrWord = wxEmptyString;
			sKeyWord = m_precompileModule->m_aLexemList[i].m_vData.GetString(); hasKeyword = true;
		}
		else if (m_precompileModule->m_aLexemList[i].m_nType == CONSTANT)
		{
			sCurrWord = m_precompileModule->m_aLexemList[i].m_vData.GetString();
			hasKeyword = StringUtils::CompareString(sKeyWord, wxT("type")) || StringUtils::CompareString(sKeyWord, wxT("showCommonForm")) || StringUtils::CompareString(sKeyWord, wxT("getCommonForm")) && !hasPoint;
		}
		else if (m_precompileModule->m_aLexemList[i].m_nType == DELIMITER
			&& m_precompileModule->m_aLexemList[i].m_nData == '.')
		{
			if (!sExpression.IsEmpty())
				sExpression += '.';

			sCurrWord = wxEmptyString; hasPoint = true; hasKeyword = false;
		}
		else
		{
			if (m_precompileModule->m_aLexemList[i].m_nType != ENDPROGRAM) {
				sExpression = wxEmptyString; sCurrWord = wxEmptyString;
			}

			hasKeyword = false;
		}

		if (i < m_precompileModule->m_aLexemList.size() - 1 &&
			m_precompileModule->m_aLexemList[i + 1].m_nNumberString >= currPos) break;
	}

	hPoint = hasPoint; return hasKeyword;
}

void CCodeEditorCtrl::PrepareTooTipExpression(unsigned int currPos, wxString& sExpression, wxString& sCurrWord, bool& hPoint)
{
	bool hasPoint = false;

	for (unsigned int i = 0; i < m_precompileModule->m_aLexemList.size(); i++)
	{
		if (m_precompileModule->m_aLexemList[i].m_nNumberString > currPos
			&& !hasPoint) break;

		if (m_precompileModule->m_aLexemList[i].m_nType == IDENTIFIER)
		{
			if (hasPoint) sExpression += m_precompileModule->m_aLexemList[i].m_vData.GetString();
			else sExpression = m_precompileModule->m_aLexemList[i].m_vData.GetString();

			sCurrWord = m_precompileModule->m_aLexemList[i].m_vData.GetString();

			if (i < m_precompileModule->m_aLexemList.size() - 1) {
				const lexem_t& lex = m_precompileModule->m_aLexemList[i + 1];
				if (lex.m_nType == DELIMITER && lex.m_nData == '(')
					sExpression = wxEmptyString;
				hasPoint = lex.m_nType == DELIMITER && lex.m_nData == '.';
			}
			else hasPoint = false;
		}
		else if (m_precompileModule->m_aLexemList[i].m_nType == DELIMITER
			&& m_precompileModule->m_aLexemList[i].m_nData == '.')
		{
			if (!sExpression.IsEmpty())
				sExpression += '.';

			sCurrWord = wxEmptyString; hasPoint = true;
		}
		else
		{
			sExpression = wxEmptyString; sCurrWord = wxEmptyString;
		}
	}

	hPoint = hasPoint;
}

void CCodeEditorCtrl::PrepareTABs()
{
	unsigned int realPos = GetRealPosition(); int nFoldLevels = 0;
	for (unsigned int i = 0; i < m_precompileModule->m_aLexemList.size() - 1; i++) {
		const lexem_t& lex = m_precompileModule->m_aLexemList[i];
		if (lex.m_nNumberString >= realPos) {

			if (
				lex.m_nData == KEY_ENDPROCEDURE
				|| lex.m_nData == KEY_ENDFUNCTION
				|| lex.m_nData == KEY_ENDIF
				|| lex.m_nData == KEY_ENDDO
				|| lex.m_nData == KEY_ENDTRY
				)
			{
				if (lex.m_nNumberString == realPos
					&& nFoldLevels > 0)
					nFoldLevels--;
			}
			else if (
				lex.m_nData == KEY_ELSE
				|| lex.m_nData == KEY_ELSEIF
				|| lex.m_nData == KEY_EXCEPT
				)
			{
				if (lex.m_nNumberString == realPos
					&& nFoldLevels > 0)
					nFoldLevels--;
			}
			break;
		}

		if (lex.m_nType == KEYWORD)
		{
			if (lex.m_nData == KEY_PROCEDURE
				|| lex.m_nData == KEY_FUNCTION
				|| lex.m_nData == KEY_IF
				|| lex.m_nData == KEY_FOR
				|| lex.m_nData == KEY_FOREACH
				|| lex.m_nData == KEY_WHILE
				|| lex.m_nData == KEY_TRY
				)
			{
				nFoldLevels++;
			}
			else if (
				lex.m_nData == KEY_ELSE
				|| lex.m_nData == KEY_ELSEIF
				|| lex.m_nData == KEY_EXCEPT
				)
			{
				if (nFoldLevels > 0)
				{
					nFoldLevels--;

					unsigned int currFold = 0; unsigned int toPos = 0;
					unsigned int currentPos = PositionFromLine(lex.m_nNumberLine);

					{
						std::string m_stringBuffer = GetLineRaw(lex.m_nNumberLine); wxString m_strData;
						for (unsigned int i = 0; i <= lex.m_nNumberLine; i++) { m_strData.Append(GetLine(i)); }
						m_strData = m_strData.Left(lex.m_nNumberString + lex.m_sData.length());

						auto m_csData = m_strData.utf8_str();

						for (unsigned int i = 0; i < m_csData.length() - currentPos; i++)
						{
							if (m_stringBuffer[i] == '\t')
							{
								currFold++; toPos = i + 1;
							}
						}
					}

					if (currFold != nFoldLevels)
					{
						std::string m_stringBuffer;
						for (int i = 0; i < nFoldLevels; i++) m_stringBuffer.push_back('\t');
						Replace(currentPos, currentPos + toPos, m_stringBuffer);
					}

					nFoldLevels++;
				}
			}
			else if (
				lex.m_nData == KEY_ENDPROCEDURE
				|| lex.m_nData == KEY_ENDFUNCTION
				|| lex.m_nData == KEY_ENDIF
				|| lex.m_nData == KEY_ENDDO
				|| lex.m_nData == KEY_ENDTRY
				)
			{
				if (nFoldLevels > 0)
				{
					nFoldLevels--;

					unsigned int currFold = 0; unsigned int toPos = 0;
					unsigned int currentPos = PositionFromLine(lex.m_nNumberLine);

					{
						wxCharBuffer m_stringBuffer = GetLineRaw(lex.m_nNumberLine); wxString m_strData;
						for (unsigned int i = 0; i <= lex.m_nNumberLine; i++) { m_strData.Append(GetLine(i)); }
						m_strData = m_strData.Left(lex.m_nNumberString + lex.m_sData.length());

						auto m_csData = m_strData.utf8_str();

						for (unsigned int i = 0; i < m_csData.length() - currentPos; i++)
						{
							if (m_stringBuffer[i] == '\t')
							{
								currFold++; toPos = i + 1;
							}
						}
					}

					if (currFold != nFoldLevels)
					{
						std::string m_stringBuffer;
						for (int i = 0; i < nFoldLevels; i++) m_stringBuffer.push_back('\t');
						Replace(currentPos, currentPos + toPos, m_stringBuffer);
					}
				}
			}
		}
	}

	if (nFoldLevels > 0)
	{
		std::string m_stringBuffer; unsigned int currentPos = GetCurrentPos();
		m_stringBuffer.append("\r\n");
		for (int i = 0; i < nFoldLevels; i++) m_stringBuffer.push_back('\t');
		InsertText(currentPos, m_stringBuffer);
		GotoLine(LineFromPosition(currentPos + m_stringBuffer.length()));
		SetEmptySelection(currentPos + m_stringBuffer.length());
	}
	else
	{
		std::string m_stringBuffer; unsigned int currentPos = GetCurrentPos();
		m_stringBuffer.append("\r\n");
		InsertText(currentPos, m_stringBuffer);
		GotoLine(LineFromPosition(currentPos + m_stringBuffer.length()));
		SetEmptySelection(currentPos + m_stringBuffer.length());
	}
}

void CCodeEditorCtrl::CalculateFoldLevels()
{
	int nFoldLevels = 0; unsigned int nLastPos = 0;

	for (unsigned int line = 0; line < (unsigned int)GetLineCount(); line++)
	{
		enum eFoldStates
		{
			eEmptyFold = 0,
			eOpenFold,
			eCloseFold
		};

		eFoldStates foldState = eFoldStates::eEmptyFold;

		for (unsigned int i = nLastPos; i < m_precompileModule->m_aLexemList.size() - 1; i++) {
			const lexem_t& lex = m_precompileModule->m_aLexemList[i];
			if (lex.m_nNumberLine < line)
				continue;
			if (lex.m_nNumberLine > line)
				break;

			if (lex.m_nType == KEYWORD) {
				if (lex.m_nData == KEY_PROCEDURE
					|| lex.m_nData == KEY_FUNCTION
					|| lex.m_nData == KEY_IF
					|| lex.m_nData == KEY_FOR
					|| lex.m_nData == KEY_FOREACH
					|| lex.m_nData == KEY_WHILE
					) {
					unsigned int level = wxSTC_FOLDLEVELBASE + (nFoldLevels++);
					SetFoldLevel(lex.m_nNumberLine, level | wxSTC_FOLDLEVELHEADERFLAG);
					foldState = eFoldStates::eOpenFold;
				}
				else if (
					lex.m_nData == KEY_ENDPROCEDURE
					|| lex.m_nData == KEY_ENDFUNCTION
					|| lex.m_nData == KEY_ENDIF
					|| lex.m_nData == KEY_ENDDO
					) {
					SetFoldLevel(line, wxSTC_FOLDLEVELBASE + nFoldLevels);
					if (nFoldLevels > 0) nFoldLevels--;
					foldState = eFoldStates::eCloseFold;
				}
			}
			nLastPos = i;
		}

		if (foldState == eFoldStates::eEmptyFold) {
			SetFoldLevel(line, wxSTC_FOLDLEVELBASE + nFoldLevels);
		}
	}
}

void CCodeEditorCtrl::LoadAutoComplete()
{
	int realPos = GetRealPosition();

	// Find the word start
	int currentPos = GetCurrentPos();

	int wordStartPos = WordStartPosition(currentPos, true);
	int wordEndPos = WordEndPosition(currentPos, false);

	// Display the autocompletion list
	int lenEntered = currentPos - wordStartPos;

	wxString sExpression, sKeyWord, sCurWord; bool hasPoint = true;

	if (ct.Active())
		ct.Cancel();

	if (!PrepareExpression(realPos, sExpression, sKeyWord, sCurWord, hasPoint)) {
		ac.Start(sCurWord, currentPos, lenEntered, TextHeight(GetCurrentLine()));
		if (hasPoint) {
			LoadIntelliList();
		}
		else {
			LoadSysKeyword();
		}
	}
	else {
		ac.Start(sCurWord, currentPos, lenEntered, TextHeight(GetCurrentLine()));
		LoadFromKeyWord(sKeyWord);
	}

	wxPoint position = PointFromPosition(wordStartPos);
	position.y += TextHeight(GetCurrentLine());
	ac.Show(position);
}

void CCodeEditorCtrl::LoadToolTip(const wxPoint& pos)
{
	if (debugClient->IsEnterLoop()) {

		int currentPos = GetRealPositionFromPoint(pos);
		wxString expression, curWord; bool hasPoint = false;
		PrepareTooTipExpression(currentPos, expression, curWord, hasPoint);

		expression.Trim(true).Trim(false);

		if (expression.IsEmpty()) {
			SetToolTip(NULL); return; 
		}

		auto foundedIt = std::find_if(m_expressions.begin(), m_expressions.end(),
			[expression](const std::pair<wxString, wxString>& p) {
				return StringUtils::CompareString(expression, p.first);
			}
		);

		if (foundedIt == m_expressions.end()) {
			IMetaObject* metaObject = m_document->GetMetaObject();
			wxASSERT(metaObject);
			debugClient->EvaluateToolTip(
				metaObject->GetFileName(),
				metaObject->GetDocPath(),
				expression
			);
		}
		else {
			SetToolTip(foundedIt->second);
		}
	}
}

void CCodeEditorCtrl::LoadCallTip()
{
	// Find the word start
	int currentPos = GetRealPosition();

	wxString sExpression, sKeyWord, sCurWord, sDescription; bool hasPoint = true;

	if (!PrepareExpression(currentPos, sExpression, sKeyWord, sCurWord, hasPoint)) {
		if (hasPoint) {
			m_precompileModule->m_nCurrentPos = GetRealPosition();
			//Cобираем текст
			if (m_precompileModule->Compile()) {
				CValue vObject = m_precompileModule->GetComputeValue();
				for (long i = 0; i < vObject.GetNMethods(); i++) {
					wxString sMethod = vObject.GetMethodName(i);
					if (StringUtils::CompareString(sMethod, sCurWord)) {
						sDescription = vObject.GetMethodHelper(i);
						break;
					}
				}

				IModuleInfo* moduleInfo = dynamic_cast<IModuleInfo*>(vObject.GetRef());
				if (moduleInfo) {
					CMetaModuleObject* computeModuleObject = moduleInfo->GetMetaObject();
					if (computeModuleObject) {
						CParserModule cParser;
						if (cParser.ParseModule(computeModuleObject->GetModuleText())) {
							for (auto code : cParser.GetAllContent()) {
								if (code.eType == eExportProcedure || code.eType == eExportFunction) {
									if (StringUtils::CompareString(code.sName, sCurWord)) {
										sDescription = code.sShortDescription;
										break;
									}
								}
							}
						}
					}
				}

				IMetaManagerInfo* metaManager = dynamic_cast<IMetaManagerInfo*>(vObject.GetRef());
				if (metaManager) {
					CMetaCommonModuleObject* computeManagerModule = metaManager->GetModuleManager();
					if (computeManagerModule) {
						CParserModule cParser;
						if (cParser.ParseModule(computeManagerModule->GetModuleText())) {
							for (auto code : cParser.GetAllContent()) {
								if (StringUtils::CompareString(code.sName, sCurWord)) {
									sDescription = code.sShortDescription;
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
					if (StringUtils::CompareString(function.first, sCurWord)) {
						sDescription = m_functionContext->sShortDescription;
						break;
					}
				}
			}
		}
	}
	else {

		if (StringUtils::CompareString(sKeyWord, wxT("new"))) {
			if (CValue::IsRegisterObject(sExpression)) {
				IObjectValueAbstract* objectValueAbstract =
					CValue::GetAvailableObject(sExpression);
				CValue* newObject = objectValueAbstract->CreateObject();
				CValue::CMethodHelper* methodHelper = newObject->GetPMethods();
				if (methodHelper != NULL) {
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

void CCodeEditorCtrl::LoadSysKeyword()
{
	m_precompileModule->m_nCurrentPos = GetRealPosition();

	for (int i = 0; i < LastKeyWord; i++) {
		ac.Append(eContentType::eVariable,
			s_aKeyWords[i].Eng,
			s_aKeyWords[i].sShortDescription
		);
	}

	if (m_precompileModule->Compile()) {
		CPrecompileContext* m_pContext = m_precompileModule->GetContext();
		for (auto variable : m_pContext->cVariables) {
			CPrecompileVariable m_variable = variable.second;
			if (m_variable.bTempVar)
				continue;
			ac.Append(m_variable.bExport ?
				eContentType::eExportVariable : eContentType::eVariable, m_variable.sRealName, wxEmptyString
			);
		}

		for (auto function : m_pContext->cFunctions) {
			CPrecompileFunction* m_functionContext = function.second;
			if (m_functionContext->m_pContext) {
				if (m_functionContext->m_pContext->nReturn == RETURN_FUNCTION) {
					ac.Append(m_functionContext->bExport ? eContentType::eExportFunction : eContentType::eFunction, m_functionContext->sRealName, m_functionContext->sShortDescription);
				}
				else {
					ac.Append(m_functionContext->bExport ? eContentType::eExportProcedure : eContentType::eProcedure, m_functionContext->sRealName, m_functionContext->sShortDescription);
				}
			}
			else {
				ac.Append(m_functionContext->bExport ? eContentType::eExportFunction : eContentType::eFunction, m_functionContext->sRealName, m_functionContext->sShortDescription);
			}

			if (m_precompileModule->m_pCurrentContext && m_precompileModule->m_pCurrentContext == m_functionContext->m_pContext) {
				for (auto variable : m_precompileModule->m_pCurrentContext->cVariables) {
					CPrecompileVariable m_variable = variable.second;
					if (m_variable.bTempVar)
						continue;
					ac.Append(m_variable.bExport ?
						eContentType::eExportVariable : eContentType::eVariable, m_variable.sRealName, wxEmptyString
					);
				}
			}
		}
	}

	m_precompileModule->Clear();
}

void CCodeEditorCtrl::LoadIntelliList()
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

#include "core/metadata/metadata.h"
#include "core/metadata/singleClass.h"

void CCodeEditorCtrl::LoadFromKeyWord(const wxString& keyWord)
{
	if (StringUtils::CompareString(keyWord, wxT("new"))) {
		for (auto className : CValue::GetAvailableObjects(eObjectType::eObjectType_object))
			ac.Append(eContentType::eVariable, className, wxEmptyString);
	}
	else if (StringUtils::CompareString(keyWord, wxT("type")))
	{
		for (auto className : CValue::GetAvailableObjects(eObjectType::eObjectType_simple))
			ac.Append(eContentType::eVariable, className, wxEmptyString);

		for (auto className : CValue::GetAvailableObjects(eObjectType::eObjectType_object))
			ac.Append(eContentType::eVariable, className, wxEmptyString);

		for (auto className : CValue::GetAvailableObjects(eObjectType::eObjectType_object_control))
			ac.Append(eContentType::eVariable, className, wxEmptyString);

		for (auto className : CValue::GetAvailableObjects(eObjectType::eObjectType_object_system))
			ac.Append(eContentType::eVariable, className, wxEmptyString);

		for (auto className : CValue::GetAvailableObjects(eObjectType::eObjectType_enum))
			ac.Append(eContentType::eVariable, className, wxEmptyString);

		if (m_document) {
			IMetaObject* metaObject = m_document->GetMetaObject();
			if (metaObject) {
				IMetadata* metaData = metaObject->GetMetadata();
				wxASSERT(metaData);

				for (auto className : metaData->GetAvailableObjects(eMetaObjectType::enObject))
					ac.Append(eContentType::eVariable, className, wxEmptyString);
				for (auto className : metaData->GetAvailableObjects(eMetaObjectType::enReference))
					ac.Append(eContentType::eVariable, className, wxEmptyString);
				for (auto className : metaData->GetAvailableObjects(eMetaObjectType::enList))
					ac.Append(eContentType::eVariable, className, wxEmptyString);
				for (auto className : metaData->GetAvailableObjects(eMetaObjectType::enManager))
					ac.Append(eContentType::eVariable, className, wxEmptyString);
				for (auto className : metaData->GetAvailableObjects(eMetaObjectType::enSelection))
					ac.Append(eContentType::eVariable, className, wxEmptyString);
			}
		}
	}
	else if (StringUtils::CompareString(keyWord, wxT("showCommonForm"))
		|| StringUtils::CompareString(keyWord, wxT("getCommonForm")))
	{
		IMetaObject* metaObject = m_document->GetMetaObject();
		wxASSERT(metaObject);
		IMetadata* metaData = metaObject->GetMetadata();
		wxASSERT(metaData);

		for (auto obj : metaData->GetMetaObjects(g_metaCommonFormCLSID))
			ac.Append(eContentType::eVariable, obj->GetName(), wxEmptyString);
	}
}

#include "utils/fs/fs.h"

void CCodeEditorCtrl::ShowAutoComp(const debugAutoCompleteData_t& autoCompleteData)
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
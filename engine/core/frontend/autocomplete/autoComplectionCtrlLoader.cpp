////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : autoComplete loader  
////////////////////////////////////////////////////////////////////////////

#include "autocomplectionctrl.h"
#include "compiler/definition.h"
#include "compiler/debugger/debugClient.h"
#include "common/docInfo.h"
#include "common/managerInfo.h"
#include "utils/stringUtils.h"

#include "autoComplectionParser.h"
#include "metadata/metaObjects/metaModuleObject.h"

void CAutocomplectionCtrl::AddKeywordFromObject(const CValue &vObject)
{
	if (vObject.GetType() != eValueTypes::TYPE_EMPTY &&
		vObject.GetType() != eValueTypes::TYPE_OLE) {
		for (unsigned int i = 0; i < vObject.GetNMethods(); i++) ac.Append(eContentType::eFunction, vObject.GetMethodName(i), vObject.GetMethodDescription(i), 356);
		for (unsigned int i = 0; i < vObject.GetNAttributes(); i++) ac.Append(eContentType::eVariable, vObject.GetAttributeName(i), wxEmptyString, 358);
		IModuleInfo *moduleInfo = dynamic_cast<IModuleInfo *>(vObject.GetRef());
		if (moduleInfo) {
			CMetaModuleObject *computeModuleObject = moduleInfo->GetMetaObject();
			if (computeModuleObject) {
				CParserModule cParser;
				if (cParser.ParseModule(computeModuleObject->GetModuleText())) {
					for (auto code : cParser.GetAllContent()) {
						if (code.eType == eExportVariable) {
							ac.Append(eContentType::eExportVariable, code.sName, wxEmptyString, 358);
						}
						else if (code.eType == eExportProcedure) {
							ac.Append(eContentType::eExportFunction, code.sName, code.sShortDescription, 352);
						}
						else if (code.eType == eExportFunction) {
							ac.Append(eContentType::eExportFunction, code.sName, code.sShortDescription, 353);
						}
					}
				}
			}
		}
		IMetaManagerInfo *metaManager = dynamic_cast<IMetaManagerInfo *>(vObject.GetRef());
		if (metaManager) {
			CMetaCommonModuleObject *computeManagerModule = metaManager->GetModuleManager();
			if (computeManagerModule) {
				CParserModule cParser;
				if (cParser.ParseModule(computeManagerModule->GetModuleText())) {
					for (auto code : cParser.GetAllContent()) {
						if (code.eType == eExportVariable) {
							ac.Append(eContentType::eExportVariable, code.sName, wxEmptyString, 358);
						}
						else if (code.eType == eExportProcedure) {
							ac.Append(eContentType::eExportFunction, code.sName, code.sShortDescription, 352);
						}
						else if (code.eType == eExportFunction) {
							ac.Append(eContentType::eExportFunction, code.sName, code.sShortDescription, 353);
						}
					}
				}
			}
		}
	}
	else if (debugClient->IsEnterLoop()) {
		CPrecompileContext *currContext = m_precompileModule->GetCurrentContext();
		if (currContext && currContext->FindVariable(m_precompileModule->sLastParentKeyword)) {
			ac.Cancel();
			debugClient->EvaluateAutocomplete(this, m_precompileModule->sLastExpression, m_precompileModule->sLastKeyword, GetCurrentPos());
		}
	}
}

bool CAutocomplectionCtrl::PrepareExpression(unsigned int currPos, wxString &sExpression, wxString &sKeyWord, wxString &sCurrWord, bool &hasPoint)
{
	bool m_hasPoint = false, m_hasKeyword = false;
	for (unsigned int i = 0; i < m_precompileModule->m_aLexemList.size(); i++)
	{
		if (m_precompileModule->m_aLexemList[i].m_nType == IDENTIFIER)
		{
			if (m_hasPoint) sExpression += m_precompileModule->m_aLexemList[i].m_vData.GetString();
			else sExpression = m_precompileModule->m_aLexemList[i].m_vData.GetString();

			sCurrWord = m_precompileModule->m_aLexemList[i].m_vData.GetString();

			if (i < m_precompileModule->m_aLexemList.size() - 1)
			{
				if (m_precompileModule->m_aLexemList[i + 1].m_nNumberString >= currPos)
					break;

				const CLexem &lex = m_precompileModule->m_aLexemList[i + 1];

				if (lex.m_nType == DELIMITER && lex.m_nData == '(') sExpression = wxEmptyString;
				if (lex.m_nType == DELIMITER && lex.m_nData == '(' && !m_hasPoint) sKeyWord = sCurrWord;

				if (lex.m_nType != ENDPROGRAM) m_hasPoint = lex.m_nType == DELIMITER && lex.m_nData == '.';
			}

			m_hasKeyword = m_hasKeyword ? i == m_precompileModule->m_aLexemList.size() - 1 : false;
		}
		else if (m_precompileModule->m_aLexemList[i].m_nType == KEYWORD && m_precompileModule->m_aLexemList[i].m_nData == KEY_NEW)
		{
			sExpression = wxEmptyString; sCurrWord = wxEmptyString;
			sKeyWord = m_precompileModule->m_aLexemList[i].m_vData.GetString(); m_hasKeyword = true;
		}
		else if (m_precompileModule->m_aLexemList[i].m_nType == CONSTANT)
		{
			sCurrWord = m_precompileModule->m_aLexemList[i].m_vData.GetString();
			m_hasKeyword = StringUtils::CompareString(sKeyWord, wxT("type")) || StringUtils::CompareString(sKeyWord, wxT("showCommonForm")) || StringUtils::CompareString(sKeyWord, wxT("getCommonForm")) && !m_hasPoint;
		}
		else if (m_precompileModule->m_aLexemList[i].m_nType == DELIMITER
			&& m_precompileModule->m_aLexemList[i].m_nData == '.')
		{
			if (!sExpression.IsEmpty())
				sExpression += '.';

			sCurrWord = wxEmptyString; m_hasPoint = true; m_hasKeyword = false;
		}
		else
		{
			if (m_precompileModule->m_aLexemList[i].m_nType != ENDPROGRAM) {
				sExpression = wxEmptyString; sCurrWord = wxEmptyString;
			}

			m_hasKeyword = false;
		}

		if (i < m_precompileModule->m_aLexemList.size() - 1 &&
			m_precompileModule->m_aLexemList[i + 1].m_nNumberString >= currPos) break;
	}

	hasPoint = m_hasPoint; return m_hasKeyword;
}

void CAutocomplectionCtrl::PrepareTooTipExpression(unsigned int currPos, wxString &sExpression, wxString &sCurrWord, bool &hasPoint)
{
	bool m_hasPoint = false;

	for (unsigned int i = 0; i < m_precompileModule->m_aLexemList.size(); i++)
	{
		if (m_precompileModule->m_aLexemList[i].m_nNumberString > currPos
			&& !m_hasPoint) break;

		if (m_precompileModule->m_aLexemList[i].m_nType == IDENTIFIER)
		{
			if (m_hasPoint) sExpression += m_precompileModule->m_aLexemList[i].m_vData.GetString();
			else sExpression = m_precompileModule->m_aLexemList[i].m_vData.GetString();

			sCurrWord = m_precompileModule->m_aLexemList[i].m_vData.GetString();

			if (i < m_precompileModule->m_aLexemList.size() - 1)
			{
				const CLexem &lex = m_precompileModule->m_aLexemList[i + 1];

				if (lex.m_nType == DELIMITER && lex.m_nData == '(') sExpression = wxEmptyString;
				m_hasPoint = lex.m_nType == DELIMITER && lex.m_nData == '.';
			}
			else m_hasPoint = false;
		}
		else if (m_precompileModule->m_aLexemList[i].m_nType == DELIMITER
			&& m_precompileModule->m_aLexemList[i].m_nData == '.')
		{
			if (!sExpression.IsEmpty())
				sExpression += '.';

			sCurrWord = wxEmptyString; m_hasPoint = true;
		}
		else
		{
			sExpression = wxEmptyString; sCurrWord = wxEmptyString;
		}
	}

	hasPoint = m_hasPoint;
}

void CAutocomplectionCtrl::PrepareTABs()
{
	unsigned int realPos = GetRealPosition(); int nFoldLevels = 0;

	for (unsigned int i = 0; i < m_precompileModule->m_aLexemList.size() - 1; i++)
	{
		const CLexem &lex = m_precompileModule->m_aLexemList[i];

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

void CAutocomplectionCtrl::CalculateFoldLevels()
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

		for (unsigned int i = nLastPos; i < m_precompileModule->m_aLexemList.size() - 1; i++)
		{
			const CLexem &lex = m_precompileModule->m_aLexemList[i];

			if (lex.m_nNumberLine < line) continue;
			if (lex.m_nNumberLine > line) break;

			if (lex.m_nType == KEYWORD)
			{
				if (lex.m_nData == KEY_PROCEDURE
					|| lex.m_nData == KEY_FUNCTION
					|| lex.m_nData == KEY_IF
					|| lex.m_nData == KEY_FOR
					|| lex.m_nData == KEY_FOREACH
					|| lex.m_nData == KEY_WHILE
					)
				{
					unsigned int level = wxSTC_FOLDLEVELBASE + (nFoldLevels++);
					SetFoldLevel(lex.m_nNumberLine, level | wxSTC_FOLDLEVELHEADERFLAG);
					foldState = eFoldStates::eOpenFold;
				}
				else if (
					lex.m_nData == KEY_ENDPROCEDURE
					|| lex.m_nData == KEY_ENDFUNCTION
					|| lex.m_nData == KEY_ENDIF
					|| lex.m_nData == KEY_ENDDO
					)
				{
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

void CAutocomplectionCtrl::LoadAutoComplete()
{
	int realPos = GetRealPosition();

	// Find the word start
	int currentPos = GetCurrentPos();

	int wordStartPos = WordStartPosition(currentPos, true);
	int wordEndPos = WordEndPosition(currentPos, false);

	// Display the autocompletion list
	int lenEntered = currentPos - wordStartPos;

	wxString sExpression, sKeyWord, sCurWord; bool m_hasPoint = true;

	if (ct.Active())
		ct.Cancel();

	if (!PrepareExpression(realPos, sExpression, sKeyWord, sCurWord, m_hasPoint)) {
		ac.Start(sCurWord, currentPos, lenEntered, TextHeight(GetCurrentLine()));
		if (m_hasPoint) {
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

void CAutocomplectionCtrl::LoadToolTip(wxPoint pos)
{
	if (debugClient->IsEnterLoop())
	{
		int currentPos = GetRealPositionFromPoint(pos);

		wxString sExpression, sCurWord; bool m_hasPoint = false;

		PrepareTooTipExpression(currentPos, sExpression, sCurWord, m_hasPoint);

		sExpression.Trim(true);
		sExpression.Trim(false);

		if (sExpression.IsEmpty()) SetToolTip(NULL);

		//Обновляем список точек останова
		wxString sModuleName = m_document->GetFilename();

		if (!sExpression.IsEmpty()
			&& m_aExpressions.find(StringUtils::MakeUpper(sExpression)) != m_aExpressions.end()) SetToolTip(m_aExpressions[StringUtils::MakeUpper(sExpression)]);
		else if (!sExpression.IsEmpty()) debugClient->EvaluateToolTip(sModuleName, sExpression);
	}
}

#include "compiler/methods.h"

void CAutocomplectionCtrl::LoadCallTip()
{
	// Find the word start
	int currentPos = GetRealPosition();

	wxString sExpression, sKeyWord, sCurWord, sDescription; bool m_hasPoint = true;

	if (!PrepareExpression(currentPos, sExpression, sKeyWord, sCurWord, m_hasPoint)) {
		if (m_hasPoint) {
			m_precompileModule->m_nCurrentPos = GetRealPosition();
			//Cобираем текст
			if (m_precompileModule->Compile()) {
				CValue vObject = m_precompileModule->GetComputeValue();
				for (unsigned int i = 0; i < vObject.GetNMethods(); i++) {
					wxString sMethod = vObject.GetMethodName(i);
					if (StringUtils::CompareString(sMethod, sCurWord)) {
						sDescription = vObject.GetMethodDescription(i);
						break;
					}
				}

				IModuleInfo *moduleInfo = dynamic_cast<IModuleInfo *>(vObject.GetRef());
				if (moduleInfo) {
					CMetaModuleObject *computeModuleObject = moduleInfo->GetMetaObject();
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

				IMetaManagerInfo *metaManager = dynamic_cast<IMetaManagerInfo *>(vObject.GetRef());
				if (metaManager) {
					CMetaCommonModuleObject *computeManagerModule = metaManager->GetModuleManager();
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
				CPrecompileContext *m_pContext = m_precompileModule->GetContext();
				for (auto function : m_pContext->cFunctions) {
					CPrecompileFunction *m_functionContext = function.second;
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

				IObjectValueAbstract *objectValueAbstract =
					CValue::GetAvailableObject(sExpression);

				CValue *newObject = objectValueAbstract->CreateObject();
				CMethods *methods = newObject->GetPMethods();

				int founded = methods->FindConstructor(sExpression);
				if (founded != wxNOT_FOUND) {
					sDescription = methods->GetConstructorDescription(founded);
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

void CAutocomplectionCtrl::LoadSysKeyword()
{
	m_precompileModule->m_nCurrentPos = GetRealPosition();

	for (int i = 0; i < LastKeyWord; i++)
	{
		wxString sKeyword = aKeyWords[i].Eng;
		wxString sShortDescription = aKeyWords[i].sShortDescription;

		ac.Append(eContentType::eVariable, sKeyword, sShortDescription, 355);
	}

	if (m_precompileModule->Compile())
	{
		CPrecompileContext *m_pContext = m_precompileModule->GetContext();

		for (auto variable : m_pContext->cVariables)
		{
			CPrecompileVariable m_variable = variable.second;
			if (m_variable.bTempVar) continue;

			ac.Append(m_variable.bExport ? eContentType::eExportVariable : eContentType::eVariable, m_variable.sRealName, wxEmptyString, 354);
		}

		for (auto function : m_pContext->cFunctions)
		{
			CPrecompileFunction *m_functionContext = function.second;

			if (m_functionContext->m_pContext) {
				if (m_functionContext->m_pContext->nReturn == RETURN_FUNCTION) {
					ac.Append(m_functionContext->bExport ? eContentType::eExportFunction : eContentType::eFunction, m_functionContext->sRealName, m_functionContext->sShortDescription, 353);
				}
				else {
					ac.Append(m_functionContext->bExport ? eContentType::eExportProcedure : eContentType::eProcedure, m_functionContext->sRealName, m_functionContext->sShortDescription, 352);
				}
			}
			else {
				ac.Append(m_functionContext->bExport ? eContentType::eExportFunction : eContentType::eFunction, m_functionContext->sRealName, m_functionContext->sShortDescription, 353);
			}

			if (m_precompileModule->m_pCurrentContext && m_precompileModule->m_pCurrentContext == m_functionContext->m_pContext)
			{
				for (auto variable : m_precompileModule->m_pCurrentContext->cVariables)
				{
					CPrecompileVariable m_variable = variable.second;
					if (m_variable.bTempVar) continue;

					ac.Append(m_variable.bExport ? eContentType::eExportVariable : eContentType::eVariable, m_variable.sRealName, wxEmptyString, 354);
				}
			}
		}
	}

	m_precompileModule->Clear();
}

void CAutocomplectionCtrl::LoadIntelliList()
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

#include "metadata/metadata.h"
#include "metadata/singleMetaTypes.h"

void CAutocomplectionCtrl::LoadFromKeyWord(const wxString &keyWord)
{
	if (StringUtils::CompareString(keyWord, wxT("new")))
	{
		for (auto className : CValue::GetAvailableObjects(eObjectType::eObjectType_object))
			ac.Append(eContentType::eVariable, className, wxEmptyString, 355);
	}
	else if (StringUtils::CompareString(keyWord, wxT("type")))
	{
		for (auto className : CValue::GetAvailableObjects(eObjectType::eObjectType_simple))
			ac.Append(eContentType::eVariable, className, wxEmptyString, 355);

		for (auto className : CValue::GetAvailableObjects(eObjectType::eObjectType_object))
			ac.Append(eContentType::eVariable, className, wxEmptyString, 355);

		for (auto className : CValue::GetAvailableObjects(eObjectType::eObjectType_object_control))
			ac.Append(eContentType::eVariable, className, wxEmptyString, 355);

		for (auto className : CValue::GetAvailableObjects(eObjectType::eObjectType_object_system))
			ac.Append(eContentType::eVariable, className, wxEmptyString, 355);

		for (auto className : CValue::GetAvailableObjects(eObjectType::eObjectType_enum))
			ac.Append(eContentType::eVariable, className, wxEmptyString, 355);

		if (m_document) {
			IMetaObject *metaObject = m_document->GetMetaObject();
			if (metaObject) {
				IMetadata *metaData = metaObject->GetMetadata();
				wxASSERT(metaData);

				for (auto className : metaData->GetAvailableObjects(eMetaObjectType::enObject))
					ac.Append(eContentType::eVariable, className, wxEmptyString, 355);
				for (auto className : metaData->GetAvailableObjects(eMetaObjectType::enReference))
					ac.Append(eContentType::eVariable, className, wxEmptyString, 355);
				for (auto className : metaData->GetAvailableObjects(eMetaObjectType::enList))
					ac.Append(eContentType::eVariable, className, wxEmptyString, 355);
				for (auto className : metaData->GetAvailableObjects(eMetaObjectType::enManager))
					ac.Append(eContentType::eVariable, className, wxEmptyString, 355);
				for (auto className : metaData->GetAvailableObjects(eMetaObjectType::enSelection))
					ac.Append(eContentType::eVariable, className, wxEmptyString, 355);
			}
		}
	}
	else if (StringUtils::CompareString(keyWord, wxT("showCommonForm"))
		|| StringUtils::CompareString(keyWord, wxT("getCommonForm")))
	{
		IMetaObject *metaObject = m_document->GetMetaObject();
		wxASSERT(metaObject);
		IMetadata *metaData = metaObject->GetMetadata();
		wxASSERT(metaData);

		for (auto obj : metaData->GetMetaObjects(g_metaCommonFormCLSID))
			ac.Append(eContentType::eVariable, obj->GetName(), wxEmptyString, 355);
	}
}

#include "utils/fs/fs.h"

void CAutocomplectionCtrl::ShowAutoCompleteFromDebugger(CMemoryReader &commandReader)
{
	wxString sExpression, sKeyWord;
	commandReader.r_stringZ(sExpression);
	commandReader.r_stringZ(sKeyWord);
	int currPos = commandReader.r_s32();

	ac.sCurrentWord = sKeyWord;

	{
		wxDebugAutocompleteEvent event(EventId::EventId_StartAutocomplete);
		event.SetCurrentPos(currPos);
		event.SetKeyWord(sKeyWord);
		wxPostEvent(this, event);
	}

	unsigned int nCountA = commandReader.r_u32();

	for (unsigned int i = 0; i < nCountA; i++)
	{
		wxString sAttributeName; commandReader.r_stringZ(sAttributeName);
		ac.Append(eContentType::eVariable, sAttributeName, wxEmptyString, 358);
	}

	unsigned int nCountM = commandReader.r_u32();

	for (unsigned int i = 0; i < nCountM; i++)
	{
		wxString sMethodName; commandReader.r_stringZ(sMethodName);
		wxString sMethodDescription; commandReader.r_stringZ(sMethodDescription);
		ac.Append(eContentType::eFunction, sMethodName, sMethodDescription, 353);
	}

	{
		wxDebugAutocompleteEvent event(EventId::EventId_ShowAutocomplete);
		event.SetCurrentPos(currPos);
		event.SetKeyWord(sKeyWord);
		wxPostEvent(this, event);
	}
}
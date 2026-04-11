////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : autoComplete loader  
////////////////////////////////////////////////////////////////////////////

#include "codeEditor.h"
#include "codeEditorParser.h"

#include "backend/debugger/debugClient.h"
#include "backend/metaCollection/partial/commonObject.h"

#include "frontend/docView/docView.h"

void ibCodeEditor::AddKeywordFromObject(const ibValue& vObject)
{
	if (vObject.GetType() != ibValueTypes::TYPE_EMPTY &&
		vObject.GetType() != ibValueTypes::TYPE_OLE) {
		for (long i = 0; i < vObject.GetNMethods(); i++) {
			if (vObject.HasRetVal(i)) {
				m_ac.Append(
					ibContentType::eFunction,
					vObject.GetMethodName(i),
					vObject.GetMethodHelper(i)
				);
			}
			else {
				m_ac.Append(
					ibContentType::eProcedure,
					vObject.GetMethodName(i),
					vObject.GetMethodHelper(i)
				);
			}
		}
		for (long i = 0; i < vObject.GetNProps(); i++) {
			m_ac.Append(
				ibContentType::eVariable,
				vObject.GetPropName(i),
				wxEmptyString
			);
		}
		ibModuleDataObject* moduleDataObject = dynamic_cast<ibModuleDataObject*>(vObject.GetRef());
		if (moduleDataObject != nullptr) {
			const ibValueMetaObjectModuleBase* computeModuleObject = moduleDataObject->GetMetaObject();
			if (computeModuleObject != nullptr) {
				ibParserModule cParser;
				if (cParser.ParseModule(computeModuleObject->GetModuleText())) {
					for (auto code : cParser.GetAllContent()) {
						if (code.eType == eExportVariable) {
							m_ac.Append(
								ibContentType::eExportVariable,
								code.strName,
								wxEmptyString
							);
						}
						else if (code.eType == eExportProcedure) {
							m_ac.Append(
								ibContentType::eExportFunction,
								code.strName,
								code.strShortDescription
							);
						}
						else if (code.eType == eExportFunction) {
							m_ac.Append(
								ibContentType::eExportFunction,
								code.strName,
								code.strShortDescription
							);
						}
					}
				}
			}
		}
		ibValueManagerDataObject* managerDataObject = dynamic_cast<ibValueManagerDataObject*>(vObject.GetRef());
		if (managerDataObject != nullptr) {
			ibValueMetaObjectCommonModule* computeManagerModule = managerDataObject->GetModuleManager();
			if (computeManagerModule != nullptr) {
				ibParserModule cParser;
				if (cParser.ParseModule(computeManagerModule->GetModuleText())) {
					for (auto code : cParser.GetAllContent()) {
						if (code.eType == eExportVariable) {
							m_ac.Append(ibContentType::eExportVariable, code.strName, wxEmptyString);
						}
						else if (code.eType == eExportProcedure) {
							m_ac.Append(ibContentType::eExportFunction, code.strName, code.strShortDescription);
						}
						else if (code.eType == eExportFunction) {
							m_ac.Append(ibContentType::eExportFunction, code.strName, code.strShortDescription);
						}
					}
				}
			}
		}
	}
	else if (debugClient->IsEnterLoop()) {
		CPrecompileContext* currContext = m_precompileModule->GetCurrentContext();
		if (currContext && currContext->FindVariable(m_precompileModule->m_strLastParentKeyword)) {
			m_ac.Cancel();
			const ibValueMetaObject* metaObject = m_document->GetMetaObject();
			wxASSERT(metaObject);
			debugClient->EvaluateAutocomplete(
				metaObject->GetFileName(),
				metaObject->GetDocPath(),
				m_precompileModule->m_strLastExpression,
				m_precompileModule->m_strLastKeyword,
				GetCurrentPos()
			);
		}
	}
}

bool ibCodeEditor::PrepareExpression(unsigned int currPos, wxString& strExpression, wxString& strKeyWord, wxString& sCurrWord, bool& hPoint)
{
	bool hasPoint = false, hasKeyword = false;
	for (unsigned int i = 0; i < m_precompileModule->m_listLexem.size(); i++)
	{
		if (m_precompileModule->m_listLexem[i].m_lexType == IDENTIFIER)
		{
			if (hasPoint) strExpression += m_precompileModule->m_listLexem[i].m_valData.GetString();
			else strExpression = m_precompileModule->m_listLexem[i].m_valData.GetString();

			sCurrWord = m_precompileModule->m_listLexem[i].m_valData.GetString();

			if (i < m_precompileModule->m_listLexem.size() - 1) {
				if (m_precompileModule->m_listLexem[i + 1].m_numString >= currPos)
					break;
				const ibLexem& lex = m_precompileModule->m_listLexem[i + 1];
				if (lex.m_lexType == DELIMITER && lex.m_numData == '(')
					strExpression = wxEmptyString;
				if (lex.m_lexType == DELIMITER && lex.m_numData == '(' && !hasPoint)
					strKeyWord = sCurrWord;

				if (lex.m_lexType != ENDPROGRAM)
					hasPoint = lex.m_lexType == DELIMITER && lex.m_numData == '.';
			}

			hasKeyword = hasKeyword ? i == m_precompileModule->m_listLexem.size() - 1 : false;
		}
		else if (m_precompileModule->m_listLexem[i].m_lexType == KEYWORD && m_precompileModule->m_listLexem[i].m_numData == KEY_NEW)
		{
			strExpression = wxEmptyString; sCurrWord = wxEmptyString;
			strKeyWord = m_precompileModule->m_listLexem[i].m_valData.GetString(); hasKeyword = true;
		}
		else if (m_precompileModule->m_listLexem[i].m_lexType == CONSTANT)
		{
			sCurrWord = m_precompileModule->m_listLexem[i].m_valData.GetString();
			hasKeyword = stringUtils::CompareString(strKeyWord, wxT("type")) || stringUtils::CompareString(strKeyWord, wxT("showCommonForm")) || stringUtils::CompareString(strKeyWord, wxT("getCommonForm")) && !hasPoint;
		}
		else if (m_precompileModule->m_listLexem[i].m_lexType == DELIMITER
			&& m_precompileModule->m_listLexem[i].m_numData == '.')
		{
			if (!strExpression.IsEmpty())
				strExpression += '.';

			sCurrWord = wxEmptyString; hasPoint = true; hasKeyword = false;
		}
		else
		{
			if (m_precompileModule->m_listLexem[i].m_lexType != ENDPROGRAM) {
				strExpression = wxEmptyString; sCurrWord = wxEmptyString;
			}

			hasKeyword = false;
		}

		if (i < m_precompileModule->m_listLexem.size() - 1 &&
			m_precompileModule->m_listLexem[i + 1].m_numString >= currPos) break;
	}

	hPoint = hasPoint; return hasKeyword;
}

void ibCodeEditor::PrepareTooTipExpression(unsigned int currPos, wxString& strExpression, wxString& sCurrWord, bool& hPoint)
{
	bool hasPoint = false;

	for (unsigned int i = 0; i < m_precompileModule->m_listLexem.size(); i++)
	{
		if (m_precompileModule->m_listLexem[i].m_numString > currPos
			&& !hasPoint) break;

		if (m_precompileModule->m_listLexem[i].m_lexType == IDENTIFIER)
		{
			if (hasPoint) strExpression += m_precompileModule->m_listLexem[i].m_valData.GetString();
			else strExpression = m_precompileModule->m_listLexem[i].m_valData.GetString();

			sCurrWord = m_precompileModule->m_listLexem[i].m_valData.GetString();

			if (i < m_precompileModule->m_listLexem.size() - 1) {
				const ibLexem& lex = m_precompileModule->m_listLexem[i + 1];
				if (lex.m_lexType == DELIMITER && lex.m_numData == '(')
					strExpression = wxEmptyString;
				hasPoint = lex.m_lexType == DELIMITER && lex.m_numData == '.';
			}
			else hasPoint = false;
		}
		else if (m_precompileModule->m_listLexem[i].m_lexType == DELIMITER
			&& m_precompileModule->m_listLexem[i].m_numData == '.')
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

void ibCodeEditor::PrepareTABs()
{
	const int curr_position = ibCodeEditor::GetCurrentPos();
	const int curr_line =
		ibCodeEditor::LineFromPosition(curr_position);

	const int start_line_pos = ibCodeEditor::PositionFromLine(curr_line);
	const int level = m_fp.GetFoldMask(curr_line);

	std::string rawBufferLine;

	if (start_line_pos != curr_position)
		rawBufferLine = ibCodeEditor::GetTextRangeRaw(start_line_pos, curr_position);

	int fold_level = level ^ wxSTC_FOLDLEVELBASE_FLAG;
	int current_fold = 0, replace_pos = 0;

	if ((level & wxSTC_FOLDLEVELHEADER_FLAG) != 0) {

		fold_level = (fold_level ^ wxSTC_FOLDLEVELHEADER_FLAG);

		std::string strBuffer = rawBufferLine;

		switch (ibCodeEditor::GetEOLMode())
		{
		case wxSTC_EOL_CRLF:
			strBuffer.erase(std::remove(strBuffer.begin(), strBuffer.end(), '\r'), strBuffer.end());
			strBuffer.erase(std::remove(strBuffer.begin(), strBuffer.end(), '\n'), strBuffer.end());
			break;
		case wxSTC_EOL_CR:
			strBuffer.erase(std::remove(strBuffer.begin(), strBuffer.end(), '\r'), strBuffer.end());
			break;
		default:
			strBuffer.erase(std::remove(strBuffer.begin(), strBuffer.end(), '\n'), strBuffer.end());
			break;
		}

		const int length = curr_position - start_line_pos;
		for (int i = 0; i < length; i++) {
			if (strBuffer[i] == '\t' || strBuffer[i] == ' ') {
				current_fold++; replace_pos = i + 1;
			}
			else break;
		}

		if (current_fold != fold_level) {

			(void)strBuffer.replace(0, replace_pos, fold_level, '\t');

			unsigned short replace_correct =
				(replace_pos > fold_level ? replace_pos - fold_level : 0);
			rawBufferLine.replace(
				0, replace_correct + strBuffer.length(), strBuffer);
		}

		if (start_line_pos + fold_level != curr_position) fold_level++;
	}
	else if ((level & wxSTC_FOLDLEVELELSE_FLAG) != 0) {

		fold_level = (fold_level ^ wxSTC_FOLDLEVELELSE_FLAG);

		if (fold_level >= 0) {

			const int len = ibCodeEditor::LineLength(curr_line);
			if (len > 0) {

				std::string strBuffer = rawBufferLine;

				switch (ibCodeEditor::GetEOLMode())
				{
				case wxSTC_EOL_CRLF:
					strBuffer.erase(std::remove(strBuffer.begin(), strBuffer.end(), '\r'), strBuffer.end());
					strBuffer.erase(std::remove(strBuffer.begin(), strBuffer.end(), '\n'), strBuffer.end());
					break;
				case wxSTC_EOL_CR:
					strBuffer.erase(std::remove(strBuffer.begin(), strBuffer.end(), '\r'), strBuffer.end());
					break;
				default:
					strBuffer.erase(std::remove(strBuffer.begin(), strBuffer.end(), '\n'), strBuffer.end());
					break;
				}

				const int length = curr_position - start_line_pos;

				for (int i = 0; i < length; i++) {
					if (strBuffer[i] == '\t' || strBuffer[i] == ' ') {
						current_fold++; replace_pos = i + 1;
					}
					else break;
				}

				if (current_fold != (fold_level - 1) && fold_level > 0) {

					(void)strBuffer.replace(0, replace_pos, fold_level - 1, '\t');

					unsigned short replace_correct =
						(replace_pos > fold_level - 1 ? replace_pos - fold_level - 1 : 0);
					rawBufferLine.replace(
						0, replace_correct + strBuffer.length(), strBuffer);
				}
				else if (current_fold != 0 && fold_level == 0) {

					(void)strBuffer.replace(0, replace_pos, 0, '\t');

					unsigned short replace_correct =
						(replace_pos > 0 ? replace_pos : 0);

					rawBufferLine.replace(
						0, replace_correct + strBuffer.length(), strBuffer);
				}
			}

			if (start_line_pos + fold_level - 1 == curr_position) fold_level--;
		}
	}
	else if ((level & wxSTC_FOLDLEVELWHITE_FLAG) != 0) {

		fold_level = (fold_level ^ wxSTC_FOLDLEVELWHITE_FLAG) - 1;

		if (fold_level >= 0) {

			const int len = ibCodeEditor::LineLength(curr_line);
			if (len > 0) {

				std::string strBuffer = rawBufferLine;

				switch (ibCodeEditor::GetEOLMode())
				{
				case wxSTC_EOL_CRLF:
					strBuffer.erase(std::remove(strBuffer.begin(), strBuffer.end(), '\r'), strBuffer.end());
					strBuffer.erase(std::remove(strBuffer.begin(), strBuffer.end(), '\n'), strBuffer.end());
					break;
				case wxSTC_EOL_CR:
					strBuffer.erase(std::remove(strBuffer.begin(), strBuffer.end(), '\r'), strBuffer.end());
					break;
				default:
					strBuffer.erase(std::remove(strBuffer.begin(), strBuffer.end(), '\n'), strBuffer.end());
					break;
				}

				const int length = curr_position - start_line_pos;
				for (int i = 0; i < length; i++) {
					if (strBuffer[i] == '\t' || strBuffer[i] == ' ') {
						current_fold++; replace_pos = i + 1;
					}
					else break;
				}

				if (current_fold != fold_level) {

					(void)strBuffer.replace(0, replace_pos, fold_level, '\t');

					unsigned short replace_correct =
						(replace_pos > fold_level ? replace_pos - fold_level : 0);
					rawBufferLine.replace(
						0, replace_correct + strBuffer.length(), strBuffer);
				}
				else if (current_fold != 0 && fold_level == 0) {

					(void)strBuffer.replace(0, replace_pos, 0, '\t');

					unsigned short replace_correct =
						(replace_pos > 0 ? replace_pos : 0);

					rawBufferLine.replace(
						0, replace_correct + strBuffer.length(), strBuffer);
				}
			}
		}
	}
	else if ((level & wxSTC_FOLDLEVELBASE_FLAG) != 0) {

		if (fold_level >= 0) {

			const int len = ibCodeEditor::LineLength(curr_line);

			if (len > 0) {

				std::string strBuffer = rawBufferLine;

				const int length = curr_position - start_line_pos;
				for (int i = 0; i < length; i++) {
					if (strBuffer[i] == '\t' || strBuffer[i] == ' ') {
						current_fold++; replace_pos = i + 1;
					}
					else break;
				}

				if (current_fold != fold_level) {

					(void)strBuffer.replace(0, replace_pos, fold_level, '\t');

					unsigned short replace_correct =
						(replace_pos > fold_level ? replace_pos - fold_level : 0);
					rawBufferLine.replace(
						0, replace_correct + strBuffer.length(), strBuffer);
				}
				else if (current_fold != 0 && fold_level == 0) {

					(void)strBuffer.replace(0, replace_pos, 0, '\t');

					unsigned short replace_correct =
						(replace_pos > 0 ? replace_pos : 0);

					rawBufferLine.replace(
						0, replace_correct + strBuffer.length(), strBuffer);
				}
			}
		}
	}

	switch (ibCodeEditor::GetEOLMode())
	{
	case wxSTC_EOL_CRLF:
		rawBufferLine.push_back('\r');
		rawBufferLine.push_back('\n');
		break;
	case wxSTC_EOL_CR:
		rawBufferLine.push_back('\r');
		break;
	default:
		rawBufferLine.push_back('\n');
		break;
	}

	rawBufferLine.append(fold_level, '\t');

	ibCodeEditor::SetTargetStart((int)start_line_pos);
	ibCodeEditor::SetTargetEnd((int)curr_position);
	ibCodeEditor::ReplaceTargetRaw(rawBufferLine.c_str(), rawBufferLine.length());

	const size_t length = rawBufferLine.length();
	ibCodeEditor::GotoLine(ibCodeEditor::LineFromPosition(start_line_pos + length));
	ibCodeEditor::SetEmptySelection(start_line_pos + length);
}

void ibCodeEditor::LoadAutoComplete()
{
	int realPos = GetRealPosition();

	// Find the word start
	int currentPos = GetCurrentPos();

	int wordStartPos = WordStartPosition(currentPos, true);
	int wordEndPos = WordEndPosition(currentPos, false);

	// Display the autocompletion list
	int lenEntered = currentPos - wordStartPos;

	wxString strExpression, strKeyWord, strCurWord; bool hasPoint = true;

	if (m_ct.Active())
		m_ct.Cancel();

	if (!PrepareExpression(realPos, strExpression, strKeyWord, strCurWord, hasPoint)) {
		m_ac.Start(strCurWord, currentPos, lenEntered, TextHeight(GetCurrentLine()));
		if (hasPoint) {
			LoadIntelliList();
		}
		else {
			LoadSysKeyword();
		}
	}
	else {
		m_ac.Start(strCurWord, currentPos, lenEntered, TextHeight(GetCurrentLine()));
		LoadFromKeyWord(strKeyWord);
	}

	wxPoint position = PointFromPosition(wordStartPos);
	position.y += TextHeight(GetCurrentLine());
	m_ac.Show(position);
}

void ibCodeEditor::LoadToolTip(const wxPoint& pos)
{
	if (debugClient->IsEnterLoop()) {

		int currentPos = GetRealPositionFromPoint(pos);
		wxString strExpression, strCurWord; bool hasPoint = false;
		PrepareTooTipExpression(currentPos, strExpression, strCurWord, hasPoint);

		strExpression.Trim(true).Trim(false);

		if (strExpression.IsEmpty()) {
			SetToolTip(nullptr); return;
		}

		auto it = std::find_if(m_expressions.begin(), m_expressions.end(),
			[strExpression](const std::pair<wxString, wxString>& p) {
				return stringUtils::CompareString(strExpression, p.first);
			}
		);

		if (it == m_expressions.end()) {
			const ibValueMetaObject* metaObject = m_document->GetMetaObject();
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

void ibCodeEditor::LoadCallTip()
{
	// Find the word start
	int currentPos = GetRealPosition();

	wxString strExpression, strKeyWord, strCurWord, sDescription; bool hasPoint = true;

	if (!PrepareExpression(currentPos, strExpression, strKeyWord, strCurWord, hasPoint)) {
		if (hasPoint) {
			m_precompileModule->m_nCurrentPos = GetRealPosition();
			//Cобираем текст
			if (m_precompileModule->Compile()) {
				ibValue vObject = m_precompileModule->GetComputeValue();
				for (long i = 0; i < vObject.GetNMethods(); i++) {
					wxString sMethod = vObject.GetMethodName(i);
					if (stringUtils::CompareString(sMethod, strCurWord)) {
						sDescription = vObject.GetMethodHelper(i);
						break;
					}
				}

				ibModuleDataObject* moduleDataObject = dynamic_cast<ibModuleDataObject*>(vObject.GetRef());
				if (moduleDataObject) {
					const ibValueMetaObjectModuleBase* computeModuleObject = moduleDataObject->GetMetaObject();
					if (computeModuleObject) {
						ibParserModule cParser;
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

				ibValueManagerDataObject* managerDataObject = dynamic_cast<ibValueManagerDataObject*>(vObject.GetRef());
				if (managerDataObject) {
					ibValueMetaObjectCommonModule* computeManagerModule = managerDataObject->GetModuleManager();
					if (computeManagerModule) {
						ibParserModule cParser;
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
					CPrecompileFunction* functionContext = function.second;
					if (stringUtils::CompareString(function.first, strCurWord)) {
						sDescription = functionContext->strShortDescription;
						break;
					}
				}
			}
		}
	}
	else {

		if (stringUtils::CompareString(strKeyWord, wxT("new"))) {
			if (ibValue::IsRegisterCtor(strExpression)) {
				const ibCtorAbstractType* objectValueAbstract =
					ibValue::GetAvailableCtor(strExpression);
				ibValue* newObject = objectValueAbstract->CreateObject();
				ibValue::ibValueMethodHelper* methodHelper = newObject->GetPMethods();
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
		m_ct.Show(currentPos, sDescription);
	}

	m_precompileModule->Clear();
}

void ibCodeEditor::LoadSysKeyword()
{
	m_precompileModule->m_nCurrentPos = GetRealPosition();

	for (int i = 0; i < LastKeyWord; i++) {
		m_ac.Append(ibContentType::eVariable,
			s_listKeyWord[i].m_strKeyWord,
			s_listKeyWord[i].m_strShortDescription
		);
	}

	if (m_precompileModule->Compile()) {
		CPrecompileContext* m_pContext = m_precompileModule->GetContext();
		for (auto variable : m_pContext->cVariables) {
			CPrecompileVariable m_variable = variable.second;
			if (m_variable.bTempVar)
				continue;
			m_ac.Append(m_variable.bExport ?
				ibContentType::eExportVariable : ibContentType::eVariable, m_variable.strRealName, wxEmptyString
			);
		}

		for (auto function : m_pContext->cFunctions) {
			CPrecompileFunction* functionContext = function.second;
			if (functionContext->m_pContext) {
				if (functionContext->m_pContext->nReturn == RETURN_FUNCTION) {
					m_ac.Append(functionContext->bExport ? ibContentType::eExportFunction : ibContentType::eFunction, functionContext->strRealName, functionContext->strShortDescription);
				}
				else {
					m_ac.Append(functionContext->bExport ? ibContentType::eExportProcedure : ibContentType::eProcedure, functionContext->strRealName, functionContext->strShortDescription);
				}
			}
			else {
				m_ac.Append(functionContext->bExport ? ibContentType::eExportFunction : ibContentType::eFunction, functionContext->strRealName, functionContext->strShortDescription);
			}

			if (m_precompileModule->m_pCurrentContext && m_precompileModule->m_pCurrentContext == functionContext->m_pContext) {
				for (auto variable : m_precompileModule->m_pCurrentContext->cVariables) {
					CPrecompileVariable m_variable = variable.second;
					if (m_variable.bTempVar)
						continue;
					m_ac.Append(m_variable.bExport ?
						ibContentType::eExportVariable : ibContentType::eVariable, m_variable.strRealName, wxEmptyString
					);
				}
			}
		}
	}

	m_precompileModule->Clear();
}

void ibCodeEditor::LoadIntelliList()
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

void ibCodeEditor::LoadFromKeyWord(const wxString& strKeyWord)
{
	if (stringUtils::CompareString(strKeyWord, wxT("new"))) {
		for (auto class_obj : ibValue::GetListCtorsByType(ibCtorObjectType::ibCtorObjectType_object_value))
			m_ac.Append(ibContentType::eVariable, class_obj->GetClassName(), wxEmptyString);
	}
	else if (stringUtils::CompareString(strKeyWord, wxT("type")))
	{
		for (auto class_obj : ibValue::GetListCtorsByType(ibCtorObjectType::ibCtorObjectType_object_primitive))
			m_ac.Append(ibContentType::eVariable, class_obj->GetClassName(), wxEmptyString);

		for (auto class_obj : ibValue::GetListCtorsByType(ibCtorObjectType::ibCtorObjectType_object_value))
			m_ac.Append(ibContentType::eVariable, class_obj->GetClassName(), wxEmptyString);

		for (auto class_obj : ibValue::GetListCtorsByType(ibCtorObjectType::ibCtorObjectType_object_control))
			m_ac.Append(ibContentType::eVariable, class_obj->GetClassName(), wxEmptyString);

		for (auto class_obj : ibValue::GetListCtorsByType(ibCtorObjectType::ibCtorObjectType_object_system))
			m_ac.Append(ibContentType::eVariable, class_obj->GetClassName(), wxEmptyString);

		for (auto class_obj : ibValue::GetListCtorsByType(ibCtorObjectType::ibCtorObjectType_object_enum))
			m_ac.Append(ibContentType::eVariable, class_obj->GetClassName(), wxEmptyString);

		if (m_document) {
			const ibValueMetaObject* metaObject = m_document->GetMetaObject();
			if (metaObject) {
				ibMetaData* metaData = metaObject->GetMetaData();
				wxASSERT(metaData);

				for (auto class_obj : metaData->GetListCtorsByType(ibCtorObjectMetaType::ibCtorObjectMetaType_Object))
					m_ac.Append(ibContentType::eVariable, class_obj->GetClassName(), wxEmptyString);
				for (auto class_obj : metaData->GetListCtorsByType(ibCtorObjectMetaType::ibCtorObjectMetaType_Reference))
					m_ac.Append(ibContentType::eVariable, class_obj->GetClassName(), wxEmptyString);
				for (auto class_obj : metaData->GetListCtorsByType(ibCtorObjectMetaType::ibCtorObjectMetaType_List))
					m_ac.Append(ibContentType::eVariable, class_obj->GetClassName(), wxEmptyString);
				for (auto class_obj : metaData->GetListCtorsByType(ibCtorObjectMetaType::ibCtorObjectMetaType_Manager))
					m_ac.Append(ibContentType::eVariable, class_obj->GetClassName(), wxEmptyString);
				for (auto class_obj : metaData->GetListCtorsByType(ibCtorObjectMetaType::ibCtorObjectMetaType_Selection))
					m_ac.Append(ibContentType::eVariable, class_obj->GetClassName(), wxEmptyString);
			}
		}
	}
	else if (stringUtils::CompareString(strKeyWord, wxT("showCommonForm"))
		|| stringUtils::CompareString(strKeyWord, wxT("getCommonForm")))
	{
		const ibValueMetaObject* metaObject = m_document->GetMetaObject();
		wxASSERT(metaObject);
		ibMetaData* metaData = metaObject->GetMetaData();
		wxASSERT(metaData);

		for (const auto object : metaData->GetAnyArrayObject(g_metaCommonFormCLSID))
			m_ac.Append(ibContentType::eVariable, object->GetName(), wxEmptyString);
	}
}

#include "backend/fileSystem/fs.h"

void ibCodeEditor::ShowAutoComplete(const ibDebugAutoCompleteData& autoCompleteData)
{
	m_ac.Cancel();

	for (unsigned int i = 0; i < autoCompleteData.m_arrVar.size(); i++) {
		m_ac.Append(ibContentType::eVariable, autoCompleteData.m_arrVar[i].m_variableName, wxEmptyString);
	}

	for (unsigned int i = 0; i < autoCompleteData.m_arrMeth.size(); i++) {
		m_ac.Append(autoCompleteData.m_arrMeth[i].m_methodRet ? ibContentType::eFunction : ibContentType::eProcedure,
			autoCompleteData.m_arrMeth[i].m_methodName,
			autoCompleteData.m_arrMeth[i].m_methodHelper
		);
	}

	m_ac.Start(autoCompleteData.m_keyword,
		autoCompleteData.m_currentPos,
		autoCompleteData.m_keyword.Length(),
		TextHeight(GetCurrentLine())
	);

	wxPoint position = PointFromPosition(autoCompleteData.m_currentPos);
	position.y += TextHeight(GetCurrentLine());
	m_ac.Show(position);
}
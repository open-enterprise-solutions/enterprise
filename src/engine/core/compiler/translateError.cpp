////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, 2С-team
//	Description : translate error and exception handler 
////////////////////////////////////////////////////////////////////////////

#include "translateError.h"
#include "definition.h"
#include "systemObjects.h"
#include "compileModule.h"
#include "valueArray.h"
#include "valueMap.h"
#include "procUnit.h"

#include "core/metadata/metadata.h"
#include "frontend/mainFrame.h"
#include "frontend/windows/errorDialogWnd.h"
#include "debugger/debugServer.h"
#include "utils/stringUtils.h"

wxString CTranslateError::s_curError;

//////////////////////////////////////////////////////////////////////
//Constant:Список сообщений об ошибках
//////////////////////////////////////////////////////////////////////
static wxString s_errorsStr[] =
{
	"Usage: %s <filename>",
	"Error reading file %s",
	"Error opening file %s",
	"ASSERT: Module system error %s in line %d",
	"ASSERT_VALID: Module system error %s in line %d",
	"System error(out of array) when trying to process an error with a number %d",
	"Symbol expected :\n%s",
	"Word expected :\n%s",
	"Constant boolean expected :\n%s",
	"Constant number expected  :\n%s",
	"Constant string expected :\n%s",
	"Constant date expected :\n%s",
	"%s\nDuplicate identifier %s",//ERROR_IDENTIFIER_DUPLICATE
	"%s\nLabel '%s' not defined",
	"%s\nOne of the keywords is expected!",
	"%s\nModule code expected",
	"%s\nIdentifier expected",//ERROR_IDENTIFIER_DEFINE
	"%s\nRegion name expected",//ERROR_IDENTIFIER_REGION
	"%s\nKeyword or identifier expected",//ERROR_CODE
	"%s\nSymbol expected '%s'",//ERROR_DELIMETER
	"%s\nClosing parenthesis or comma expected",//ERROR_FUNC_DELIMETER
	"%s\nFunction or procedure declaration keyword expected",//ERROR_FUNC_DEFINE
	"%s\nModule cannot have a return statement",//ERROR_RETURN
	"%s\nExpected constant",//ERROR_CONST_DEFINE
	"%s\nAn operator is expected to complete a procedure or function!",//ERROR_ENDFUNC_DEFINE
	"%s\nError writing file: %s",//ERROR_FILE_WRITE
	"%s\nError in expression:\n%s",//ERROR_EXPRESSION
	"%s\nKeyword expected %s",//ERROR_KEYWORD
	"%s\nIdentifier '%s' not found",//ERROR_IDENTIFIER_NOT_FOUND
	"%s\nOperator Break can be used only inside the cycle",//ERROR_USE_BREAK
	"%s\nOperator Continue can be used only inside the cycle",//ERROR_USE_CONTINUE
	"%s\nOperator Return cannot be used outside the procedure or function",//ERROR_USE_RETURN
	"%s\nExpected program operators",//ERROR_USE_BLOCK
	"%s\nExpected expression",//ERROR_EXPRESSION_REQUIRE
	"%s\nProcedure or function not detected (%s)",//ERROR_CALL_FUNCTION
	"%s\nVariable with the specified name is already defined (%s)",//ERROR_DEF_VARIABLE
	"%s\nA procedure or function with the specified name is already defined (%s)",//ERROR_DEF_FUNCTION
	"%s\nToo many parameters",//ERROR_MANY_PARAMS
	"%s\nNot enough parameters",//ERROR_FEW_PARAMS
	"%s\nVar is not found (%s)",//ERROR_VAR_NOT_FOUND
	"%s\nUnexpected program code termination",//ERROR_END_PROGRAM
	"%s\nThis module may contain only definitions of procedures and functions", //ERROR_ONLY_FUNCTION
	"%s\nUse procedure as function (%s)", //ERROR_USE_PROCEDURE_AS_FUNCTION
	"%s\nExpected integer positive sign constant",//ERROR_ARRAY_SIZE_CONST
	"%s\nRe-import the parent module",//ERROR_DUBLICATE_IMPORT
	"%s\nModule not found",//ERROR_MODULE_NOT_FOUND
	"%s\nThe import statement must be at the beginning of the module",//ERROR_USE_IMPORT
	"%s\nFinal conditional compilation statement expected",//ERROR_USE_ENDDEF
	"%s\nA pending region statement is expected",//ERROR_USE_ENDREGION

	"%s\nConstructor not found (%s)",//ERROR_CALL_CONSTRUCTOR

	"%s\nType error define",//ERROR_TYPE_DEF
	"%s\nBad variable type",//ERROR_BAD_TYPE
	"%s\nBad value type",//ERROR_BAD_TYPE_EXPRESSION
	"%s\nVariable must be a numeric type",//ERROR_NUMBER_TYPE

	"%s\nBoolean value expected",//ERROR_BAD_TYPE_EXPRESSION_B
	"%s\nNumeric value expected",//ERROR_BAD_TYPE_EXPRESSION_N
	"%s\nString value expected",//ERROR_BAD_TYPE_EXPRESSION_S
	"%s\nDate value expected",//ERROR_BAD_TYPE_EXPRESSION_D

	"%s\nVariable type does not support this operation",//ERROR_TYPE_OPERATION
};

//////////////////////////////////////////////////////////////////////

inline void ThrowErrorTypeOperation(const wxString& fromType, wxClassInfo* clsInfo)
{
	if (!appData->DesignerMode()) {
		wxString clsName = wxEmptyString;
		if (clsInfo != NULL) {
			const CLASS_ID& clsid = CValue::GetTypeIDByRef(clsInfo);
			if (clsid > 0) {
				clsName = CValue::GetNameObjectFromID(clsid);
			}
		}
		CProcUnit::Raise(); CTranslateError::Error(ERROR_TYPE_OPERATION, fromType, clsName);
	}
}

//////////////////////////////////////////////////////////////////////

bool CTranslateError::s_simpleMode = false;

//////////////////////////////////////////////////////////////////////
// Обработка ошибок
//////////////////////////////////////////////////////////////////////

CTranslateError::CTranslateError(const wxString& sErrorString) : std::exception(sErrorString) {}

#include "core/frontend/docView/docManager.h"
#include "core/metadata/metaObjects/metaModuleObject.h"

void CTranslateError::ProcessError(const byteRaw_t& error, const wxString& descError)
{
	bool isSimpleMode = CTranslateError::IsSimpleMode();

	wxString fileName = error.m_sFileName;
	wxString moduleName = error.m_sModuleName;
	wxString docPath = error.m_sDocPath;

	if (appData->EnterpriseMode() || appData->ServiceMode()) {		
		wxString moduleData;	
		if (!isSimpleMode) {
			if (moduleData.IsEmpty() &&
				fileName.IsEmpty()) {
				CMetaModuleObject* foundedDoc = dynamic_cast<CMetaModuleObject*>(
					metadata->FindByName(error.m_sDocPath)
					);
				wxASSERT(foundedDoc);
				moduleData = foundedDoc->GetModuleText();
			}
			if (moduleData.IsEmpty() &&
				!fileName.IsEmpty()) {
				IMetaDataDocument* const foundedDoc = dynamic_cast<IMetaDataDocument*>(
					docManager->FindDocumentByPath(fileName)
					);
				if (foundedDoc) {
					IMetadata* metaData = foundedDoc->GetMetadata();
					wxASSERT(metaData);
					CMetaModuleObject* foundedDoc = dynamic_cast<CMetaModuleObject*>(
						metaData->FindByName(error.m_sDocPath)
						);
					wxASSERT(foundedDoc);
					moduleData = foundedDoc->GetModuleText();
				}
			}
		}

		const wxString& codeLineError = isSimpleMode ? wxEmptyString :
			CTranslateError::FindErrorCodeLine(moduleData, error.m_nNumberString);

		CTranslateError::ProcessError(fileName,
			moduleName, docPath,
			error.m_nNumberString, isSimpleMode ? error.m_nNumberLine : error.m_nNumberLine + 1,
			codeLineError, wxNOT_FOUND, descError
		);
	}
}

#include "frontend/output/outputWindow.h"

void CTranslateError::ProcessError(const wxString& fileName,
	const wxString& moduleName, const wxString& docPath,
	unsigned int currPos, unsigned int currLine,
	const wxString& codeLineError, int codeError, const wxString& descError)
{
	wxString errorMessage = wxT("{") + moduleName + wxT("(") + wxString::Format("%i", currLine) + wxT(")}: ") +
		(codeError > 0 ?
			CTranslateError::Format(codeError, wxEmptyString, descError) : descError) + '\n' + (s_simpleMode ? wxEmptyString : codeLineError);

	if (s_simpleMode)
		errorMessage.Replace('\n', ' ');

	errorMessage.Trim(true);
	errorMessage.Trim(false);

	if (!appData->DesignerMode()) {

		if (!s_simpleMode) {
			
			//open error dialog
			CErrorDialog* errDlg = new CErrorDialog(wxAuiDocMDIFrame::GetFrame(), wxID_ANY);

			errDlg->SetErrorMessage(
				errorMessage
			);

			int retCode = errDlg->ShowModal();

			//send error to designer
			if (retCode > 1) {
				debugServer->SendErrorToDesigner(
					fileName,
					docPath,
					currLine,
					errorMessage
				);
			}

			//close window
			if (retCode > 2) {
				appDataDestroy();
				std::exit(1);
			}

			outputWindow->OutputError(errorMessage);
		}
	}
	else {
		outputWindow->OutputError(errorMessage,
			fileName, docPath,
			currLine);
	}

	s_curError = errorMessage;

#ifdef _DEBUG
	wxLogDebug(errorMessage);
#endif // !_DEBUG

	throw(new CTranslateError(errorMessage));
}

const wxString& CTranslateError::GetErrorDesc(int codeError)
{
	if (0 <= codeError && codeError < LastError)
		return s_errorsStr[codeError];
	return s_errorsStr[ERROR_SYS1];
}

#if !wxUSE_UTF8_LOCALE_ONLY
wxString CTranslateError::DoFormatWchar(const wxChar* format, ...)
{
	va_list args;
	va_start(args, format);
	return FormatV(format, args);
}

void CTranslateError::DoErrorWchar(const wxChar* format, ...)
{
	va_list args;
	va_start(args, format);
	ErrorV(format, args);
}
#endif

#if wxUSE_UNICODE_UTF8
wxString CTranslateError::DoFormatUtf8(const wxChar* format, ...)
{
	va_list args;
	va_start(args, format);
	wxString strFormat;
	strFormat.FormatV(format, args);
	va_end(args);
	return strFormat;
}

void CTranslateError::DoErrorUtf8(const wxChar* format, ...)
{
	va_list args;
	va_start(args, format);
	ErrorV(format, args);
}
#endif

wxString CTranslateError::FormatV(const wxString& fmt, va_list& list)
{
	wxString errorBuffer = wxString::FormatV(_(fmt), list); va_end(list);
	if (CTranslateError::IsSimpleMode())
		errorBuffer.Replace("\n", " ");
	errorBuffer.Trim(true)
		.Trim(false);
	return errorBuffer;
}

//служебные процедуры обработки ошибок
void CTranslateError::ErrorV(const wxString& fmt, va_list& list)
{
	const wxString& errorBuffer = FormatV(fmt, list);
#ifdef _DEBUG
	wxLogDebug(errorBuffer);
#endif // !_DEBUG
	s_curError = errorBuffer;
	throw(new CTranslateError(errorBuffer));
}

wxString CTranslateError::FindErrorCodeLine(const wxString& buffer, int currPos)
{
	int sizeText = buffer.length();

	if (currPos >= sizeText)
		currPos = sizeText - 1;

	if (currPos < 0)
		currPos = 0;

	int startPos = currPos;
	int endPos = sizeText;

	for (int i = currPos; i > 0; i--) {
		if (buffer[i] == '\n') {
			startPos = i;
			break;
		};
	}

	//ищем конец строки в которой выдается сообщение об ошибке трансляции
	for (int i = currPos + 1; i < sizeText; i++) { 
		if (buffer[i] == '\n') {
			endPos = i; break;
		};
	}

	//определяем номер строки
	unsigned int currLine = 1 + buffer.Left(startPos).Replace('\n', '\n');

	wxString strError = buffer.Mid(startPos, endPos - startPos);
	strError.Replace("\r", "");
	strError.Replace("\t", " ");
	strError.Trim(true).
		Trim(false);

	return strError;
}
////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, 2С-team
//	Description : translate error and exception handler 
////////////////////////////////////////////////////////////////////////////

#include "backend/backend_exception.h"

#include "backend/metadataConfiguration.h"
#include "backend/debugger/debugServer.h"
#include "backend/appData.h"

#include "backend_mainFrame.h"

wxString CBackendException::sm_strError;

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
	"Constant boolean expected:\n%s",
	"Constant number expected:\n%s",
	"Constant string expected:\n%s",
	"Constant date expected:\n%s",
	"Duplicate identifier %s",//ERROR_IDENTIFIER_DUPLICATE
	"Label '%s' not defined",
	"One of the keywords is expected!",
	"Module code expected",
	"Identifier expected",//ERROR_IDENTIFIER_DEFINE
	"Region name expected",//ERROR_IDENTIFIER_REGION
	"Keyword or identifier expected",//ERROR_CODE
	"Symbol expected '%s'",//ERROR_DELIMETER
	"Closing parenthesis or comma expected",//ERROR_FUNC_DELIMETER
	"Function or procedure declaration keyword expected",//ERROR_FUNC_DEFINE
	"Module cannot have a return statement",//ERROR_RETURN
	"Expected constant",//ERROR_CONST_DEFINE
	"An operator is expected to complete a procedure or function!",//ERROR_ENDFUNC_DEFINE
	"Error writing file: %s",//ERROR_FILE_WRITE
	"Error in expression:\n%s",//ERROR_EXPRESSION
	"Keyword expected %s",//ERROR_KEYWORD
	"Identifier '%s' not found",//ERROR_IDENTIFIER_NOT_FOUND
	"Operator Break can be used only inside the cycle",//ERROR_USE_BREAK
	"Operator Continue can be used only inside the cycle",//ERROR_USE_CONTINUE
	"Operator Return cannot be used outside the procedure or function",//ERROR_USE_RETURN
	"Expected program operators",//ERROR_USE_BLOCK
	"Expected expression",//ERROR_EXPRESSION_REQUIRE
	"Procedure or function not detected (%s)",//ERROR_CALL_FUNCTION
	"Variable with the specified name is already defined (%s)",//ERROR_DEF_VARIABLE
	"A procedure or function with the specified name is already defined (%s)",//ERROR_DEF_FUNCTION
	"Too many parameters",//ERROR_MANY_PARAMS
	"Not enough parameters",//ERROR_FEW_PARAMS
	"Var is not found (%s)",//ERROR_VAR_NOT_FOUND
	"Unexpected program code termination",//ERROR_END_PROGRAM
	"This module may contain only definitions of procedures and functions", //ERROR_ONLY_FUNCTION
	"Use procedure as function (%s)", //ERROR_USE_PROCEDURE_AS_FUNCTION
	"Expected integer positive sign constant",//ERROR_ARRAY_SIZE_CONST
	"Re-import the parent module",//ERROR_DUBLICATE_IMPORT
	"Module not found",//ERROR_MODULE_NOT_FOUND
	"The import statement must be at the beginning of the module",//ERROR_USE_IMPORT
	"Final conditional compilation statement expected",//ERROR_USE_ENDDEF
	"A pending region statement is expected",//ERROR_USE_ENDREGION

	"Constructor not found (%s)",//ERROR_CALL_CONSTRUCTOR

	"Type error define",//ERROR_TYPE_DEF
	"Bad variable type",//ERROR_BAD_TYPE
	"Bad value type",//ERROR_BAD_TYPE_EXPRESSION
	"Variable must be a numeric type",//ERROR_NUMBER_TYPE

	"Boolean value expected",//ERROR_BAD_TYPE_EXPRESSION_B
	"Numeric value expected",//ERROR_BAD_TYPE_EXPRESSION_N
	"String value expected",//ERROR_BAD_TYPE_EXPRESSION_S
	"Date value expected",//ERROR_BAD_TYPE_EXPRESSION_D

	"Variable type does not support this operation",//ERROR_TYPE_OPERATION
};

//////////////////////////////////////////////////////////////////////

bool CBackendException::sm_evalMode = false;

//////////////////////////////////////////////////////////////////////
// Обработка ошибок
//////////////////////////////////////////////////////////////////////

CBackendException::CBackendException(const wxString& strErrorString) : std::exception(strErrorString) {}

#include "backend/metaCollection/metaModuleObject.h"

void CBackendException::ProcessError(const CByteUnit& error, const wxString& strErrorDesc)
{
	const bool isEvalMode = CBackendException::IsEvalMode();

	const wxString& strFileName = error.m_strFileName;
	const wxString& strModuleName = error.m_strModuleName;
	const wxString& strDocPath = error.m_strDocPath;

	if (!appData->DesignerMode()) {
		wxString strModuleData;
		if (!isEvalMode) {
			if (strModuleData.IsEmpty() && strFileName.IsEmpty()) {
				CMetaObjectModule* foundedDoc = dynamic_cast<CMetaObjectModule*>(commonMetaData->FindByName(error.m_strDocPath));
				wxASSERT(foundedDoc);
				strModuleData = foundedDoc->GetModuleText();
			}
		
			if (strModuleData.IsEmpty() && !strFileName.IsEmpty()) {
				if (backend_mainFrame != nullptr) {
					IMetaData* metadata = backend_mainFrame->FindMetadataByPath(strFileName);
					wxASSERT(metadata);
					CMetaObjectModule* foundedDoc = dynamic_cast<CMetaObjectModule*>(metadata->FindByName(error.m_strDocPath));
					wxASSERT(foundedDoc);
					strModuleData = foundedDoc->GetModuleText();
				}
			}
		}

		const wxString& strCodeLineError = isEvalMode ? wxEmptyString :
			CBackendException::FindErrorCodeLine(strModuleData, error.m_nNumberString);

		CBackendException::ProcessError(strFileName,
			strModuleName, strDocPath,
			error.m_nNumberString, isEvalMode ? error.m_nNumberLine : error.m_nNumberLine + 1,
			strCodeLineError, wxNOT_FOUND, strErrorDesc
		);
	}
}

void CBackendException::ProcessError(const wxString& strFileName,
	const wxString& strModuleName, const wxString& strDocPath,
	const unsigned int currPos, const unsigned int currLine,
	const wxString& strCodeLineError, const int codeError, const wxString& strErrorDesc)
{
	wxString strErrorMessage;

	strErrorMessage += wxT("{") + strModuleName + wxT("(") + wxString::Format("%i", currLine) + wxT(")}: ");
	strErrorMessage += (codeError > 0 ? CBackendException::Format(codeError, strErrorDesc) : strErrorDesc) + wxT("\n");
	strErrorMessage += (sm_evalMode ? wxEmptyString : strCodeLineError);

	if (sm_evalMode) strErrorMessage.Replace('\n', ' ');

	stringUtils::TrimAll(strErrorMessage);

	if (!sm_evalMode && backend_mainFrame != nullptr) {
		backend_mainFrame->BackendError(
			strFileName,
			strDocPath,
			currLine,
			strErrorMessage
		);
	}

	sm_strError = strErrorMessage;

#ifdef DEBUG
	wxLogDebug(strErrorMessage);
#endif // !DEBUG

	throw(new CBackendException(strErrorMessage));
}

const wxString& CBackendException::GetErrorDesc(int codeError)
{
	if (0 <= codeError && codeError < LastError)
		return s_errorsStr[codeError];
	return s_errorsStr[ERROR_SYS1];
}

#if !wxUSE_UTF8_LOCALE_ONLY
wxString CBackendException::DoFormatWchar(const wxChar* format, ...)
{
	va_list args;
	va_start(args, format);
	return FormatV(format, args);
}

void CBackendException::DoErrorWchar(const wxChar* format, ...)
{
	va_list args;
	va_start(args, format);
	ErrorV(format, args);
}
#endif

#if wxUSE_UNICODE_UTF8
wxString CBackendException::DoFormatUtf8(const wxChar* format, ...)
{
	va_list args;
	va_start(args, format);
	wxString strFormat;
	strFormat.FormatV(format, args);
	va_end(args);
	return strFormat;
}

void CBackendException::DoErrorUtf8(const wxChar* format, ...)
{
	va_list args;
	va_start(args, format);
	ErrorV(format, args);
}
#endif

wxString CBackendException::FormatV(const wxString& fmt, va_list& list)
{
	wxString strErrorBuffer = wxString::FormatV(_(fmt), list); va_end(list);
	if (CBackendException::IsEvalMode())
		strErrorBuffer.Replace('\n', " ");
	stringUtils::TrimAll(strErrorBuffer);
	return strErrorBuffer;
}

//служебные процедуры обработки ошибок
void CBackendException::ErrorV(const wxString& fmt, va_list& list)
{
	const wxString& errorBuffer = FormatV(fmt, list);
#ifdef DEBUG
	wxLogDebug(errorBuffer);
#endif // !DEBUG
	sm_strError = errorBuffer;
	throw(new CBackendException(errorBuffer));
}

wxString CBackendException::FindErrorCodeLine(const wxString& strBuffer, int currPos)
{
	const int sizeText = strBuffer.length();

	if (currPos >= sizeText)
		currPos = sizeText - 1;

	if (currPos < 0)
		currPos = 0;

	int startPos = 0;
	int endPos = sizeText;

	for (int i = currPos; i > 0; i--) {
		if (strBuffer[i] == '\n') {
			startPos = i + 1;
			break;
		};
	}

	//ищем конец строки в которой выдается сообщение об ошибке трансляции
	for (int i = currPos; i < sizeText; i++) {
		if (strBuffer[i] == '\n') {
			endPos = i; break;
		};
	}

	//определяем номер строки
	unsigned int currLine = 1 + strBuffer.Left(startPos).Replace('\n', '\n');

	wxString strError = wxString::Format("%s <<?>> %s", strBuffer.Mid(startPos, currPos - startPos), strBuffer.Mid(currPos, endPos - currPos));
	strError.Replace('\r', '\0');
	strError.Replace('\t', ' ');

	stringUtils::TrimAll(strError);

	return strError;
}
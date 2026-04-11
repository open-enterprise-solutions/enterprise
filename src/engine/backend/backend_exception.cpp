////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, 2�-team
//	Description : translate error and exception handler 
////////////////////////////////////////////////////////////////////////////

#include "backend/backend_exception.h"

#include "backend/metadataConfiguration.h"
#include "backend/debugger/debugServer.h"
#include "backend/appData.h"

#include "backend_mainFrame.h"

wxString ibBackendException::ms_strError;

//////////////////////////////////////////////////////////////////////
//					List of error messages							//
//////////////////////////////////////////////////////////////////////
static wxString gs_listErrorString[] =
{
	_("Usage: %s <filename>"),
	_("Error reading file %s"),
	_("Error opening file %s"),
	_("ASSERT: Module system error %s in line %d"),
	_("ASSERT_VALID: Module system error %s in line %d"),
	_("System error(out of array) when trying to process an error with a number %d"),
	_("Symbol expected :\n%s"),
	_("Word expected :\n%s"),
	_("Constant boolean expected:\n%s"),
	_("Constant number expected:\n%s"),
	_("Constant string expected:\n%s"),
	_("Constant date expected:\n%s"),
	_("Duplicate identifier %s"),//ERROR_IDENTIFIER_DUPLICATE
	_("Label '%s' not defined"),
	_("One of the keywords is expected!"),
	_("Module code expected"),
	_("Identifier expected"),//ERROR_IDENTIFIER_DEFINE
	_("Region name expected"),//ERROR_IDENTIFIER_REGION
	_("Keyword or identifier expected"),//ERROR_CODE
	_("Symbol expected '%s'"),//ERROR_DELIMETER
	_("Closing parenthesis or comma expected"),//ERROR_FUNC_DELIMETER
	_("Function or procedure declaration keyword expected"),//ERROR_FUNC_DEFINE
	_("Module cannot have a return statement"),//ERROR_RETURN
	_("Expected constant"),//ERROR_CONST_DEFINE
	_("An operator is expected to complete a procedure or function!"),//ERROR_ENDFUNC_DEFINE
	_("Error writing file: %s"),//ERROR_FILE_WRITE
	_("Error in expression:\n%s"),//ERROR_EXPRESSION
	_("Keyword expected %s"),//ERROR_KEYWORD
	_("Identifier '%s' not found"),//ERROR_IDENTIFIER_NOT_FOUND
	_("Operator Break can be used only inside the cycle"),//ERROR_USE_BREAK
	_("Operator Continue can be used only inside the cycle"),//ERROR_USE_CONTINUE
	_("Operator Return cannot be used outside the procedure or function"),//ERROR_USE_RETURN
	_("Expected program operators"),//ERROR_USE_BLOCK
	_("Expected expression"),//ERROR_EXPRESSION_REQUIRE
	_("Procedure or function not detected (%s)"),//ERROR_CALL_FUNCTION
	_("Variable with the specified name is already defined (%s)"),//ERROR_DEF_VARIABLE
	_("A procedure or function with the specified name is already defined (%s)"),//ERROR_DEF_FUNCTION
	_("Too many parameters"),//ERROR_MANY_PARAMS
	_("Not enough parameters"),//ERROR_FEW_PARAMS
	_("Var is not found (%s)"),//ERROR_VAR_NOT_FOUND
	_("Unexpected program code termination"),//ERROR_END_PROGRAM
	_("This module may contain only definitions of procedures and functions"), //ERROR_ONLY_FUNCTION
	_("Use procedure as function (%s)"), //ERROR_USE_PROCEDURE_AS_FUNCTION
	_("Expected integer positive sign constant"),//ERROR_ARRAY_SIZE_CONST
	_("Re-import the parent module"),//ERROR_DUBLICATE_IMPORT
	_("Module not found"),//ERROR_MODULE_NOT_FOUND
	_("The import statement must be at the beginning of the module"),//ERROR_USE_IMPORT
	_("Final conditional compilation statement expected"),//ERROR_USE_ENDDEF
	_("A pending region statement is expected"),//ERROR_USE_ENDREGION

	_("Constructor not found (%s)"),//ERROR_CALL_CONSTRUCTOR

	_("Type error define"),//ERROR_TYPE_DEF
	_("Bad variable type"),//ERROR_BAD_TYPE
	_("Bad value type"),//ERROR_BAD_TYPE_EXPRESSION
	_("Variable must be a numeric type"),//ERROR_NUMBER_TYPE

	_("Boolean value expected"),//ERROR_BAD_TYPE_EXPRESSION_B
	_("Numeric value expected"),//ERROR_BAD_TYPE_EXPRESSION_N
	_("String value expected"),//ERROR_BAD_TYPE_EXPRESSION_S
	_("Date value expected"),//ERROR_BAD_TYPE_EXPRESSION_D

	_("Variable type does not support this operation"),//ERROR_TYPE_OPERATION
};

//////////////////////////////////////////////////////////////////////

static bool gs_evalMode = false, gs_processBackendError = false;

//////////////////////////////////////////////////////////////////////
// Error handling
//////////////////////////////////////////////////////////////////////

ibBackendException::ibBackendException(const wxString& strErrorDescription)
	: m_strErrorDescription(strErrorDescription), m_errorHandled(false)
{
#ifdef DEBUG
	wxLogDebug(strErrorDescription);
#endif // !DEBUG

	ibProcUnit::Raise();

	ms_strError = strErrorDescription;
}

#include "backend/metaCollection/metaModuleObject.h"

void ibBackendException::ProcessError(const ibBackendException* err, const ibByteUnit& error)
{
	const bool isEvalMode = ibBackendException::IsEvalMode();

	const wxString& strFileName = error.m_strFileName;
	const wxString& strModuleName = error.m_strModuleName;
	const wxString& strDocPath = error.m_strDocPath;

	if (err != nullptr && !err->m_errorHandled) {

		if (activeMetaData != nullptr) {

			wxString strModuleData;

			if (!isEvalMode && strFileName.IsEmpty()) {
				const ibGuid& guidDocPath = error.m_strDocPath;
				const ibValueMetaObjectModuleBase* foundedDoc = activeMetaData->FindAnyObjectByFilter<ibValueMetaObjectModuleBase>(guidDocPath, true);
				wxASSERT(foundedDoc);
				strModuleData = foundedDoc->GetModuleText();
			}
			else if (!isEvalMode && !strFileName.IsEmpty() && backend_mainFrame != nullptr) {
				const ibMetaData* metadata = backend_mainFrame->FindMetadataByPath(strFileName);
				wxASSERT(metadata);
				const ibGuid& guidDocPath = error.m_strDocPath;
				const ibValueMetaObjectModuleBase* foundedDoc = metadata->FindAnyObjectByFilter<ibValueMetaObjectModuleBase>(guidDocPath, true);
				wxASSERT(foundedDoc);
				strModuleData = foundedDoc->GetModuleText();
			}

			const wxString strCodeError = isEvalMode ? wxString(wxEmptyString) :
				ibBackendException::FindErrorCodeLine(strModuleData, error.m_numString);

			ibBackendException::ProcessExceptionError(strFileName,
				strModuleName, strDocPath,
				error.m_numString, isEvalMode ? error.m_numLine : error.m_numLine + 1,
				strCodeError, wxNOT_FOUND, err->GetErrorDescription()
			);
		}
		else {

			ibBackendException::ProcessExceptionError(strFileName,
				strModuleName, strDocPath,
				error.m_numString, error.m_numLine + 1,
				wxEmptyString, wxNOT_FOUND, err->GetErrorDescription()
			);
		}

		err->m_errorHandled = true;
	}

	//throw this exception
	throw(err);
}

void ibBackendException::ProcessError(const wxString& strFileName,
	const wxString& strModuleName, const wxString& strDocPath,
	const unsigned int currPos, const unsigned int currLine,
	const wxString& strCodeError, const int codeError, const wxString& strErrorDesc)
{
	//throw this exception
	ibBackendCoreException::Error(
		ibBackendException::ProcessExceptionError(strFileName, strModuleName, strDocPath, currPos, currLine, strCodeError, codeError, strErrorDesc));
}

////////////////////////////////////////////////////////////////////

wxString ibBackendException::ProcessExceptionError(const wxString& strFileName,
	const wxString& strModuleName, const wxString& strDocPath,
	const unsigned int currPos, const unsigned int currLine,
	const wxString& strCodeError, const int codeError, const wxString& strErrorDesc)
{
	wxString strErrorMessage;

	strErrorMessage += wxT("{") + strModuleName + wxT("(") + (gs_evalMode ? wxString(wxT(" ")) : wxString::Format(wxT("%i"), currLine)) + wxT(")}: ");
	strErrorMessage += (codeError > 0 ? ibBackendException::Format(codeError, strErrorDesc) : strErrorDesc) + wxT("\n");
	strErrorMessage += (gs_evalMode ? wxString(wxEmptyString) : strCodeError);

	if (gs_evalMode) strErrorMessage.Replace(wxT('\n'), wxT(' '));

	if (!gs_evalMode && backend_mainFrame != nullptr) {

		// set stack 
		wxString strStackMessage;

		for (unsigned int i = 0; i < ibProcUnit::GetCountRunContext(); i++) {
			const ibRunContext* stackContext = ibProcUnit::GetRunContext(i);
			wxASSERT(stackContext);
			const ibByteCode* stackByteCode = stackContext->GetByteCode();
			wxASSERT(stackByteCode);
			strStackMessage += wxString::Format(wxT("\n%i: %s (#line %d)"),
				i + 1,
				stackByteCode->m_strModuleName,
				stackByteCode->m_listCode[stackContext->m_lCurLine].m_numLine + 1
			);
		}

		gs_processBackendError = true;

		//show message
		backend_mainFrame->BackendError(
			strFileName,
			strDocPath,
			currLine,
			strErrorMessage + (strStackMessage.IsEmpty() ? wxT("") : wxT("\n\nCall stack:") + strStackMessage)
		);

		gs_processBackendError = false;
	}

	ms_strError = strErrorMessage;

#ifdef DEBUG
	wxLogDebug(strErrorMessage);
#endif // !DEBUG

	return strErrorMessage;
}

////////////////////////////////////////////////////////////////////

const wxString& ibBackendException::GetErrorDesc(int codeError)
{
	if (0 <= codeError && codeError < LastError)
		return gs_listErrorString[codeError];
	return gs_listErrorString[ERROR_SYS1];
}

////////////////////////////////////////////////////////////////////

bool ibBackendException::IsErrorOutputProcessing()
{
	return gs_processBackendError;
}

void ibBackendException::SetEvalMode(bool mode)
{
	gs_evalMode = mode;
}

bool ibBackendException::IsEvalMode()
{
	return gs_evalMode;
}

////////////////////////////////////////////////////////////////////

#if !wxUSE_UTF8_LOCALE_ONLY
wxString ibBackendException::DoFormatWchar(const wxChar* format, ...)
{
	va_list args;
	va_start(args, format);
	return FormatV(format, args);
}
#endif

#if wxUSE_UNICODE_UTF8
wxString ibBackendException::DoFormatUtf8(const wxChar* format, ...)
{
	va_list args;
	va_start(args, format);
	wxString strFormat;
	strFormat.FormatV(format, args);
	va_end(args);
	return strFormat;
}
#endif

wxString ibBackendException::FormatV(const wxString& format, va_list& list)
{
	wxString strErrorBuffer =
		wxString::FormatV(wxGetTranslation(format), list);

	va_end(list);

	if (ibBackendException::IsEvalMode())
		strErrorBuffer.Replace(wxT('\n'), wxT(' '));

	stringUtils::TrimAll(strErrorBuffer);
	return strErrorBuffer;
}

wxString ibBackendException::FindErrorCodeLine(const wxString& strBuffer, unsigned int currPos)
{
	const unsigned int sizeText = strBuffer.length();

	unsigned int startPos = 0;
	unsigned int endPos = sizeText;

	for (unsigned int i = (currPos == sizeText ? currPos - 1 : currPos); i > 0; i--) {
		if (strBuffer[i] == wxT('\n')) {
			startPos = i + 1;
			break;
		};
	}

	//look for the end of the line where the translation error message is returned
	for (unsigned int i = currPos; i < sizeText; i++) {
		if (strBuffer[i] == wxT('\n')) {
			endPos = i; break;
		};
	}

	//determine the line number
	unsigned int currLine = 1 + strBuffer.Left(startPos).Replace(wxT('\n'), wxT('\n'));

	wxString strError = wxString::Format(wxT("%s <<?>> %s"), strBuffer.Mid(startPos, currPos - startPos), strBuffer.Mid(currPos, endPos - currPos));
	strError.Replace('\r', '\0');
	strError.Replace('\t', ' ');

	stringUtils::TrimAll(strError);

	return strError;
}

#pragma region _exception_h_

//service error handling procedures

#if !wxUSE_UTF8_LOCALE_ONLY
void ibBackendCoreException::DoErrorWchar(const wxChar* format, ...)
{
	va_list args;
	va_start(args, format);

	const wxString& strErrorBuffer =
		FormatV(format, args);

	throw(new ibBackendCoreException(strErrorBuffer));
}
#endif

#if wxUSE_UNICODE_UTF8
void ibBackendCoreException::DoErrorUtf8(const wxChar* format, ...)
{
	va_list args;
	va_start(args, format);
	ErrorV(format, args);
}
#endif

void ibBackendInterruptException::Error()
{
	throw(new ibBackendInterruptException);
}

void ibBackendAccessException::Error()
{
	throw(new ibBackendAccessException);
}

#pragma endregion

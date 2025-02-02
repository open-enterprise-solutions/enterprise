////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : system objects 
////////////////////////////////////////////////////////////////////////////

#include "systemManager.h"
#include "backend/backend_form.h"

enum
{
	//--- Базовые:
	enBoolean = 0,
	enNumber,
	enDate,
	enString,
	//--- Математические:
	enRound,
	enInt,
	enLog10,
	enLn,
	enMax,
	enMin,
	enSqrt,
	//--- Строковые:
	enStrLen,
	enIsBlankString,
	enTrimL,
	enTrimR,
	enTrimAll,
	enLeft,
	enRight,
	enMid,
	enFind,
	enStrReplace,
	enStrCountOccur,
	enStrLineCount,
	enStrGetLine,
	enUpper,
	enLower,
	enChr,
	enAsc,
	//--- Работа с датой и временем:
	enCurrentDate,
	enWorkingDate,
	enAddMonth,
	enBegOfMonth,
	enEndOfMonth,
	enBegOfQuart,
	enEndOfQuart,
	enBegOfYear,
	enEndOfYear,
	enBegOfWeek,
	enEndOfWeek,
	enBegOfDay,
	enEndOfDay,
	enGetYear,
	enGetMonth,
	enGetDay,
	enGetHour,
	enGetMinute,
	enGetSecond,
	enGetWeekOfYear,
	enGetDayOfYear,
	enGetDayOfWeek,
	enGetQuartOfYear,
	//--- Работа с файлами: 
	enFileCopy,
	enFileDelete,
	enGetTempDir,
	enGetTempFileName,
	//--- Работа с окнами: 
	enActiveWindow,
	//--- Специальные:
	enMessage,
	enAlert,
	enQuestion,
	enSetStatus,
	enClearMessage,
	enSetError,
	enRaise,
	enErrorDescription,
	enIsEmptyValue,
	enEvaluate,
	enExecute,
	enFormat,
	enType,
	enTypeOf,
	enRand,
	enArgCount,
	enArgValue,
	enComputerName,
	enRunApp,
	enSetAppTitle,
	enUserDir,
	enUserName,
	enUserPassword,
	enExclusiveMode,
	enGeneralLanguage,
	enEndJob,
	enUserInterruptProcessing,
	enGetCommonForm,
	enShowCommonForm,
	enBeginTransaction,
	enCommitTransaction,
	enRollBackTransaction
};

void CSystemFunction::PrepareNames() const
{
	m_methodHelper->ClearHelper();

	//--- Базовые:
	m_methodHelper->AppendFunc("boolean", 1, "boolean(value)");
	m_methodHelper->AppendFunc("number", 1, "number(value)");
	m_methodHelper->AppendFunc("date", 1, "date(value)");
	m_methodHelper->AppendFunc("string", 1, "string(value)");
	//--- Математические:
	m_methodHelper->AppendFunc("round", 3, "round(number, number, roundMode)");
	m_methodHelper->AppendFunc("int", 1, "int(number)");
	m_methodHelper->AppendFunc("log10", 1, "log10(number)");
	m_methodHelper->AppendFunc("ln", 1, "ln(number)");
	m_methodHelper->AppendFunc("max", -1, "max(number, ...)");
	m_methodHelper->AppendFunc("min", -1, "min(number, ...)");
	m_methodHelper->AppendFunc("sqrt", 1, "sqrt(number)");
	//--- Строковые:
	m_methodHelper->AppendFunc("strLen", 1, "strLen(string)");
	m_methodHelper->AppendFunc("isBlankString", 1, "isBlankString(string)");
	m_methodHelper->AppendFunc("trimL", 1, "trimL(string)");
	m_methodHelper->AppendFunc("trimR", 1, "trimR(string)");
	m_methodHelper->AppendFunc("trimAll", 1, "trimAll(string)");
	m_methodHelper->AppendFunc("left", 2, "left(string, number)");
	m_methodHelper->AppendFunc("right", 2, "right(string, number)");
	m_methodHelper->AppendFunc("mid", 3, "mid(string, number, number)");
	m_methodHelper->AppendFunc("find", 3, "find(string, string, number)");
	m_methodHelper->AppendFunc("strReplace", 3, "strReplace(string, string, string)");
	m_methodHelper->AppendFunc("strCountOccur", 2, "strCountOccur(string, string)");
	m_methodHelper->AppendFunc("strLineCount", 1, "strLineCount(string)");
	m_methodHelper->AppendFunc("strGetLine", 1, "strGetLine(string)");
	m_methodHelper->AppendFunc("upper", 1, "upper(string)");
	m_methodHelper->AppendFunc("lower", 1, "lower(string)");
	m_methodHelper->AppendFunc("chr", 1, "chr(number)");
	m_methodHelper->AppendFunc("asc", 1, "asc(string)");
	//--- Работа с датой и временем:
	m_methodHelper->AppendFunc("currentDate", "currentDate()");
	m_methodHelper->AppendFunc("workingDate", 1, "workingDate(date)");
	m_methodHelper->AppendFunc("addMonth", 2, "addMonth(date, number)");
	m_methodHelper->AppendFunc("begOfMonth", 1, "begOfMonth(date)");
	m_methodHelper->AppendFunc("endOfMonth", 1, "endOfMonth(date)");
	m_methodHelper->AppendFunc("begOfQuart", 1, "begOfQuart(date)");
	m_methodHelper->AppendFunc("endOfQuart", 1, "endOfQuart(date)");
	m_methodHelper->AppendFunc("begOfYear", 1, "begOfYear(date)");
	m_methodHelper->AppendFunc("endOfYear", 1, "endOfYear(date)");
	m_methodHelper->AppendFunc("begOfWeek", 1, "begOfWeek(date)");
	m_methodHelper->AppendFunc("endOfWeek", 1, "endOfWeek(date)");
	m_methodHelper->AppendFunc("begOfDay", 1, "begOfDay(date)");
	m_methodHelper->AppendFunc("endOfDay", 1, "endOfDay(date)");
	m_methodHelper->AppendFunc("getYear", 1, "getYear(date)");
	m_methodHelper->AppendFunc("getMonth", 1, "getMonth(date)");
	m_methodHelper->AppendFunc("getDay", 1, "getDay(date)");
	m_methodHelper->AppendFunc("getHour", 1, "getHour(date)");
	m_methodHelper->AppendFunc("getMinute", 1, "getMinute(date)");
	m_methodHelper->AppendFunc("getSecond", 1, "getSecond(date)");
	m_methodHelper->AppendFunc("getWeekOfYear", 1, "getWeekOfYear(date)");
	m_methodHelper->AppendFunc("getDayOfYear", 1, "getDayOfYear(date)");
	m_methodHelper->AppendFunc("getDayOfWeek", 1, "getDayOfWeek(date)");
	m_methodHelper->AppendFunc("getQuartOfYear", 1, "getQuartOfYear(date)");
	//--- Работа с файлами: 
	m_methodHelper->AppendFunc("fileDelete", 1, "fileDelete(string)");
	m_methodHelper->AppendFunc("fileCopy", 2, "fileCopy(string, string)");
	m_methodHelper->AppendFunc("getTempDir", "getTempDir()");
	m_methodHelper->AppendFunc("getTempFileName", "getTempFileName()");
	//--- Работа с окнами: 
	m_methodHelper->AppendFunc("activeWindow", "activeWindow()");
	//--- Специальные:
	m_methodHelper->AppendFunc("message", 2, "message(string, statusMessage)");
	m_methodHelper->AppendFunc("alert", 1, "alert(string)");
	m_methodHelper->AppendFunc("question", 2, "question(string, questionMode)");
	m_methodHelper->AppendFunc("setStatus", 1, "setStatus(string)");
	m_methodHelper->AppendFunc("clearMessages", "clearMessages()");
	m_methodHelper->AppendFunc("setError", 1, "setError(string)");
	m_methodHelper->AppendFunc("raise", 1, "raise(string)");
	m_methodHelper->AppendFunc("errorDescription", "errorDescription()");
	m_methodHelper->AppendFunc("isEmptyValue", 1, "isEmptyValue(value)");
	m_methodHelper->AppendFunc("evaluate", 1, "evaluate(string)");
	m_methodHelper->AppendFunc("execute", 2, "execute(string)");
	m_methodHelper->AppendFunc("format", 2, "format(value, string)");
	m_methodHelper->AppendFunc("type", 1, "type(string)");
	m_methodHelper->AppendFunc("typeOf", 1, "typeOf(value)");
	m_methodHelper->AppendFunc("rand", "rand()");
	m_methodHelper->AppendFunc("argCount", "argCount()");
	m_methodHelper->AppendFunc("argValue", "argValue()");
	m_methodHelper->AppendFunc("computerName", "computerName()");
	m_methodHelper->AppendFunc("runApp", 1, "runApp(string)");
	m_methodHelper->AppendFunc("setAppTitle", 1, "setAppTitle(string)");
	m_methodHelper->AppendFunc("userDir", "userDir()");
	m_methodHelper->AppendFunc("userName", "userName()");
	m_methodHelper->AppendFunc("userPassword", "userPassword()");
	m_methodHelper->AppendFunc("exclusiveMode", "exclusiveMode()");
	m_methodHelper->AppendFunc("generalLanguage", "generalLanguage()");
	m_methodHelper->AppendFunc("endJob", 1, "endJob(boolean)");
	m_methodHelper->AppendFunc("userInterruptProcessing", "userInterruptProcessing()");
	m_methodHelper->AppendFunc("getCommonForm", 3, "getCommonForm(string, owner, uniqueGuid)");
	m_methodHelper->AppendFunc("showCommonForm", 3, "showCommonForm(string, owner, uniqueGuid)");
	m_methodHelper->AppendFunc("beginTransaction", "beginTransaction()");
	m_methodHelper->AppendFunc("commitTransaction", "commitTransaction()");
	m_methodHelper->AppendFunc("rollBackTransaction", "rollBackTransaction()");
};

#include "backend/compiler/enum.h"
#include "backend/compiler/value/valueGuid.h"

#include "backend/appData.h"

bool CSystemFunction::CallAsFunc(const long lMethodNum, CValue& pvarRetValue, CValue** paParams, const long lSizeArray)
{
	if (!appData->DesignerMode()) {
		switch (lMethodNum)
		{
			//--- Базовые:
		case enBoolean: pvarRetValue = Boolean(*paParams[0]); return true;
		case enNumber: pvarRetValue = Number(*paParams[0]); return true;
		case enDate: pvarRetValue = Date(*paParams[0]); return true;
		case enString: pvarRetValue = String(*paParams[0]); return true;
			//--- Математические:
		case enRound: pvarRetValue = Round(*paParams[0],
			lSizeArray > 1 ? paParams[1]->GetInteger() : 0,
			lSizeArray > 2 ?
			paParams[2]->ConvertToEnumValue<eRoundMode>() : eRoundMode::eRoundMode_Round15as20
		); return true;
		case enInt: pvarRetValue = Int(*paParams[0]); return true;
		case enLog10: pvarRetValue = Log10(*paParams[0]); return true;
		case enLn: pvarRetValue = Ln(*paParams[0]); return true;
		case enMax: pvarRetValue = Max(paParams, lSizeArray); return true;
		case enMin: pvarRetValue = Min(paParams, lSizeArray); return true;
		case enSqrt: pvarRetValue = Sqrt(*paParams[0]); return true;
			//--- Строковые:  
		case enStrLen: pvarRetValue = StrLen(*paParams[0]); return true;
		case enIsBlankString: pvarRetValue = IsBlankString(*paParams[0]); return true;
		case enTrimL: pvarRetValue = TrimL(*paParams[0]); return true;
		case enTrimR: pvarRetValue = TrimR(*paParams[0]); return true;
		case enTrimAll: pvarRetValue = TrimAll(*paParams[0]); return true;
		case enLeft: pvarRetValue = Left(*paParams[0], paParams[1]->GetInteger()); return true;
		case enRight: pvarRetValue = Right(*paParams[0], paParams[1]->GetInteger()); return true;
		case enMid: pvarRetValue = Mid(*paParams[0], paParams[1]->GetInteger(), lSizeArray > 1 ? paParams[2]->GetInteger() : 1); return true;
		case enFind: pvarRetValue = Find(*paParams[0], paParams[1], lSizeArray > 1 ? paParams[2]->GetInteger() : 0); return true;
		case enStrReplace: pvarRetValue = StrReplace(*paParams[0], paParams[1], paParams[2]); return true;
		case enStrCountOccur: pvarRetValue = StrCountOccur(*paParams[0], *paParams[1]); return true;
		case enStrLineCount: pvarRetValue = StrLineCount(*paParams[0]); return true;
		case enStrGetLine: pvarRetValue = StrGetLine(*paParams[0], paParams[1]->GetInteger()); return true;
		case enUpper: pvarRetValue = Upper(*paParams[0]); return true;
		case enLower: pvarRetValue = Lower(*paParams[0]); return true;
		case enChr: pvarRetValue = Chr(paParams[0]->GetInteger()); return true;
		case enAsc: pvarRetValue = Asc(*paParams[0]); return true;
			//--- Работа с датой и временем:
		case enCurrentDate: pvarRetValue = CurrentDate(); return true;
		case enWorkingDate: pvarRetValue = WorkingDate(); return true;
		case enAddMonth: pvarRetValue = AddMonth(*paParams[0], paParams[1]->GetInteger()); return true;
		case enBegOfMonth: pvarRetValue = BegOfMonth(*paParams[0]); return true;
		case enEndOfMonth: pvarRetValue = EndOfMonth(*paParams[0]); return true;
		case enBegOfQuart: pvarRetValue = BegOfQuart(*paParams[0]); return true;
		case enEndOfQuart: pvarRetValue = EndOfQuart(*paParams[0]); return true;
		case enBegOfYear: pvarRetValue = BegOfYear(*paParams[0]); return true;
		case enEndOfYear: pvarRetValue = EndOfYear(*paParams[0]); return true;
		case enBegOfWeek: pvarRetValue = BegOfWeek(*paParams[0]); return true;
		case enEndOfWeek: pvarRetValue = EndOfWeek(*paParams[0]); return true;
		case enBegOfDay: pvarRetValue = BegOfDay(*paParams[0]); return true;
		case enEndOfDay: pvarRetValue = EndOfDay(*paParams[0]); return true;
		case enGetYear: pvarRetValue = GetYear(*paParams[0]); return true;
		case enGetMonth: pvarRetValue = GetMonth(*paParams[0]); return true;
		case enGetDay: pvarRetValue = GetDay(*paParams[0]); return true;
		case enGetHour: pvarRetValue = GetHour(*paParams[0]); return true;
		case enGetMinute: pvarRetValue = GetMinute(*paParams[0]); return true;
		case enGetSecond: pvarRetValue = GetSecond(*paParams[0]); return true;
		case enGetWeekOfYear: pvarRetValue = GetWeekOfYear(*paParams[0]); return true;
		case enGetDayOfYear: pvarRetValue = GetDayOfYear(*paParams[0]); return true;
		case enGetDayOfWeek: pvarRetValue = GetDayOfWeek(*paParams[0]); return true;
		case enGetQuartOfYear: pvarRetValue = GetQuartOfYear(*paParams[0]); return true;
			//--- Работа с файлами:
		case enFileCopy: pvarRetValue = CopyFile(paParams[0]->GetString(), paParams[1]->GetString()); return true;
		case enFileDelete: pvarRetValue = DeleteFile(paParams[0]->GetString()); return true;
		case enGetTempDir: pvarRetValue = GetTempDir(); return true;
		case enGetTempFileName: pvarRetValue = GetTempFileName(); return true;
			//--- Работа с окнами: 
		case enActiveWindow: pvarRetValue = ActiveWindow(); return true;
			//--- Специальные:
		case enMessage:
			Message(paParams[0]->GetString(),
				lSizeArray > 1 ? paParams[1]->ConvertToEnumValue<eStatusMessage>() : eStatusMessage::eStatusMessage_Information);
			return true;
		case enAlert: Alert(paParams[0]->GetString()); return true;
		case enQuestion: pvarRetValue = Question(paParams[0]->GetString(), paParams[1]->ConvertToEnumValue<eQuestionMode>());
		case enSetStatus: SetStatus(paParams[0]->GetString()); return true;
		case enClearMessage: ClearMessage(); return true;
		case enSetError: SetError(paParams[0]->GetString()); return true;
		case enRaise: Raise(paParams[0]->GetString()); return true;
		case enErrorDescription: pvarRetValue = ErrorDescription();
		case enIsEmptyValue: pvarRetValue = IsEmptyValue(*paParams[0]); return true;
		case enEvaluate: pvarRetValue = Evaluate(paParams[0]->GetString()); return true;
		case enExecute: Execute(paParams[0]->GetString()); return true;
		case enFormat: pvarRetValue = Format(*paParams[0], paParams[1]->GetString()); return true;
		case enType: pvarRetValue = Type(*paParams[0]); return true;
		case enTypeOf: pvarRetValue = TypeOf(*paParams[0]); return true;
		case enRand: pvarRetValue = Rand(); return true;
		case enArgCount: pvarRetValue = ArgCount(); return true;
		case enArgValue: pvarRetValue = ArgValue(paParams[0]->GetInteger());
		case enComputerName: pvarRetValue = ComputerName(); return true;
		case enRunApp: RunApp(paParams[0]->GetString()); return true;
		case enSetAppTitle: SetAppTitle(paParams[0]->GetString()); return true;
		case enUserDir: pvarRetValue = UserDir(); return true;
		case enUserName: pvarRetValue = UserName(); return true;
		case enUserPassword: pvarRetValue = UserPassword(); return true;
		case enExclusiveMode: pvarRetValue = ExclusiveMode(); return true;
		case enGeneralLanguage: pvarRetValue = GeneralLanguage();
		case enEndJob: EndJob(paParams[0]->GetInteger()); return true;
		case enUserInterruptProcessing: UserInterruptProcessing(); return true;
		case enGetCommonForm: pvarRetValue = GetCommonForm(
			paParams[0]->GetString(),
			lSizeArray > 1 ? paParams[1]->ConvertToType<IBackendControlFrame>() : nullptr,
			lSizeArray > 2 ? paParams[2]->ConvertToType<CValueGuid>() : nullptr);
			return true;
		case enShowCommonForm: ShowCommonForm(
			paParams[0]->GetString(),
			lSizeArray > 1 ? paParams[1]->ConvertToType<IBackendControlFrame>() : nullptr,
			lSizeArray > 2 ? paParams[2]->ConvertToType<CValueGuid>() : nullptr);
			return true;
			//--- Тразакции:
		case enBeginTransaction: BeginTransaction(); return true;
		case enCommitTransaction: CommitTransaction(); return true;
		case enRollBackTransaction: RollBackTransaction(); return true;
		}
	}
	else
	{
		switch (lMethodNum)
		{
			//--- Специальные:
		case enType:
			pvarRetValue = Type(*paParams[0]);
			return true;
		case enTypeOf:
			pvarRetValue = TypeOf(*paParams[0]);
			return true;

		case enGetCommonForm:
			pvarRetValue = GetCommonForm(paParams[0]->GetString(),
				lSizeArray > 1 ? paParams[1]->ConvertToType<IBackendControlFrame>() : nullptr,
				lSizeArray > 2 ? paParams[2]->ConvertToType<CValueGuid>() : nullptr);
		case enShowCommonForm:
			ShowCommonForm(paParams[0]->GetString(),
				lSizeArray > 1 ? paParams[1]->ConvertToType<IBackendControlFrame>() : nullptr,
				lSizeArray > 2 ? paParams[2]->ConvertToType<CValueGuid>() : nullptr);
			return true;
		}
	}

	return false;
}

//**********************************************************************

wxDateTime CSystemFunction::sm_workDate = wxDateTime::Now();

class wxOESRandModule : public wxModule
{
public:
	wxOESRandModule() : wxModule() {}
	virtual bool OnInit() {
		srand((unsigned)time(nullptr));
		return true;
	}
	virtual void OnExit() {}
private:
	wxDECLARE_DYNAMIC_CLASS(wxOESRandModule);
};

wxIMPLEMENT_DYNAMIC_CLASS(wxOESRandModule, wxModule)

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

CONTEXT_TYPE_REGISTER(CSystemFunction, "sysManager", string_to_clsid("CO_SYSM"));
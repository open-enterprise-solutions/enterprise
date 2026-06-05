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
	enTStr,
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
	enSetExclusive,
	enGeneralLanguage,
	enEndJob,
	enUserInterruptProcessing,
	enAccessRight,
	enIsInRole,
	enGetCommonForm,
	enShowCommonForm,
	enGetCommonTemplate,
	enBeginTransaction,
	enCommitTransaction,
	enRollBackTransaction
};

void ibValueSystemFunction_BindNames(ibValue::ibMemberTable& helper, const ibValue* /*ctx*/)
{

	//--- Базовые:
	helper.AppendFunc(wxT("Boolean"), 1, wxT("Boolean(value : any)"));
	helper.AppendFunc(wxT("Number"), 1, wxT("Number(value: any)"));
	helper.AppendFunc(wxT("Date"), 1, wxT("Date(value: any)"));
	helper.AppendFunc(wxT("String"), 1, wxT("String(value: any)"));
	//--- Математические:
	helper.AppendFunc(wxT("Round"), 3, wxT("Round(num : number, number, roundMode)"));
	helper.AppendFunc(wxT("Lnt"), 1, wxT("Lnt(num : number)"));
	helper.AppendFunc(wxT("Log10"), 1, wxT("Log10(num : number)"));
	helper.AppendFunc(wxT("Ln"), 1, wxT("Ln(num : number)"));
	helper.AppendFunc(wxT("Max"), -1, wxT("Max(num : number, ...)"));
	helper.AppendFunc(wxT("Min"), -1, wxT("Min(num : number, ...)"));
	helper.AppendFunc(wxT("Sqrt"), 1, wxT("Sqrt(num : number)"));
	//--- Строковые:
	helper.AppendFunc(wxT("StrLen"), 1, wxT("StrLen(str : string)"));
	helper.AppendFunc(wxT("IsBlankString"), 1, wxT("IsBlankString(str : string)"));
	helper.AppendFunc(wxT("TrimL"), 1, wxT("TrimL(str : string)"));
	helper.AppendFunc(wxT("TrimR"), 1, wxT("TrimR(str : string)"));
	helper.AppendFunc(wxT("TrimAll"), 1, wxT("TrimAll(str : string)"));
	helper.AppendFunc(wxT("Left"), 2, wxT("Left(str : string, number)"));
	helper.AppendFunc(wxT("Right"), 2, wxT("Right(str : string, number)"));
	helper.AppendFunc(wxT("Mid"), 3, wxT("Mid(str : string, number, number)"));
	helper.AppendFunc(wxT("Find"), 3, wxT("Find(str : string, string, number)"));
	helper.AppendFunc(wxT("StrReplace"), 3, wxT("StrReplace(str : string, string, string)"));
	helper.AppendFunc(wxT("StrCountOccur"), 2, wxT("StrCountOccur(str : string, string)"));
	helper.AppendFunc(wxT("StrLineCount"), 1, wxT("StrLineCount(str : string)"));
	helper.AppendFunc(wxT("StrGetLine"), 1, wxT("StrGetLine(str : string)"));
	helper.AppendFunc(wxT("Upper"), 1, wxT("Upper(str : string)"));
	helper.AppendFunc(wxT("Lower"), 1, wxT("Lower(str : string)"));
	helper.AppendFunc(wxT("Chr"), 1, wxT("Chr(num : number)"));
	helper.AppendFunc(wxT("Asc"), 1, wxT("Asc(str : string)"));
	helper.AppendFunc(wxT("Tstr"), 2, wxT("Tstr(text : string, langCode : string)"));
	//--- Работа с датой и временем:
	helper.AppendFunc(wxT("CurrentDate"), wxT("CurrentDate()"));
	helper.AppendFunc(wxT("WorkingDate"), 1, wxT("WorkingDate(d : date)"));
	helper.AppendFunc(wxT("AddMonth"), 2, wxT("AddMonth(d : date, num : number)"));
	helper.AppendFunc(wxT("BegOfMonth"), 1, wxT("BegOfMonth(d : date)"));
	helper.AppendFunc(wxT("EndOfMonth"), 1, wxT("EndOfMonth(d : date)"));
	helper.AppendFunc(wxT("BegOfQuart"), 1, wxT("BegOfQuart(d : date)"));
	helper.AppendFunc(wxT("EndOfQuart"), 1, wxT("EndOfQuart(d : date)"));
	helper.AppendFunc(wxT("BegOfYear"), 1, wxT("BegOfYear(d : date)"));
	helper.AppendFunc(wxT("EndOfYear"), 1, wxT("EndOfYear(d : date)"));
	helper.AppendFunc(wxT("BegOfWeek"), 1, wxT("BegOfWeek(d : date)"));
	helper.AppendFunc(wxT("EndOfWeek"), 1, wxT("EndOfWeek(d : date)"));
	helper.AppendFunc(wxT("BegOfDay"), 1, wxT("BegOfDay(d : date)"));
	helper.AppendFunc(wxT("EndOfDay"), 1, wxT("EndOfDay(d : date)"));
	helper.AppendFunc(wxT("GetYear"), 1, wxT("GetYear(d : date)"));
	helper.AppendFunc(wxT("GetMonth"), 1, wxT("GetMonth(d : date)"));
	helper.AppendFunc(wxT("GetDay"), 1, wxT("GetDay(d : date)"));
	helper.AppendFunc(wxT("GetHour"), 1, wxT("GetHour(d : date)"));
	helper.AppendFunc(wxT("GetMinute"), 1, wxT("GetMinute(d : date)"));
	helper.AppendFunc(wxT("GetSecond"), 1, wxT("GetSecond(d : date)"));
	helper.AppendFunc(wxT("GetWeekOfYear"), 1, wxT("GetWeekOfYear(d : date)"));
	helper.AppendFunc(wxT("GetDayOfYear"), 1, wxT("GetDayOfYear(d : date)"));
	helper.AppendFunc(wxT("GetDayOfWeek"), 1, wxT("GetDayOfWeek(d : date)"));
	helper.AppendFunc(wxT("GetQuartOfYear"), 1, wxT("GetQuartOfYear(d : date)"));
	//--- Работа с файлами: 
	helper.AppendFunc(wxT("FileDelete"), 1, wxT("FileDelete(fileName : string)"));
	helper.AppendFunc(wxT("FileCopy"), 2, wxT("FileCopy(fileDstName : string, fileSrcName : string)"));
	helper.AppendFunc(wxT("GetTempDir"), wxT("GetTempDir()"));
	helper.AppendFunc(wxT("GetTempFileName"), wxT("GetTempFileName()"));
	//--- Работа с окнами: 
	helper.AppendFunc(wxT("ActiveWindow"), wxT("ActiveWindow()"));
	//--- Специальные:
	helper.AppendProc(wxT("Message"), 2, wxT("Message(message : string, statusMessage : statusMessage)"));
	helper.AppendFunc(wxT("Alert"), 1, wxT("Alert(message : string)"));
	helper.AppendFunc(wxT("Question"), 2, wxT("Question(message : string, questionMode)"));
	helper.AppendFunc(wxT("SetStatus"), 1, wxT("SetStatus(text : string)"));
	helper.AppendFunc(wxT("ClearMessages"), wxT("ClearMessages()"));
	helper.AppendFunc(wxT("SetError"), 1, wxT("SetError(string)"));
	helper.AppendFunc(wxT("Raise"), 1, wxT("Raise(string)"));
	helper.AppendFunc(wxT("ErrorDescription"), wxT("ErrorDescription()"));
	helper.AppendFunc(wxT("IsEmptyValue"), 1, wxT("IsEmptyValue(value : any)"));
	helper.AppendFunc(wxT("Evaluate"), 1, wxT("Evaluate(expr : string)"));
	helper.AppendFunc(wxT("Execute"), 2, wxT("Execute(expr : string)"));
	helper.AppendFunc(wxT("Format"), 2, wxT("Format(value : any, format : string)"));
	helper.AppendFunc(wxT("Type"), 1, wxT("Type(strType : string)"));
	helper.AppendFunc(wxT("TypeOf"), 1, wxT("TypeOf(value : any)"));
	helper.AppendFunc(wxT("Rand"), wxT("Rand()"));
	helper.AppendFunc(wxT("ArgCount"), wxT("ArgCount()"));
	helper.AppendFunc(wxT("ArgValue"), wxT("ArgValue()"));
	helper.AppendFunc(wxT("ComputerName"), wxT("ComputerName()"));
	helper.AppendFunc(wxT("RunApp"), 1, wxT("RunApp(command : string)"));
	helper.AppendFunc(wxT("SetAppTitle"), 1, wxT("SetAppTitle(title : string)"));
	helper.AppendFunc(wxT("UserDir"), wxT("UserDir()"));
	helper.AppendFunc(wxT("UserName"), wxT("UserName()"));
	helper.AppendFunc(wxT("UserPassword"), wxT("UserPassword()"));
	helper.AppendFunc(wxT("ExclusiveMode"), wxT("ExclusiveMode()"));
	helper.AppendProc(wxT("SetExclusive"), 1, wxT("SetExclusive(on : boolean)"));
	helper.AppendFunc(wxT("GeneralLanguage"), wxT("GeneralLanguage()"));
	helper.AppendFunc(wxT("EndJob"), 1, wxT("EndJob(force : boolean)"));
	helper.AppendFunc(wxT("UserInterruptProcessing"), wxT("UserInterruptProcessing()"));
	helper.AppendFunc(wxT("AccessRight"), 2, wxT("AccessRight(strRole : string, metadata)"));
	helper.AppendFunc(wxT("IsInRole"), 1, wxT("IsInRole(strRole : string)"));
	helper.AppendFunc(wxT("GetCommonForm"), 3, wxT("GetCommonForm(name : string, owner : any, id : guid)"));
	helper.AppendProc(wxT("ShowCommonForm"), 3, wxT("ShowCommonForm(name : string, owner : any, id : guid)"));
	helper.AppendFunc(wxT("GetCommonTemplate"), 1, wxT("GetCommonTemplate(name : string)"));
	helper.AppendProc(wxT("BeginTransaction"), wxT("BeginTransaction()"));
	helper.AppendProc(wxT("CommitTransaction"), wxT("CommitTransaction()"));
	helper.AppendProc(wxT("RollBackTransaction"), wxT("RollBackTransaction()"));
};

#include "backend/compiler/enumUnit.h"
#include "backend/system/value/valueGuid.h"

#include "backend/appData.h"

bool ibValueSystemFunction::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
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
			paParams[2]->ConvertToEnumValue<ibRoundMode>() : ibRoundMode::ibRoundMode_Round15as20
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
		case enFind: pvarRetValue = Find(*paParams[0], *paParams[1], lSizeArray > 1 ? paParams[2]->GetInteger() : 0); return true;
		case enStrReplace: pvarRetValue = StrReplace(*paParams[0], *paParams[1], *paParams[2]); return true;
		case enStrCountOccur: pvarRetValue = StrCountOccur(*paParams[0], *paParams[1]); return true;
		case enStrLineCount: pvarRetValue = StrLineCount(*paParams[0]); return true;
		case enStrGetLine: pvarRetValue = StrGetLine(*paParams[0], paParams[1]->GetInteger()); return true;
		case enUpper: pvarRetValue = Upper(*paParams[0]); return true;
		case enLower: pvarRetValue = Lower(*paParams[0]); return true;
		case enChr: pvarRetValue = Chr(paParams[0]->GetInteger()); return true;
		case enAsc: pvarRetValue = Asc(*paParams[0]); return true;
		case enTStr: pvarRetValue = TStr(*paParams[0], lSizeArray > 0 ? paParams[1]->GetString() : wxT("")); return true;
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
				lSizeArray > 1 ? paParams[1]->ConvertToEnumValue<ibStatusMessage>() : ibStatusMessage::ibStatusMessage_Information);
			return true;
		case enAlert: Alert(paParams[0]->GetString()); return true;
		case enQuestion: pvarRetValue = Question(paParams[0]->GetString(), paParams[1]->ConvertToEnumValue<ibQuestionMode>());
		case enSetStatus: SetStatus(paParams[0]->GetString()); return true;
		case enClearMessage: ClearMessage(); return true;
		case enSetError: SetError(paParams[0]->GetString()); return true;
		case enRaise: Raise(paParams[0]->GetString()); return true;
		case enErrorDescription: pvarRetValue = ErrorDescription(); return true;
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
		case enGeneralLanguage: pvarRetValue = GeneralLanguage(); return true;
		case enEndJob: EndJob(paParams[0]->GetInteger()); return true;
		case enUserInterruptProcessing: UserInterruptProcessing(); return true;
		case enAccessRight:
			if (lSizeArray > 1)
				pvarRetValue = AccessRight(paParams[0]->GetString(), *paParams[1]);
			return lSizeArray > 1;
		case enIsInRole:
			if (lSizeArray > 0)
				pvarRetValue = IsInRole(*paParams[0]);
			return lSizeArray > 0;
		case enGetCommonForm: pvarRetValue = GetCommonForm(
			paParams[0]->GetString(),
			lSizeArray > 1 ? paParams[1]->ConvertToType<ibBackendControlFrame>() : nullptr,
			lSizeArray > 2 ? paParams[2]->ConvertToType<ibValueGuid>() : nullptr);
			return true;
		case enShowCommonForm: ShowCommonForm(
			paParams[0]->GetString(),
			lSizeArray > 1 ? paParams[1]->ConvertToType<ibBackendControlFrame>() : nullptr,
			lSizeArray > 2 ? paParams[2]->ConvertToType<ibValueGuid>() : nullptr);
			return true;
		case enGetCommonTemplate:
			pvarRetValue = GetCommonTemplate(paParams[0]->GetString());
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
				lSizeArray > 1 ? paParams[1]->ConvertToType<ibBackendControlFrame>() : nullptr,
				lSizeArray > 2 ? paParams[2]->ConvertToType<ibValueGuid>() : nullptr);
			return true;

		case enGetCommonTemplate:
			pvarRetValue = GetCommonTemplate(paParams[0]->GetString());
			return true;
		}
	}

	return false;
}

bool ibValueSystemFunction::CallAsProc(const long lMethodNum, ibValue** paParams, const long lSizeArray)
{
	if (!appData->DesignerMode()) {
		switch (lMethodNum)
		{
			//--- Специальные:
		case enMessage:
			Message(paParams[0]->GetString(),
				lSizeArray > 1 ? paParams[1]->ConvertToEnumValue<ibStatusMessage>() : ibStatusMessage::ibStatusMessage_Information);
			return true;
		case enAlert: Alert(paParams[0]->GetString()); return true;
		case enSetStatus: SetStatus(paParams[0]->GetString()); return true;
		case enClearMessage: ClearMessage(); return true;
		case enSetError: SetError(paParams[0]->GetString()); return true;
		case enRaise: Raise(paParams[0]->GetString()); return true;
		case enExecute: Execute(paParams[0]->GetString()); return true;
		case enRunApp: RunApp(paParams[0]->GetString()); return true;
		case enSetAppTitle: SetAppTitle(paParams[0]->GetString()); return true;
		case enSetExclusive: SetExclusive(paParams[0]->GetBoolean()); return true;
		case enEndJob: EndJob(paParams[0]->GetInteger()); return true;
		case enUserInterruptProcessing: UserInterruptProcessing(); return true;
		case enShowCommonForm: ShowCommonForm(
			paParams[0]->GetString(),
			lSizeArray > 1 ? paParams[1]->ConvertToType<ibBackendControlFrame>() : nullptr,
			lSizeArray > 2 ? paParams[2]->ConvertToType<ibValueGuid>() : nullptr);
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
		case enShowCommonForm:
			ShowCommonForm(paParams[0]->GetString(),
				lSizeArray > 1 ? paParams[1]->ConvertToType<ibBackendControlFrame>() : nullptr,
				lSizeArray > 2 ? paParams[2]->ConvertToType<ibValueGuid>() : nullptr);
			return true;
		}
	}

	return false;
}

//**********************************************************************

wxDateTime ibValueSystemFunction::ms_workDate = wxDateTime::Now();

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
};


//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

CONTEXT_TYPE_REGISTER(ibValueSystemFunction, "SystemManager", string_to_clsid("CO_SYSM"));
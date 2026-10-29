////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : system objects 
////////////////////////////////////////////////////////////////////////////

#include "systemManager.h"
#include "backend/backend_form.h"

enum
{
	//--- Basic:
	enBoolean = 0,
	enNumber,
	enDate,
	enString,
	//--- Math:
	enRound,
	enInt,
	enLog10,
	enLn,
	enMax,
	enMin,
	enSqrt,
	//--- Strings:
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
	//--- Date and time:
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
	enBegOfHour,
	enEndOfHour,
	enGetYear,
	enGetMonth,
	enGetDay,
	enGetHour,
	enGetMinute,
	enGetSecond,
	enGetMillisecond,
	enGetWeekOfYear,
	enGetDayOfYear,
	enGetDayOfWeek,
	enGetQuartOfYear,
	//--- File operations: 
	enFileCopy,
	enFileDelete,
	enGetTempDir,
	enGetTempFileName,
	//--- JSON:
	enReadJSON,
	enWriteJSON,
	//--- Window operations: 
	enActiveWindow,
	//--- Special:
	enMessage,
	enWriteJournalEvent,
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
	enRollBackTransaction,
	enIsNull,
	enValueIsFilled,
	//--- Jobs: appended at the END on purpose — the enumerator's ordinal IS the
	// method index into the bind table below, so inserting anywhere above would
	// silently re-point every later method at the wrong case.
	enRunScheduledJobs,
	enRunJob,
	enRunBackground,
	//--- Value as text: appended after the jobs for the same reason they were appended after
	// everything else — the ordinal IS the method index, so a new name goes at the END or every
	// later case answers to the wrong verb.
	enJSONString,
	enJSONValue,
	//--- A value as XML text and back: at the END, for the same reason.
	enXMLString,
	enXMLValue
};

void ibValueSystemFunction_BindNames(ibValue::ibMemberTable& helper, const ibValue* /*ctx*/)
{

	//--- Basic:
	helper.AppendFunc(ibT("Boolean"), 1, ibT("Boolean(value : any)"));
	helper.AppendFunc(ibT("Number"), 1, ibT("Number(value: any)"));
	helper.AppendFunc(ibT("Date"), 6, ibT("Date(value: any) | Date(year, month, day[, hour, minute, second])"));
	helper.AppendFunc(ibT("String"), 1, ibT("String(value: any)"));
	//--- Math:
	helper.AppendFunc(ibT("Round"), 3, ibT("Round(num : number, number, roundMode)"));
	helper.AppendFunc(ibT("Int"), 1, ibT("Int(num : number)"));
	helper.AppendFunc(ibT("Log10"), 1, ibT("Log10(num : number)"));
	helper.AppendFunc(ibT("Ln"), 1, ibT("Ln(num : number)"));
	helper.AppendFunc(ibT("Max"), -1, ibT("Max(num : number, ...)"));
	helper.AppendFunc(ibT("Min"), -1, ibT("Min(num : number, ...)"));
	helper.AppendFunc(ibT("Sqrt"), 1, ibT("Sqrt(num : number)"));
	//--- Strings:
	helper.AppendFunc(ibT("StrLen"), 1, ibT("StrLen(str : string)"));
	helper.AppendFunc(ibT("IsBlankString"), 1, ibT("IsBlankString(str : string)"));
	helper.AppendFunc(ibT("TrimL"), 1, ibT("TrimL(str : string)"));
	helper.AppendFunc(ibT("TrimR"), 1, ibT("TrimR(str : string)"));
	helper.AppendFunc(ibT("TrimAll"), 1, ibT("TrimAll(str : string)"));
	helper.AppendFunc(ibT("Left"), 2, ibT("Left(str : string, number)"));
	helper.AppendFunc(ibT("Right"), 2, ibT("Right(str : string, number)"));
	helper.AppendFunc(ibT("Mid"), 3, ibT("Mid(str : string, number, number)"));
	helper.AppendFunc(ibT("Find"), 3, ibT("Find(str : string, string, number)"));
	helper.AppendFunc(ibT("StrReplace"), 3, ibT("StrReplace(str : string, string, string)"));
	helper.AppendFunc(ibT("StrCountOccur"), 2, ibT("StrCountOccur(str : string, string)"));
	helper.AppendFunc(ibT("StrLineCount"), 1, ibT("StrLineCount(str : string)"));
	helper.AppendFunc(ibT("StrGetLine"), 2, ibT("StrGetLine(str : string, line : number)"));
	helper.AppendFunc(ibT("Upper"), 1, ibT("Upper(str : string)"));
	helper.AppendFunc(ibT("Lower"), 1, ibT("Lower(str : string)"));
	helper.AppendFunc(ibT("Chr"), 1, ibT("Chr(num : number)"));
	helper.AppendFunc(ibT("Asc"), 1, ibT("Asc(str : string)"));
	helper.AppendFunc(ibT("Tstr"), 2, ibT("Tstr(text : string, langCode : string)"));
	//--- Date and time:
	helper.AppendFunc(ibT("CurrentDate"), ibT("CurrentDate()"));
	helper.AppendFunc(ibT("WorkingDate"), ibT("WorkingDate()"));
	helper.AppendFunc(ibT("AddMonth"), 2, ibT("AddMonth(d : date, num : number)"));
	helper.AppendFunc(ibT("BegOfMonth"), 1, ibT("BegOfMonth(d : date)"));
	helper.AppendFunc(ibT("EndOfMonth"), 1, ibT("EndOfMonth(d : date)"));
	helper.AppendFunc(ibT("BegOfQuart"), 1, ibT("BegOfQuart(d : date)"));
	helper.AppendFunc(ibT("EndOfQuart"), 1, ibT("EndOfQuart(d : date)"));
	helper.AppendFunc(ibT("BegOfYear"), 1, ibT("BegOfYear(d : date)"));
	helper.AppendFunc(ibT("EndOfYear"), 1, ibT("EndOfYear(d : date)"));
	helper.AppendFunc(ibT("BegOfWeek"), 1, ibT("BegOfWeek(d : date)"));
	helper.AppendFunc(ibT("EndOfWeek"), 1, ibT("EndOfWeek(d : date)"));
	helper.AppendFunc(ibT("BegOfDay"), 1, ibT("BegOfDay(d : date)"));
	helper.AppendFunc(ibT("EndOfDay"), 1, ibT("EndOfDay(d : date)"));
	helper.AppendFunc(ibT("BegOfHour"), 1, ibT("BegOfHour(d : date)"));
	helper.AppendFunc(ibT("EndOfHour"), 1, ibT("EndOfHour(d : date)"));
	helper.AppendFunc(ibT("GetYear"), 1, ibT("GetYear(d : date)"));
	helper.AppendFunc(ibT("GetMonth"), 1, ibT("GetMonth(d : date)"));
	helper.AppendFunc(ibT("GetDay"), 1, ibT("GetDay(d : date)"));
	helper.AppendFunc(ibT("GetHour"), 1, ibT("GetHour(d : date)"));
	helper.AppendFunc(ibT("GetMinute"), 1, ibT("GetMinute(d : date)"));
	helper.AppendFunc(ibT("GetSecond"), 1, ibT("GetSecond(d : date)"));
	helper.AppendFunc(ibT("GetMillisecond"), 1, ibT("GetMillisecond(d : date)"));
	helper.AppendFunc(ibT("GetWeekOfYear"), 1, ibT("GetWeekOfYear(d : date)"));
	helper.AppendFunc(ibT("GetDayOfYear"), 1, ibT("GetDayOfYear(d : date)"));
	helper.AppendFunc(ibT("GetDayOfWeek"), 1, ibT("GetDayOfWeek(d : date)"));
	helper.AppendFunc(ibT("GetQuartOfYear"), 1, ibT("GetQuartOfYear(d : date)"));
	//--- File operations:
	// Order MUST match the enFileCopy..enGetTempFileName enum block above —
	// the method number is the index into this table (ibValue::ibMemberTable::
	// FindMethod), so a swap here calls the wrong case in CallAsFunc.
	helper.AppendFunc(ibT("FileCopy"), 2, ibT("FileCopy(fileSrcName : string, fileDstName : string)"));
	helper.AppendFunc(ibT("FileDelete"), 1, ibT("FileDelete(fileName : string)"));
	helper.AppendFunc(ibT("GetTempDir"), ibT("GetTempDir()"));
	helper.AppendFunc(ibT("GetTempFileName"), ibT("GetTempFileName()"));
	//--- JSON: the reference system's spelling of JSONReader.ReadValue() / JSONWriter.WriteValue(value).
	// Order MUST match enReadJSON..enWriteJSON above.
	helper.AppendFunc(ibT("ReadJSON"), 1, ibT("ReadJSON(reader : JSONReader)"));
	helper.AppendFunc(ibT("WriteJSON"), 2, ibT("WriteJSON(writer : JSONWriter, value : any)"));
	//--- Window operations: 
	helper.AppendFunc(ibT("ActiveWindow"), ibT("ActiveWindow()"));
	//--- Special:
	helper.AppendProc(ibT("Message"), 2, ibT("Message(message : string, statusMessage : statusMessage)"));
	// ⭐ THE DURABLE TWIN OF `Message` — see systemManager.h. Registered beside it because that is
	// where a reader looks for it: the two answer the same need for two different readers, one
	// watching now and one reading tomorrow.
	helper.AppendProc(ibT("WriteJournalEvent"), 4,
		ibT("WriteJournalEvent(message : string, statusMessage : statusMessage, category : string, object)"));
	helper.AppendFunc(ibT("Alert"), 1, ibT("Alert(message : string)"));
	helper.AppendFunc(ibT("Question"), 2, ibT("Question(message : string, questionMode)"));
	helper.AppendFunc(ibT("SetStatus"), 1, ibT("SetStatus(text : string)"));
	helper.AppendFunc(ibT("ClearMessages"), ibT("ClearMessages()"));
	helper.AppendFunc(ibT("SetError"), 1, ibT("SetError(string)"));
	helper.AppendFunc(ibT("Raise"), 1, ibT("Raise(string)"));
	helper.AppendFunc(ibT("ErrorDescription"), ibT("ErrorDescription()"));
	helper.AppendFunc(ibT("IsEmptyValue"), 1, ibT("IsEmptyValue(value : any)"));
	helper.AppendFunc(ibT("Evaluate"), 1, ibT("Evaluate(expr : string)"));
	helper.AppendFunc(ibT("Execute"), 1, ibT("Execute(expr : string)"));
	helper.AppendFunc(ibT("Format"), 2, ibT("Format(value : any, format : string)"));
	helper.AppendFunc(ibT("Type"), 1, ibT("Type(strType : string)"));
	helper.AppendFunc(ibT("TypeOf"), 1, ibT("TypeOf(value : any)"));
	helper.AppendFunc(ibT("Rand"), ibT("Rand()"));
	helper.AppendFunc(ibT("ArgCount"), ibT("ArgCount()"));
	helper.AppendFunc(ibT("ArgValue"), 1, ibT("ArgValue(index : number)"));
	helper.AppendFunc(ibT("ComputerName"), ibT("ComputerName()"));
	helper.AppendFunc(ibT("RunApp"), 1, ibT("RunApp(command : string)"));
	helper.AppendFunc(ibT("SetAppTitle"), 1, ibT("SetAppTitle(title : string)"));
	helper.AppendFunc(ibT("UserDir"), ibT("UserDir()"));
	helper.AppendFunc(ibT("UserName"), ibT("UserName()"));
	helper.AppendFunc(ibT("UserPassword"), ibT("UserPassword()"));
	helper.AppendFunc(ibT("ExclusiveMode"), ibT("ExclusiveMode()"));
	helper.AppendProc(ibT("SetExclusive"), 1, ibT("SetExclusive(on : boolean)"));
	helper.AppendFunc(ibT("GeneralLanguage"), ibT("GeneralLanguage()"));
	helper.AppendFunc(ibT("EndJob"), 1, ibT("EndJob(force : boolean)"));
	helper.AppendFunc(ibT("UserInterruptProcessing"), ibT("UserInterruptProcessing()"));
	helper.AppendFunc(ibT("AccessRight"), 2, ibT("AccessRight(strRole : string, metadata)"));
	helper.AppendFunc(ibT("IsInRole"), 1, ibT("IsInRole(strRole : string)"));
	helper.AppendFunc(ibT("GetCommonForm"), 3, ibT("GetCommonForm(name : string, owner : any, id : guid)"));
	helper.AppendProc(ibT("ShowCommonForm"), 3, ibT("ShowCommonForm(name : string, owner : any, id : guid)"));
	helper.AppendFunc(ibT("GetCommonTemplate"), 1, ibT("GetCommonTemplate(name : string)"));
	helper.AppendProc(ibT("BeginTransaction"), ibT("BeginTransaction()"));
	helper.AppendProc(ibT("CommitTransaction"), ibT("CommitTransaction()"));
	helper.AppendProc(ibT("RollBackTransaction"), ibT("RollBackTransaction()"));
	helper.AppendFunc(ibT("IsNull"), 1, ibT("IsNull(value : any)"));
	helper.AppendFunc(ibT("ValueIsFilled"), 1, ibT("ValueIsFilled(value : any)"));
	//--- Jobs: keep last, matching the enRunScheduledJobs..enRunJob enum tail.
	helper.AppendFunc(ibT("RunScheduledJobs"), ibT("RunScheduledJobs()"));
	helper.AppendFunc(ibT("RunJob"), 1, ibT("RunJob(name : string)"));
	helper.AppendFunc(ibT("RunBackground"), 2, ibT("RunBackground(procedure : string, args : array)"));
	//--- …and value-as-text last, matching the enJSONString / enJSONValue tail above.
	helper.AppendFunc(ibT("JSONString"), 1, ibT("JSONString(value : any)"));
	helper.AppendFunc(ibT("JSONValue"), 1, ibT("JSONValue(json : string)"));
	helper.AppendFunc(ibT("XMLString"), 1, ibT("XMLString(value : any)"));
	helper.AppendFunc(ibT("XMLValue"), 1, ibT("XMLValue(xml : string)"));
};

#include "backend/compiler/enumUnit.h"
#include "backend/system/value/valueGuid.h"
#include "backend/system/value/valueJson.h"   // ReadJSON / WriteJSON
#include "backend/backend_exception.h"   // a call this dispatcher itself refuses (Date with two arguments)

#include "backend/appData.h"

bool ibValueSystemFunction::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	if (!appData->DesignerMode()) {
		switch (lMethodNum)
		{
			//--- Basic:
		case enBoolean: pvarRetValue = Boolean(*paParams[0]); return true;
		case enNumber: pvarRetValue = Number(*paParams[0]); return true;
		// ONE ARGUMENT CONVERTS, THREE OR MORE BUILD. Two is neither, and is refused by name rather
		// than read as a year with a stray month — see the calendar form in systemManagerFunc.cpp.
		case enDate:
			if (lSizeArray >= 3)
				pvarRetValue = Date((int)paParams[0]->GetInteger(), (int)paParams[1]->GetInteger(),
					(int)paParams[2]->GetInteger(),
					lSizeArray > 3 ? (int)paParams[3]->GetInteger() : 0,
					lSizeArray > 4 ? (int)paParams[4]->GetInteger() : 0,
					lSizeArray > 5 ? (int)paParams[5]->GetInteger() : 0);
			else if (lSizeArray == 2)
				ibBackendCoreException::Error(
					_("Date takes one value to convert, or a year, a month and a day"));
			else if (lSizeArray == 1)
				pvarRetValue = Date(*paParams[0]);
			else
				pvarRetValue = ibValue(ibValueTypes::TYPE_DATE);
			return true;
		case enString: pvarRetValue = String(*paParams[0]); return true;
			//--- Math:
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
			//--- Strings:  
		case enStrLen: pvarRetValue = StrLen(*paParams[0]); return true;
		case enIsBlankString: pvarRetValue = IsBlankString(*paParams[0]); return true;
		case enTrimL: pvarRetValue = TrimL(*paParams[0]); return true;
		case enTrimR: pvarRetValue = TrimR(*paParams[0]); return true;
		case enTrimAll: pvarRetValue = TrimAll(*paParams[0]); return true;
		case enLeft: pvarRetValue = Left(*paParams[0], paParams[1]->GetInteger()); return true;
		case enRight: pvarRetValue = Right(*paParams[0], paParams[1]->GetInteger()); return true;
			// An omitted length means TO THE END, not one character.
		case enMid: pvarRetValue = Mid(*paParams[0], (size_t)paParams[1]->GetInteger(),
			lSizeArray > 2 ? (size_t)paParams[2]->GetInteger() : std::numeric_limits<size_t>::max()); return true;
		case enFind: pvarRetValue = Find(*paParams[0], *paParams[1], lSizeArray > 2 ? paParams[2]->GetInteger() : 0); return true;
		case enStrReplace: pvarRetValue = StrReplace(*paParams[0], *paParams[1], *paParams[2]); return true;
		case enStrCountOccur: pvarRetValue = StrCountOccur(*paParams[0], *paParams[1]); return true;
		case enStrLineCount: pvarRetValue = StrLineCount(*paParams[0]); return true;
		case enStrGetLine: pvarRetValue = StrGetLine(*paParams[0], paParams[1]->GetInteger()); return true;
		case enUpper: pvarRetValue = Upper(*paParams[0]); return true;
		case enLower: pvarRetValue = Lower(*paParams[0]); return true;
		case enChr: pvarRetValue = Chr(paParams[0]->GetInteger()); return true;
		case enAsc: pvarRetValue = Asc(*paParams[0]); return true;
		case enTStr: pvarRetValue = TStr(*paParams[0], lSizeArray > 1 ? paParams[1]->GetString() : ibT("")); return true;
			//--- Date and time:
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
		case enBegOfHour: pvarRetValue = BegOfHour(*paParams[0]); return true;
		case enEndOfHour: pvarRetValue = EndOfHour(*paParams[0]); return true;
		case enGetYear: pvarRetValue = GetYear(*paParams[0]); return true;
		case enGetMonth: pvarRetValue = GetMonth(*paParams[0]); return true;
		case enGetDay: pvarRetValue = GetDay(*paParams[0]); return true;
		case enGetHour: pvarRetValue = GetHour(*paParams[0]); return true;
		case enGetMinute: pvarRetValue = GetMinute(*paParams[0]); return true;
		case enGetSecond: pvarRetValue = GetSecond(*paParams[0]); return true;
		case enGetMillisecond: pvarRetValue = GetMillisecond(*paParams[0]); return true;
		case enGetWeekOfYear: pvarRetValue = GetWeekOfYear(*paParams[0]); return true;
		case enGetDayOfYear: pvarRetValue = GetDayOfYear(*paParams[0]); return true;
		case enGetDayOfWeek: pvarRetValue = GetDayOfWeek(*paParams[0]); return true;
		case enGetQuartOfYear: pvarRetValue = GetQuartOfYear(*paParams[0]); return true;
			//--- File operations:
		case enFileCopy: pvarRetValue = CopyFile(paParams[0]->GetString(), paParams[1]->GetString()); return true;
		case enFileDelete: pvarRetValue = DeleteFile(paParams[0]->GetString()); return true;
		case enGetTempDir: pvarRetValue = GetTempDir(); return true;
		case enGetTempFileName: pvarRetValue = GetTempFileName(); return true;
			//--- JSON:
		case enReadJSON: {
			ibValueJsonReader* const reader = lSizeArray > 0 ? dynamic_cast<ibValueJsonReader*>(paParams[0]->GetRef()) : nullptr;
			if (reader == nullptr)
				ibBackendCoreException::Error(_("ReadJSON: the argument is a JSONReader"));
			pvarRetValue = reader->ReadValue();
			return true;
		}
		case enWriteJSON: {
			ibValueJsonWriter* const writer = lSizeArray > 1 ? dynamic_cast<ibValueJsonWriter*>(paParams[0]->GetRef()) : nullptr;
			if (writer == nullptr)
				ibBackendCoreException::Error(_("WriteJSON: the arguments are a JSONWriter and the value to write"));
			writer->WriteValue(*paParams[1]);
			return true;
		}
			//--- Window operations: 
		case enActiveWindow: pvarRetValue = ActiveWindow(); return true;
			//--- Special:
		case enMessage:
			Message(paParams[0]->GetString(),
				lSizeArray > 1 ? paParams[1]->ConvertToEnumValue<ibStatusMessage>() : ibStatusMessage::ibStatusMessage_Information);
			return true;
		case enWriteJournalEvent:
			WriteJournalEvent(paParams[0]->GetString(),
				lSizeArray > 1 ? paParams[1]->ConvertToEnumValue<ibStatusMessage>() : ibStatusMessage::ibStatusMessage_Information,
				lSizeArray > 2 ? paParams[2]->GetString() : ibString(),
				lSizeArray > 3 ? *paParams[3] : ibValue());
			return true;
		case enAlert: Alert(paParams[0]->GetString()); return true;
		case enQuestion: pvarRetValue = Question(paParams[0]->GetString(),
			lSizeArray > 1 ? paParams[1]->ConvertToEnumValue<ibQuestionMode>() : ibQuestionMode::ibQuestionMode_OK);
			return true;
		case enSetStatus: SetStatus(paParams[0]->GetString()); return true;
		case enClearMessage: ClearMessage(); return true;
		case enSetError: SetError(paParams[0]->GetString()); return true;
		case enRaise: Raise(paParams[0]->GetString()); return true;
		case enErrorDescription: pvarRetValue = ErrorDescription(); return true;
		case enIsEmptyValue: pvarRetValue = IsEmptyValue(*paParams[0]); return true;
		case enIsNull: pvarRetValue = IsNull(*paParams[0]); return true;
		case enValueIsFilled: pvarRetValue = ValueIsFilled(*paParams[0]); return true;
		case enRunScheduledJobs: pvarRetValue = RunScheduledJobs(); return true;
		case enRunJob: pvarRetValue = RunJob(paParams[0]->GetString()); return true;
		case enRunBackground:
			pvarRetValue = RunBackground(paParams[0]->GetString(),
				lSizeArray > 1 ? paParams[1] : nullptr);
			return true;
		case enJSONString: pvarRetValue = JSONString(*paParams[0]); return true;
		case enJSONValue: pvarRetValue = JSONValue(paParams[0]->GetString()); return true;
		case enXMLString: pvarRetValue = XMLString(*paParams[0]); return true;
		case enXMLValue: pvarRetValue = XMLValue(paParams[0]->GetString()); return true;
		case enEvaluate: pvarRetValue = Evaluate(paParams[0]->GetString()); return true;
		case enExecute: Execute(paParams[0]->GetString()); return true;
		case enFormat: pvarRetValue = Format(*paParams[0], paParams[1]->GetString()); return true;
		case enType: pvarRetValue = Type(*paParams[0]); return true;
		case enTypeOf: pvarRetValue = TypeOf(*paParams[0]); return true;
		case enRand: pvarRetValue = Rand(); return true;
		case enArgCount: pvarRetValue = ArgCount(); return true;
		case enArgValue: pvarRetValue = ArgValue(paParams[0]->GetInteger()); return true;
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
			ibFormRequest(paParams[0]->GetString()),
			lSizeArray > 1 ? paParams[1]->ConvertToType<ibBackendControlFrame>() : nullptr,
			lSizeArray > 2 ? paParams[2]->ConvertToType<ibValueGuid>() : nullptr);
			return true;
		case enShowCommonForm: ShowCommonForm(
			ibFormRequest(paParams[0]->GetString()),
			lSizeArray > 1 ? paParams[1]->ConvertToType<ibBackendControlFrame>() : nullptr,
			lSizeArray > 2 ? paParams[2]->ConvertToType<ibValueGuid>() : nullptr);
			return true;
		case enGetCommonTemplate:
			pvarRetValue = GetCommonTemplate(paParams[0]->GetString());
			return true;
			//--- Transactions:
		case enBeginTransaction: BeginTransaction(); return true;
		case enCommitTransaction: CommitTransaction(); return true;
		case enRollBackTransaction: RollBackTransaction(); return true;
		}
	}
	else
	{
		switch (lMethodNum)
		{
			//--- Special:
		case enType:
			pvarRetValue = Type(*paParams[0]);
			return true;
		case enTypeOf:
			pvarRetValue = TypeOf(*paParams[0]);
			return true;

		case enGetCommonForm:
			pvarRetValue = GetCommonForm(ibFormRequest(paParams[0]->GetString()),
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
			//--- Special:
		case enMessage:
			Message(paParams[0]->GetString(),
				lSizeArray > 1 ? paParams[1]->ConvertToEnumValue<ibStatusMessage>() : ibStatusMessage::ibStatusMessage_Information);
			return true;
		case enWriteJournalEvent:
			WriteJournalEvent(paParams[0]->GetString(),
				lSizeArray > 1 ? paParams[1]->ConvertToEnumValue<ibStatusMessage>() : ibStatusMessage::ibStatusMessage_Information,
				lSizeArray > 2 ? paParams[2]->GetString() : ibString(),
				lSizeArray > 3 ? *paParams[3] : ibValue());
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
			ibFormRequest(paParams[0]->GetString()),
			lSizeArray > 1 ? paParams[1]->ConvertToType<ibBackendControlFrame>() : nullptr,
			lSizeArray > 2 ? paParams[2]->ConvertToType<ibValueGuid>() : nullptr);
			return true;
			//--- Transactions:
		case enBeginTransaction: BeginTransaction(); return true;
		case enCommitTransaction: CommitTransaction(); return true;
		case enRollBackTransaction: RollBackTransaction(); return true;
		}
	}
	else
	{
		switch (lMethodNum)
		{
			//--- Special:
		case enShowCommonForm:
			ShowCommonForm(ibFormRequest(paParams[0]->GetString()),
				lSizeArray > 1 ? paParams[1]->ConvertToType<ibBackendControlFrame>() : nullptr,
				lSizeArray > 2 ? paParams[2]->ConvertToType<ibValueGuid>() : nullptr);
			return true;
		}
	}

	return false;
}

//**********************************************************************

ibDateTime ibValueSystemFunction::ms_workDate = ibDateTime::Now();

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

CONTEXT_TYPE_REGISTER(ibValueSystemFunction, "SystemManager", context_to_clsid("CO_SYSM"));

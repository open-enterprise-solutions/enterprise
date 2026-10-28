#ifndef _SYSTEM_OBJECTS_H__
#define _SYSTEM_OBJECTS_H__

#include <limits>   // Mid's "to the end of the string" default

#include "backend/backend.h"
#include "backend/compiler/value.h"
#include "backend/createRequest.h"   // ibFormRequest — a common form is opened with one too

//-- Constants:
#define PageBreak ibT("\n\n")
#define LineBreak ibT("\n")
#define TabSymbol ibT("\t")

#include "backend/system/systemEnum.h"

void ibValueSystemFunction_BindNames(ibValue::ibMemberTable& helper, const ibValue* ctx);

class BACKEND_API ibValueSystemFunction : public ibValueStaticMembers<&ibValueSystemFunction_BindNames> {
	public:
	static ibDateTime ms_workDate;
public:

	//--- Basic:
	static bool Boolean(const ibValue& cValue);
	static ibNumber Number(const ibValue& cValue);
	static ibDateTime Date(const ibValue& cValue);

	// ⭐ THE CALENDAR FORM, which is how a person writes a date they know: Date(2026, 9, 2). The
	// help has published this signature all along and the compiler took one argument only, so
	// every first attempt at a literal date was refused ("Too many parameters", measured
	// 2026-09-02). The one-argument form stays what it was — a CONVERSION, of a string or of a
	// number of seconds — because those are different questions and only one of them is a
	// calendar.
	static ibDateTime Date(int year, int month, int day,
		int hour = 0, int minute = 0, int second = 0);

	static ibString String(const ibValue& cValue);

	//--- Math:
	static ibNumber Round(const ibValue& cValue, int precision = 0, ibRoundMode mode = ibRoundMode::ibRoundMode_Round15as20);
	static ibValue Int(const ibValue& cNumber);
	static ibNumber Log10(const ibValue& cValue);
	static ibNumber Ln(const ibValue& cValue);
	static ibValue Max(ibValue** paParams, const long lSizeArray);
	static ibValue Min(ibValue** paParams, const long lSizeArray);
	static ibValue Sqrt(const ibValue& cValue);

	//--- Strings:
	static int StrLen(const ibValue& cValue);
	static bool IsBlankString(const ibValue& cValue);
	static ibString TrimL(const ibValue& cValue);
	static ibString TrimR(const ibValue& cValue);
	static ibString TrimAll(const ibValue& cValue);
	static ibString Left(const ibValue& cValue, unsigned int nCount);
	static ibString Right(const ibValue& cValue, unsigned int nCount);
	// nFirst is 1-BASED, like Find's answer — see the note on the definition.
	// nCount defaults to "to the end of the string".
	static ibString Mid(const ibValue& cValue, size_t nFirst, size_t nCount = std::numeric_limits<size_t>::max());
	static unsigned int Find(const ibValue& cValue, const ibValue& cValue2, unsigned int nStart);
	static ibString StrReplace(const ibValue& cSource, const ibValue& cValue1, const ibValue& cValue2);
	static int StrCountOccur(const ibValue& cSource, const ibValue& cValue1);
	static int StrLineCount(const ibValue& cSource);
	static ibString StrGetLine(const ibValue& cValue, unsigned int nLine);
	static ibString Upper(const ibValue& cSource);
	static ibString Lower(const ibValue& cSource);
	static ibString Chr(short nCode);
	static short Asc(const ibValue& cSource);
	static ibString TStr(const ibValue& cSource, const ibValue& cLanguage);

	//--- Date and time:
	static ibValue CurrentDate();
	static ibValue WorkingDate();
	static ibValue AddMonth(const ibValue& cData, int nMonthAdd = 1);
	static ibValue BegOfMonth(const ibValue& cData);
	static ibValue EndOfMonth(const ibValue& cData);
	static ibValue BegOfQuart(const ibValue& cData);
	static ibValue EndOfQuart(const ibValue& cData);
	static ibValue BegOfYear(const ibValue& cData);
	static ibValue EndOfYear(const ibValue& cData);
	static ibValue BegOfWeek(const ibValue& cData);
	static ibValue EndOfWeek(const ibValue& cData);
	static ibValue BegOfDay(const ibValue& cData);
	static ibValue EndOfDay(const ibValue& cData);
	static int GetYear(const ibValue& cData);
	static int GetMonth(const ibValue& cData);
	static int GetDay(const ibValue& cData);
	static int GetHour(const ibValue& cData);
	static int GetMinute(const ibValue& cData);
	static int GetSecond(const ibValue& cData);
	static int GetWeekOfYear(const ibValue& cData);
	static int GetDayOfYear(const ibValue& cData);
	static int GetDayOfWeek(const ibValue& cData);
	static int GetQuartOfYear(const ibValue& cData);

	//--- File operations:
	static bool CopyFile(const ibString& src, const ibString& dst);
	static bool DeleteFile(const ibString& file);
	static ibString GetTempDir();
	static ibString GetTempFileName();

	//--- Window operations:
	static class ibBackendValueForm* ActiveWindow();

	//--- Notifications:
	static void Message(const ibString& strMessage, ibStatusMessage status = ibStatusMessage::ibStatusMessage_Information);

	// ⭐⭐ WRITE A LINE INTO THE REGISTRATION JOURNAL — the durable record, stamped with the session
	// that wrote it, readable afterwards by anyone (journal_read, and the person's own journal).
	//
	// ⚠ IT IS NOT A SECOND `Message`, AND THE DIFFERENCE MATTERS MOST WHERE `Message` FAILS. A
	// message is addressed to whoever is watching a window; a BACKGROUND session is tied to nobody,
	// so its messages reach no one at all. Code run as a background job therefore has no way to say
	// what it is doing except this — which is why filling code, posting code and anything else sent
	// to run unattended reports HERE.
	// ⚠ THE ARGUMENTS ARE THE ROW'S OWN COLUMNS, not a shape invented here: a journal line already
	// has a level, a text, a CATEGORY (event_type) and the OBJECT it is about (ref_guid +
	// ref_meta_id). Handing a reference in `object` is what makes a line findable later by the thing
	// it happened to, rather than by reading messages.
	//
	// Message first, because it is the one a caller always has; everything else has a default, so
	// the shortest useful call is one argument - the same shape as `Message` beside it.
	static void WriteJournalEvent(const ibString& strMessage,
		ibStatusMessage status = ibStatusMessage::ibStatusMessage_Information,
		const ibString& strEvent = ibEmptyString,
		const ibValue& objectValue = ibValue());
	static void Alert(const ibString& strMessage);
	static ibValue Question(const ibString& strMessage, ibQuestionMode mode = ibQuestionMode::ibQuestionMode_OK);
	static void SetStatus(const ibString& sStatus);
	static void ClearMessage();
	static void SetError(const ibString& strError);
	static void Raise(const ibString& strError);
	static ibString ErrorDescription();
	static bool IsEmptyValue(const ibValue& cData);
	static bool IsNull(const ibValue& cData);
	static bool ValueIsFilled(const ibValue& cData);

	// ⭐⭐ A RUNTIME VALUE AS TEXT, AND BACK. The engine has packed values into an ibDataNode for a
	// while (value.h `Serialize` / `Deserialize`, ibMetaData's door for the types only a
	// configuration has), and a provider has been able to write that node as JSON — but nothing in
	// the LANGUAGE could ask for either, so the whole road was reachable from C++ alone.
	//
	// These two open it: a script hands over any value it holds and gets text it can print, store,
	// send or compare — and hands text back to get the value again. What it buys beyond printing:
	// a structure survives the trip (a printed form does not), and an assistant on the other end of
	// the debugger's sandbox can BUILD a value by writing JSON, which is the only way to construct
	// one from outside the process (Max, 2026-09-02: *"as a bonus, with these you can create
	// runtime values just by passing JSON"*).
	static ibString SerializeValue(const ibValue& cData);
	static ibValue  DeserializeValue(const ibString& strJson);

	// (⛔ NO MESSAGE CAPTURE HERE. Getting the lines a run printed is not a new mechanism: the
	//  debug channel already carries messages from a running application to whoever is attached —
	//  `CommandId_MessageFromServer`, sent by ibDebuggerServer::SendErrorToClient. It looked like
	//  it did not work because that sender was called from NOWHERE. A road with no writer, not a
	//  missing road: Message hands them to it below.)
	static ibValue Evaluate(const ibString& expression);
	static void Execute(const ibString& sCode);
	static ibString Format(ibValue& cData, const ibString& fmt = ibEmptyString);
	static ibValue Type(const ibValue& cTypeName);
	static ibValue TypeOf(const ibValue& cData);
	static int Rand();
	static int ArgCount();
	static ibString ArgValue(int n);
	static ibString ComputerName();
	static void RunApp(const ibString& sCommand);
	static void SetAppTitle(const ibString& sTitle);
	static ibString UserDir();
	static ibString UserName();
	static ibString UserPassword();
	static bool ExclusiveMode();
	static void SetExclusive(bool on);
	static ibString GeneralLanguage();
	static ibValue CurrentLanguage();
	static ibValue LanguageObject(class ibMetaData* meta, const ibString& code);
	static bool PrivilegedMode();
	static void SetPrivilegedMode(bool on);
	static void EndJob(bool force = false);

	static void UserInterruptProcessing();

	static bool AccessRight(const ibString& strRoleName, const ibValue& cData);
	static bool IsInRole(const ibValue& cData);

	static ibValue GetCommonForm(const ibFormRequest& request, class ibBackendControlFrame* owner, class ibValueGuid* unique);
	static void ShowCommonForm(const ibFormRequest& request, class ibBackendControlFrame* owner, class ibValueGuid* unique);

	static ibValue GetCommonTemplate(const ibString& strTemplateName);

	static void BeginTransaction();
	static void CommitTransaction();
	static void RollBackTransaction();

	//--- Jobs:
	// The tick. Launches every job that is due and returns how many were launched;
	// does NOT wait for any of them. A file deployment has no daemon to keep time,
	// so the schedule advances only when something calls this — the platform timer
	// on a desktop host, the compute server's own loop where one exists, or script
	// that wants to force a round. See docs/private/job-manager.md § "The tick".
	static int  RunScheduledJobs();

	// Run one job now, ignoring its interval and window. False when the name is
	// unknown or that job is already running. Without this a job could only be
	// exercised by waiting out its schedule, which makes a misbehaving tenant
	// impossible to tell apart from a misbehaving manager.
	static bool RunJob(const ibString& strJobName);

	// Start `procedure` on a session of its own and return a BackgroundJob value —
	// the caller may wait on it, poll it, cancel it, or drop it and let the work
	// finish unattended.
	//
	// `procedure` is `ModuleName.MethodName`: a PUBLIC method of a common module.
	// The run adopts the CALLER's identity, so it sees exactly what the caller
	// sees. `args` is an Array whose elements are passed positionally; every one
	// of them must be transferable (ibValue::IsTransferable) — passing a form, an
	// object or a record set throws here rather than failing in the background.
	static ibValue RunBackground(const ibString& strProcedureName, ibValue* pArgs);

public:

	ibValueSystemFunction() :
		ibValueStaticMembers(ibValueTypes::TYPE_VALUE, true) {
	}

	virtual ~ibValueSystemFunction() {
	}

	//****************************************************************************
	//*                              Support methods                             *
	//****************************************************************************

	// DoGetPMethods (protected) + Shared<&ibValueSystemFunction_BindNames> come from the base.

	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray);
	virtual bool CallAsProc(const long lMethodNum, ibValue** paParams, const long lSizeArray);

	//check is empty
	virtual bool IsEmpty() const {
		return false;
	}
};

#endif

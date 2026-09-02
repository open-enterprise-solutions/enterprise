#ifndef _SYSTEM_OBJECTS_H__
#define _SYSTEM_OBJECTS_H__

#include "backend/backend.h"
#include "backend/compiler/value.h"

//-- Constants:
#define PageBreak wxT("\n\n")
#define LineBreak wxT("\n")
#define TabSymbol wxT("\t")

#include "backend/system/systemEnum.h"

void ibValueSystemFunction_BindNames(ibValue::ibMemberTable& helper, const ibValue* ctx);

class BACKEND_API ibValueSystemFunction : public ibValueStaticMembers<&ibValueSystemFunction_BindNames> {
	public:
	static wxDateTime ms_workDate;
public:

	//--- Basic:
	static bool Boolean(const ibValue& cValue);
	static ibNumber Number(const ibValue& cValue);
	static wxLongLong_t Date(const ibValue& cValue);
	static wxString String(const ibValue& cValue);

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
	static ibString Mid(const ibValue& cValue, unsigned int nFirst, unsigned int nCount);
	static unsigned int Find(const ibValue& cValue, const ibValue& cValue2, unsigned int nStart);
	static ibString StrReplace(const ibValue& cSource, const ibValue& cValue1, const ibValue& cValue2);
	static int StrCountOccur(const ibValue& cSource, const ibValue& cValue1);
	static int StrLineCount(const ibValue& cSource);
	static wxString StrGetLine(const ibValue& cValue, unsigned int nLine);
	static ibString Upper(const ibValue& cSource);
	static ibString Lower(const ibValue& cSource);
	static wxString Chr(short nCode);
	static short Asc(const ibValue& cSource);
	static wxString TStr(const ibValue& cSource, const ibValue& cLanguage);

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
	static bool CopyFile(const wxString& src, const wxString& dst);
	static bool DeleteFile(const wxString& file);
	static wxString GetTempDir();
	static wxString GetTempFileName();

	//--- Window operations:
	static class ibBackendValueForm* ActiveWindow();

	//--- Notifications:
	static void Message(const wxString& strMessage, ibStatusMessage status = ibStatusMessage::ibStatusMessage_Information);
	static void Alert(const wxString& strMessage);
	static ibValue Question(const wxString& strMessage, ibQuestionMode mode = ibQuestionMode::ibQuestionMode_OK);
	static void SetStatus(const wxString& sStatus);
	static void ClearMessage();
	static void SetError(const wxString& strError);
	static void Raise(const wxString& strError);
	static wxString ErrorDescription();
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
	static wxString SerializeValue(const ibValue& cData);
	static ibValue  DeserializeValue(const wxString& strJson);

	// (⛔ NO MESSAGE CAPTURE HERE. Getting the lines a run printed is not a new mechanism: the
	//  debug channel already carries messages from a running application to whoever is attached —
	//  `CommandId_MessageFromServer`, sent by ibDebuggerServer::SendErrorToClient. It looked like
	//  it did not work because that sender was called from NOWHERE. A road with no writer, not a
	//  missing road: Message hands them to it below.)
	static ibValue Evaluate(const wxString& expression);
	static void Execute(const wxString& sCode);
	static wxString Format(ibValue& cData, const wxString& fmt = wxEmptyString);
	static ibValue Type(const ibValue& cTypeName);
	static ibValue TypeOf(const ibValue& cData);
	static int Rand();
	static int ArgCount();
	static wxString ArgValue(int n);
	static wxString ComputerName();
	static void RunApp(const wxString& sCommand);
	static void SetAppTitle(const wxString& sTitle);
	static wxString UserDir();
	static wxString UserName();
	static wxString UserPassword();
	static bool ExclusiveMode();
	static void SetExclusive(bool on);
	static wxString GeneralLanguage();
	static void EndJob(bool force = false);

	static void UserInterruptProcessing();

	static bool AccessRight(const wxString& strRoleName, const ibValue& cData);
	static bool IsInRole(const ibValue& cData);

	static ibValue GetCommonForm(const wxString& strFormName, class ibBackendControlFrame* owner, class ibValueGuid* unique);
	static void ShowCommonForm(const wxString& strFormName, class ibBackendControlFrame* owner, class ibValueGuid* unique);

	static ibValue GetCommonTemplate(const wxString& strTemplateName);

	static void BeginTransaction();
	static void CommitTransaction();
	static void RollBackTransaction();

	//--- Jobs:
	// The tick. Launches every job that is due and returns how many were launched;
	// does NOT wait for any of them. A file deployment has no daemon to keep time,
	// so the schedule advances only when something calls this — the platform timer
	// on a desktop host, the compute server's own loop where one exists, or script
	// that wants to force a round. See docs/job-manager.md § "The tick".
	static int  RunScheduledJobs();

	// Run one job now, ignoring its interval and window. False when the name is
	// unknown or that job is already running. Without this a job could only be
	// exercised by waiting out its schedule, which makes a misbehaving tenant
	// impossible to tell apart from a misbehaving manager.
	static bool RunJob(const wxString& strJobName);

	// Start `procedure` on a session of its own and return a BackgroundJob value —
	// the caller may wait on it, poll it, cancel it, or drop it and let the work
	// finish unattended.
	//
	// `procedure` is `ModuleName.MethodName`: a PUBLIC method of a common module.
	// The run adopts the CALLER's identity, so it sees exactly what the caller
	// sees. `args` is an Array whose elements are passed positionally; every one
	// of them must be transferable (ibValue::IsTransferable) — passing a form, an
	// object or a record set throws here rather than failing in the background.
	static ibValue RunBackground(const wxString& strProcedureName, ibValue* pArgs);

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

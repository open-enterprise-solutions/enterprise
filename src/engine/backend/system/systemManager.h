#ifndef _SYSTEM_OBJECTS_H__
#define _SYSTEM_OBJECTS_H__

#include "backend/backend.h"
#include "backend/compiler/value.h"

//--Константы:
#define PageBreak wxT("\n\n")
#define LineBreak wxT("\n")
#define TabSymbol wxT("\t")

#include "backend/system/systemEnum.h"

class BACKEND_API ibValueSystemFunction : public ibValue {
	public:
	static wxDateTime ms_workDate;
public:

	//--- Базовые:
	static bool Boolean(const ibValue& cValue);
	static ibNumber Number(const ibValue& cValue);
	static wxLongLong_t Date(const ibValue& cValue);
	static wxString String(const ibValue& cValue);

	//--- Математика:
	static ibNumber Round(const ibValue& cValue, int precision = 0, ibRoundMode mode = ibRoundMode::ibRoundMode_Round15as20);
	static ibValue Int(const ibValue& cNumber);
	static ibNumber Log10(const ibValue& cValue);
	static ibNumber Ln(const ibValue& cValue);
	static ibValue Max(ibValue** paParams, const long lSizeArray);
	static ibValue Min(ibValue** paParams, const long lSizeArray);
	static ibValue Sqrt(const ibValue& cValue);

	//--- Строки:
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

	//--- Дата и время:
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

	//--- Работа с файлами:
	static bool CopyFile(const wxString& src, const wxString& dst);
	static bool DeleteFile(const wxString& file);
	static wxString GetTempDir();
	static wxString GetTempFileName();

	//--- Работа с окнами:
	static class ibBackendValueForm* ActiveWindow();

	//--- Уведомления:
	static void Message(const wxString& strMessage, ibStatusMessage status = ibStatusMessage::ibStatusMessage_Information);
	static void Alert(const wxString& strMessage);
	static ibValue Question(const wxString& strMessage, ibQuestionMode mode = ibQuestionMode::ibQuestionMode_OK);
	static void SetStatus(const wxString& sStatus);
	static void ClearMessage();
	static void SetError(const wxString& strError);
	static void Raise(const wxString& strError);
	static wxString ErrorDescription();
	static bool IsEmptyValue(const ibValue& cData);
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

public:

	ibValueSystemFunction() :
		ibValue(ibValueTypes::TYPE_VALUE, true), m_methodHelper(new ibValueMethodHelper) {
	}

	virtual ~ibValueSystemFunction() {
		wxDELETE(m_methodHelper);
	}

	//****************************************************************************
	//*                              Support methods                             *
	//****************************************************************************

	virtual ibValueMethodHelper* GetPMethods() const {
		//PrepareNames();
		return m_methodHelper;
	}

	virtual void PrepareNames() const;

	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray);
	virtual bool CallAsProc(const long lMethodNum, ibValue** paParams, const long lSizeArray);

	//check is empty
	virtual bool IsEmpty() const {
		return false;
	}

protected:

	ibValueMethodHelper* m_methodHelper;
};

#endif

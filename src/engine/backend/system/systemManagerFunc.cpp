////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : system objects 
////////////////////////////////////////////////////////////////////////////

#include "systemManager.h"

#include "backend/databaseLayer/databaseLayer.h"
#include "backend/metaCollection/metaFormObject.h"
#include "backend/metadataConfiguration.h"

#include "backend/backend_mainFrame.h"
#include "backend/backend_form.h"

#include "backend/compiler/translateCode.h"
#include "backend/compiler/procUnit.h"
#include "backend/appData.h"
#include "backend/session/session.h"

#include "systemManagerEnum.h"

//--- Базовые:
bool ibValueSystemFunction::Boolean(const ibValue& cValue)
{
	return cValue.GetBoolean();
}

ibNumber ibValueSystemFunction::Number(const ibValue& cValue)
{
	return cValue.GetNumber();
}

wxLongLong_t ibValueSystemFunction::Date(const ibValue& cValue)
{
	return cValue.GetDate();
}

wxString ibValueSystemFunction::String(const ibValue& cValue)
{
	return cValue.GetString();
}

//---Математические:
ibNumber ibValueSystemFunction::Round(const ibValue& cValue, int precision, ibRoundMode mode)
{
	ibNumber fNumber = cValue.GetNumber();
	if (precision > MAX_PRECISION_NUMBER) precision = MAX_PRECISION_NUMBER;

	// ibNumber::Round implements half-away-from-zero. The Round15as20 mode matches
	// that exactly; the default mode is "round half down" (only digits >= 6 round
	// up). Approximated by subtracting 0.4 of the last kept place's unit before
	// rounding for the default mode — equivalent to old ttmath behaviour.
	if (mode == ibRoundMode::ibRoundMode_Round15as20) {
		return fNumber.Round(precision);
	}

	// Default: round half down. Build adjustment = 0.4 * 10^-precision and shrink
	// magnitude so anything < .5 stays down, .5..<.6 rounds down, .6..<1 rounds up.
	ibNumber adjust = ibNumber(4);
	for (int i = 0; i < precision + 1; ++i) adjust /= ibNumber(10);
	if (fNumber.IsSign()) fNumber += adjust;
	else                  fNumber -= adjust;
	return fNumber.Round(precision);
}

ibValue ibValueSystemFunction::Int(const ibValue& cValue)
{
	return ibValue(cValue.GetNumber().Trunc());
}

ibNumber ibValueSystemFunction::Log10(const ibValue& cValue)
{
	return cValue.GetNumber().Log(ibNumber(10));
}

ibNumber ibValueSystemFunction::Ln(const ibValue& cValue)
{
	ibNumber fNumber = cValue.GetNumber();
	return std::log(fNumber.ToDouble());
}

ibValue ibValueSystemFunction::Max(ibValue** paParams, const long lSizeArray)
{
	ibValue* maxValue = paParams[0]; int i = 1;
	while (i < lSizeArray) {
		if (paParams[i]->GetNumber() > maxValue->GetNumber())
			maxValue = paParams[i++];
	}

	return maxValue;
}

ibValue ibValueSystemFunction::Min(ibValue** paParams, const long lSizeArray)
{
	ibValue* minValue = paParams[0]; int i = 1;
	while (i < lSizeArray) {
		if (paParams[i]->GetNumber() < minValue->GetNumber())
			minValue = paParams[i++];
	}
	return minValue;
}

ibValue ibValueSystemFunction::Sqrt(const ibValue& cValue)
{
	ibNumber fNumber = cValue.GetNumber();
	if (fNumber.Sqrt() == 0)
		return fNumber;

	ibBackendCoreException::Error(_("Incorrect argument value for built-in function (Sqrt)"));
	return ibValue();
}

//---Строковые:
int ibValueSystemFunction::StrLen(const ibValue& cValue)
{
	wxString stringValue = cValue.GetString();
	return stringValue.Length();
}

bool ibValueSystemFunction::IsBlankString(const ibValue& cValue)
{
	wxString stringValue = cValue.GetString();
	stringValue.Trim(true);
	stringValue.Trim(false);
	return stringValue.IsEmpty();
}

wxString ibValueSystemFunction::TrimL(const ibValue& cValue)
{
	wxString stringValue = cValue.GetString();
	stringValue.Trim(false);
	return stringValue;
}

wxString ibValueSystemFunction::TrimR(const ibValue& cValue)
{
	wxString stringValue = cValue.GetString();
	stringValue.Trim(true);
	return stringValue;
}

wxString ibValueSystemFunction::TrimAll(const ibValue& cValue)
{
	wxString stringValue = cValue.GetString();
	stringValue.Trim(true);
	stringValue.Trim(false);
	return stringValue;
}

wxString ibValueSystemFunction::Left(const ibValue& cValue, unsigned int nCount)
{
	wxString stringValue = cValue.GetString();
	return stringValue.Left(nCount);
}

wxString ibValueSystemFunction::Right(const ibValue& cValue, unsigned int nCount)
{
	wxString stringValue = cValue.GetString();
	return stringValue.Right(nCount);
}

wxString ibValueSystemFunction::Mid(const ibValue& cValue, unsigned int nFirst, unsigned int nCount)
{
	wxString stringValue = cValue.GetString();
	return stringValue.Mid(nFirst, nCount);
}

unsigned int ibValueSystemFunction::Find(const ibValue& cValue, const ibValue& cValue2, unsigned int nStart)
{
	if (nStart < 1) nStart = 1;
	wxString stringValue = cValue.GetString();
	return stringValue.find(cValue2.GetString(), nStart - 1) + 1;
}

wxString ibValueSystemFunction::StrReplace(const ibValue& cSource, const ibValue& cValue1, const ibValue& cValue2)
{
	wxString stringValue = cSource.GetString();
	stringValue.Replace(cValue1.GetString(), cValue2.GetString());
	return stringValue;
}

int ibValueSystemFunction::StrCountOccur(const ibValue& cSource, const ibValue& cValue1)
{
	wxString stringValue = cSource.GetString();
	return stringValue.find(cValue1.GetString());
}

int ibValueSystemFunction::StrLineCount(const ibValue& cSource)
{
	wxString stringValue = cSource.GetString();
	return stringValue.find('\n') + 1;
}

wxString ibValueSystemFunction::StrGetLine(const ibValue& cValue, unsigned int nLine)
{
	if (nLine == 0) return wxEmptyString;

	const wxString src = cValue.GetString();
	if (src.IsEmpty()) return wxEmptyString;

	// Per-thread cache: callers usually iterate lines sequentially (1, 2, 3...);
	// remember where the last requested line started so the next call resumes
	// from there instead of rescanning from offset 0. Worker-pool-safe: no
	// shared static state across threads.
	struct Cache {
		wxString     source;
		size_t       startPos = 0;
		unsigned int line     = 1;
	};
	thread_local Cache cache;

	size_t       pos     = 0;
	unsigned int curLine = 1;
	if (cache.source == src && cache.line <= nLine) {
		pos     = cache.startPos;
		curLine = cache.line;
	}

	const size_t len = src.length();
	while (true) {
		// Find next line break — handles \r\n, \n, and lone \r.
		const size_t brk = src.find_first_of(wxT("\r\n"), pos);
		const size_t lineEnd = (brk == wxString::npos) ? len : brk;

		if (curLine == nLine) {
			// Refresh cache for the most likely next call (line nLine+1).
			cache.source   = src;
			cache.startPos = pos;
			cache.line     = curLine;
			return src.Mid(pos, lineEnd - pos);
		}

		if (brk == wxString::npos) return wxEmptyString; // beyond last line

		// Advance past the line break (handle CRLF as a single break).
		pos = brk + 1;
		if (src[brk] == wxT('\r') && pos < len && src[pos] == wxT('\n'))
			++pos;
		++curLine;
	}
}

wxString ibValueSystemFunction::Upper(const ibValue& cSource)
{
	wxString stringValue = cSource.GetString();
	stringValue.MakeUpper();
	return stringValue;
}

wxString ibValueSystemFunction::Lower(const ibValue& cSource)
{
	wxString stringValue = cSource.GetString();
	stringValue.MakeLower();
	return stringValue;
}

wxString ibValueSystemFunction::Chr(short nCode)
{
	return wxString(static_cast<wchar_t>(nCode));
}

short ibValueSystemFunction::Asc(const ibValue& cSource)
{
	wxString stringValue = cSource.GetString();
	if (!stringValue.Length()) return 0;
	return static_cast<wchar_t>(stringValue[0]);
}

wxString ibValueSystemFunction::TStr(const ibValue& cSource, const ibValue& cLanguage)
{
	return ibBackendLocalization::GetTranslateGetRawLocText(
		cLanguage.GetString(), cSource.GetString());
}

//---Работа с датой и временем
ibValue ibValueSystemFunction::CurrentDate()
{
	wxDateTime timeNow = wxDateTime::Now();
	wxLongLong m_llValue = timeNow.GetValue();

	ibValue valueNow = ibValueTypes::TYPE_DATE;
	valueNow.m_dData = m_llValue.GetValue();
	return valueNow;
}

ibValue ibValueSystemFunction::WorkingDate() {
	// Session-aware via ibSession::Current() — when a worker scope is
	// active the session's m_workDate is used; otherwise process-wide
	// ms_workDate (codeRunner / pre-Connect bootstrap).
	wxDateTime d = ibSession::Current() != nullptr
		? ibSession::Current()->GetWorkDate()
		: ms_workDate;
	d.SetHour(0);
	d.SetMinute(0);
	d.SetSecond(0);
	return d;
}

ibValue ibValueSystemFunction::AddMonth(const ibValue& cData, int nMonthAdd)
{
	int nYear, nMonth, nDay;
	cData.FromDate(nYear, nMonth, nDay);
	int SummaMonth = nYear * 12 + nMonth - 1;
	SummaMonth += nMonthAdd;
	nYear = SummaMonth / 12;
	nMonth = SummaMonth % 12 + 1;
	return ibValue(nYear, nMonth, nDay);
}

ibValue ibValueSystemFunction::BegOfMonth(const ibValue& cData)
{
	int nYear, nMonth, nDay;
	cData.FromDate(nYear, nMonth, nDay);
	return ibValue(nYear, nMonth, 1);
}

ibValue ibValueSystemFunction::EndOfMonth(const ibValue& cData)
{
	int nYear, nMonth, nDay;
	cData.FromDate(nYear, nMonth, nDay);

	ibValue m_date = ibValue(nYear, nMonth, 1, 23, 59, 59);
	return AddMonth(m_date, 1) - 1;
}

ibValue ibValueSystemFunction::BegOfQuart(const ibValue& cData)
{
	int nYear, nMonth, nDay;
	cData.FromDate(nYear, nMonth, nDay);
	return ibValue(nYear, 1 + ((nMonth - 1) / 3) * 3, 1);
}

ibValue ibValueSystemFunction::EndOfQuart(const ibValue& cData)
{
	return AddMonth(BegOfQuart(cData), 3) - 1;
}

ibValue ibValueSystemFunction::BegOfYear(const ibValue& cData)
{
	int nYear, nMonth, nDay;
	cData.FromDate(nYear, nMonth, nDay);
	return ibValue(nYear, 1, 1);
}

ibValue ibValueSystemFunction::EndOfYear(const ibValue& cData)
{
	int nYear, nMonth, nDay;
	cData.FromDate(nYear, nMonth, nDay);
	return ibValue(nYear, 12, 31, 23, 59, 59);
}

ibValue ibValueSystemFunction::BegOfWeek(const ibValue& cData)
{
	int nYear, nMonth, nDay, DayOfWeek, DayOfYear, WeekOfYear;
	cData.FromDate(nYear, nMonth, nDay, DayOfWeek, DayOfYear, WeekOfYear);
	ibValue Date1 = ibValue(nYear, nMonth, nDay) - (DayOfWeek + 1);
	return Date1;
}

ibValue ibValueSystemFunction::EndOfWeek(const ibValue& cData)
{
	int nYear, nMonth, nDay, DayOfWeek, DayOfYear, WeekOfYear;
	cData.FromDate(nYear, nMonth, nDay, DayOfWeek, DayOfYear, WeekOfYear);
	return ibValue(nYear, nMonth, nDay) + (7 - DayOfWeek);
}

ibValue ibValueSystemFunction::BegOfDay(const ibValue& cData)
{
	int nYear, nMonth, nDay;
	cData.FromDate(nYear, nMonth, nDay);
	return ibValue(nYear, nMonth, nDay, 0, 0, 0);
}

ibValue ibValueSystemFunction::EndOfDay(const ibValue& cData)
{
	int nYear, nMonth, nDay;
	cData.FromDate(nYear, nMonth, nDay);
	return ibValue(nYear, nMonth, nDay, 23, 59, 59);
}

int ibValueSystemFunction::GetYear(const ibValue& cData)
{
	int nYear, nMonth, nDay;
	cData.FromDate(nYear, nMonth, nDay);
	return nYear;
}

int ibValueSystemFunction::GetMonth(const ibValue& cData)
{
	int nYear, nMonth, nDay;
	cData.FromDate(nYear, nMonth, nDay);
	return nMonth;
}

int ibValueSystemFunction::GetDay(const ibValue& cData)
{
	int nYear, nMonth, nDay;
	cData.FromDate(nYear, nMonth, nDay);
	return nDay;
}

int ibValueSystemFunction::GetHour(const ibValue& cData)
{
	int nYear, nMonth, nDay; unsigned short nHour, nMinutes, nSeconds;
	cData.FromDate(nYear, nMonth, nDay, nHour, nMinutes, nSeconds);
	return nHour;
}

int ibValueSystemFunction::GetMinute(const ibValue& cData)
{
	int nYear, nMonth, nDay; unsigned short nHour, nMinutes, nSeconds;
	cData.FromDate(nYear, nMonth, nDay, nHour, nMinutes, nSeconds);
	return nMinutes;
}

int ibValueSystemFunction::GetSecond(const ibValue& cData)
{
	int nYear, nMonth, nDay; unsigned short nHour, nMinutes, nSeconds;
	cData.FromDate(nYear, nMonth, nDay, nHour, nMinutes, nSeconds);
	return nSeconds;
}

int ibValueSystemFunction::GetWeekOfYear(const ibValue& cData)
{
	int nYear, nMonth, nDay, DayOfWeek, DayOfYear, WeekOfYear;
	cData.FromDate(nYear, nMonth, nDay, DayOfWeek, DayOfYear, WeekOfYear);
	return WeekOfYear;
}

int ibValueSystemFunction::GetDayOfYear(const ibValue& cData)
{
	int nYear, nMonth, nDay, DayOfWeek, DayOfYear, WeekOfYear;
	cData.FromDate(nYear, nMonth, nDay, DayOfWeek, DayOfYear, WeekOfYear);
	return DayOfYear;
}

int ibValueSystemFunction::GetDayOfWeek(const ibValue& cData)
{
	int nYear, nMonth, nDay, DayOfWeek, DayOfYear, WeekOfYear;
	cData.FromDate(nYear, nMonth, nDay, DayOfWeek, DayOfYear, WeekOfYear);
	return DayOfWeek;
}

int ibValueSystemFunction::GetQuartOfYear(const ibValue& cData)
{
	int nYear, nMonth, nDay;
	cData.FromDate(nYear, nMonth, nDay);
	return 1 + ((nMonth - 1) / 3);
}

//--- Работа с файлами: 

#include <wx/filename.h>

bool ibValueSystemFunction::CopyFile(const wxString& src, const wxString& dst)
{
	return wxCopyFile(src, dst);
}

bool ibValueSystemFunction::DeleteFile(const wxString& file)
{
	return wxRemoveFile(file);
}

wxString ibValueSystemFunction::GetTempDir()
{
	return wxFileName::GetTempDir();
}

wxString ibValueSystemFunction::GetTempFileName()
{
	return wxFileName::CreateTempFileName(
		wxEmptyString
	);
}

//--- Работа с окнами:
ibBackendValueForm* ibValueSystemFunction::ActiveWindow()
{
	auto* frame = ibSession::CurrentFrame();
	return frame != nullptr ? frame->ActiveWindow() : nullptr;
}

//--- Специальные:
void ibValueSystemFunction::Message(const wxString& strMessage, ibStatusMessage status)
{
	if (ibBackendException::IsEvalMode())
		return;

	// Frame is responsible for thread safety. Web's ibWebFrame::Message
	// queues under a mutex; desktop's eventual override (if it ever
	// touches wx UI) must marshal to the main thread itself. The old
	// `wxIsMainThread() return` guard blocked legitimate calls from the
	// per-session worker thread on web.
	if (auto* frame = ibSession::CurrentFrame())
		frame->Message(strMessage, status);
}

void ibValueSystemFunction::Alert(const wxString& strMessage) //Alert
{
	if (ibBackendException::IsEvalMode())
		return;

	// Frontend-owned: frame knows whether to pop a wx-modal (desktop)
	// or emit a toast/HTTP notification (web). ShowModalMessage on web
	// parks the worker on a future until the client replies — safe to
	// call from any thread that owns the script's execution context.
	if (auto* frame = ibSession::CurrentFrame())
		frame->ShowModalMessage(strMessage, _("Warning"), wxICON_WARNING | wxOK);
}

ibValue ibValueSystemFunction::Question(const wxString& strMessage, ibQuestionMode mode)//Question
{
	if (ibBackendException::IsEvalMode()) {
		return ibValue::CreateAndPrepareValueRef<ibValuibQuestionReturnCode>();
	}

	int wndStyle = 0;

	if (mode == ibQuestionMode::ibQuestionMode_OK)
		wndStyle = wxOK;
	else if (mode == ibQuestionMode::ibQuestionMode_OKCancel)
		wndStyle = wxOK | wxCANCEL;
	else if (mode == ibQuestionMode::ibQuestionMode_YesNo)
		wndStyle = wxYES | wxNO;
	else if (mode == ibQuestionMode::ibQuestionMode_YesNoCancel)
		wndStyle = wxYES | wxNO | wxCANCEL;

	// Route through the frame's MessageBox virtual so backend stays
	// wx-free here. No frame = script is running in a context without
	// UI (daemon, codeRunner) — default to "Cancel" to keep flows that
	// assume success conservative.
	auto* frame = ibSession::CurrentFrame();
	int retCode = frame != nullptr
		? frame->ShowModalMessage(strMessage, _("Question"), wndStyle | wxICON_QUESTION)
		: wxCANCEL;

	ibValuibQuestionReturnCode* retValue = ibValue::CreateAndPrepareValueRef<ibValuibQuestionReturnCode>();
	switch (retCode) {
	case wxOK:
		retValue->InitializeEnumeration(ibQuestionReturnCode::ibQuestionReturnCode_OK);
		break;
	case wxCANCEL:
		retValue->InitializeEnumeration(ibQuestionReturnCode::ibQuestionReturnCode_Cancel);
		break;
	case wxYES:
		retValue->InitializeEnumeration(ibQuestionReturnCode::ibQuestionReturnCode_Yes);
		break;
	case wxNO:
		retValue->InitializeEnumeration(ibQuestionReturnCode::ibQuestionReturnCode_No);
		break;
	default:
		retValue->InitializeEnumeration(ibQuestionReturnCode::ibQuestionReturnCode_Yes);
		break;
	}

	return retValue;
}

void ibValueSystemFunction::SetStatus(const wxString& sStatus)
{
	if (ibBackendException::IsEvalMode())
		return;

	// Frame override owns thread safety; web's SetStatusText just stores
	// a wxString under no lock (single owner). See Message() above for
	// the reasoning behind dropping the wxIsMainThread() guard.
	if (auto* frame = ibSession::CurrentFrame())
		frame->SetStatusText(sStatus);
}

void ibValueSystemFunction::ClearMessage()
{
	if (ibBackendException::IsEvalMode())
		return;

	if (auto* frame = ibSession::CurrentFrame())
		frame->ClearMessage();
}

void ibValueSystemFunction::SetError(const wxString& strError)
{
	if (ibBackendException::IsEvalMode())
		return;

	ibBackendCoreException::Error(strError);
}

void ibValueSystemFunction::Raise(const wxString& strError)
{
	if (ibBackendException::IsEvalMode())
		return;

	if (auto* puState = ibSession::GetPUState())
		puState->Raise();
	ibBackendCoreException::Error(strError);
}

wxString ibValueSystemFunction::ErrorDescription()
{
	if (ibBackendException::IsEvalMode())
		return wxEmptyString;

	return ibBackendException::GetLastError();
}

bool ibValueSystemFunction::IsEmptyValue(const ibValue& cData)
{
	return cData.IsEmpty();
}

ibValue ibValueSystemFunction::Evaluate(const wxString& strExpression)
{
	auto* puState = ibSession::GetPUState();
	ibValue retValue;
	ibProcUnit::Evaluate(strExpression, puState ? puState->GetCurrentRunContext() : nullptr, retValue, false);
	return retValue;
}

void ibValueSystemFunction::Execute(const wxString& strExpression)
{
	if (ibBackendException::IsEvalMode())
		return;
	auto* puState = ibSession::GetPUState();
	ibValue retValue;
	ibProcUnit::Evaluate(strExpression, puState ? puState->GetCurrentRunContext() : nullptr, retValue, true);
}

//boolean 
#define BT wxT("BT")
#define BF wxT("BF")

//number
#define ND wxT("ND")
#define NFD wxT("NFD")
#define NS wxT("NS")
#define NZ wxT("NZ")
#define NLZ wxT("NLZ")
#define NN wxT("NN")
#define NDS wxT("NDS")
#define NGS wxT("NGS")
#define NG wxT("NG")
//date 
#define DF wxT("DF")
#define DE wxT("DE")

wxString ibValueSystemFunction::Format(ibValue& cData, const wxString& fmt)
{
	wxString leftParam, rightParam;
	std::map<wxString, wxString> paParams;
	bool bLeftParam = true;
	for (unsigned int i = 0; i < fmt.length(); i++) {
		auto c = fmt.at(i);
		if (c == ';') {
			leftParam.Trim(true); leftParam.Trim(false);
			rightParam.Trim(true); rightParam.Trim(false);
			paParams.insert_or_assign(leftParam, rightParam);
			bLeftParam = true; leftParam = ""; rightParam = "";
			continue;
		}
		else if (c == '=') {
			bLeftParam = false;
		}

		if (c != '=') {
			if (bLeftParam) {
				leftParam += c;
			}
			else {
				rightParam += c;
			}
		}

		if (i == fmt.length() - 1) {
			leftParam.Trim(true); leftParam.Trim(false);
			rightParam.Trim(true); rightParam.Trim(false);
			paParams.insert_or_assign(leftParam, rightParam);
			bLeftParam = true; leftParam = ""; rightParam = "";
		}
	}

	switch (cData.GetType()) {
	case ibValueTypes::TYPE_BOOLEAN: {
		if (cData.GetBoolean()) {
			auto foundedBT = paParams.find(BT);
			if (foundedBT != paParams.end()) {
				return foundedBT->second;
			}
		}
		else {
			auto foundedBT = paParams.find(BF);
			if (foundedBT != paParams.end()) {
				return foundedBT->second;
			}
		}
		return cData.GetString();
	}
	case ibValueTypes::TYPE_NUMBER:
	{
		ibNumber number = cData.GetNumber();

		// NZ: replacement string when value is exactly zero.
		if (number.IsZero()) {
			auto foundedNZ = paParams.find(NZ);
			if (foundedNZ != paParams.end()) {
				return foundedNZ->second;
			}
		}

		ibNumber::Format fmt;
		auto fnd = paParams.find(NFD);
		if (fnd != paParams.end()) fmt.fracDigits = wxAtoi(fnd->second);
		fnd = paParams.find(ND);
		if (fnd != paParams.end()) fmt.precision  = wxAtoi(fnd->second);
		fnd = paParams.find(NDS);
		if (fnd != paParams.end() && !fnd->second.IsEmpty()) fmt.decimalSep = fnd->second[0];
		fnd = paParams.find(NGS);
		if (fnd != paParams.end() && !fnd->second.IsEmpty()) fmt.groupSep   = fnd->second[0];
		fnd = paParams.find(NG);
		if (fnd != paParams.end()) fmt.groupSize  = wxAtoi(fnd->second);

		return number.ToString(fmt);
	}
	case ibValueTypes::TYPE_DATE:

		if (cData.IsEmpty()) {
			auto foundedDE = paParams.find(DE);
			if (foundedDE != paParams.end()) {
				return foundedDE->second;
			};
		}

		auto foundedDF = paParams.find(DF);
		if (foundedDF != paParams.end()) {

			wxString newFormat = foundedDF->second;

			//year 
			if (newFormat.Replace("yyyy", "%Y") == 0) {
				if (newFormat.Replace("yyy", "%y") == 0) {
					if (newFormat.Replace("yy", "%y") == 0) {
						newFormat.Replace("y", "%y");
					}
				}
			}

			//mouth 
			if (newFormat.Replace("mm", "%m") == 0) {
				newFormat.Replace("m", "%m");
			}

			//day 
			if (newFormat.Replace("dd", "%d") == 0) {
				newFormat.Replace("d", "%d");
			}

			//hour
			if (newFormat.Replace("HH", "%H") == 0) {
				newFormat.Replace("H", "%H");
			}

			//minute
			if (newFormat.Replace("MM", "%M") == 0) {
				newFormat.Replace("M", "%M");
			}

			//secound
			if (newFormat.Replace("SS", "%S") == 0) {
				newFormat.Replace("S", "%S");
			}

			wxDateTime dateTime = wxLongLong(cData.GetDate());
			return dateTime.Format(newFormat);
		}

		return cData.GetString();
	}

	return cData.GetString();
}

#include "backend/system/value/valueType.h"

ibValue ibValueSystemFunction::Type(const ibValue& cTypeName)
{
	if (cTypeName.GetType() != ibValueTypes::TYPE_STRING) {
		ibBackendCoreException::Error(_("Cannot convert value"));
		return ibValue();
	}

	const wxString& strTypeName = cTypeName.GetString();
	if (!activeMetaData->IsRegisterCtor(strTypeName))
		ibBackendCoreException::Error(_("Type not found '%s'"), strTypeName);

	return ibValue::CreateAndPrepareValueRef<ibValueType>(strTypeName);
}

ibValue ibValueSystemFunction::TypeOf(const ibValue& cData)
{
	return ibValue::CreateAndPrepareValueRef<ibValueType>(cData);
}

int ibValueSystemFunction::Rand()
{
	return rand();
}

int ibValueSystemFunction::ArgCount()//ArgCount
{
#ifdef __WXMSW__
	return __argc;
#else
	return wxTheApp ? wxTheApp->argc : 0;
#endif
}

wxString ibValueSystemFunction::ArgValue(int n)//ArgValue
{
	int count = ArgCount();
	if (n < 0 || n > count)
		ibBackendCoreException::Error(_("Invalid argument index"));
#ifdef __WXMSW__
	return __wargv[n];
#else
	if (wxTheApp)
		return wxString(wxTheApp->argv[n]);
	return wxString();
#endif
}

wxString ibValueSystemFunction::ComputerName()//ComputerName
{
	return wxGetHostName();
}

void ibValueSystemFunction::RunApp(const wxString& sCommand)//RunApp
{
	if (ibBackendException::IsEvalMode())
		return;
	wxExecute(sCommand);
}

void ibValueSystemFunction::SetAppTitle(const wxString& sTitle)//SetAppTitle
{
	if (ibBackendException::IsEvalMode())
		return;
	if (auto* frame = ibSession::CurrentFrame())
		frame->SetTitle(sTitle);
}

wxString ibValueSystemFunction::UserDir() {
	return wxEmptyString;
}

wxString ibValueSystemFunction::UserName() {
	return appData->GetUserName();
}

wxString ibValueSystemFunction::UserPassword() {
	return appData->GetUserPassword();
}

bool ibValueSystemFunction::ExclusiveMode() {
	return appData->ExclusiveMode();
}

void ibValueSystemFunction::SetExclusive(bool on) {
	// Acquire/release exclusive mode for the calling session. Throws
	// ibBackendCoreException with a localized reason on rejection — the
	// script's try/except is the natural retry point.
	auto* s = ibSession::Current();
	if (s == nullptr)
		ibBackendCoreException::Error(_("No active session"));
	s->SetExclusive(on);
}

wxString ibValueSystemFunction::GeneralLanguage() {
	return appData->GetUserLanguageCode();
}

#include "backend/metaData.h"

void ibValueSystemFunction::EndJob(bool force) //EndJob
{
	// Just close the current session through the manager. force=true
	// skips BeforeExit / OnExit veto checks. The registry's
	// OnLastDisconnect listener fires when the auth-counter hits 0 and
	// requests ForceExit there — declined by keep-alive predicates if
	// other clients (web tabs, etc.) are still live.
	if (auto* session = ibSession::Current())
		session->Close(force);
}

void ibValueSystemFunction::UserInterruptProcessing()
{
	if (wxGetKeyState(WXK_CONTROL) && wxGetKeyState(WXK_CANCEL))
		ibBackendInterruptException::Error();
}

bool ibValueSystemFunction::AccessRight(const wxString& strRoleName, const ibValue& cData)
{
	const ibValueMetaObject* creator = cData.ConvertToType<ibValueMetaObject>();
	return creator != nullptr ?
		creator->AccessRight(strRoleName) : false;
}

bool ibValueSystemFunction::IsInRole(const ibValue& cData)
{
	const ibValueMetaObject* creator = activeMetaData->FindAnyObjectByFilter(cData.GetString(), g_metaRoleCLSID);
	if (creator == nullptr) return false;

	if (creator != nullptr) {
		for (const auto role : appData->GetUserRoleArray()) {
			if (role.m_miRoleId == creator->GetMetaID())
				return true;
		}
	}

	return false;
}

ibValue ibValueSystemFunction::GetCommonForm(const wxString& strFormName, ibBackendControlFrame* ownerControl, ibValueGuid* unique)
{
	if (!strFormName.IsEmpty()) {

		const ibValueMetaObjectCommonForm* creator =
			activeMetaData->FindAnyObjectByFilter<ibValueMetaObjectCommonForm>(strFormName, g_metaCommonFormCLSID);

		if (creator != nullptr)
			return creator->GetObjectForm(ownerControl, unique ? ((ibGuid)*unique) : ibGuid());
	}

	ibBackendCoreException::Error(_("Common form not found '%s'"), strFormName);
	return wxEmptyValue;
}

void ibValueSystemFunction::ShowCommonForm(const wxString& strFormName, ibBackendControlFrame* ownerControl, ibValueGuid* unique)
{
	if (ibBackendException::IsEvalMode())
		return;

	const ibValue& cValue = GetCommonForm(strFormName, ownerControl, unique);

	ibBackendValueForm* valueForm = dynamic_cast<ibBackendValueForm*>(cValue.GetRef());
	if (valueForm != nullptr) valueForm->ShowForm();
}

#include "backend/system/value/valueSpreadsheet.h"

ibValue ibValueSystemFunction::GetCommonTemplate(const wxString& strTemplateName)
{
	if (!strTemplateName.IsEmpty()) {

		const ibValueMetaObjectCommonSpreadsheet* creator =
			activeMetaData->FindAnyObjectByFilter<ibValueMetaObjectCommonSpreadsheet>(strTemplateName, g_metaCommonTemplateCLSID);

		if (creator != nullptr)
			return ibValue::CreateAndPrepareValueRef<ibValueSpreadsheetDocument>(creator->GetSpreadsheetDesc());
	}

	ibBackendCoreException::Error(_("Common template not found '%s'"), strTemplateName);
	return wxEmptyValue;
}

void ibValueSystemFunction::BeginTransaction()
{
	if (ibBackendException::IsEvalMode())
		return;

	ses_query->BeginTransaction();
}

void ibValueSystemFunction::CommitTransaction()
{
	if (ibBackendException::IsEvalMode())
		return;

	ses_query->Commit();
}

void ibValueSystemFunction::RollBackTransaction()
{
	if (ibBackendException::IsEvalMode())
		return;

	ses_query->RollBack();
}

////////////////////////////////////////////////////////////////////////////
//                              Test assertions                            //
////////////////////////////////////////////////////////////////////////////
//
// Every helper throws ibBackendTestAssertException via the static Error()
// factory on failure. The test runner is the canonical catcher; outside
// the runner the throw surfaces through the regular backend error path
// (ProcessError → frame), which is fine for ad-hoc Evaluate() probes.
//
// Eval-mode guard intentionally OMITTED — unlike Raise/SetError, an
// assertion is a deterministic check the caller wants to fire even in
// debugger watch contexts. If a watch expression triggers an assertion
// fail, that's signal, not noise.

#include "backend/backend_exception.h"
#include "backend/compiler/procUnitValues.h"   // AsFunction for AssertThrows

namespace {

// Stringify an ibValue for use inside an assertion failure message. We
// only need a stable text round-trip; the runner picks up actual/expected
// as plain text and ships them through MCP structuredContent.
wxString StringifyValueForAssert(const ibValue& v)
{
	const ibValueTypes t = v.GetType();
	switch (t) {
	case ibValueTypes::TYPE_EMPTY:   return wxT("Empty");
	case ibValueTypes::TYPE_NULL:    return wxT("Null");
	case ibValueTypes::TYPE_BOOLEAN: return v.GetBoolean() ? wxT("True") : wxT("False");
	case ibValueTypes::TYPE_STRING:  return wxT("\"") + v.GetString() + wxT("\"");
	default:                         return v.GetString();
	}
}

} // namespace

void ibValueSystemFunction::AssertEquals(const ibValue& actual, const ibValue& expected, const wxString& message)
{
	if (actual == expected) return;
	ibBackendTestAssertException::Error(
		wxT("AssertEquals"),
		StringifyValueForAssert(actual),
		StringifyValueForAssert(expected),
		message);
}

void ibValueSystemFunction::AssertNotEquals(const ibValue& actual, const ibValue& expected, const wxString& message)
{
	if (actual != expected) return;
	ibBackendTestAssertException::Error(
		wxT("AssertNotEquals"),
		StringifyValueForAssert(actual),
		wxT("not ") + StringifyValueForAssert(expected),
		message);
}

void ibValueSystemFunction::AssertNotNull(const ibValue& value, const wxString& message)
{
	if (!value.IsEmpty()) return;
	ibBackendTestAssertException::Error(
		wxT("AssertNotNull"),
		StringifyValueForAssert(value),
		wxT("non-null"),
		message);
}

void ibValueSystemFunction::AssertTrue(const ibValue& condition, const wxString& message)
{
	if (condition.GetBoolean()) return;
	ibBackendTestAssertException::Error(
		wxT("AssertTrue"),
		StringifyValueForAssert(condition),
		wxT("True"),
		message);
}

void ibValueSystemFunction::AssertFalse(const ibValue& condition, const wxString& message)
{
	if (!condition.GetBoolean()) return;
	ibBackendTestAssertException::Error(
		wxT("AssertFalse"),
		StringifyValueForAssert(condition),
		wxT("False"),
		message);
}

void ibValueSystemFunction::AssertGreater(const ibValue& a, const ibValue& b, const wxString& message)
{
	if (a > b) return;
	ibBackendTestAssertException::Error(
		wxT("AssertGreater"),
		StringifyValueForAssert(a),
		wxT("> ") + StringifyValueForAssert(b),
		message);
}

void ibValueSystemFunction::AssertLess(const ibValue& a, const ibValue& b, const wxString& message)
{
	if (a < b) return;
	ibBackendTestAssertException::Error(
		wxT("AssertLess"),
		StringifyValueForAssert(a),
		wxT("< ") + StringifyValueForAssert(b),
		message);
}

void ibValueSystemFunction::AssertThrows(ibValue& callable,
                                          const wxString& expectedMsgContains,
                                          const wxString& message)
{
	ibValueFunction* fn = AsFunction(&callable);
	if (fn == nullptr) {
		ibBackendTestAssertException::Error(
			wxT("AssertThrows"),
			wxT("non-callable"),
			wxT("Function value"),
			message);
		return;
	}

	bool didThrow = false;
	wxString actualMsg;
	try {
		// AssertThrows takes a callable wrapping a 0-arg Function. The
		// helper consumes the lambda's TYPE_REFFER wrapper just like
		// InvokeLambdaWithArg, but we want zero-arg invocation — pass
		// a TYPE_UNDEFINED placeholder. Lambdas with declared params
		// receive Undefined; lambdas with zero declared params ignore
		// it (Execute drives by m_listLocals shape, not arg count).
		ibValue noArg;
		ibValue retVal;
		// Re-use the public host helper so the lambda exec path is
		// the same one LINQ uses. Failure here (non-callable) maps
		// to the "lambda did not throw" branch — the caller asked
		// the lambda to throw, and the lambda refused to even run,
		// which is morally the same outcome.
		InvokeLambdaWithArg(callable, noArg, retVal);
	}
	catch (const ibBackendTestAssertException&) {
		// Inner assertion shouldn't be swallowed by an outer AssertThrows
		// — propagate it as a real test failure.
		throw;
	}
	catch (const ibBackendException& err) {
		didThrow = true;
		actualMsg = err.GetErrorDescription();
	}
	catch (...) {
		didThrow = true;
		actualMsg = wxT("<non-backend exception>");
	}

	if (!didThrow) {
		ibBackendTestAssertException::Error(
			wxT("AssertThrows"),
			wxT("did not throw"),
			wxT("throws exception") +
				(expectedMsgContains.IsEmpty()
					? wxString(wxEmptyString)
					: wxT(" containing \"") + expectedMsgContains + wxT("\"")),
			message);
		return;
	}

	if (!expectedMsgContains.IsEmpty() && actualMsg.Find(expectedMsgContains) == wxNOT_FOUND) {
		ibBackendTestAssertException::Error(
			wxT("AssertThrows"),
			wxT("\"") + actualMsg + wxT("\""),
			wxT("error message containing \"") + expectedMsgContains + wxT("\""),
			message);
	}
}
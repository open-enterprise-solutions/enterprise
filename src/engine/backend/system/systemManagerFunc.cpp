////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : system objects 
////////////////////////////////////////////////////////////////////////////

#include "systemManager.h"

#include "backend/metaCollection/metaFormObject.h"
#include "backend/metadataConfiguration.h"

#include "backend/backend_mainFrame.h"
#include "backend/backend_form.h"

#include "backend/compiler/translateCode.h"
#include "backend/compiler/procUnit.h"
#include "backend/appData.h"
#include "backend/session/session.h"

#include "systemManagerEnum.h"

//--- Basic:
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

//--- Math:
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
	return cValue.GetNumber().Ln();   // high-precision exact-tier ln (was std::log → double)
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
	// Was broken after the ttmath removal: ttmath's Sqrt() returned a status
	// (0 = ok) and mutated in place, so `if (Sqrt() == 0) return fNumber;`
	// made sense. The self-contained ibNumber::Sqrt() returns the *value*, so
	// that guard threw "Incorrect argument" for every non-zero input. Now we
	// reject only a negative argument (no real root) and return the root.
	ibNumber fNumber = cValue.GetNumber();
	if (fNumber.IsSign())
		ibBackendCoreException::Error(_("Incorrect argument value for built-in function (Sqrt)"));
	return ibValue(fNumber.Sqrt());
}

//--- Strings:
int ibValueSystemFunction::StrLen(const ibValue& cValue)
{
	ibString scratch;
	return static_cast<int>(cValue.GetString(scratch).Length());
}

bool ibValueSystemFunction::IsBlankString(const ibValue& cValue)
{
	ibString scratch;
	return cValue.GetString(scratch).IsBlank();
}

ibString ibValueSystemFunction::TrimL(const ibValue& cValue)
{
	ibString scratch;
	return cValue.GetString(scratch).Trim(false);
}

ibString ibValueSystemFunction::TrimR(const ibValue& cValue)
{
	ibString scratch;
	return cValue.GetString(scratch).Trim(true);
}

ibString ibValueSystemFunction::TrimAll(const ibValue& cValue)
{
	ibString scratch;
	return cValue.GetString(scratch).TrimAll();
}

ibString ibValueSystemFunction::Left(const ibValue& cValue, unsigned int nCount)
{
	ibString scratch;
	return cValue.GetString(scratch).Left(nCount);
}

ibString ibValueSystemFunction::Right(const ibValue& cValue, unsigned int nCount)
{
	ibString scratch;
	return cValue.GetString(scratch).Right(nCount);
}

ibString ibValueSystemFunction::Mid(const ibValue& cValue, unsigned int nFirst, unsigned int nCount)
{
	ibString scratch;
	return cValue.GetString(scratch).Mid(nFirst, nCount);
}

unsigned int ibValueSystemFunction::Find(const ibValue& cValue, const ibValue& cValue2, unsigned int nStart)
{
	if (nStart < 1) nStart = 1;
	ibString s1, s2;
	// npos + 1 wraps to 0 — same "not found → 0" contract as the old wxString.find path.
	return static_cast<unsigned int>(cValue.GetString(s1).Find(cValue2.GetString(s2), nStart - 1) + 1);
}

ibString ibValueSystemFunction::StrReplace(const ibValue& cSource, const ibValue& cValue1, const ibValue& cValue2)
{
	ibString sSrc, sFrom, sTo;
	ibString result(cSource.GetString(sSrc));   // mutable copy of the source
	result.Replace(cValue1.GetString(sFrom), cValue2.GetString(sTo));
	return result;
}

int ibValueSystemFunction::StrCountOccur(const ibValue& cSource, const ibValue& cValue1)
{
	ibString s1, s2;
	return static_cast<int>(cSource.GetString(s1).Find(cValue1.GetString(s2)));
}

int ibValueSystemFunction::StrLineCount(const ibValue& cSource)
{
	ibString scratch;
	return static_cast<int>(cSource.GetString(scratch).Find(L'\n') + 1);
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

ibString ibValueSystemFunction::Upper(const ibValue& cSource)
{
	ibString scratch;
	return cSource.GetString(scratch).Upper();
}

ibString ibValueSystemFunction::Lower(const ibValue& cSource)
{
	ibString scratch;
	return cSource.GetString(scratch).Lower();
}

wxString ibValueSystemFunction::Chr(short nCode)
{
	return wxString(static_cast<wchar_t>(nCode));
}

short ibValueSystemFunction::Asc(const ibValue& cSource)
{
	ibString scratch;
	const ibString& s = cSource.GetString(scratch);
	if (s.IsEmpty()) return 0;
	return static_cast<short>(s[0]);
}

wxString ibValueSystemFunction::TStr(const ibValue& cSource, const ibValue& cLanguage)
{
	return ibBackendLocalization::GetTranslateGetRawLocText(
		cLanguage.GetString(), cSource.GetString());
}

//--- Date and time:
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

//--- File operations: 

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

//--- Window operations:
ibBackendValueForm* ibValueSystemFunction::ActiveWindow()
{
	auto* frame = ibSession::CurrentFrame();
	return frame != nullptr ? frame->ActiveWindow() : nullptr;
}

//--- Special:
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
		return new ibValuibQuestionReturnCode();
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

	ibValuibQuestionReturnCode* retValue = new ibValuibQuestionReturnCode();
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

// SQL / explicit NULL test (TYPE_NULL only). Distinct from IsEmptyValue / ValueIsFilled: an EMPTY
// reference or Undefined is NOT a NULL.
bool ibValueSystemFunction::IsNull(const ibValue& cData)
{
	return cData.IsNull();
}

// "Filled" = carries a real value: false for Undefined, NULL, an EMPTY reference (type chosen, no
// guid), "", 0, empty date (the value-is-filled predicate). An empty reference is "not filled"
// yet NOT IsNull — it stays a typed empty reference, matching the composite value model.
bool ibValueSystemFunction::ValueIsFilled(const ibValue& cData)
{
	return !cData.IsEmpty();
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

	return new ibValueType(strTypeName);
}

ibValue ibValueSystemFunction::TypeOf(const ibValue& cData)
{
	return new ibValueType(cData);
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
			return new ibValueSpreadsheetDocument(creator->GetSpreadsheetDesc());
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
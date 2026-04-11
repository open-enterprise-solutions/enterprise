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

	if (precision > MAX_PRECISION_NUMBER) {
		precision = MAX_PRECISION_NUMBER;
	}

	ttmath::Int<TTMATH_BITS(128)> nDelta;
	if (!fNumber.ToInt(nDelta)) {
		fNumber = fNumber - nDelta;
	}

	ibNumber fTemp = 10;
	fTemp.Pow(precision + 1);
	fTemp = fTemp * fNumber;

	ttmath::Int<TTMATH_BITS(128)> N;
	fTemp.ToInt(N);

	//округление - в зависимости от метода
	if (mode == ibRoundMode::ibRoundMode_Round15as20) {
		if (N % 10 >= 5) N = N / 10 + 1;
		else N = N / 10;
	}
	else {
		if (N % 10 >= 6)
			N = N / 10 + 1;
		else N = N / 10;
	}

	ibNumber G = 10; G.Pow(precision);

	if (!fTemp.FromInt(N))
	{
		fTemp = fTemp / G;
		fTemp.Add(nDelta);

		return fTemp;
	}

	return 0;
}

ibValue ibValueSystemFunction::Int(const ibValue& cValue)
{
	ttmath::Int<TTMATH_BITS(128)> int128;
	ibNumber fNumber = cValue.GetNumber();
	if (!fNumber.ToInt(int128)) return ibValue(ibNumber(int128));
	else return ibValue(ibNumber(0));
}

ibNumber ibValueSystemFunction::Log10(const ibValue& cValue)
{
	ibNumber fNumber = cValue.GetNumber();
	return 	fNumber.Log(fNumber, 10);
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
	wxString stringValue = cValue.GetString() + wxT("\r\n");

	unsigned int nLast = 0;
	unsigned int nStartLine = 1;

	//********блок для ускорения
	static wxString _csStaticSource;

	static unsigned int _nStaticLast = 0;
	static unsigned int _nStaticLine = 0;

	if (_csStaticSource == stringValue)
	{
		if (_nStaticLine <= nLine)
		{
			nLast = _nStaticLast;//т.е. начинаем поиск не с начала
			nStartLine = _nStaticLine;
		}
	}

	//перебираем строчки втупую
	for (unsigned int i = nStartLine;; i++)
	{
		unsigned int nIndex = stringValue.find(wxT("\r\n"), nLast);

		if (nIndex < 0) return wxEmptyString;

		if (i == nLine)
		{
			_csStaticSource = stringValue;
			_nStaticLast = nIndex + 2;
			_nStaticLine = nLine + 1;

			return stringValue.Mid(nLast, nIndex - nLast);
		}

		nLast = nIndex + 2;
	}

	return wxEmptyString;
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
	ms_workDate.SetHour(0);
	ms_workDate.SetMinute(0);
	ms_workDate.SetSecond(0);
	return ms_workDate;
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
	return backend_mainFrame ?
		backend_mainFrame->ActiveWindow() : nullptr;
}

//--- Специальные:
void ibValueSystemFunction::Message(const wxString& strMessage, ibStatusMessage status)
{
	if (ibBackendException::IsEvalMode())
		return;

	if (!wxIsMainThread())
		return;

	if (backend_mainFrame != nullptr)
		backend_mainFrame->Message(strMessage, status);
}

void ibValueSystemFunction::Alert(const wxString& strMessage) //Alert
{
	if (ibBackendException::IsEvalMode())
		return;

	if (!wxIsMainThread())
		return;

	if (backend_mainFrame != nullptr) {
		wxMessageBox(strMessage, _("Warning"), wxICON_WARNING, backend_mainFrame->GetFrameHandler());
	}
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

	int retCode = wxMessageBox(strMessage, _("Question"), wndStyle | wxICON_QUESTION,
		backend_mainFrame ? backend_mainFrame->GetFrameHandler() : nullptr
	);

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

	if (!wxIsMainThread())
		return;

	if (backend_mainFrame != nullptr) {
		backend_mainFrame->SetStatusText(sStatus);
	}
}

void ibValueSystemFunction::ClearMessage()
{
	if (ibBackendException::IsEvalMode())
		return;

	if (!wxIsMainThread())
		return;

	if (backend_mainFrame != nullptr)
		backend_mainFrame->ClearMessage();
}

void ibValueSystemFunction::SetError(const wxString& strError)
{
	if (ibBackendException::IsEvalMode())
		return;

	if (!wxIsMainThread())
		return;

	ibBackendCoreException::Error(strError);
}

void ibValueSystemFunction::Raise(const wxString& strError)
{
	if (ibBackendException::IsEvalMode())
		return;

	if (!wxIsMainThread())
		return;

	ibProcUnit::Raise();
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
	ibValue retValue;
	ibProcUnit::Evaluate(strExpression, ibProcUnit::GetCurrentRunContext(), retValue, false);
	return retValue;
}

void ibValueSystemFunction::Execute(const wxString& strExpression)
{
	if (ibBackendException::IsEvalMode())
		return;
	ibValue retValue;
	ibProcUnit::Evaluate(strExpression, ibProcUnit::GetCurrentRunContext(), retValue, true);
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
		ttmath::Conv conv;

		auto foundedND = paParams.find(ND);
		if (foundedND != paParams.end()) {
			conv.precision = wxAtoi(foundedND->second);
			conv.trim_zeroes = true;
		}

		auto foundedNFD = paParams.find(NFD);
		if (foundedNFD != paParams.end()) {
			conv.round = wxAtoi(foundedNFD->second);
			conv.trim_zeroes = false;
		}

		auto foundedNDS = paParams.find(NDS);
		if (foundedNDS != paParams.end()) {
			conv.comma = foundedNDS->second[0];
		}

		auto foundedNGS = paParams.find(NGS);
		if (foundedNGS != paParams.end()) {
			conv.comma2 = foundedNGS->second[0];
		}

		auto foundedNG = paParams.find(NG);
		if (foundedNG != paParams.end()) {
			wxString group, group_digits; bool digits = false;
			for (auto c : foundedNG->second) {
				if (c == ',') {
					digits = true;
					continue;
				}
				if (digits == false) {
					group += c;
				}
				else {
					group_digits += c;
				}
			}
			group.Trim(true);
			group.Trim(false);
			group_digits.Trim(true);
			group_digits.Trim(false);

			conv.group = wxAtoi(group);
			if (!group_digits.IsEmpty()) {
				conv.group_digits = wxAtoi(group_digits);
			}
		}

		auto foundedNLZ = paParams.find(NLZ);
		if (foundedNLZ != paParams.end()) {
			conv.leading_zero = true;
		}

		ibNumber number = cData.GetNumber();

		if (number.IsZero()) {
			auto foundedNZ = paParams.find(NZ);

			if (foundedNZ != paParams.end()) {
				return foundedNZ->second;
			}
		}

		return number.ToString(conv);
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
	if (backend_mainFrame != nullptr) {
		backend_mainFrame->SetTitle(sTitle);
	}
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

wxString ibValueSystemFunction::GeneralLanguage() {
	return appData->GetUserLanguageCode();
}

#include "backend/metaData.h"

void ibValueSystemFunction::EndJob(bool force) //EndJob
{
	if (force) {
		ibApplicationData::ForceExit();
	}
	else if (activeMetaData != nullptr) {
		ibValueModuleManager* moduleManager = activeMetaData->GetModuleManager();
		if (moduleManager->DestroyMainModule()) {
			ibApplicationData::ForceExit();
		}
	}
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

	db_query->BeginTransaction();
}

void ibValueSystemFunction::CommitTransaction()
{
	if (ibBackendException::IsEvalMode())
		return;

	db_query->Commit();
}

void ibValueSystemFunction::RollBackTransaction()
{
	if (ibBackendException::IsEvalMode())
		return;

	db_query->RollBack();
}
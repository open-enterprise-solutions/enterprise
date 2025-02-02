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
bool CSystemFunction::Boolean(const CValue& cValue)
{
	return cValue.GetBoolean();
}

number_t CSystemFunction::Number(const CValue& cValue)
{
	return cValue.GetNumber();
}

wxLongLong_t CSystemFunction::Date(const CValue& cValue)
{
	return cValue.GetDate();
}

wxString CSystemFunction::String(const CValue& cValue)
{
	return cValue.GetString();
}

//---Математические:
number_t CSystemFunction::Round(const CValue& cValue, int precision, eRoundMode mode)
{
	number_t fNumber = cValue.GetNumber();

	if (precision > MAX_PRECISION_NUMBER) {
		precision = MAX_PRECISION_NUMBER;
	}

	ttmath::Int<TTMATH_BITS(128)> nDelta;
	if (!fNumber.ToInt(nDelta)) {
		fNumber = fNumber - nDelta;
	}

	number_t fTemp = 10;
	fTemp.Pow(precision + 1);
	fTemp = fTemp * fNumber;

	ttmath::Int<TTMATH_BITS(128)> N;
	fTemp.ToInt(N);

	//округление - в зависимости от метода
	if (mode == eRoundMode::eRoundMode_Round15as20) {
		if (N % 10 >= 5) N = N / 10 + 1;
		else N = N / 10;
	}
	else {
		if (N % 10 >= 6)
			N = N / 10 + 1;
		else N = N / 10;
	}

	number_t G = 10; G.Pow(precision);

	if (!fTemp.FromInt(N))
	{
		fTemp = fTemp / G;
		fTemp.Add(nDelta);

		return fTemp;
	}

	return 0;
}

CValue CSystemFunction::Int(const CValue& cValue)
{
	ttmath::Int<TTMATH_BITS(128)> int128;
	number_t fNumber = cValue.GetNumber();
	if (!fNumber.ToInt(int128)) return int128;
	else return 0;
}

number_t CSystemFunction::Log10(const CValue& cValue)
{
	number_t fNumber = cValue.GetNumber();
	return 	fNumber.Log(fNumber, 10);
}

number_t CSystemFunction::Ln(const CValue& cValue)
{
	number_t fNumber = cValue.GetNumber();
	return std::log(fNumber.ToDouble());
}

CValue CSystemFunction::Max(CValue** paParams, const long lSizeArray)
{
	CValue* maxValue = paParams[0]; int i = 1;
	while (i < lSizeArray) {
		if (paParams[i]->GetNumber() > maxValue->GetNumber())
			maxValue = paParams[i++];
	}

	return maxValue;
}

CValue CSystemFunction::Min(CValue** paParams, const long lSizeArray)
{
	CValue* minValue = paParams[0]; int i = 1;
	while (i < lSizeArray) {
		if (paParams[i]->GetNumber() < minValue->GetNumber())
			minValue = paParams[i++];
	}
	return minValue;
}

CValue CSystemFunction::Sqrt(const CValue& cValue)
{
	number_t fNumber = cValue.GetNumber();
	if (fNumber.Sqrt() == 0) {
		return fNumber;
	}
	CSystemFunction::Raise("Incorrect argument value for built-in function (Sqrt)");
	return CValue();
}

//---Строковые:
int CSystemFunction::StrLen(const CValue& cValue)
{
	wxString stringValue = cValue.GetString();
	return stringValue.Length();
}

bool CSystemFunction::IsBlankString(const CValue& cValue)
{
	wxString stringValue = cValue.GetString();
	stringValue.Trim(true);
	stringValue.Trim(false);
	return stringValue.IsEmpty();
}

wxString CSystemFunction::TrimL(const CValue& cValue)
{
	wxString stringValue = cValue.GetString();
	stringValue.Trim(false);
	return stringValue;
}

wxString CSystemFunction::TrimR(const CValue& cValue)
{
	wxString stringValue = cValue.GetString();
	stringValue.Trim(true);
	return stringValue;
}

wxString CSystemFunction::TrimAll(const CValue& cValue)
{
	wxString stringValue = cValue.GetString();
	stringValue.Trim(true);
	stringValue.Trim(false);
	return stringValue;
}

wxString CSystemFunction::Left(const CValue& cValue, unsigned int nCount)
{
	wxString stringValue = cValue.GetString();
	return stringValue.Left(nCount);
}

wxString CSystemFunction::Right(const CValue& cValue, unsigned int nCount)
{
	wxString stringValue = cValue.GetString();
	return stringValue.Right(nCount);
}

wxString CSystemFunction::Mid(const CValue& cValue, unsigned int nFirst, unsigned int nCount)
{
	wxString stringValue = cValue.GetString();
	return stringValue.Mid(nFirst, nCount);
}

unsigned int CSystemFunction::Find(const CValue& cValue, const CValue& cValue2, unsigned int nStart)
{
	if (nStart < 1) nStart = 1;
	wxString stringValue = cValue.GetString();
	return stringValue.find(cValue2.GetString(), nStart - 1) + 1;
}

wxString CSystemFunction::StrReplace(const CValue& cSource, const CValue& cValue1, const CValue& cValue2)
{
	wxString stringValue = cSource.GetString();
	stringValue.Replace(cValue1.GetString(), cValue2.GetString());
	return stringValue;
}

int CSystemFunction::StrCountOccur(const CValue& cSource, const CValue& cValue1)
{
	wxString stringValue = cSource.GetString();
	return stringValue.find(cValue1.GetString());
}

int CSystemFunction::StrLineCount(const CValue& cSource)
{
	wxString stringValue = cSource.GetString();
	return stringValue.find('\n') + 1;
}

wxString CSystemFunction::StrGetLine(const CValue& cValue, unsigned int nLine)
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

wxString CSystemFunction::Upper(const CValue& cSource)
{
	wxString stringValue = cSource.GetString();
	stringValue.MakeUpper();
	return stringValue;
}

wxString CSystemFunction::Lower(const CValue& cSource)
{
	wxString stringValue = cSource.GetString();
	stringValue.MakeLower();
	return stringValue;
}

wxString CSystemFunction::Chr(short nCode)
{
	return wxString(static_cast<wchar_t>(nCode));
}

short CSystemFunction::Asc(const CValue& cSource)
{
	wxString stringValue = cSource.GetString();
	if (!stringValue.Length()) return 0;
	return static_cast<wchar_t>(stringValue[0]);
}

//---Работа с датой и временем
CValue CSystemFunction::CurrentDate()
{
	wxDateTime timeNow = wxDateTime::Now();
	wxLongLong m_llValue = timeNow.GetValue();

	CValue valueNow = eValueTypes::TYPE_DATE;
	valueNow.m_dData = m_llValue.GetValue();
	return valueNow;
}

CValue CSystemFunction::WorkingDate() {
	sm_workDate.SetHour(0);
	sm_workDate.SetMinute(0);
	sm_workDate.SetSecond(0);
	return sm_workDate;
}

CValue CSystemFunction::AddMonth(const CValue& cData, int nMonthAdd)
{
	int nYear, nMonth, nDay;
	cData.FromDate(nYear, nMonth, nDay);
	int SummaMonth = nYear * 12 + nMonth - 1;
	SummaMonth += nMonthAdd;
	nYear = SummaMonth / 12;
	nMonth = SummaMonth % 12 + 1;
	return CValue(nYear, nMonth, nDay);
}

CValue CSystemFunction::BegOfMonth(const CValue& cData)
{
	int nYear, nMonth, nDay;
	cData.FromDate(nYear, nMonth, nDay);
	return CValue(nYear, nMonth, 1);
}

CValue CSystemFunction::EndOfMonth(const CValue& cData)
{
	int nYear, nMonth, nDay;
	cData.FromDate(nYear, nMonth, nDay);

	CValue m_date = CValue(nYear, nMonth, 1, 23, 59, 59);
	return AddMonth(m_date, 1) - 1;
}

CValue CSystemFunction::BegOfQuart(const CValue& cData)
{
	int nYear, nMonth, nDay;
	cData.FromDate(nYear, nMonth, nDay);
	return CValue(nYear, 1 + ((nMonth - 1) / 3) * 3, 1);
}

CValue CSystemFunction::EndOfQuart(const CValue& cData)
{
	return AddMonth(BegOfQuart(cData), 3) - 1;
}

CValue CSystemFunction::BegOfYear(const CValue& cData)
{
	int nYear, nMonth, nDay;
	cData.FromDate(nYear, nMonth, nDay);
	return CValue(nYear, 1, 1);
}

CValue CSystemFunction::EndOfYear(const CValue& cData)
{
	int nYear, nMonth, nDay;
	cData.FromDate(nYear, nMonth, nDay);
	return CValue(nYear, 12, 31, 23, 59, 59);
}

CValue CSystemFunction::BegOfWeek(const CValue& cData)
{
	int nYear, nMonth, nDay, DayOfWeek, DayOfYear, WeekOfYear;
	cData.FromDate(nYear, nMonth, nDay, DayOfWeek, DayOfYear, WeekOfYear);
	CValue Date1 = CValue(nYear, nMonth, nDay) - (DayOfWeek + 1);
	return Date1;
}

CValue CSystemFunction::EndOfWeek(const CValue& cData)
{
	int nYear, nMonth, nDay, DayOfWeek, DayOfYear, WeekOfYear;
	cData.FromDate(nYear, nMonth, nDay, DayOfWeek, DayOfYear, WeekOfYear);
	return CValue(nYear, nMonth, nDay) + (7 - DayOfWeek);
}

CValue CSystemFunction::BegOfDay(const CValue& cData)
{
	int nYear, nMonth, nDay;
	cData.FromDate(nYear, nMonth, nDay);
	return CValue(nYear, nMonth, nDay, 0, 0, 0);
}

CValue CSystemFunction::EndOfDay(const CValue& cData)
{
	int nYear, nMonth, nDay;
	cData.FromDate(nYear, nMonth, nDay);
	return CValue(nYear, nMonth, nDay, 23, 59, 59);
}

int CSystemFunction::GetYear(const CValue& cData)
{
	int nYear, nMonth, nDay;
	cData.FromDate(nYear, nMonth, nDay);
	return nYear;
}

int CSystemFunction::GetMonth(const CValue& cData)
{
	int nYear, nMonth, nDay;
	cData.FromDate(nYear, nMonth, nDay);
	return nMonth;
}

int CSystemFunction::GetDay(const CValue& cData)
{
	int nYear, nMonth, nDay;
	cData.FromDate(nYear, nMonth, nDay);
	return nDay;
}

int CSystemFunction::GetHour(const CValue& cData)
{
	int nYear, nMonth, nDay; unsigned short nHour, nMinutes, nSeconds;
	cData.FromDate(nYear, nMonth, nDay, nHour, nMinutes, nSeconds);
	return nHour;
}

int CSystemFunction::GetMinute(const CValue& cData)
{
	int nYear, nMonth, nDay; unsigned short nHour, nMinutes, nSeconds;
	cData.FromDate(nYear, nMonth, nDay, nHour, nMinutes, nSeconds);
	return nMinutes;
}

int CSystemFunction::GetSecond(const CValue& cData)
{
	int nYear, nMonth, nDay; unsigned short nHour, nMinutes, nSeconds;
	cData.FromDate(nYear, nMonth, nDay, nHour, nMinutes, nSeconds);
	return nSeconds;
}

int CSystemFunction::GetWeekOfYear(const CValue& cData)
{
	int nYear, nMonth, nDay, DayOfWeek, DayOfYear, WeekOfYear;
	cData.FromDate(nYear, nMonth, nDay, DayOfWeek, DayOfYear, WeekOfYear);
	return WeekOfYear;
}

int CSystemFunction::GetDayOfYear(const CValue& cData)
{
	int nYear, nMonth, nDay, DayOfWeek, DayOfYear, WeekOfYear;
	cData.FromDate(nYear, nMonth, nDay, DayOfWeek, DayOfYear, WeekOfYear);
	return DayOfYear;
}

int CSystemFunction::GetDayOfWeek(const CValue& cData)
{
	int nYear, nMonth, nDay, DayOfWeek, DayOfYear, WeekOfYear;
	cData.FromDate(nYear, nMonth, nDay, DayOfWeek, DayOfYear, WeekOfYear);
	return DayOfWeek;
}

int CSystemFunction::GetQuartOfYear(const CValue& cData)
{
	int nYear, nMonth, nDay;
	cData.FromDate(nYear, nMonth, nDay);
	return 1 + ((nMonth - 1) / 3);
}

//--- Работа с файлами: 

#include <wx/filename.h>

bool CSystemFunction::CopyFile(const wxString& src, const wxString& dst)
{
	return wxCopyFile(src, dst);
}

bool CSystemFunction::DeleteFile(const wxString& file)
{
	return wxRemoveFile(file);
}

wxString CSystemFunction::GetTempDir()
{
	return wxFileName::GetTempDir();
}

wxString CSystemFunction::GetTempFileName()
{
	return wxFileName::CreateTempFileName(
		wxEmptyString
	);
}

//--- Работа с окнами: 
IBackendValueForm* CSystemFunction::ActiveWindow()
{
	return backend_mainFrame ?
		backend_mainFrame->ActiveWindow() : nullptr;
}

//--- Специальные:
void CSystemFunction::Message(const wxString& strMessage, eStatusMessage status)
{
	if (CBackendException::IsEvalMode()) {
		return;
	}

	if (backend_mainFrame != nullptr) {
		backend_mainFrame->Message(strMessage, status);
	}
}

void CSystemFunction::Alert(const wxString& strMessage) //Предупреждение
{
	if (CBackendException::IsEvalMode()) {
		return;
	}

	if (backend_mainFrame != nullptr) {
		wxMessageBox(strMessage, _("Warning"), wxICON_WARNING, backend_mainFrame->GetFrameHandler());
	}
}

CValue CSystemFunction::Question(const wxString& strMessage, eQuestionMode mode)//Вопрос
{
	if (CBackendException::IsEvalMode()) {
		return CValue::CreateAndConvertObjectValueRef<CValueQuestionReturnCode>();
	}

	int wndStyle = 0;

	if (mode == eQuestionMode::eQuestionMode_OK)
		wndStyle = wxOK;
	else if (mode == eQuestionMode::eQuestionMode_OKCancel)
		wndStyle = wxOK | wxCANCEL;
	else if (mode == eQuestionMode::eQuestionMode_YesNo)
		wndStyle = wxYES | wxNO;
	else if (mode == eQuestionMode::eQuestionMode_YesNoCancel)
		wndStyle = wxYES | wxNO | wxCANCEL;

	int retCode = wxMessageBox(strMessage, _("Question"), wndStyle | wxICON_QUESTION,
		backend_mainFrame ? backend_mainFrame->GetFrameHandler() : nullptr
	);

	switch (retCode) {
	case wxOK:
		return CValue::CreateAndConvertObjectValueRef<CValueQuestionReturnCode>(eQuestionReturnCode::eQuestionReturnCode_OK);
	case wxCANCEL:
		return CValue::CreateAndConvertObjectValueRef<CValueQuestionReturnCode>(eQuestionReturnCode::eQuestionReturnCode_Cancel);
	case wxYES:
		return CValue::CreateAndConvertObjectValueRef<CValueQuestionReturnCode>(eQuestionReturnCode::eQuestionReturnCode_Yes);
	case wxNO:
		return CValue::CreateAndConvertObjectValueRef<CValueQuestionReturnCode>(eQuestionReturnCode::eQuestionReturnCode_No);
	}

	return CValue();
}

void CSystemFunction::SetStatus(const wxString& sStatus)
{
	if (CBackendException::IsEvalMode())
		return;

	if (backend_mainFrame != nullptr) {
		backend_mainFrame->SetStatusText(sStatus);
	}
}

void CSystemFunction::ClearMessage()
{
	if (CBackendException::IsEvalMode())
		return;

	if (backend_mainFrame != nullptr) {
		backend_mainFrame->ClearMessage();
	}
}

void CSystemFunction::SetError(const wxString& strError)
{
	if (CBackendException::IsEvalMode())
		return;
	CBackendException::Error(strError);
}

void CSystemFunction::Raise(const wxString& strError)
{
	if (CBackendException::IsEvalMode())
		return;
	CProcUnit::Raise(); CBackendException::Error(strError);
}

wxString CSystemFunction::ErrorDescription()
{
	if (CBackendException::IsEvalMode())
		return wxEmptyString;
	return CBackendException::GetLastError();
}

bool CSystemFunction::IsEmptyValue(const CValue& cData)
{
	return cData.IsEmpty();
}

CValue CSystemFunction::Evaluate(const wxString& strExpression)
{
	CValue retValue;
	CProcUnit::Evaluate(strExpression, CProcUnit::GetCurrentRunContext(), retValue, false);
	return retValue;
}

void CSystemFunction::Execute(const wxString& strExpression)
{
	if (CBackendException::IsEvalMode())
		return;
	CValue retValue;
	CProcUnit::Evaluate(strExpression, CProcUnit::GetCurrentRunContext(), retValue, true);
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

wxString CSystemFunction::Format(CValue& cData, const wxString& fmt)
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
	case eValueTypes::TYPE_BOOLEAN: {
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
	case eValueTypes::TYPE_NUMBER:
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

		number_t number = cData.GetNumber();

		if (number.IsZero()) {
			auto foundedNZ = paParams.find(NZ);

			if (foundedNZ != paParams.end()) {
				return foundedNZ->second;
			}
		}

		return number.ToString(conv);
	}
	case eValueTypes::TYPE_DATE:

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

#include "backend/compiler/value/valueType.h"

CValue CSystemFunction::Type(const CValue& cTypeName)
{
	if (cTypeName.GetType() != eValueTypes::TYPE_STRING) {
		Raise(_("Cannot convert value"));
		return CValue();
	}
	wxString typeName = cTypeName.GetString();
	if (!commonMetaData->IsRegisterCtor(typeName)) {
		Raise(wxString::Format(_("Type not found '%s'"), typeName));
	}
	return CValue::CreateAndConvertObjectValueRef<CValueType>(typeName);
}

CValue CSystemFunction::TypeOf(const CValue& cData)
{
	return CValue::CreateAndConvertObjectValueRef<CValueType>(cData);
}

int CSystemFunction::Rand()
{
	return rand();
}

int CSystemFunction::ArgCount()//КоличествоАргументовПрограммы
{
	return __argc;
}

wxString CSystemFunction::ArgValue(int n)//ЗначениеАргументаПрограммы
{
	if (n<0 || n> __argc) CBackendException::Error("Invalid argument index");
	return __wargv[n];
}

wxString CSystemFunction::ComputerName()//ИмяКомпьютера
{
	return wxGetHostName();
}

void CSystemFunction::RunApp(const wxString& sCommand)//ЗапуститьПриложение
{
	if (CBackendException::IsEvalMode())
		return;
	wxExecute(sCommand);
}

void CSystemFunction::SetAppTitle(const wxString& sTitle)//ЗаголовокСистемы
{
	if (CBackendException::IsEvalMode())
		return;
	if (backend_mainFrame != nullptr) {
		backend_mainFrame->SetTitle(sTitle);
	}
}

wxString CSystemFunction::UserDir() {
	return wxEmptyString;
}

wxString CSystemFunction::UserName() {
	return appData->GetUserName();
}

wxString CSystemFunction::UserPassword() {
	return appData->GetUserPassword();
}

bool CSystemFunction::ExclusiveMode() {
	return appData->ExclusiveMode();
}

wxString CSystemFunction::GeneralLanguage() {
	return "en_US";
}

////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : system objects 
////////////////////////////////////////////////////////////////////////////

#include "backend/metaData.h"

void CSystemFunction::EndJob(bool force)//ЗавершитьРаботуСистемы
{
	if (force) {
		appDataDestroy();
		std::exit(EXIT_SUCCESS);
	}
	else {
		IModuleManager* moduleManager = commonMetaData->GetModuleManager();
		if (moduleManager->DestroyMainModule()) {
			appDataDestroy();
			std::exit(EXIT_SUCCESS);
		}
	}
}

void CSystemFunction::UserInterruptProcessing()
{
	bool bNeedInterruptProcessing = wxGetKeyState(WXK_CONTROL)
		&& wxGetKeyState(WXK_CANCEL);
	if (bNeedInterruptProcessing) {
		throw (new CBackendInterrupt());
	}
}

CValue CSystemFunction::GetCommonForm(const wxString& formName, IBackendControlFrame* ownerControl, CValueGuid* unique)
{
	if (!appData->DesignerMode()) {
		for (auto& obj : commonMetaData->GetMetaObject(g_metaCommonFormCLSID)) {
			if (stringUtils::CompareString(formName, obj->GetName())) {
				CMetaObjectCommonForm* commonForm = obj->ConvertToType<CMetaObjectCommonForm>();
				wxASSERT(commonForm);
				IBackendValueForm* valueForm = commonForm->GenerateForm(ownerControl, nullptr, unique ? ((Guid)*unique) : Guid());
				if (valueForm != nullptr) {
					const CValue cValue = valueForm->GetImplValueRef();
					valueForm->InitializeFormModule();
					return cValue;
				}
			}
		}
		CSystemFunction::Raise(_("Common form not found '") + formName + "'");
	}
	return CValue();
}

void CSystemFunction::ShowCommonForm(const wxString& formName, IBackendControlFrame* ownerControl, CValueGuid* unique)
{
	if (CBackendException::IsEvalMode())
		return;
	const CValue& cValue = GetCommonForm(formName, ownerControl, unique);
	IBackendValueForm* valueForm = dynamic_cast<IBackendValueForm*>(cValue.GetRef());
	if (valueForm != nullptr) {
		valueForm->ShowForm();
	}
}

void CSystemFunction::BeginTransaction()
{
	if (CBackendException::IsEvalMode())
		return;

	db_query->BeginTransaction();
}

void CSystemFunction::CommitTransaction()
{
	if (CBackendException::IsEvalMode())
		return;

	db_query->Commit();
}

void CSystemFunction::RollBackTransaction()
{
	if (CBackendException::IsEvalMode())
		return;

	db_query->RollBack();
}
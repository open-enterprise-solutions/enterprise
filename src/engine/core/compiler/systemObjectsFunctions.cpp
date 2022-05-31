#include "systemObjects.h"
#include "databaseLayer/databaseLayer.h"
#include "frontend/output/outputWindow.h"
#include "frontend/visualView/controls/form.h"
#include "metadata/metaObjects/metaFormObject.h"
#include "metadata/metadata.h"
#include "utils/stringUtils.h"
#include "frontend/mainFrame.h"
#include "translateModule.h"
#include "procUnit.h"
#include "appData.h"

#include "systemObjectsEnums.h"

//--- Базовые:
bool CSystemObjects::Boolean(const CValue& cValue)
{
	return cValue.GetBoolean();
}

number_t CSystemObjects::Number(const CValue& cValue)
{
	return cValue.GetNumber();
}

wxLongLong_t CSystemObjects::Date(const CValue& cValue)
{
	return cValue.GetDate();
}

wxString CSystemObjects::String(const CValue& cValue)
{
	return cValue.GetString();
}

//---Математические:
number_t CSystemObjects::Round(const CValue& cValue, int precision, eRoundMode mode)
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

CValue CSystemObjects::Int(const CValue& cValue)
{
	ttmath::Int<TTMATH_BITS(128)> int128;
	number_t fNumber = cValue.GetNumber();
	if (!fNumber.ToInt(int128)) return int128;
	else return 0;
}

number_t CSystemObjects::Log10(const CValue& cValue)
{
	number_t fNumber = cValue.GetNumber();
	return std::log10(fNumber.ToDouble());
}

number_t CSystemObjects::Ln(const CValue& cValue)
{
	number_t fNumber = cValue.GetNumber();
	return std::log(fNumber.ToDouble());
}

CValue CSystemObjects::Max(CValue** aParams)
{
	int i = 1;
	const CValue* pRes = aParams[0];
	while (aParams[i]->GetType() != TYPE_EMPTY) { if (aParams[i]->GetNumber() > pRes->GetNumber()) pRes = aParams[i]; i++; }
	return *pRes;
}

CValue CSystemObjects::Min(CValue** aParams)
{
	int i = 1;
	const CValue* pRes = aParams[0];
	while (aParams[i]->GetType() != TYPE_EMPTY) { if (aParams[i]->GetNumber() < pRes->GetNumber()) pRes = aParams[i]; i++; }
	return *pRes;
}

CValue CSystemObjects::Sqrt(const CValue& cValue)
{
	number_t fNumber = cValue.GetNumber();
	if (fNumber.Sqrt() == 0) {
		return fNumber;
	}
	CSystemObjects::Raise("Incorrect argument value for built-in function (Sqrt)");
	return CValue();
}

//---Строковые:
int CSystemObjects::StrLen(const CValue& cValue)
{
	wxString csString = cValue.GetString();
	return csString.Length();
}

bool CSystemObjects::IsBlankString(const CValue& cValue)
{
	wxString csString = cValue.GetString();
	csString.Trim(true);
	csString.Trim(false);
	return csString.IsEmpty();
}

wxString CSystemObjects::TrimL(const CValue& cValue)
{
	wxString csString = cValue.GetString();
	csString.Trim(false);
	return csString;
}

wxString CSystemObjects::TrimR(const CValue& cValue)
{
	wxString csString = cValue.GetString();
	csString.Trim(true);
	return csString;
}

wxString CSystemObjects::TrimAll(const CValue& cValue)
{
	wxString csString = cValue.GetString();
	csString.Trim(true);
	csString.Trim(false);
	return csString;
}

wxString CSystemObjects::Left(const CValue& cValue, unsigned int nCount)
{
	wxString csString = cValue.GetString();
	return csString.Left(nCount);
}

wxString CSystemObjects::Right(const CValue& cValue, unsigned int nCount)
{
	wxString csString = cValue.GetString();
	return csString.Right(nCount);
}

wxString CSystemObjects::Mid(const CValue& cValue, unsigned int nFirst, unsigned int nCount)
{
	wxString csString = cValue.GetString();
	return csString.Mid(nFirst, nCount);
}

unsigned int CSystemObjects::Find(const CValue& cValue, const CValue& cValue2, unsigned int nStart)
{
	if (nStart < 1) nStart = 1;
	wxString csStr = cValue.GetString();
	return csStr.find(cValue2.GetString(), nStart - 1) + 1;
}

wxString CSystemObjects::StrReplace(const CValue& cSource, const CValue& cValue1, const CValue& cValue2)
{
	wxString csSource = cSource.GetString();
	csSource.Replace(cValue1.GetString(), cValue2.GetString());
	return csSource;
}

int CSystemObjects::StrCountOccur(const CValue& cSource, const CValue& cValue1)
{
	wxString csSource = cSource.GetString();
	return csSource.find(cValue1.GetString());
}

int CSystemObjects::StrLineCount(const CValue& cSource)
{
	wxString csSource = cSource.GetString();
	return csSource.find('\n') + 1;
}

wxString CSystemObjects::StrGetLine(const CValue& cValue, unsigned int nLine)
{
	wxString csSource = cValue.GetString() + wxT("\r\n");

	unsigned int nLast = 0;
	unsigned int nStartLine = 1;

	//********блок для ускорения
	static wxString _csStaticSource;

	static unsigned int _nStaticLast = 0;
	static unsigned int _nStaticLine = 0;

	if (_csStaticSource == csSource)
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
		unsigned int nIndex = csSource.find(wxT("\r\n"), nLast);

		if (nIndex < 0) return wxEmptyString;

		if (i == nLine)
		{
			_csStaticSource = csSource;
			_nStaticLast = nIndex + 2;
			_nStaticLine = nLine + 1;

			return csSource.Mid(nLast, nIndex - nLast);
		}

		nLast = nIndex + 2;
	}

	return wxEmptyString;
}

wxString CSystemObjects::Upper(const CValue& cSource)
{
	wxString csSource = cSource.GetString();
	csSource.MakeUpper();
	return csSource;
}

wxString CSystemObjects::Lower(const CValue& cSource)
{
	wxString csSource = cSource.GetString();
	csSource.MakeLower();
	return csSource;
}

wxString CSystemObjects::Chr(short nCode)
{
	return wxString(static_cast<wchar_t>(nCode));
}

short CSystemObjects::Asc(const CValue& cSource)
{
	wxString csSource = cSource.GetString();
	if (!csSource.Length()) return 0;
	return static_cast<wchar_t>(csSource[0]);
}

//---Работа с датой и временем
CValue CSystemObjects::CurrentDate()
{
	wxDateTime timeNow = wxDateTime::Now();
	wxLongLong m_llValue = timeNow.GetValue();

	CValue valueNow = eValueTypes::TYPE_DATE;
	valueNow.m_dData = m_llValue.GetValue();
	return valueNow;
}

CValue CSystemObjects::WorkingDate()
{
	m_workDate.SetHour(0);
	m_workDate.SetMinute(0);
	m_workDate.SetSecond(0);

	return m_workDate;
}

CValue CSystemObjects::AddMonth(const CValue& cData, int nMonthAdd)
{
	int nYear, nMonth, nDay;
	cData.FromDate(nYear, nMonth, nDay);
	int SummaMonth = nYear * 12 + nMonth - 1;
	SummaMonth += nMonthAdd;
	nYear = SummaMonth / 12;
	nMonth = SummaMonth % 12 + 1;

	return CValue(nYear, nMonth, nDay);
}

CValue CSystemObjects::BegOfMonth(const CValue& cData)
{
	int nYear, nMonth, nDay;
	cData.FromDate(nYear, nMonth, nDay);
	return CValue(nYear, nMonth, 1);
}

CValue CSystemObjects::EndOfMonth(const CValue& cData)
{
	int nYear, nMonth, nDay;
	cData.FromDate(nYear, nMonth, nDay);

	CValue m_date = CValue(nYear, nMonth, 1, 23, 59, 59);
	return AddMonth(m_date, 1) - 1;
}

CValue CSystemObjects::BegOfQuart(const CValue& cData)
{
	int nYear, nMonth, nDay;
	cData.FromDate(nYear, nMonth, nDay);
	return CValue(nYear, 1 + ((nMonth - 1) / 3) * 3, 1);
}

CValue CSystemObjects::EndOfQuart(const CValue& cData)
{
	return AddMonth(BegOfQuart(cData), 3) - 1;
}

CValue CSystemObjects::BegOfYear(const CValue& cData)
{
	int nYear, nMonth, nDay;
	cData.FromDate(nYear, nMonth, nDay);
	return CValue(nYear, 1, 1);
}

CValue CSystemObjects::EndOfYear(const CValue& cData)
{
	int nYear, nMonth, nDay;
	cData.FromDate(nYear, nMonth, nDay);
	return CValue(nYear, 12, 31, 23, 59, 59);
}

CValue CSystemObjects::BegOfWeek(const CValue& cData)
{
	int nYear, nMonth, nDay, DayOfWeek, DayOfYear, WeekOfYear;
	cData.FromDate(nYear, nMonth, nDay, DayOfWeek, DayOfYear, WeekOfYear);
	CValue Date1 = CValue(nYear, nMonth, nDay) - (DayOfWeek + 1);
	return Date1;
}

CValue CSystemObjects::EndOfWeek(const CValue& cData)
{
	int nYear, nMonth, nDay, DayOfWeek, DayOfYear, WeekOfYear;
	cData.FromDate(nYear, nMonth, nDay, DayOfWeek, DayOfYear, WeekOfYear);
	return CValue(nYear, nMonth, nDay) + (7 - DayOfWeek);
}

CValue CSystemObjects::BegOfDay(const CValue& cData)
{
	int nYear, nMonth, nDay;
	cData.FromDate(nYear, nMonth, nDay);
	return CValue(nYear, nMonth, nDay, 0, 0, 0);
}

CValue CSystemObjects::EndOfDay(const CValue& cData)
{
	int nYear, nMonth, nDay;
	cData.FromDate(nYear, nMonth, nDay);
	return CValue(nYear, nMonth, nDay, 23, 59, 59);
}

int CSystemObjects::GetYear(const CValue& cData)
{
	int nYear, nMonth, nDay;
	cData.FromDate(nYear, nMonth, nDay);
	return nYear;
}

int CSystemObjects::GetMonth(const CValue& cData)
{
	int nYear, nMonth, nDay;
	cData.FromDate(nYear, nMonth, nDay);
	return nMonth;
}

int CSystemObjects::GetDay(const CValue& cData)
{
	int nYear, nMonth, nDay;
	cData.FromDate(nYear, nMonth, nDay);
	return nDay;
}

int CSystemObjects::GetHour(const CValue& cData)
{
	int nYear, nMonth, nDay; unsigned short nHour, nMinutes, nSeconds;
	cData.FromDate(nYear, nMonth, nDay, nHour, nMinutes, nSeconds);
	return nHour;
}

int CSystemObjects::GetMinute(const CValue& cData)
{
	int nYear, nMonth, nDay; unsigned short nHour, nMinutes, nSeconds;
	cData.FromDate(nYear, nMonth, nDay, nHour, nMinutes, nSeconds);
	return nMinutes;
}

int CSystemObjects::GetSecond(const CValue& cData)
{
	int nYear, nMonth, nDay; unsigned short nHour, nMinutes, nSeconds;
	cData.FromDate(nYear, nMonth, nDay, nHour, nMinutes, nSeconds);
	return nSeconds;
}

int CSystemObjects::GetWeekOfYear(const CValue& cData)
{
	int nYear, nMonth, nDay, DayOfWeek, DayOfYear, WeekOfYear;
	cData.FromDate(nYear, nMonth, nDay, DayOfWeek, DayOfYear, WeekOfYear);
	return WeekOfYear;
}

int CSystemObjects::GetDayOfYear(const CValue& cData)
{
	int nYear, nMonth, nDay, DayOfWeek, DayOfYear, WeekOfYear;
	cData.FromDate(nYear, nMonth, nDay, DayOfWeek, DayOfYear, WeekOfYear);
	return DayOfYear;
}

int CSystemObjects::GetDayOfWeek(const CValue& cData)
{
	int nYear, nMonth, nDay, DayOfWeek, DayOfYear, WeekOfYear;
	cData.FromDate(nYear, nMonth, nDay, DayOfWeek, DayOfYear, WeekOfYear);
	return DayOfWeek;
}

int CSystemObjects::GetQuartOfYear(const CValue& cData)
{
	int nYear, nMonth, nDay;
	cData.FromDate(nYear, nMonth, nDay);
	return 1 + ((nMonth - 1) / 3);
}

//--- Работа с файлами: 

#include <wx/filename.h>

bool CSystemObjects::CopyFile(const wxString& src, const wxString& dst)
{
	return wxCopyFile(src, dst);
}

bool CSystemObjects::DeleteFile(const wxString& file)
{
	return wxRemoveFile(file);
}

wxString CSystemObjects::GetTempDir()
{
	return wxFileName::GetTempDir();
}

wxString CSystemObjects::GetTempFileName()
{
	return wxFileName::CreateTempFileName(
		wxEmptyString
	);
}

//--- Работа с окнами: 
CValueForm* CSystemObjects::ActiveWindow()
{
	if (CMainFrame::Get()) {
		wxDocChildFrameAnyBase* activeChild =
			dynamic_cast<wxDocChildFrameAnyBase*>(mainFrame->GetActiveChild());
		if (activeChild != NULL) {
			CVisualView* formView = dynamic_cast<CVisualView*>(activeChild->GetView());
			if (formView != NULL) {
				return formView->GetValueForm();
			}
		}
	}

	return NULL;
}

//--- Специальные:
void CSystemObjects::Message(const wxString& sMessage, eStatusMessage status)
{
	if (CTranslateError::IsSimpleMode()) {
		return;
	}

	if (status == eStatusMessage::eStatusMessage_Information) {
		outputWindow->OutputMessage(sMessage);
	}
	else if (status == eStatusMessage::eStatusMessage_Warning) {
		outputWindow->OutputWarning(sMessage);
	}
	else {
		outputWindow->OutputError(sMessage);
	}
}

void CSystemObjects::Alert(const wxString& sMessage)//Предупреждение
{
	if (CTranslateError::IsSimpleMode()) {
		return;
	}

	wxMessageBox(sMessage, wxT("Warning"), wxICON_WARNING, CMainFrame::Get());
}

CValue CSystemObjects::Question(const wxString& sMessage, eQuestionMode mode)//Вопрос
{
	if (CTranslateError::IsSimpleMode()) {
		return CValue();
	}

	int m_wndStyle = 0;

	if (mode == eQuestionMode::eQuestionMode_OK) m_wndStyle = wxOK;
	else if (mode == eQuestionMode::eQuestionMode_OKCancel) m_wndStyle = wxOK | wxCANCEL;
	else if (mode == eQuestionMode::eQuestionMode_YesNo) m_wndStyle = wxYES | wxNO;
	else if (mode == eQuestionMode::eQuestionMode_YesNoCancel) m_wndStyle = wxYES | wxNO | wxCANCEL;

	int retCode = wxMessageBox(sMessage, wxT("Question"), m_wndStyle | wxICON_QUESTION, CMainFrame::Get());

	if (retCode == wxOK) {
		return new CValueQuestionReturnCode(eQuestionReturnCode::eQuestionReturnCode_OK);
	}
	else if (retCode == wxCANCEL) {
		return new CValueQuestionReturnCode(eQuestionReturnCode::eQuestionReturnCode_Cancel);
	}
	else if (retCode == wxYES) {
		return new CValueQuestionReturnCode(eQuestionReturnCode::eQuestionReturnCode_Yes);
	}
	else if (retCode == wxNO) {
		return new CValueQuestionReturnCode(eQuestionReturnCode::eQuestionReturnCode_No);
	}

	return CValue();
}

void CSystemObjects::SetStatus(const wxString& sStatus)
{
	if (CTranslateError::IsSimpleMode()) return;
	mainFrame->SetStatusText(sStatus);
}

void CSystemObjects::ClearMessages()
{
	if (CTranslateError::IsSimpleMode()) return;
	outputWindow->Clear();
}

void CSystemObjects::SetError(const wxString& sError)
{
	if (CTranslateError::IsSimpleMode()) return;
	CTranslateError::Error(sError);
}

void CSystemObjects::Raise(const wxString& sError)
{
	if (CTranslateError::IsSimpleMode()) return;
	CProcUnit::Raise(); CTranslateError::Error(sError);
}

wxString CSystemObjects::ErrorDescription()
{
	if (CTranslateError::IsSimpleMode())
		return wxEmptyString;
	return CTranslateError::GetLastError();
}

bool CSystemObjects::IsEmptyValue(const CValue& cData)
{
	return cData.IsEmpty();
}

CValue CSystemObjects::Evaluate(const wxString& sExpression)
{
	return CProcUnit::Evaluate(sExpression, CProcUnit::GetCurrentRunContext(), false);
}

void CSystemObjects::Execute(const wxString& sExpression)
{
	if (CTranslateError::IsSimpleMode())
		return;
	CProcUnit::Evaluate(sExpression, CProcUnit::GetCurrentRunContext(), true);
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

wxString CSystemObjects::Format(CValue& cData, const wxString& fmt)
{
	wxString leftParam, rightParam;
	std::map<wxString, wxString> aParams;
	bool bLeftParam = true;
	for (unsigned int i = 0; i < fmt.length(); i++) {
		auto c = fmt.at(i);
		if (c == ';') {
			leftParam.Trim(true); leftParam.Trim(false);
			rightParam.Trim(true); rightParam.Trim(false);
			aParams.insert_or_assign(leftParam, rightParam);
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
			aParams.insert_or_assign(leftParam, rightParam);
			bLeftParam = true; leftParam = ""; rightParam = "";
		}
	}

	switch (cData.GetType()) {
	case eValueTypes::TYPE_BOOLEAN: {
		if (cData.GetBoolean()) {
			auto foundedBT = aParams.find(BT);
			if (foundedBT != aParams.end()) {
				return foundedBT->second;
			}
		}
		else {
			auto foundedBT = aParams.find(BF);
			if (foundedBT != aParams.end()) {
				return foundedBT->second;
			}
		}
		return cData.GetString();
	}
	case eValueTypes::TYPE_NUMBER:
	{
		ttmath::Conv conv;

		auto foundedND = aParams.find(ND);
		if (foundedND != aParams.end()) {
			conv.precision = wxAtoi(foundedND->second);
			conv.trim_zeroes = true;
		}

		auto foundedNFD = aParams.find(NFD);
		if (foundedNFD != aParams.end()) {
			conv.round = wxAtoi(foundedNFD->second);
			conv.trim_zeroes = false;
		}

		auto foundedNDS = aParams.find(NDS);
		if (foundedNDS != aParams.end()) {
			conv.comma = foundedNDS->second[0];
		}

		auto foundedNGS = aParams.find(NGS);
		if (foundedNGS != aParams.end()) {
			conv.comma2 = foundedNGS->second[0];
		}

		auto foundedNG = aParams.find(NG);
		if (foundedNG != aParams.end()) {
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

		auto foundedNLZ = aParams.find(NLZ);
		if (foundedNLZ != aParams.end()) {
			conv.leading_zero = true;
		}

		number_t number = cData.GetNumber();

		if (number.IsZero()) {
			auto foundedNZ = aParams.find(NZ);

			if (foundedNZ != aParams.end()) {
				return foundedNZ->second;
			}
		}

		return number.ToString(conv);
	}
	case eValueTypes::TYPE_DATE:

		if (cData.IsEmpty()) {
			auto foundedDE = aParams.find(DE);
			if (foundedDE != aParams.end()) {
				return foundedDE->second;
			};
		}

		auto foundedDF = aParams.find(DF);
		if (foundedDF != aParams.end()) {

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

#include "valueType.h"

CValue CSystemObjects::Type(const CValue& cTypeName)
{
	if (cTypeName.GetType() != eValueTypes::TYPE_STRING) {
		Raise(_("Cannot convert value"));
		return CValue();
	}
	wxString typeName = cTypeName.GetString();
	if (!metadata->IsRegisterObject(typeName))
		Raise(wxString::Format(_("Type not found '%s'"), typeName));
	return new CValueType(typeName);
}

CValue CSystemObjects::TypeOf(const CValue& cData)
{
	return new CValueType(cData);
}

int CSystemObjects::Rand()
{
	return rand();
}

int CSystemObjects::ArgCount()//КоличествоАргументовПрограммы
{
	return __argc;
}

wxString CSystemObjects::ArgValue(int n)//ЗначениеАргументаПрограммы
{
	if (n<0 || n> __argc) CTranslateError::Error(_("Invalid argument index"));
	return __wargv[n];
}

wxString CSystemObjects::ComputerName()//ИмяКомпьютера
{
	return wxGetHostName();
}

void CSystemObjects::RunApp(const wxString& sCommand)//ЗапуститьПриложение
{
	if (CTranslateError::IsSimpleMode())
		return;
	wxExecute(sCommand);
}

void CSystemObjects::SetAppTitle(const wxString& sTitle)//ЗаголовокСистемы
{
	if (CTranslateError::IsSimpleMode())
		return;
	mainFrame->SetTitle(sTitle);
}

wxString CSystemObjects::UserDir() {
	return appData->GetApplicationPath();
}

wxString CSystemObjects::UserName() {
	return appData->GetUserName();
}

wxString CSystemObjects::UserPassword() {
	return appData->GetUserPassword();
}

bool CSystemObjects::SingleMode() { return appData->SingleMode(); }

int CSystemObjects::GeneralLanguage() { return 1; }

////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : system objects 
////////////////////////////////////////////////////////////////////////////

#include "metadata/metadata.h"

void CSystemObjects::EndJob(bool force)//ЗавершитьРаботуСистемы
{
	if (force) {
		appDataDestroy();
		std::exit(EXIT_SUCCESS);
	}
	else {
		IModuleManager* moduleManager = metadata->GetModuleManager();
		if (moduleManager->DestroyMainModule()) {
			appDataDestroy();
			std::exit(EXIT_SUCCESS);
		}
	}
}

void CSystemObjects::UserInterruptProcessing()
{
	bool bNeedInterruptProcessing = wxGetKeyState(WXK_CONTROL)
		&& wxGetKeyState(WXK_CANCEL);
	if (bNeedInterruptProcessing) {
		throw (new CInterruptBreak());
	}
}

CValueForm* CSystemObjects::GetCommonForm(const wxString& sFormName, IValueFrame* owner, CValueGuid* unique)
{
	for (auto commonForm : metadata->GetMetaObjects(g_metaCommonFormCLSID)) {
		if (StringUtils::MakeUpper(sFormName) == StringUtils::MakeUpper(commonForm->GetName())) {
			CValueForm* valueForm = static_cast<CMetaFormObject*>(commonForm)->GenerateForm(owner, NULL, unique ? ((Guid)*unique) : Guid());
			if (valueForm) {
				unsigned int nRefOld = valueForm->GetRefCount();
				valueForm->IncrRef();
				if (appData->EnterpriseMode()) {
					valueForm->InitializeFormModule();
				}
				valueForm->SetRefCount(nRefOld);
				return valueForm;
			}
		}
	}

	Raise(_("Сommon form not found '") + sFormName + "'");
	return NULL;
}

void CSystemObjects::ShowCommonForm(const wxString& sFormName, IValueFrame* owner, CValueGuid* unique)
{
	if (CTranslateError::IsSimpleMode())
		return;

	CValueForm* generatedForm = GetCommonForm(sFormName, owner, unique);
	if (generatedForm) {
		generatedForm->ShowForm();
	}
}

void CSystemObjects::BeginTransaction()
{
	if (CTranslateError::IsSimpleMode())
		return;
	databaseLayer->BeginTransaction();
}

void CSystemObjects::CommitTransaction()
{
	if (CTranslateError::IsSimpleMode())
		return;
	databaseLayer->Commit();
}

void CSystemObjects::RollBackTransaction()
{
	if (CTranslateError::IsSimpleMode())
		return;
	databaseLayer->RollBack();
}
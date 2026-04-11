#include "valueSpreadsheet.h"
#include "backend/backend_mainFrame.h"

#pragma region __collection__h__

#include "backend/system/value/valueMap.h"

void ibValueSpreadsheetDocumentBorder::PrepareNames() const
{
	m_methodHelper.ClearHelper();

	m_methodHelper.AppendProp(wxT("Style"));
	m_methodHelper.AppendProp(wxT("Colour"));
	m_methodHelper.AppendProp(wxT("Width"));
}

bool ibValueSpreadsheetDocumentBorder::SetPropVal(const long lPropNum, const ibValue& varPropVal)
{
	return false;
}

#include "valueColour.h"

bool ibValueSpreadsheetDocumentBorder::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	switch (lPropNum)
	{
	case enPropStyle:
		pvarPropVal = ibValue::CreateEnumObject<ibValueEnumSpreadsheetBorder>(m_style);
		return true;
	case enPropColour:
		pvarPropVal = ibValue::CreateAndPrepareValueRef<ibValueColour>(m_colour);
		return true;
	case enPropWidth:
		pvarPropVal = m_width;
		return true;
	}

	return false;
}

class ibValueSpreadsheetDocumentRange :
	public ibValue {

	enum
	{
		enPropLabel,
		enPropStart,
		enPropEnd
	};

public:

	ibValueSpreadsheetDocumentRange() : ibValue(ibValueTypes::TYPE_VALUE), m_label(), m_start(-1), m_end(-1) {}
	ibValueSpreadsheetDocumentRange(const wxString& label, int start, int end) : ibValue(ibValueTypes::TYPE_VALUE), m_label(label), m_start(start), m_end(end) {}

	virtual bool IsEmpty() const { return false; }

	virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal) { return false; }
	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal) {

		switch (lPropNum)
		{
		case enPropLabel:
			pvarPropVal = m_label;
			return true;
		case enPropStart:
			pvarPropVal = m_start;
			return true;
		case enPropEnd:
			pvarPropVal = m_end;
			return true;
		}

		return false;
	}

	virtual ibValueMethodHelper* GetPMethods() const { // get a reference to the class helper for parsing attribute and method names
		//PrepareNames(); 
		return &m_methodHelper;
	}

	virtual void PrepareNames() const { // this method is automatically called to initialize attribute and method names
		m_methodHelper.ClearHelper();
		m_methodHelper.AppendProp(wxT("Label"));
		m_methodHelper.AppendProp(wxT("Start"));
		m_methodHelper.AppendProp(wxT("End"));
	}

private:

	const wxString m_label;
	const int m_start, m_end;

	static ibValueMethodHelper m_methodHelper;

	wxDECLARE_DYNAMIC_CLASS(ibValueSpreadsheetDocumentRange);
};

class ibValueSpreadsheetDocumentAreaCollection :
	public ibValueStructure {

public:

	ibValueSpreadsheetDocumentAreaCollection() :
		ibValueStructure(true)
	{
	}

	ibValueSpreadsheetDocumentAreaCollection(const wxObjectDataPtr<ibBackendSpreadsheetObject>& spreadsheetDoc) :
		ibValueStructure(true), m_spreadsheetDoc(spreadsheetDoc)
	{
		for (int idx = 0; idx < spreadsheetDoc->GetSpreadsheetDesc().GetAreaNumberRows(); idx++) {
			const ibSpreadsheetAreaDescription* area = spreadsheetDoc->GetSpreadsheetDesc().GetRowAreaByIdx(idx);
			if (area == nullptr)
				continue;
			ibValueStructure::Insert(area->m_label,
				ibValue::CreateAndPrepareValueRef<ibValueSpreadsheetDocumentRange>(area->m_label, area->m_start, area->m_end));
		}
	}

private:

	wxObjectDataPtr<ibBackendSpreadsheetObject> m_spreadsheetDoc;
	wxDECLARE_DYNAMIC_CLASS(ibValueSpreadsheetDocumentAreaCollection);
};

class ibValueSpreadsheetDocumentParameterCollection : public ibValue {

	enum
	{
		enCount,
		enFill,
		enGet,
		enSet,
	};

	static wxVector<wxString> ParseBrackets(const wxString& str) {

		wxVector<wxString> tokens;

		size_t start_pos = 0;
		size_t end_pos = 0;

		const wxString delimiters = wxT("[]");

		// Find the first opening or closing bracket
		start_pos = str.find_first_of(delimiters, start_pos);

		while (start_pos != wxString::npos) {

			// Find the next bracket of any type
			end_pos = str.find_first_of(delimiters, start_pos + 1);

			if (end_pos != wxString::npos) {
				// Extract the substring between the brackets
				// +1 to start after the opening bracket
				wxString token = str.substr(start_pos + 1, end_pos - start_pos - 1);
				if (!token.empty()) {
					tokens.push_back(token);
				}
				// Move start_pos to the character after the closing bracket for the next iteration
				start_pos = end_pos + 1;
			}
			else {
				// No matching end bracket found, stop
				break;
			}

			// Find the next opening bracket for the next iteration
			start_pos = str.find_first_of(delimiters, start_pos);
		}

		return tokens;
	}

public:

	ibValueSpreadsheetDocumentParameterCollection() :
		ibValue(ibValueTypes::TYPE_VALUE), m_methodHelper(nullptr)
	{
	}

	ibValueSpreadsheetDocumentParameterCollection(const wxObjectDataPtr<ibBackendSpreadsheetObject>& spreadsheetDoc) :
		ibValue(ibValueTypes::TYPE_VALUE), m_spreadsheetDoc(spreadsheetDoc), m_methodHelper(new ibValueMethodHelper)
	{
	}

	virtual ~ibValueSpreadsheetDocumentParameterCollection() { wxDELETE(m_methodHelper); }

	virtual bool IsEmpty() const { return false; }

	virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal) {
		m_spreadsheetDoc->SetParameter(m_methodHelper->GetPropName(lPropNum), varPropVal);
		return true;
	}

	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal) {
		m_spreadsheetDoc->GetParameter(m_methodHelper->GetPropName(lPropNum), pvarPropVal);
		return true;
	}

	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray) {

		switch (lMethodNum)
		{
		case enCount:
			pvarRetValue = ibValue(ibNumber(m_methodHelper ? static_cast<int>(m_methodHelper->GetNProps()) : 0));
			return true;
		case enGet:
			m_spreadsheetDoc->GetParameter(paParams[0]->GetString(), pvarRetValue);
			return true;
		}

		return false;
	}

	virtual bool CallAsProc(const long lMethodNum, ibValue** paParams, const long lSizeArray) {

		switch (lMethodNum)
		{
		case enFill:
		{
			ibValueMethodHelper* methodHelper = paParams[0]->GetPMethods();
			if (methodHelper != nullptr) {
				for (int lPropPos = 0; lPropPos < m_methodHelper->GetNProps(); lPropPos++) {
					if (lPropPos >= methodHelper->GetNProps())
						continue;
					const wxString& strPropName = methodHelper ->GetPropName(lPropPos);
					long lPropNum = methodHelper->FindProp(strPropName);
					if (lPropNum == wxNOT_FOUND)
						continue;
					ibValue pvarRetValue;
					paParams[0]->GetPropVal(lPropNum, pvarRetValue);
					m_spreadsheetDoc->SetParameter(strPropName, pvarRetValue);
				}
				return true;
			}
			return false;
		}
		case enSet:
			m_spreadsheetDoc->SetParameter(paParams[0]->GetString(), *paParams[1]);
			return true;
		}

		return false;
	}

	virtual ibValueMethodHelper* GetPMethods() const { // get a reference to the class helper for parsing attribute and method names
		//PrepareNames(); 
		return m_methodHelper;
	}

	virtual void PrepareNames() const { // this method is automatically called to initialize attribute and method names.

		// Define the comparator struct
		struct wxCompareStringFunc {
			bool operator()(const wxString& lhs, const wxString& rhs) const {
				// Sort based on the 'key' member in ascending order
				return lhs.Upper() < rhs.Upper();
			}
		};

		m_methodHelper->ClearHelper();

		m_methodHelper->AppendFunc(wxT("Count"), wxT("Count"));
		m_methodHelper->AppendProc(wxT("Fill"), 1, wxT("Fill(any : value)"));
		m_methodHelper->AppendFunc(wxT("Get"), 1, wxT("Get(parameter: string)"));
		m_methodHelper->AppendProc(wxT("Set"), 2, wxT("Set(parameter: string, any: value)"));

		std::set<wxString, wxCompareStringFunc> arrParameter;

		for (int idx = 0; idx < m_spreadsheetDoc->GetSpreadsheetDesc().GetCellCount(); idx++) {

			const ibSpreadsheetCellDescription* cell = m_spreadsheetDoc->GetSpreadsheetDesc().GetCellByIdx(idx);
			if (cell == nullptr)
				continue;

			if (cell->m_fillSetType == ibSpreadsheetFillType::ibSpreadsheetFillType_StrParameter) {
				if (!cell->IsEmptyValue()) arrParameter.insert(cell->m_value);
			}
			else if (cell->m_fillSetType == ibSpreadsheetFillType::ibSpreadsheetFillType_StrTemplate) {
				if (!cell->IsEmptyValue()) for (auto s : ParseBrackets(cell->m_value)) if (!s.IsEmpty()) arrParameter.insert(s);
			}

			if (!cell->IsEmptyParameter()) arrParameter.insert(cell->m_detailsParameter);
		}

		for (auto p : arrParameter) m_methodHelper->AppendProp(p);
	}

private:

	wxObjectDataPtr<ibBackendSpreadsheetObject> m_spreadsheetDoc;
	ibValueMethodHelper* m_methodHelper;
	wxDECLARE_DYNAMIC_CLASS(ibValueSpreadsheetDocumentParameterCollection);
};

wxIMPLEMENT_DYNAMIC_CLASS(ibValueSpreadsheetDocumentRange, ibValue);
wxIMPLEMENT_DYNAMIC_CLASS(ibValueSpreadsheetDocumentAreaCollection, ibValueStructure);
wxIMPLEMENT_DYNAMIC_CLASS(ibValueSpreadsheetDocumentParameterCollection, ibValue);

#pragma endregion

wxIMPLEMENT_DYNAMIC_CLASS(ibValueSpreadsheetDocument, ibValue);

wxIMPLEMENT_DYNAMIC_CLASS(ibValueEnumSpreadsheetOrient, ibValue);
wxIMPLEMENT_DYNAMIC_CLASS(ibValueEnumSpreadsheetHorizontalAlignment, ibValue);
wxIMPLEMENT_DYNAMIC_CLASS(ibValueEnumSpreadsheetVerticalAlignment, ibValue);
wxIMPLEMENT_DYNAMIC_CLASS(ibValueEnumSpreadsheetFitMode, ibValue);
wxIMPLEMENT_DYNAMIC_CLASS(ibValueEnumSpreadsheetBorder, ibValue);
wxIMPLEMENT_DYNAMIC_CLASS(ibValueEnumSpreadsheetFillType, ibValue);

ibValue::ibValueMethodHelper ibValueSpreadsheetDocument::m_methodHelper;
ibValue::ibValueMethodHelper ibValueSpreadsheetDocumentRange::m_methodHelper;

enum
{
	eFixedLeft,
	eFixedTop,
	eAreas,
	eParameters,
	eReadOnly,
	ePrinterName,
	eLanguageCode,
};

enum
{
	eArea,
	eRange,
	ePutVerticalPageBreak,
	ePutHorizontalPageBreak,
	eGetArea,
	eClear,
	ePrint,
	eShow,
	ePut,
	eJoin
};

void ibValueSpreadsheetDocument::PrepareNames() const
{
	m_methodHelper.ClearHelper();

	//freeze row/col
	m_methodHelper.AppendProp(wxT("FixedLeft"), eFixedLeft);
	m_methodHelper.AppendProp(wxT("FixedTop"), eFixedTop);
	m_methodHelper.AppendProp(wxT("Areas"), eAreas);
	m_methodHelper.AppendProp(wxT("Parameters"), eParameters);
	m_methodHelper.AppendProp(wxT("ReadOnly"), eReadOnly);
	m_methodHelper.AppendProp(wxT("PrinterName"), ePrinterName);
	m_methodHelper.AppendProp(wxT("LanguageCode"), eLanguageCode);

	m_methodHelper.AppendFunc(wxT("Area"), 2, wxT("Area(string: left, string: top = <empty>)"));
	m_methodHelper.AppendFunc(wxT("Range"), 2, wxT("Range(number: row start, number: row end, number: col start = -1, number: col end = -1)"));
	m_methodHelper.AppendProc(wxT("PutVerticalPageBreak"), wxT("PutVerticalPageBreak()"));
	m_methodHelper.AppendProc(wxT("PutHorizontalPageBreak"), wxT("PutHorizontalPageBreak()"));
	m_methodHelper.AppendFunc(wxT("GetArea"), 1, wxT("GetArea(string: label)"));
	m_methodHelper.AppendProc(wxT("Clear"), wxT("Clear()"));
	m_methodHelper.AppendProc(wxT("Print"), wxT("Print(bool: showPrintDlg = true)"));
	m_methodHelper.AppendProc(wxT("Show"), 1, wxT("Show(string: title)"));
	m_methodHelper.AppendProc(wxT("Put"), 1, wxT("Put(spreadsheetDocument: table)"));
	m_methodHelper.AppendProc(wxT("Join"), 1, wxT("Join(spreadsheetDocument: table)"));
}

bool ibValueSpreadsheetDocument::SetPropVal(const long lPropNum, const ibValue& varPropVal)
{
	switch (lPropNum)
	{
	case eFixedLeft:
		m_spreadsheetDoc->SetColFreeze(varPropVal.GetInteger());
		return true;
	case eFixedTop:
		m_spreadsheetDoc->SetRowFreeze(varPropVal.GetInteger());
		return true;
	case eAreas:
		return false;
	case eParameters:
		return false;
	case eReadOnly:
		m_spreadsheetDoc->EnableEditing(!varPropVal.GetBoolean());
		return true;
	case ePrinterName:
		m_spreadsheetDoc->SetPrinterName(varPropVal.GetString());
		return true;
	case eLanguageCode:
		m_spreadsheetDoc->SetLangCode(varPropVal.GetString());
		return true;
	}

	return false;
}

bool ibValueSpreadsheetDocument::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	switch (lPropNum)
	{
	case eFixedLeft:
		pvarPropVal = m_spreadsheetDoc->GetColFreeze();
		return true;
	case eFixedTop:
		pvarPropVal = m_spreadsheetDoc->GetRowFreeze();
		return true;
	case eAreas:
		pvarPropVal = ibValue::CreateAndPrepareValueRef<ibValueSpreadsheetDocumentAreaCollection>(m_spreadsheetDoc);
		return true;
	case eParameters:
		pvarPropVal = ibValue::CreateAndPrepareValueRef<ibValueSpreadsheetDocumentParameterCollection>(m_spreadsheetDoc);
		return true;
	case eReadOnly:
		pvarPropVal = !m_spreadsheetDoc->IsEditable();
		return true;
	case ePrinterName:
		pvarPropVal = m_spreadsheetDoc->GetPrinterName();
		return true;
	case eLanguageCode:
		pvarPropVal = m_spreadsheetDoc->GetLangCode();
		return true;
	}

	return false;
}

#include <wx/tokenzr.h>

bool ibValueSpreadsheetDocument::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	if (lMethodNum == eArea) {

		const ibSpreadsheetCellDescription* cell = m_spreadsheetDoc->GetSpreadsheetDesc().GetCell(
			paParams[0]->GetInteger(), lSizeArray > 1 ? paParams[1]->GetInteger() : 0);

		if (cell != nullptr) {
			pvarRetValue = ibValue::CreateAndPrepareValueRef<ibValueSpreadsheetDocumentArea>(
				m_spreadsheetDoc, paParams[0]->GetInteger(), lSizeArray > 1 ? paParams[1]->GetInteger() : 0);
			return true;
		}

		return false;
	}
	else if (lMethodNum == eRange) {

		if (paParams[0]->GetInteger() > m_spreadsheetDoc->GetNumberRows())
			return false;

		else if (paParams[1]->GetInteger() > m_spreadsheetDoc->GetNumberCols())
			return false;

		pvarRetValue = ibValue::CreateAndPrepareValueRef<ibValueSpreadsheetDocument>(m_spreadsheetDoc->GetArea(
			paParams[0]->GetInteger(), paParams[1]->GetInteger(), lSizeArray > 2 ? paParams[2]->GetInteger() : -1, lSizeArray > 3 ? paParams[3]->GetInteger() : -1));

		return true;
	}
	else if (lMethodNum == eGetArea) {

		if (lSizeArray == 1) {

			wxStringTokenizer tkn(paParams[0]->GetString(), wxT("|"));

			const ibSpreadsheetAreaDescription* r = m_spreadsheetDoc->GetSpreadsheetDesc().GetRowAreaByName(tkn.GetNextToken());
			const ibSpreadsheetAreaDescription* c = m_spreadsheetDoc->GetSpreadsheetDesc().GetRowAreaByName(tkn.GetNextToken());

			if (!r)
				return false;

			pvarRetValue = ibValue::CreateAndPrepareValueRef<ibValueSpreadsheetDocument>(m_spreadsheetDoc->GetAreaByName(r->m_label, c ? c->m_label : wxT("")));
			return true;
		}

		const ibSpreadsheetAreaDescription* r = m_spreadsheetDoc->GetSpreadsheetDesc().GetRowAreaByName(paParams[0]->GetString());
		const ibSpreadsheetAreaDescription* c = m_spreadsheetDoc->GetSpreadsheetDesc().GetRowAreaByName(lSizeArray > 1 ? paParams[1]->GetString() : wxT(""));

		if (!r)
			return false;

		pvarRetValue = ibValue::CreateAndPrepareValueRef<ibValueSpreadsheetDocument>(m_spreadsheetDoc->GetAreaByName(
			r->m_label, c ? c->m_label : wxT("")));

		return true;
	}

	return false;
}

#include "backend/backend_exception.h"

bool ibValueSpreadsheetDocument::CallAsProc(const long lMethodNum, ibValue** paParams, const long lSizeArray)
{
	if (lMethodNum == ePutHorizontalPageBreak) {
		m_spreadsheetDoc->AddRowBrake(m_spreadsheetDoc->GetNumberRows());
		return true;
	}
	else if (lMethodNum == ePutVerticalPageBreak) {
		m_spreadsheetDoc->AddColBrake(m_spreadsheetDoc->GetNumberCols());
		return true;
	}
	else if (lMethodNum == eClear) {
		m_spreadsheetDoc->ClearSpreadsheet();
		return true;
	}
	else if (lMethodNum == ePrint) {
		if (backend_mainFrame != nullptr)
			return backend_mainFrame->PrintSpreadsheetDocument(m_spreadsheetDoc, lSizeArray > 0 ? paParams[0]->GetBoolean() : false);
		ibBackendCoreException::Error(_("Context functions are not available!"));
		return false;
	}
	else if (lMethodNum == eShow) {
		if (backend_mainFrame != nullptr)
			return backend_mainFrame->ShowSpreadsheetDocument(paParams[0]->GetString(), m_spreadsheetDoc);
		ibBackendCoreException::Error(_("Context functions are not available!"));
		return false;
	}
	else if (lMethodNum == ePut) {
		ibValuePtr<ibValueSpreadsheetDocument> valueSpreadsheet(
			paParams[0]->ConvertToType<ibValueSpreadsheetDocument>());
		if (valueSpreadsheet)
			m_spreadsheetDoc->PutArea(valueSpreadsheet->GetSpreadsheetDocument());
		return true;
	}
	else if (lMethodNum == eJoin) {
		ibValuePtr<ibValueSpreadsheetDocument> valueSpreadsheet(
			paParams[0]->ConvertToType<ibValueSpreadsheetDocument>());
		if (valueSpreadsheet)
			m_spreadsheetDoc->JoinArea(valueSpreadsheet->GetSpreadsheetDocument());
		return true;
	}

	return false;
}

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

VALUE_TYPE_REGISTER(ibValueSpreadsheetDocument, "SpreadsheetDocument", string_to_clsid("VL_SPSTD"));
SYSTEM_TYPE_REGISTER(ibValueSpreadsheetDocumentRange, "SpreadsheetAreaRange", string_to_clsid("SY_SPPRA"));
SYSTEM_TYPE_REGISTER(ibValueSpreadsheetDocumentAreaCollection, "SpreadsheetAreaCollection", string_to_clsid("SY_SPAEA"));
SYSTEM_TYPE_REGISTER(ibValueSpreadsheetDocumentParameterCollection, "SpreadsheetParameterCollection", string_to_clsid("SY_SPPRM"));
ENUM_TYPE_REGISTER(ibValueEnumSpreadsheetOrient, "SpreadsheetOrient", string_to_clsid("EN_SORNT"));
ENUM_TYPE_REGISTER(ibValueEnumSpreadsheetHorizontalAlignment, "SpreadsheetHorizontalAlignment", string_to_clsid("EN_SHOAL"));
ENUM_TYPE_REGISTER(ibValueEnumSpreadsheetVerticalAlignment, "SpreadsheetVerticalAlignment", string_to_clsid("EN_SVEAL"));
ENUM_TYPE_REGISTER(ibValueEnumSpreadsheetBorder, "SpreadsheetBorder", string_to_clsid("EN_SBORD"));
ENUM_TYPE_REGISTER(ibValueEnumSpreadsheetFitMode, "SpreadsheetFitMode", string_to_clsid("EN_SFTMD"));
ENUM_TYPE_REGISTER(ibValueEnumSpreadsheetFillType, "SpreadsheetTemplate", string_to_clsid("EN_SFTMP"));


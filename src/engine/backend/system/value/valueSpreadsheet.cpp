#include "valueSpreadsheet.h"
#include "backend/backend_mainFrame.h"
#include "backend/session/session.h"

#pragma region __collection__h__

#include "backend/system/value/valueMap.h"

void ibValueSpreadsheetDocumentBorder::FillMembers(ibMemberTable& helper) const
{
	helper.AppendProp(wxT("Style"));
	helper.AppendProp(wxT("Colour"));
	helper.AppendProp(wxT("Width"));
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
		pvarPropVal = new ibValueColour(m_colour);
		return true;
	case enPropWidth:
		pvarPropVal = m_width;
		return true;
	}

	return false;
}

static void ibValueSpreadsheetDocumentRange_BindNames(ibValue::ibMemberTable& helper, const ibValue* /*ctx*/)
{
	helper.AppendProp(wxT("Label"));
	helper.AppendProp(wxT("Start"));
	helper.AppendProp(wxT("End"));
}

class ibValueSpreadsheetDocumentRange :
	public ibValueStaticMembers<&ibValueSpreadsheetDocumentRange_BindNames> {
	public:

	enum
	{
		enPropLabel,
		enPropStart,
		enPropEnd
	};

public:

	ibValueSpreadsheetDocumentRange() : ibValueStaticMembers(ibValueTypes::TYPE_VALUE), m_label(), m_start(-1), m_end(-1) {}
	ibValueSpreadsheetDocumentRange(const wxString& label, int start, int end) : ibValueStaticMembers(ibValueTypes::TYPE_VALUE), m_label(label), m_start(start), m_end(end) {}

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

	// DoGetPMethods (protected) + Shared<&ibValueSpreadsheetDocumentRange_BindNames> come from the base.

private:

	const wxString m_label;
	const int m_start, m_end;

};

class ibValueSpreadsheetDocumentAreaCollection :
	public ibValueStructure {
	public:

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
				new ibValueSpreadsheetDocumentRange(area->m_label, area->m_start, area->m_end));
		}
	}

private:

	wxObjectDataPtr<ibBackendSpreadsheetObject> m_spreadsheetDoc;
};

class ibValueSpreadsheetDocumentParameterCollection : public ibValueDynamicMembers {
	public:

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
		ibValueDynamicMembers(ibValueTypes::TYPE_VALUE)
	{
		// No doc → no parameters; leave the helper empty (no binder).
	}

	ibValueSpreadsheetDocumentParameterCollection(const wxObjectDataPtr<ibBackendSpreadsheetObject>& spreadsheetDoc) :
		ibValueDynamicMembers(ibValueTypes::TYPE_VALUE), m_spreadsheetDoc(spreadsheetDoc)
	{
		m_members.Bind(this, &ibValueSpreadsheetDocumentParameterCollection::FillMembers);
	}

	virtual ~ibValueSpreadsheetDocumentParameterCollection() {}

	virtual bool IsEmpty() const { return false; }

	virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal) {
		m_spreadsheetDoc->SetParameter(m_members.GetPropName(lPropNum), varPropVal);
		return true;
	}

	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal) {
		m_spreadsheetDoc->GetParameter(m_members.GetPropName(lPropNum), pvarPropVal);
		return true;
	}

	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray) {

		switch (lMethodNum)
		{
		case enCount:
			pvarRetValue = ibValue(ibNumber(static_cast<int>(GetPMethods()->GetNProps())));
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
			ibMemberTable* methodHelper = paParams[0]->GetPMethods();
			if (methodHelper != nullptr) {
				const long lSelfProps = GetPMethods()->GetNProps();
				for (int lPropPos = 0; lPropPos < lSelfProps; lPropPos++) {
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

	void FillMembers(ibMemberTable& helper) const {   // bound in ctor (was PrepareNames)

		// Define the comparator struct
		struct wxCompareStringFunc {
			bool operator()(const wxString& lhs, const wxString& rhs) const {
				// Sort based on the 'key' member in ascending order
				return lhs.Upper() < rhs.Upper();
			}
		};

		helper.AppendFunc(wxT("Count"), wxT("Count"));
		helper.AppendProc(wxT("Fill"), 1, wxT("Fill(any : value)"));
		helper.AppendFunc(wxT("Get"), 1, wxT("Get(parameter: string)"));
		helper.AppendProc(wxT("Set"), 2, wxT("Set(parameter: string, any: value)"));

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

		for (auto p : arrParameter) helper.AppendProp(p);
	}

private:

	wxObjectDataPtr<ibBackendSpreadsheetObject> m_spreadsheetDoc;
};


#pragma endregion



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
	eJoin,
	eBeginRowGroup,
	eEndRowGroup,
	eBeginColGroup,
	eEndColGroup,
};

void ibValueSpreadsheetDocument_BindNames(ibValue::ibMemberTable& helper, const ibValue* /*ctx*/)
{
	//freeze row/col
	helper.AppendProp(wxT("FixedLeft"), eFixedLeft);
	helper.AppendProp(wxT("FixedTop"), eFixedTop);
	helper.AppendProp(wxT("Areas"), eAreas);
	helper.AppendProp(wxT("Parameters"), eParameters);
	helper.AppendProp(wxT("ReadOnly"), eReadOnly);
	helper.AppendProp(wxT("PrinterName"), ePrinterName);
	helper.AppendProp(wxT("LanguageCode"), eLanguageCode);

	helper.AppendFunc(wxT("Area"), 2, wxT("Area(string: left, string: top = <empty>)"));
	helper.AppendFunc(wxT("Range"), 2, wxT("Range(number: row start, number: row end, number: col start = -1, number: col end = -1)"));
	helper.AppendProc(wxT("PutVerticalPageBreak"), wxT("PutVerticalPageBreak()"));
	helper.AppendProc(wxT("PutHorizontalPageBreak"), wxT("PutHorizontalPageBreak()"));
	helper.AppendFunc(wxT("GetArea"), 1, wxT("GetArea(string: label)"));
	helper.AppendProc(wxT("Clear"), wxT("Clear()"));
	helper.AppendProc(wxT("Print"), wxT("Print(bool: showPrintDlg = true)"));
	helper.AppendProc(wxT("Show"), 1, wxT("Show(string: title)"));
	helper.AppendProc(wxT("Put"), 2, wxT("Put(spreadsheetDocument: table, number: groupLevel = 0)"));
	helper.AppendProc(wxT("Join"), 2, wxT("Join(spreadsheetDocument: table, number: groupLevel = 0)"));
	helper.AppendProc(wxT("BeginRowGroup"), wxT("BeginRowGroup()"));
	helper.AppendProc(wxT("EndRowGroup"), wxT("EndRowGroup()"));
	helper.AppendProc(wxT("BeginColGroup"), wxT("BeginColGroup()"));
	helper.AppendProc(wxT("EndColGroup"), wxT("EndColGroup()"));
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
		pvarPropVal = new ibValueSpreadsheetDocumentAreaCollection(m_spreadsheetDoc);
		return true;
	case eParameters:
		pvarPropVal = new ibValueSpreadsheetDocumentParameterCollection(m_spreadsheetDoc);
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
			pvarRetValue = new ibValueSpreadsheetDocumentArea(
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

		pvarRetValue = new ibValueSpreadsheetDocument(m_spreadsheetDoc->GetArea(
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

			pvarRetValue = new ibValueSpreadsheetDocument(m_spreadsheetDoc->GetAreaByName(r->m_label, c ? c->m_label : wxT("")));
			return true;
		}

		const ibSpreadsheetAreaDescription* r = m_spreadsheetDoc->GetSpreadsheetDesc().GetRowAreaByName(paParams[0]->GetString());
		const ibSpreadsheetAreaDescription* c = m_spreadsheetDoc->GetSpreadsheetDesc().GetRowAreaByName(lSizeArray > 1 ? paParams[1]->GetString() : wxT(""));

		if (!r)
			return false;

		pvarRetValue = new ibValueSpreadsheetDocument(m_spreadsheetDoc->GetAreaByName(
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
		if (auto* frame = ibSession::CurrentFrame())
			return frame->PrintSpreadsheetDocument(m_spreadsheetDoc, lSizeArray > 0 ? paParams[0]->GetBoolean() : false);
		ibBackendCoreException::Error(_("Context functions are not available!"));
		return false;
	}
	else if (lMethodNum == eShow) {
		if (auto* frame = ibSession::CurrentFrame())
			return frame->ShowSpreadsheetDocument(paParams[0]->GetString(), m_spreadsheetDoc);
		ibBackendCoreException::Error(_("Context functions are not available!"));
		return false;
	}
	else if (lMethodNum == ePut) {
		ibValuePtr<ibValueSpreadsheetDocument> valueSpreadsheet(
			paParams[0]->ConvertToType<ibValueSpreadsheetDocument>());
		const unsigned int groupLevel = (lSizeArray > 1)
			? (unsigned int)wxMax(0, paParams[1]->GetInteger()) : 0u;
		if (valueSpreadsheet)
			m_spreadsheetDoc->PutArea(valueSpreadsheet->GetSpreadsheetDocument(), groupLevel);
		return true;
	}
	else if (lMethodNum == eJoin) {
		ibValuePtr<ibValueSpreadsheetDocument> valueSpreadsheet(
			paParams[0]->ConvertToType<ibValueSpreadsheetDocument>());
		const unsigned int groupLevel = (lSizeArray > 1)
			? (unsigned int)wxMax(0, paParams[1]->GetInteger()) : 0u;
		if (valueSpreadsheet)
			m_spreadsheetDoc->JoinArea(valueSpreadsheet->GetSpreadsheetDocument(), groupLevel);
		return true;
	}
	else if (lMethodNum == eBeginRowGroup) { m_spreadsheetDoc->BeginRowGroup(); return true; }
	else if (lMethodNum == eEndRowGroup)   { m_spreadsheetDoc->EndRowGroup();   return true; }
	else if (lMethodNum == eBeginColGroup) { m_spreadsheetDoc->BeginColGroup(); return true; }
	else if (lMethodNum == eEndColGroup)   { m_spreadsheetDoc->EndColGroup();   return true; }

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


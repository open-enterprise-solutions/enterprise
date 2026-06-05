#ifndef __VALUE_SPREADSHEET_DOC_H__
#define __VALUE_SPREADSHEET_DOC_H__

#include "backend/compiler/value.h"
#include "backend/backend_spreadsheet.h"

void ibValueSpreadsheetDocument_BindNames(ibValue::ibMemberTable& helper, const ibValue* ctx);

class BACKEND_API ibValueSpreadsheetDocument :
	public ibValueStaticMembers<&ibValueSpreadsheetDocument_BindNames> {
	public:

	wxObjectDataPtr<ibBackendSpreadsheetObject> GetSpreadsheetDocument() const { return m_spreadsheetDoc; }

	ibSpreadsheetDescription& GetSpreadsheetDesc() { return m_spreadsheetDoc->GetSpreadsheetDesc(); }
	const ibSpreadsheetDescription& GetSpreadsheetDesc() const { return m_spreadsheetDoc->GetSpreadsheetDesc(); }

	ibValueSpreadsheetDocument(const ibSpreadsheetDescription& spreadsheetDesc = ibSpreadsheetDescription()) :
		ibValueStaticMembers(ibValueTypes::TYPE_VALUE), m_spreadsheetDoc(new ibBackendSpreadsheetObject(spreadsheetDesc)) {
	}

	virtual bool IsEmpty() const { return m_spreadsheetDoc->IsEmptyDocument(); }

	virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal);        //setting attribute
	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal);                   //attribute value

	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray);       //function call
	virtual bool CallAsProc(const long lMethodNum, ibValue** paParams, const long lSizeArray);       //procudre call

	// DoGetPMethods (protected) + Shared<&ibValueSpreadsheetDocument_BindNames> come from the base.

private:
	wxObjectDataPtr<ibBackendSpreadsheetObject> m_spreadsheetDoc;
};

#pragma region enumeration 
#include "backend/compiler/enumUnit.h"
class BACKEND_API ibValueEnumSpreadsheetOrient :
	public ibValueEnumeration<ibSpreadsheetOrientation> {
	public:

	ibValueEnumSpreadsheetOrient() : ibValueEnumeration() {}

	virtual void CreateEnumeration() {
		AddEnumeration(ibSpreadsheetOrientation::ibOrient_Horizontal, wxT("Horizontal"), _("Horizontal"));
		AddEnumeration(ibSpreadsheetOrientation::ibOrient_Vertical, wxT("Vertical"), _("Vertical"));
	}

private:
};

class BACKEND_API ibValueEnumSpreadsheetHorizontalAlignment :
	public ibValueEnumeration<ibSpreadsheetAlignmentHorz> {
	public:

	ibValueEnumSpreadsheetHorizontalAlignment() : ibValueEnumeration() {}

	virtual void CreateEnumeration() {
		AddEnumeration(ibSpreadsheetAlignmentHorz::ibAlignmentHorz_Left, wxT("Left"), _("Left"));
		AddEnumeration(ibSpreadsheetAlignmentHorz::ibAlignmentHorz_Center, wxT("Center"), _("Center"));
		AddEnumeration(ibSpreadsheetAlignmentHorz::ibAlignmentHorz_Right, wxT("Right"), _("Right"));
	}

private:
};

class BACKEND_API ibValueEnumSpreadsheetVerticalAlignment :
	public ibValueEnumeration<ibSpreadsheetAlignmentVert> {
	public:

	ibValueEnumSpreadsheetVerticalAlignment() : ibValueEnumeration() {}

	virtual void CreateEnumeration() {
		AddEnumeration(ibSpreadsheetAlignmentVert::ibAlignmentVert_Top, wxT("Top"), _("Top"));
		AddEnumeration(ibSpreadsheetAlignmentVert::ibAlignmentVert_Center, wxT("Center"), _("Center"));
		AddEnumeration(ibSpreadsheetAlignmentVert::ibAlignmentVert_Bottom, wxT("Bottom"), _("Bottom"));
	}

private:
};

class BACKEND_API ibValueEnumSpreadsheetFitMode :
	public ibValueEnumeration<ibSpreadsheetFitMode> {
	public:

	ibValueEnumSpreadsheetFitMode() : ibValueEnumeration() {}

	virtual void CreateEnumeration() {
		AddEnumeration(ibSpreadsheetFitMode::ibFitMode_Overflow, wxT("Overflow"), _("Overflow"));
		AddEnumeration(ibSpreadsheetFitMode::ibFitMode_Clip, wxT("Clip"), _("Clip"));
	}

private:
};

class BACKEND_API ibValueEnumSpreadsheetBorder :
	public ibValueEnumeration<ibSpreadsheetPenStyle> {
	public:

	ibValueEnumSpreadsheetBorder() : ibValueEnumeration() {}

	virtual void CreateEnumeration() {
		AddEnumeration(ibSpreadsheetPenStyle::ibPenStyle_Transparent, wxT("None"), _("None"));
		AddEnumeration(ibSpreadsheetPenStyle::ibPenStyle_Solid, wxT("Solid"), _("Solid"));
		AddEnumeration(ibSpreadsheetPenStyle::ibPenStyle_Dot, wxT("Dotted"), _("Dotted"));
		AddEnumeration(ibSpreadsheetPenStyle::ibPenStyle_ShortDash, wxT("ThinDashed"), _("Thin dashed"));
		AddEnumeration(ibSpreadsheetPenStyle::ibPenStyle_DotDash, wxT("ThickDashed"), _("Thick dashed"));
		AddEnumeration(ibSpreadsheetPenStyle::ibPenStyle_LongDash, wxT("LargeDashed"), _("Large dashed"));
	}

private:

};

class BACKEND_API ibValueEnumSpreadsheetFillType :
	public ibValueEnumeration<ibSpreadsheetFillType> {
	public:

	ibValueEnumSpreadsheetFillType() : ibValueEnumeration() {}

	virtual void CreateEnumeration() {
		AddEnumeration(ibSpreadsheetFillType::ibSpreadsheetFillType_StrText, wxT("Text"), _("Text"));
		AddEnumeration(ibSpreadsheetFillType::ibSpreadsheetFillType_StrParameter, wxT("Parameter"), _("Parameter"));
		AddEnumeration(ibSpreadsheetFillType::ibSpreadsheetFillType_StrTemplate, wxT("Template"), _("Template"));
	}

private:

};
#pragma endregion 

class BACKEND_API ibValueSpreadsheetDocumentArea :
	public ibValueDynamicMembers {
	public:

	ibValueSpreadsheetDocumentArea() : ibValueDynamicMembers(ibValueTypes::TYPE_VALUE), m_row(-1), m_col(-1), m_spreadsheetDoc() {
		m_members.Bind(this, &ibValueSpreadsheetDocumentArea::FillMembers);
	}
	ibValueSpreadsheetDocumentArea(wxObjectDataPtr<ibBackendSpreadsheetObject>& spreadsheetDoc, int row, int col) :
		ibValueDynamicMembers(ibValueTypes::TYPE_VALUE), m_row(row), m_col(col), m_spreadsheetDoc(spreadsheetDoc) {
		m_members.Bind(this, &ibValueSpreadsheetDocumentArea::FillMembers);
	}

	virtual bool IsEmpty() const { return m_spreadsheetDoc->IsEmptyCell(m_row, m_col); }

	virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal);        //setting attribute
	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal);                   //attribute value

	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray);       //function call
	virtual bool CallAsProc(const long lMethodNum, ibValue** paParams, const long lSizeArray);       //procudre call

	void FillMembers(ibMemberTable& helper) const;   // bound in ctor (was PrepareNames)

private:

	int m_row, m_col;
	wxObjectDataPtr<ibBackendSpreadsheetObject> m_spreadsheetDoc;

};

class BACKEND_API ibValueSpreadsheetDocumentBorder :
	public ibValueDynamicMembers {
	public:

	enum
	{
		enPropStyle,
		enPropColour,
		enPropWidth
	};

public:

	wxPenStyle GetStyle() const { return m_style; }
	wxColour GetColour() const { return m_colour; }
	int GetWidth() const { return m_width; }

	ibValueSpreadsheetDocumentBorder(wxPenStyle style = wxPENSTYLE_TRANSPARENT, const wxColour& colour = *wxBLACK, int width = 1) :
		ibValueDynamicMembers(ibValueTypes::TYPE_VALUE), m_style(style), m_colour(colour), m_width(width) {
		m_members.Bind(this, &ibValueSpreadsheetDocumentBorder::FillMembers);
	}

	virtual bool IsEmpty() const { return false; }

	virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal);        //setting attribute
	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal);                   //attribute value

	void FillMembers(ibMemberTable& helper) const;   // bound in ctor (was PrepareNames)

private:
	wxPenStyle m_style = wxPENSTYLE_TRANSPARENT;
	wxColour m_colour = *wxBLACK;
	int m_width = 1;

};

#endif 
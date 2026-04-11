#include "valueSpreadsheet.h"

wxIMPLEMENT_DYNAMIC_CLASS(ibValueSpreadsheetDocumentArea, ibValue);
wxIMPLEMENT_DYNAMIC_CLASS(ibValueSpreadsheetDocumentBorder, ibValue);

ibValue::ibValueMethodHelper ibValueSpreadsheetDocumentArea::m_methodHelper;
ibValue::ibValueMethodHelper ibValueSpreadsheetDocumentBorder::m_methodHelper;

enum
{
	eBackgroundColour,
	eTextColour,
	eTextOrient,
	eFont,

	eAlignmentHorz,
	eAlignmentVert,

	eBorderLeft,
	eBorderRight,
	eBorderTop,
	eBorderBottom,

	eSize,
	eReadOnly,

	eValue
};

void ibValueSpreadsheetDocumentArea::PrepareNames() const
{
	m_methodHelper.ClearHelper();
	m_methodHelper.AppendProp(wxT("BackgroundColour"));
	m_methodHelper.AppendProp(wxT("TextColour"));
	m_methodHelper.AppendProp(wxT("TextOrient"));
	m_methodHelper.AppendProp(wxT("Font"));
	m_methodHelper.AppendProp(wxT("AlignHorizontal"));
	m_methodHelper.AppendProp(wxT("AlignVertical"));

	m_methodHelper.AppendProp(wxT("BorderLeft"));
	m_methodHelper.AppendProp(wxT("BorderRight"));
	m_methodHelper.AppendProp(wxT("BorderTop"));
	m_methodHelper.AppendProp(wxT("BorderBottom"));

	m_methodHelper.AppendProp(wxT("Size"));
	m_methodHelper.AppendProp(wxT("ReadOnly"));

	m_methodHelper.AppendProp(wxT("Value"));
}

#include "valueFont.h"
#include "valueColour.h"
#include "valueSize.h"

bool ibValueSpreadsheetDocumentArea::SetPropVal(const long lPropNum, const ibValue& varPropVal)
{
	switch (lPropNum)
	{
	case eBackgroundColour:
	{
		m_spreadsheetDoc->SetCellBackgroundColour(m_row, m_col, *varPropVal.ConvertToType<ibValueColour>());
		return true;
	}
	case eTextColour:
	{
		m_spreadsheetDoc->SetCellTextColour(m_row, m_col, *varPropVal.ConvertToType<ibValueColour>());
		return true;
	}
	case eTextOrient:
	{
		m_spreadsheetDoc->SetCellTextOrient(m_row, m_col, varPropVal.ConvertToEnumType<wxOrientation>());
		return true;
	}
	case eFont:
	{
		m_spreadsheetDoc->SetCellFont(m_row, m_col, *varPropVal.ConvertToType<ibValueFont>());
		return true;
	}
	case eAlignmentHorz:
	{
		int vertical;
		m_spreadsheetDoc->GetCellAlignment(m_row, m_col, nullptr, &vertical);
		m_spreadsheetDoc->SetCellAlignment(m_row, m_col,
			varPropVal.ConvertToEnumType<ibSpreadsheetAlignmentHorz>(), vertical);
		return true;
	}
	case eAlignmentVert:
	{
		int horizontal;
		m_spreadsheetDoc->GetCellAlignment(m_row, m_col, &horizontal, nullptr);
		m_spreadsheetDoc->SetCellAlignment(m_row, m_col,
			horizontal, varPropVal.ConvertToEnumType<ibSpreadsheetAlignmentVert>());
		return true;
	}
	case eBorderLeft:
	{
		ibValuePtr<ibValueSpreadsheetDocumentBorder> valueBorder(varPropVal.ConvertToType<ibValueSpreadsheetDocumentBorder>());
		m_spreadsheetDoc->SetCellBorderLeft(m_row, m_col, { valueBorder->GetStyle(), valueBorder->GetColour(), valueBorder->GetWidth() });
		return true;
	}
	case eBorderRight:
	{
		ibValuePtr<ibValueSpreadsheetDocumentBorder> valueBorder(varPropVal.ConvertToType<ibValueSpreadsheetDocumentBorder>());
		m_spreadsheetDoc->SetCellBorderRight(m_row, m_col, { valueBorder->GetStyle(), valueBorder->GetColour(), valueBorder->GetWidth() });
		return true;
	}
	case eBorderTop:
	{
		ibValuePtr<ibValueSpreadsheetDocumentBorder> valueBorder(varPropVal.ConvertToType<ibValueSpreadsheetDocumentBorder>());
		m_spreadsheetDoc->SetCellBorderTop(m_row, m_col, { valueBorder->GetStyle(), valueBorder->GetColour(), valueBorder->GetWidth() });
		return true;
	}
	case eBorderBottom:
	{
		ibValuePtr<ibValueSpreadsheetDocumentBorder> valueBorder(varPropVal.ConvertToType<ibValueSpreadsheetDocumentBorder>());
		m_spreadsheetDoc->SetCellBorderBottom(m_row, m_col, { valueBorder->GetStyle(), valueBorder->GetColour(), valueBorder->GetWidth() });
		return true;
	}
	case eSize:
	{
		ibValuePtr<ibValueSize> valueSize(varPropVal.ConvertToType<ibValueSize>());
		m_spreadsheetDoc->SetCellSize(m_row, m_col, valueSize->m_size.x, valueSize->m_size.y);
		return true;
	}
	case eReadOnly:
	{
		m_spreadsheetDoc->SetCellReadOnly(m_row, m_col, varPropVal.GetBoolean());
		return true;
	}
	case eValue:
	{
		m_spreadsheetDoc->SetCellValue(m_row, m_col, varPropVal.GetString());
		return true;
	}
	}

	return false;
}

bool ibValueSpreadsheetDocumentArea::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	switch (lPropNum)
	{
	case eBackgroundColour:
	{
		pvarPropVal = ibValue::CreateAndPrepareValueRef<ibValueColour>(m_spreadsheetDoc->GetCellBackgroundColour(m_row, m_col));
		return true;
	}
	case eTextColour:
	{
		pvarPropVal = ibValue::CreateAndPrepareValueRef<ibValueColour>(m_spreadsheetDoc->GetCellTextColour(m_row, m_col));
		return true;
	}
	case eTextOrient:
	{
		pvarPropVal = ibValue::CreateAndConvertEnumObjectRef<ibValueEnumSpreadsheetOrient>(
			static_cast<ibSpreadsheetOrientation>(m_spreadsheetDoc->GetCellTextOrient(m_row, m_col)));
		return true;
	}
	case eFont:
	{
		pvarPropVal = ibValue::CreateAndPrepareValueRef<ibValueFont>(m_spreadsheetDoc->GetCellFont(m_row, m_col));
		return true;
	}
	case eAlignmentHorz:
	{
		int horizontal;
		m_spreadsheetDoc->GetCellAlignment(m_row, m_col, &horizontal, nullptr);
		pvarPropVal = ibValue::CreateAndConvertEnumObjectRef<ibValueEnumSpreadsheetHorizontalAlignment>(
			static_cast<ibSpreadsheetAlignmentHorz>(horizontal));
		return true;
	}
	case eAlignmentVert:
	{
		int vertical;
		m_spreadsheetDoc->GetCellAlignment(m_row, m_col, nullptr, &vertical);
		pvarPropVal = ibValue::CreateAndConvertEnumObjectRef<ibValueEnumSpreadsheetVerticalAlignment>(
			static_cast<ibSpreadsheetAlignmentVert>(vertical));
		return true;
	}
	case eBorderLeft:
	{
		const ibSpreadsheetBorderDescription& borderDesc = m_spreadsheetDoc->GetCellBorderLeft(m_row, m_col);
		pvarPropVal = ibValue::CreateAndPrepareValueRef<ibValueSpreadsheetDocumentBorder>(borderDesc.m_style, borderDesc.m_colour, borderDesc.m_width);
		return true;
	}
	case eBorderRight:
	{
		const ibSpreadsheetBorderDescription& borderDesc = m_spreadsheetDoc->GetCellBorderRight(m_row, m_col);
		pvarPropVal = ibValue::CreateAndPrepareValueRef<ibValueSpreadsheetDocumentBorder>(borderDesc.m_style, borderDesc.m_colour, borderDesc.m_width);
		return true;
	}
	case eBorderTop:
	{
		const ibSpreadsheetBorderDescription& borderDesc = m_spreadsheetDoc->GetCellBorderTop(m_row, m_col);
		pvarPropVal = ibValue::CreateAndPrepareValueRef<ibValueSpreadsheetDocumentBorder>(borderDesc.m_style, borderDesc.m_colour, borderDesc.m_width);
		return true;
	}
	case eBorderBottom:
	{
		const ibSpreadsheetBorderDescription& borderDesc = m_spreadsheetDoc->GetCellBorderBottom(m_row, m_col);
		pvarPropVal = ibValue::CreateAndPrepareValueRef<ibValueSpreadsheetDocumentBorder>(borderDesc.m_style, borderDesc.m_colour, borderDesc.m_width);
		return true;
	}
	case eSize:
	{
		ibValuePtr<ibValueSize> valueSize(ibValue::CreateAndPrepareValueRef<ibValueSize>());
		m_spreadsheetDoc->GetCellSize(m_row, m_col, &valueSize->m_size.x, &valueSize->m_size.y);
		pvarPropVal = valueSize;
		return true;
	}
	case eReadOnly:
	{
		pvarPropVal = m_spreadsheetDoc->IsCellReadOnly(m_row, m_col);
		return true;
	}
	case eValue:
	{
		pvarPropVal = m_spreadsheetDoc->GetCellValue(m_row, m_col);
		return true;
	}
	}

	return false;
}

bool ibValueSpreadsheetDocumentArea::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	return false;
}

bool ibValueSpreadsheetDocumentArea::CallAsProc(const long lMethodNum, ibValue** paParams, const long lSizeArray)
{
	return false;
}

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

SYSTEM_TYPE_REGISTER(ibValueSpreadsheetDocumentArea, "SpreadsheetArea", string_to_clsid("VL_SPSTA"));
VALUE_TYPE_REGISTER(ibValueSpreadsheetDocumentBorder, "SpreadsheetBorderRow", string_to_clsid("VL_SPSBO"));
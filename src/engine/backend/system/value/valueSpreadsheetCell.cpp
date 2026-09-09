#include "valueSpreadsheet.h"

#include "backend/backend_localization.h"   // the cell's Value resolves the localisation envelope


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

void ibValueSpreadsheetDocumentArea::FillMembers(ibMemberTable& helper) const
{
	helper.AppendProp(wxT("BackgroundColour"));
	helper.AppendProp(wxT("TextColour"));
	helper.AppendProp(wxT("TextOrient"));
	helper.AppendProp(wxT("Font"));
	helper.AppendProp(wxT("AlignHorizontal"));
	helper.AppendProp(wxT("AlignVertical"));

	helper.AppendProp(wxT("BorderLeft"));
	helper.AppendProp(wxT("BorderRight"));
	helper.AppendProp(wxT("BorderTop"));
	helper.AppendProp(wxT("BorderBottom"));

	helper.AppendProp(wxT("Size"));
	helper.AppendProp(wxT("ReadOnly"));

	helper.AppendProp(wxT("Value"));
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
		pvarPropVal = new ibValueColour(m_spreadsheetDoc->GetCellBackgroundColour(m_row, m_col));
		return true;
	}
	case eTextColour:
	{
		pvarPropVal = new ibValueColour(m_spreadsheetDoc->GetCellTextColour(m_row, m_col));
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
		pvarPropVal = new ibValueFont(m_spreadsheetDoc->GetCellFont(m_row, m_col));
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
		pvarPropVal = new ibValueSpreadsheetDocumentBorder(borderDesc.m_style, borderDesc.m_colour, borderDesc.m_width);
		return true;
	}
	case eBorderRight:
	{
		const ibSpreadsheetBorderDescription& borderDesc = m_spreadsheetDoc->GetCellBorderRight(m_row, m_col);
		pvarPropVal = new ibValueSpreadsheetDocumentBorder(borderDesc.m_style, borderDesc.m_colour, borderDesc.m_width);
		return true;
	}
	case eBorderTop:
	{
		const ibSpreadsheetBorderDescription& borderDesc = m_spreadsheetDoc->GetCellBorderTop(m_row, m_col);
		pvarPropVal = new ibValueSpreadsheetDocumentBorder(borderDesc.m_style, borderDesc.m_colour, borderDesc.m_width);
		return true;
	}
	case eBorderBottom:
	{
		const ibSpreadsheetBorderDescription& borderDesc = m_spreadsheetDoc->GetCellBorderBottom(m_row, m_col);
		pvarPropVal = new ibValueSpreadsheetDocumentBorder(borderDesc.m_style, borderDesc.m_colour, borderDesc.m_width);
		return true;
	}
	case eSize:
	{
		ibValuePtr<ibValueSize> valueSize(new ibValueSize());
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
		// ⭐ WHAT THE CELL SAYS, NOT HOW IT IS STORED. A cell filled by parameter substitution
		// keeps its text in the localisation envelope — ComputeStringValueFromParameters ends
		// on CreateLocalizationRawLocText for both the parameter and the template fill — while
		// a caption typed into the template is stored as it stands. The renderer resolves the
		// envelope on its way to the paper, so the printout is right; a SCRIPT asking a cell
		// what it holds got `en = 'Автомобиль';` from one cell and plain text from the caption
		// beside it (2026-09-09, reading a built printout back cell by cell).
		//
		// The unwrap ANSWERS FALSE AND CLEARS on a string that is not an envelope, so the raw
		// value is the fallback rather than the empty string that would otherwise be handed
		// back for every caption on the sheet.
		const wxString strRaw = m_spreadsheetDoc->GetCellValue(m_row, m_col);
		wxString strText;
		if (!ibBackendLocalization::GetTranslateGetRawLocText(
				m_spreadsheetDoc->GetLangCode(), strRaw, strText))
			strText = strRaw;
		pvarPropVal = strText;
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

SYSTEM_TYPE_REGISTER(ibValueSpreadsheetDocumentArea, "SpreadsheetArea", system_to_clsid("VL_SPSTA"));
VALUE_TYPE_REGISTER(ibValueSpreadsheetDocumentBorder, "SpreadsheetBorderRow", value_to_clsid("VL_SPSBO"));

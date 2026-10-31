#ifndef _FRMCLIENT_BACKEND_VALUE_SPREADSHEET_H__
#define _FRMCLIENT_BACKEND_VALUE_SPREADSHEET_H__

#include "frmclient/backend/compiler/value.h"
#include "core/spreadsheet/spreadsheetObject.h"
#include "frmclient/backend/backend_localization.h"   // …and the reading of a sheet's texts, as the engine's brought it
#include "core/memory/memoryStream.h"          // …and the sheet's byte form (the clipboard's)

// THE SHEET'S ENUMERATIONS — the engine's (backend/system/value/valueSpreadsheet.h), as the editor's properties offer
// their members. The document as a script's value is the server's.

#pragma region enumeration
#include "frmclient/backend/compiler/enumUnit.h"
class FRMCLIENT_API ibValueEnumSpreadsheetOrient :
	public ibValueEnumeration<ibSpreadsheetOrientation> {
	public:

	ibValueEnumSpreadsheetOrient() : ibValueEnumeration() {}

	virtual void CreateEnumeration() {
		AddEnumeration(ibSpreadsheetOrientation::ibOrient_Horizontal, ibT("Horizontal"), _("Horizontal"));
		AddEnumeration(ibSpreadsheetOrientation::ibOrient_Vertical, ibT("Vertical"), _("Vertical"));
	}

private:
};

class FRMCLIENT_API ibValueEnumSpreadsheetHorizontalAlignment :
	public ibValueEnumeration<ibSpreadsheetAlignmentHorz> {
	public:

	ibValueEnumSpreadsheetHorizontalAlignment() : ibValueEnumeration() {}

	// The class the engine registers it by — what a member is written as (valueSpreadsheet.cpp).
	virtual ibClassID GetClassType() const override { return enum_to_clsid("EN_SHOAL"); }

	virtual void CreateEnumeration() {
		AddEnumeration(ibSpreadsheetAlignmentHorz::ibAlignmentHorz_Left, ibT("Left"), _("Left"));
		AddEnumeration(ibSpreadsheetAlignmentHorz::ibAlignmentHorz_Center, ibT("Center"), _("Center"));
		AddEnumeration(ibSpreadsheetAlignmentHorz::ibAlignmentHorz_Right, ibT("Right"), _("Right"));
	}

private:
};

class FRMCLIENT_API ibValueEnumSpreadsheetVerticalAlignment :
	public ibValueEnumeration<ibSpreadsheetAlignmentVert> {
	public:

	ibValueEnumSpreadsheetVerticalAlignment() : ibValueEnumeration() {}

	virtual void CreateEnumeration() {
		AddEnumeration(ibSpreadsheetAlignmentVert::ibAlignmentVert_Top, ibT("Top"), _("Top"));
		AddEnumeration(ibSpreadsheetAlignmentVert::ibAlignmentVert_Center, ibT("Center"), _("Center"));
		AddEnumeration(ibSpreadsheetAlignmentVert::ibAlignmentVert_Bottom, ibT("Bottom"), _("Bottom"));
	}

private:
};

class FRMCLIENT_API ibValueEnumSpreadsheetFitMode :
	public ibValueEnumeration<ibSpreadsheetFitMode> {
	public:

	ibValueEnumSpreadsheetFitMode() : ibValueEnumeration() {}

	virtual void CreateEnumeration() {
		AddEnumeration(ibSpreadsheetFitMode::ibFitMode_Overflow, ibT("Overflow"), _("Overflow"));
		AddEnumeration(ibSpreadsheetFitMode::ibFitMode_Clip, ibT("Clip"), _("Clip"));
		AddEnumeration(ibSpreadsheetFitMode::ibFitMode_Wrap, ibT("Wrap"), _("Wrap"));
	}

private:
};

class FRMCLIENT_API ibValueEnumSpreadsheetBorder :
	public ibValueEnumeration<ibSpreadsheetPenStyle> {
	public:

	ibValueEnumSpreadsheetBorder() : ibValueEnumeration() {}

	virtual void CreateEnumeration() {
		AddEnumeration(ibSpreadsheetPenStyle::ibPenStyle_Transparent, ibT("None"), _("None"));
		AddEnumeration(ibSpreadsheetPenStyle::ibPenStyle_Solid, ibT("Solid"), _("Solid"));
		AddEnumeration(ibSpreadsheetPenStyle::ibPenStyle_Dot, ibT("Dotted"), _("Dotted"));
		AddEnumeration(ibSpreadsheetPenStyle::ibPenStyle_ShortDash, ibT("ThinDashed"), _("Thin dashed"));
		AddEnumeration(ibSpreadsheetPenStyle::ibPenStyle_DotDash, ibT("ThickDashed"), _("Thick dashed"));
		AddEnumeration(ibSpreadsheetPenStyle::ibPenStyle_LongDash, ibT("LargeDashed"), _("Large dashed"));
	}

private:

};

class FRMCLIENT_API ibValueEnumSpreadsheetFillType :
	public ibValueEnumeration<ibSpreadsheetFillType> {
	public:

	ibValueEnumSpreadsheetFillType() : ibValueEnumeration() {}

	virtual void CreateEnumeration() {
		AddEnumeration(ibSpreadsheetFillType::ibSpreadsheetFillType_StrText, ibT("Text"), _("Text"));
		AddEnumeration(ibSpreadsheetFillType::ibSpreadsheetFillType_StrParameter, ibT("Parameter"), _("Parameter"));
		AddEnumeration(ibSpreadsheetFillType::ibSpreadsheetFillType_StrTemplate, ibT("Template"), _("Template"));
	}

private:

};
#pragma endregion

#endif

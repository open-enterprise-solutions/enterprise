////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : system objects 
////////////////////////////////////////////////////////////////////////////

#include "systemManagerEnum.h"


//add new enumeration
ENUM_TYPE_REGISTER(ibValueEnumStatusMessage, "StatusMessage", enum_to_clsid("EN_STMS"));
ENUM_TYPE_REGISTER(ibValueEnumQuestionMode, "QuestionMode", enum_to_clsid("EN_QSMD"));
ENUM_TYPE_REGISTER(ibValueEnumQuestionReturnCode, "QuestionReturnCode", enum_to_clsid("EN_QSRC"));
ENUM_TYPE_REGISTER(ibValueEnumRoundMode, "RoundMode", enum_to_clsid("EN_ROMO"));
ENUM_TYPE_REGISTER(ibValueEnumTextEncoding, "TextEncoding", enum_to_clsid("EN_TXEN"));
ENUM_TYPE_REGISTER(ibValueEnumHttpMethod, "HTTPMethod", enum_to_clsid("EN_HTMT"));

ENUM_TYPE_REGISTER(ibValueChars, "Chars", enum_to_clsid("EN_CHAR"));

ENUM_TYPE_REGISTER(ibValueEnumJsonValueType, "JSONValueType", enum_to_clsid("EN_JSVT"));
ENUM_TYPE_REGISTER(ibValueEnumJsonFormatting, "JSONFormatting", enum_to_clsid("EN_JSFM"));

// Form-script enumerations. The member number is its place in the name list
// (1-based) and is what a saved value stores, so the lists are not reordered.
// The script names are the 1C English names. The C++ enumerators stay in this
// file: systemEnum.h is included widely, and a new member would rebuild it.

enum ibFormFieldType { ibFormFieldType_Empty = 0 };

class ibValueEnumFormFieldType : public ibValueEnumeration<ibFormFieldType> {
public:
	virtual void CreateEnumeration() override {
		const wxChar* names[] = {
			wxT("InputField"), wxT("LabelField"), wxT("CheckBoxField"), wxT("PictureField"),
			wxT("RadioButtonField"), wxT("CalendarField"), wxT("SpreadsheetDocumentField"),
			wxT("TextDocumentField"), wxT("FormattedDocumentField"), wxT("HTMLDocumentField"),
			wxT("PeriodField"), wxT("ProgressBarField"), wxT("TrackBarField"), wxT("ChartField"),
			wxT("GanttChartField"), wxT("DendrogramField"), wxT("GraphicalSchemaField"),
			wxT("GeographicalSchemaField"), wxT("PDFDocumentField"), wxT("PlannerField")
		};
		for (int i = 0; i < (int)(sizeof(names) / sizeof(names[0])); ++i)
			AddEnumeration(static_cast<ibFormFieldType>(i + 1), names[i]);
	}
};

enum ibButtonRepresentation { ibButtonRepresentation_Empty = 0 };

class ibValueEnumButtonRepresentation : public ibValueEnumeration<ibButtonRepresentation> {
public:
	virtual void CreateEnumeration() override {
		const wxChar* names[] = {
			wxT("Auto"), wxT("Text"), wxT("Picture"), wxT("PictureAndText")
		};
		for (int i = 0; i < (int)(sizeof(names) / sizeof(names[0])); ++i)
			AddEnumeration(static_cast<ibButtonRepresentation>(i + 1), names[i]);
	}
};

enum ibColumnsGroup { ibColumnsGroup_Empty = 0 };

class ibValueEnumColumnsGroup : public ibValueEnumeration<ibColumnsGroup> {
public:
	virtual void CreateEnumeration() override {
		const wxChar* names[] = {
			wxT("Horizontal"), wxT("Vertical"), wxT("InCell")
		};
		for (int i = 0; i < (int)(sizeof(names) / sizeof(names[0])); ++i)
			AddEnumeration(static_cast<ibColumnsGroup>(i + 1), names[i]);
	}
};

enum ibStandardPeriodVariant { ibStandardPeriodVariant_Empty = 0 };

class ibValueEnumStandardPeriodVariant : public ibValueEnumeration<ibStandardPeriodVariant> {
public:
	virtual void CreateEnumeration() override {
		const wxChar* names[] = {
			wxT("Custom"), wxT("Today"), wxT("ThisWeek"), wxT("ThisTenDays"),
			wxT("ThisMonth"), wxT("ThisQuarter"), wxT("ThisHalfYear"), wxT("ThisYear"),
			wxT("FromBeginningOfThisWeek"), wxT("FromBeginningOfThisTenDays"),
			wxT("FromBeginningOfThisMonth"), wxT("FromBeginningOfThisQuarter"),
			wxT("FromBeginningOfThisHalfYear"), wxT("FromBeginningOfThisYear"),
			wxT("Yesterday"), wxT("LastWeek"), wxT("LastTenDays"), wxT("LastMonth"),
			wxT("LastQuarter"), wxT("LastHalfYear"), wxT("LastYear"),
			wxT("LastWeekTillSameWeekDay"), wxT("LastTenDaysTillSameDayNumber"),
			wxT("LastMonthTillSameDate"), wxT("LastQuarterTillSameDate"),
			wxT("LastHalfYearTillSameDate"), wxT("LastYearTillSameDate"),
			wxT("Tomorrow"), wxT("NextWeek"), wxT("NextTenDays"), wxT("NextMonth"),
			wxT("NextQuarter"), wxT("NextHalfYear"), wxT("NextYear"),
			wxT("NextWeekTillSameWeekDay"), wxT("NextTenDaysTillSameDayNumber"),
			wxT("NextMonthTillSameDate"), wxT("NextQuarterTillSameDate"),
			wxT("NextHalfYearTillSameDate"), wxT("NextYearTillSameDate"),
			wxT("TillEndOfThisWeek"), wxT("TillEndOfThisTenDays"),
			wxT("TillEndOfThisMonth"), wxT("TillEndOfThisQuarter"),
			wxT("TillEndOfThisHalfYear"), wxT("TillEndOfThisYear"),
			wxT("Last7Days"), wxT("Next7Days"), wxT("Month")
		};
		for (int i = 0; i < (int)(sizeof(names) / sizeof(names[0])); ++i)
			AddEnumeration(static_cast<ibStandardPeriodVariant>(i + 1), names[i]);
	}
};

ENUM_TYPE_REGISTER(ibValueEnumFormFieldType, "FormFieldType", enum_to_clsid("EN_FFTP"));
ENUM_TYPE_REGISTER(ibValueEnumButtonRepresentation, "ButtonRepresentation", enum_to_clsid("EN_BTRP"));
ENUM_TYPE_REGISTER(ibValueEnumColumnsGroup, "ColumnsGroup", enum_to_clsid("EN_CLGP"));
ENUM_TYPE_REGISTER(ibValueEnumStandardPeriodVariant, "StandardPeriodVariant", enum_to_clsid("EN_SPVR"));

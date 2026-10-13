#include "documentEnum.h"

#include "core/fdatetime.h"


//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

//add new enumeration
ENUM_TYPE_REGISTER(ibValueEnumDocumentWriteMode, "DocumentWriteMode", enum_to_clsid("EN_WRMO"));
ENUM_TYPE_REGISTER(ibValueEnumDocumentPostingMode, "DocumentPostingMode", enum_to_clsid("EN_POMO"));
ENUM_TYPE_REGISTER(ibValueEnumDocumentRecordsDeletion, "DocumentRecordsDeletion", enum_to_clsid("EN_RCDL"));
ENUM_TYPE_REGISTER(ibValueEnumNumberPeriodicity, "NumberPeriodicity", enum_to_clsid("EN_NPER"));

int ibDocumentNumberInterval(ibNumberPeriodicity period, const ibDateTime& when)
{
	if (period == ibNumberPeriodicity::ibNumberPeriodicity_Nonperiodical)
		return ibNumberSeriesNonperiodical;

	const int year  = static_cast<int>(when.GetPart(ibDatePart::Year));
	const int month = static_cast<int>(when.GetPart(ibDatePart::Month));
	const int day   = static_cast<int>(when.GetPart(ibDatePart::Day));
	switch (period) {
	case ibNumberPeriodicity::ibNumberPeriodicity_Year:
		return static_cast<int>(1000000000LL + year);
	case ibNumberPeriodicity::ibNumberPeriodicity_Quarter:
		return static_cast<int>(1100000000LL + year * 10LL + ((month - 1) / 3 + 1));
	case ibNumberPeriodicity::ibNumberPeriodicity_Month:
		return static_cast<int>(1200000000LL + year * 100LL + month);
	case ibNumberPeriodicity::ibNumberPeriodicity_Day:
		return static_cast<int>(1300000000LL + year * 10000LL + month * 100LL + day);
	default:
		return ibNumberSeriesNonperiodical;
	}
}

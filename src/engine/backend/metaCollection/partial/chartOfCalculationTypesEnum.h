#ifndef _CHART_OF_CALCULATION_TYPES_ENUM_H__
#define _CHART_OF_CALCULATION_TYPES_ENUM_H__

// WHICH PERIOD OF A BASE RECORD PUTS IT INTO A BASE — the chart's answer, read by GetBase
// (calculationRegisterManager_impl.cpp). The order is the one the property had while it was a bare
// number with no reader (0 / 1 / 2).
enum ibBaseDependence {
	eBaseNone,                   // the chart's types take no base at all
	eBaseByActionPeriod,         // the days a base record is actually in force, weighed against the base period
	eBaseByRegistrationPeriod,   // a base record counts whole when it is registered inside the base period
};

// …and the calculation REGISTER's one enumeration, kept with its family's: THE REGISTER'S GRAIN. What a
// registration period — and the month-for action period — names: a day, a month, a quarter or a year.
// Read by the record-set write, which registers a record at the START of the period its date falls in
// (calculationRegister.h, GetPeriodicityUnit). The numbers are what a saved configuration holds, so
// they are this enumeration's own and stable, not the calendar unit's (ibTotalsPeriod grows in the
// middle).
enum ibCalcPeriodicity {
	eCalcPeriodDay,
	eCalcPeriodMonth,
	eCalcPeriodQuarter,
	eCalcPeriodYear,
};

#pragma region enumeration
#include "backend/compiler/enumUnit.h"
class ibValueEnumBaseDependence : public ibValueEnumeration<ibBaseDependence> {
public:
	ibValueEnumBaseDependence() : ibValueEnumeration() {}

	virtual void CreateEnumeration() {
		AddEnumeration(ibBaseDependence::eBaseNone, wxT("None"), _("No base"));
		AddEnumeration(ibBaseDependence::eBaseByActionPeriod, wxT("ByActionPeriod"), _("By action period"));
		AddEnumeration(ibBaseDependence::eBaseByRegistrationPeriod, wxT("ByRegistrationPeriod"), _("By registration period"));
	}
};
class ibValueEnumCalcPeriodicity : public ibValueEnumeration<ibCalcPeriodicity> {
public:
	ibValueEnumCalcPeriodicity() : ibValueEnumeration() {}

	virtual void CreateEnumeration() {
		AddEnumeration(ibCalcPeriodicity::eCalcPeriodDay, wxT("Day"), _("Day"));
		AddEnumeration(ibCalcPeriodicity::eCalcPeriodMonth, wxT("Month"), _("Month"));
		AddEnumeration(ibCalcPeriodicity::eCalcPeriodQuarter, wxT("Quarter"), _("Quarter"));
		AddEnumeration(ibCalcPeriodicity::eCalcPeriodYear, wxT("Year"), _("Year"));
	}
};
#pragma endregion

#endif

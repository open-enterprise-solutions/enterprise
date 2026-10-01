#ifndef _INFORMATION_REGISTER_ENUM_H__
#define _INFORMATION_REGISTER_ENUM_H__

enum ibWriteRegisterMode {
	eIndependent,
	eSubordinateRecorder
};

// What a record's period is truncated to (ibValueMetaObjectInformationRegister::GetPeriodicityUnit). The numbers are
// what a saved configuration holds, so a new one goes at the end.
enum ibPeriodicity {
	eNonPeriodic,
	eWithinSecond,
	eWithinDay,
	eWithinMonth,
	eWithinQuarter,
	eWithinYear,
};

#pragma region enumeration
#include "backend/compiler/enumUnit.h"
class ibValueEnumPeriodicity : public ibValueEnumeration<ibPeriodicity> {
	public:
	ibValueEnumPeriodicity() : ibValueEnumeration() {}
	//ibValueEnumPeriodicity(ibPeriodicity periodicity) : ibValueEnumeration(periodicity) {}

	virtual void CreateEnumeration() {
		AddEnumeration(ibPeriodicity::eNonPeriodic, wxT("NonPeriodic"), _("Non periodic"));
		AddEnumeration(ibPeriodicity::eWithinSecond, wxT("WithinSecond"), _("Within second"));
		AddEnumeration(ibPeriodicity::eWithinDay, wxT("WithinDay"), _("Within day"));
		AddEnumeration(ibPeriodicity::eWithinMonth, wxT("WithinMonth"), _("Within month"));
		AddEnumeration(ibPeriodicity::eWithinQuarter, wxT("WithinQuarter"), _("Within quarter"));
		AddEnumeration(ibPeriodicity::eWithinYear, wxT("WithinYear"), _("Within year"));
	}
};
class ibValueEnumWriteRegisterMode : public ibValueEnumeration<ibWriteRegisterMode> {
	public:
	ibValueEnumWriteRegisterMode() : ibValueEnumeration() {}
	//ibValueEnumWriteRegisterMode(ibWriteRegisterMode mode) : ibValueEnumeration(mode) {}

	virtual void CreateEnumeration() {
		AddEnumeration(ibWriteRegisterMode::eIndependent, wxT("Independent"), _("Independent"));
		AddEnumeration(ibWriteRegisterMode::eSubordinateRecorder, wxT("SubordinateRecorder"), _("Subordinate recorder"));
	}
};
#pragma endregion 

#endif
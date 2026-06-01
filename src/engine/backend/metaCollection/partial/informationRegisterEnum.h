#ifndef _INFORMATION_REGISTER_ENUM_H__
#define _INFORMATION_REGISTER_ENUM_H__

enum ibWriteRegisterMode {
	eIndependent,
	eSubordinateRecorder
};

enum ibPeriodicity {
	eNonPeriodic,
	eWithinSecond,
	eWithinDay,
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
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
		AddEnumeration(ibPeriodicity::eNonPeriodic, ibT("NonPeriodic"), ibTRANSLATE("Non periodic"));
		AddEnumeration(ibPeriodicity::eWithinSecond, ibT("WithinSecond"), ibTRANSLATE("Within second"));
		AddEnumeration(ibPeriodicity::eWithinDay, ibT("WithinDay"), ibTRANSLATE("Within day"));
		AddEnumeration(ibPeriodicity::eWithinMonth, ibT("WithinMonth"), ibTRANSLATE("Within month"));
		AddEnumeration(ibPeriodicity::eWithinQuarter, ibT("WithinQuarter"), ibTRANSLATE("Within quarter"));
		AddEnumeration(ibPeriodicity::eWithinYear, ibT("WithinYear"), ibTRANSLATE("Within year"));
	}
};
class ibValueEnumWriteRegisterMode : public ibValueEnumeration<ibWriteRegisterMode> {
	public:
	ibValueEnumWriteRegisterMode() : ibValueEnumeration() {}
	//ibValueEnumWriteRegisterMode(ibWriteRegisterMode mode) : ibValueEnumeration(mode) {}

	virtual void CreateEnumeration() {
		AddEnumeration(ibWriteRegisterMode::eIndependent, ibT("Independent"), ibTRANSLATE("Independent"));
		AddEnumeration(ibWriteRegisterMode::eSubordinateRecorder, ibT("SubordinateRecorder"), ibTRANSLATE("Subordinate recorder"));
	}
};
#pragma endregion 

#endif
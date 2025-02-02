#ifndef _INFORMATION_REGISTER_ENUM_H__
#define _INFORMATION_REGISTER_ENUM_H__

enum eWriteRegisterMode {
	eIndependent,
	eSubordinateRecorder
};

enum ePeriodicity {
	eNonPeriodic,
	eWithinSecond,
	eWithinDay,
};

#endif
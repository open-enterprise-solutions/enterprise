#ifndef _CORE_BUILD_H__
#define _CORE_BUILD_H__

// THE PLATFORM'S VERSION — the last of the list below, said once: the code reads it as version_oes_last, and every
// program's and library's version resource as these three (core/version.rc2 — the resource compiler reads this file
// up to RC_INVOKED).
#define OES_VERSION_MAJOR   1
#define OES_VERSION_MINOR   0
#define OES_VERSION_RELEASE 1

#ifndef RC_INVOKED

#include "core/core.h"

//*******************************************************************************************
//*                                 Versions support									    *
//*******************************************************************************************

#define version_generate(major, minor, release) \
		( (major * 1000) + (minor * 100) + release )

enum ibProgramVersion {
	version_oes_1_0_0 = version_generate(1, 0, 0),
	version_oes_1_0_1 = version_generate(1, 0, 1),
	version_oes_last  = version_generate(OES_VERSION_MAJOR, OES_VERSION_MINOR, OES_VERSION_RELEASE)
};

// THE BUILD — its number: the days from 2018-01-01 to the day it was compiled, the VERSION, stable across a day of
// rebuilds; one for the engine and its clients alike.
CORE_API unsigned int GetBuildId();

// THE BUILD, SPELLED OUT: the number above plus when it was actually compiled —
// "3164 (Sep  2 2026 16:55:03)". The number is the VERSION and is stable across a day of rebuilds,
// which is right for saying which engine this is and wrong for telling two of them apart; this is
// for the second question (a banner that has to be exact). The bytecode cache does not use it:
// its engine half is the format version, which moves when the bytecode does.
CORE_API const char* GetBuildStamp();

#endif // RC_INVOKED

#endif

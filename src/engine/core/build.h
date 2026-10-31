#ifndef _CORE_BUILD_H__
#define _CORE_BUILD_H__

// THE PLATFORM'S VERSION, said once: the journal's first line reads these three, and every program's and library's
// version resource (core/version.rc2 — the resource compiler reads this file up to RC_INVOKED).
#define OES_VERSION_MAJOR   2
#define OES_VERSION_MINOR   0
#define OES_VERSION_RELEASE 0

#ifndef RC_INVOKED

#include "core/core.h"

//*******************************************************************************************
//*                                 Versions support									    *
//*******************************************************************************************

#define version_generate(major, minor, release) \
		( (major * 1000) + (minor * 100) + release )

// The compatibility ladder (compatibility-version.md): a rung per behaviour a configuration may still ask for. Its
// number is what a configuration saves, so it is written out here and does not follow the platform's version: a rung
// is added by a change of behaviour, not by a release. The alpha's rungs, 1.0.0 and 1.0.1, went with 2.0.0
// (2026-10-28) — a configuration of the alpha is not carried over.
enum ibProgramVersion {
	version_oes_last  = version_generate(2, 0, 0)
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

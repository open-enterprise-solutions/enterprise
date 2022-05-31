#ifndef _CORE_H__
#define _CORE_H__

#ifdef CORE_EXPORTS
#define CORE_API WXEXPORT
#else
#define CORE_API WXIMPORT
#endif

unsigned int GetBuildId();

#endif 
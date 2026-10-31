#ifndef _METADATA_OES_H__
#define _METADATA_OES_H__

#include <wx/wx.h>

#include "core/core.h"   // IB_EXPORT / IB_IMPORT, IB_FORCEINLINE / IB_NOINLINE

#ifdef BACKEND_EXPORTS
#define BACKEND_API IB_EXPORT
#else
#define BACKEND_API IB_IMPORT
#endif

#endif
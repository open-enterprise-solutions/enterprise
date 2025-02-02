#ifndef _METADATA_OES_H__
#define _METADATA_OES_H__

#include <wx/wx.h>

#ifdef BACKEND_EXPORTS
#define BACKEND_API WXEXPORT
#else
#define BACKEND_API WXIMPORT
#endif

#endif 
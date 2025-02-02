#ifndef _FRONTEND_OES_H__
#define _FRONTEND_OES_H__

#include <wx/wx.h>

#ifdef FRONTEND_EXPORTS
#define FRONTEND_API WXEXPORT
#else
#define FRONTEND_API WXIMPORT
#endif

#endif 
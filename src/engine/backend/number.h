#ifndef __NUMBER_T_H__
#define __NUMBER_T_H__

#include <wx/wx.h>

//*******************************************************************************************
#ifdef wxUSE_THREADS
#define TTMATH_MULTITHREADS
#endif

#define TTMATH_NOASM
//*******************************************************************************************

#include "backend/compiler/ttmath/ttmath.h"

//*******************************************************************************************
//*                                 Declare number type                                     *
//*******************************************************************************************

typedef ttmath::Big<TTMATH_BITS(128), TTMATH_BITS(128)> number_t;

#endif
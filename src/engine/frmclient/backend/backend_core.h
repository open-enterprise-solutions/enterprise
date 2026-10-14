#ifndef _FRMCLIENT_BACKEND_CORE_H__
#define _FRMCLIENT_BACKEND_CORE_H__

// WHAT THE ENGINE'S CORE HEADER BRINGS (backend/backend_core.h) to the code copied from the desktop: wx, the core (the
// ids, the kinds of a value, the strings, numbers and dates), the journal — the parts of it the client has.
#include <wx/wx.h>
#include <map>

#include "frmclient/frmclient.h"
#include "core/guid.h"
#include "core/core.h"
#include "core/fnumber.h"
#include "core/fdatetime.h"
#include "core/fstring.h"
#include "frmclient/win/typeconv.h"
#include "core/stringUtils.h"
#include "core/diagnostics/journal.h"

#define oes_clipboard_template	wxT("oes_clipboard_template")

//max precision
#define MAX_PRECISION_NUMBER 32
//max precision
#define MAX_LENGTH_STRING 1024

class ibValue;   // frmclient/backend/compiler/value.h

#endif

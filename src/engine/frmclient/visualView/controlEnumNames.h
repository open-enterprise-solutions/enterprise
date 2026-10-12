#ifndef _FRMCLIENT_CONTROL_ENUM_NAMES_H__
#define _FRMCLIENT_CONTROL_ENUM_NAMES_H__

#include "frmclient/frmclient.h"

#include <wx/string.h>

// Script names of the client enumerations, read inside the library. A caller
// that only links libfrmclient.so cannot construct ibValueEnumeration: the
// ibValue vtable that construction emits is not exported from the shared library.

FRMCLIENT_API unsigned int ibFormClientCellAlignCount();
FRMCLIENT_API wxString ibFormClientCellAlignName(unsigned int index);
FRMCLIENT_API unsigned int ibFormClientWindowOpeningModeCount();
FRMCLIENT_API wxString ibFormClientWindowOpeningModeName(unsigned int index);
FRMCLIENT_API unsigned int ibFormClientQuickChoiceCount();
FRMCLIENT_API wxString ibFormClientQuickChoiceName(unsigned int index);

#endif

#include "backend/system/value/composition/valueComposerSettings.h"
#include "backend/compiler/typeCtor.h"

// ===========================================================================
//  The composer's settings AT RUNTIME — see valueComposerSettings.h.
//
//  What lives here today is the VOCABULARY a settings surface is spoken in.
//  The container that unfolds a description into script-visible collections is
//  the next operation, and it belongs in this file when it comes.
// ===========================================================================

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

// ⚠ THE SAME CLASS IDS THE WORDS CARRIED IN listFilter.cpp. An id is what a saved value is read
// back by, so moving a declaration must not move its id — a new one would re-read every stored
// comparison, direction and unfold as a type nobody has.
ENUM_TYPE_REGISTER(ibValueEnumComparisonKind,    "ComparisonKind",    enum_to_clsid("EN_CMPK"));
ENUM_TYPE_REGISTER(ibValueEnumSortDirection,     "SortDirection",     enum_to_clsid("EN_SDIR"));
ENUM_TYPE_REGISTER(ibValueEnumGroupKind,         "GroupKind",         enum_to_clsid("EN_GRPK"));
ENUM_TYPE_REGISTER(ibValueEnumFilterGroupKind,   "FilterGroupKind",   enum_to_clsid("EN_FGRP"));
ENUM_TYPE_REGISTER(ibValueEnumFilterDisplayMode, "FilterDisplayMode", enum_to_clsid("EN_FDSP"));

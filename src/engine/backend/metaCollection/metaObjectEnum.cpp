#include "metaObjectEnum.h"


//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

// Moved here with the enum itself (from attribute/metaAttributeObjectEnum.cpp): the clsid
// and the wire name are unchanged, so nothing serialised is affected.
ENUM_TYPE_REGISTER(ibValueEnumSelectMode, "SelectMode", enum_to_clsid("EN_SEMO"));

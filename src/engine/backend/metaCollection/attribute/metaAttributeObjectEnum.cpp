#include "metaAttributeObjectEnum.h"


//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

//add new enumeration
ENUM_TYPE_REGISTER(ibValueEnumItemMode, "ItemMode", string_to_clsid("EN_ITMO"));
ENUM_TYPE_REGISTER(ibValueEnumSelectMode, "SelectMode", string_to_clsid("EN_SEMO"));
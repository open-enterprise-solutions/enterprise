#include "metaAttributeObjectEnum.h"


//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

//add new enumeration
ENUM_TYPE_REGISTER(ibValueEnumItemMode, "ItemMode", enum_to_clsid("EN_ITMO"));
// ibValueEnumSelectMode registers in metaCollection/metaObjectEnum.cpp — it moved with its enum.
ENUM_TYPE_REGISTER(ibValueEnumIndexingMode, "IndexingMode", enum_to_clsid("EN_INMO"));

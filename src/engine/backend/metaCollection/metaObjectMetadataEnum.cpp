#include "metaObjectMetadataEnum.h"


//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

//add new enumeration
ENUM_TYPE_REGISTER(ibValueEnumVersion, "ProgramVersion", enum_to_clsid("EN_VRSN"));
ENUM_TYPE_REGISTER(ibValueEnumSyntax, "ProgramSyntax", enum_to_clsid("EN_SYNTX"));

#include "informationRegisterEnum.h"


//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

//add new enumeration
ENUM_TYPE_REGISTER(ibValueEnumPeriodicity, "InformationPeriodicity", string_to_clsid("EN_PRST"));
ENUM_TYPE_REGISTER(ibValueEnumWriteRegisterMode, "InformationWriteRegisterMode", string_to_clsid("EN_WMOD"));
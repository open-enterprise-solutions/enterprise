#include "accumulationRegisterEnum.h"


//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

//add new enumeration
ENUM_TYPE_REGISTER(ibValueEnumAccumulationRegisterType, "AccumulationRegisterType", string_to_clsid("EN_RTTP"));
ENUM_TYPE_REGISTER(ibValueEnumAccumulationRegisterRecordType, "AccumulationRecordType", g_enumRecordTypeCLSID);
////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : value orient  
////////////////////////////////////////////////////////////////////////////

#include "valueOrient.h"

wxIMPLEMENT_DYNAMIC_CLASS(CValueEnumOrient, CValue);

//add new enumeration
ENUM_TYPE_REGISTER(CValueEnumOrient, "windowOrient", string_to_clsid("EN_WORI"));
////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : value orient  
////////////////////////////////////////////////////////////////////////////

#include "valueOrient.h"
#include "core/compiler/enumFactory.h"

wxIMPLEMENT_DYNAMIC_CLASS(CValueEnumOrient, CValue);

//add new enumeration
ENUM_REGISTER(CValueEnumOrient, "windowOrient", TEXT2CLSID("EN_WORI"));
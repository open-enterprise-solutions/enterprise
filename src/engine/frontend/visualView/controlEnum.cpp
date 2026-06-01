////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : value enum  
////////////////////////////////////////////////////////////////////////////

#include "controlEnum.h"


//add new enumeration
ENUM_TYPE_REGISTER(ibValueEnumOrient, "WindowOrient", string_to_clsid("EN_WORI"));
ENUM_TYPE_REGISTER(ibValueEnumStretch, "WindowStretch", string_to_clsid("EN_STRH"));
ENUM_TYPE_REGISTER(ibValueEnumOrientNotebookPage, "WindowOrientNotebookPage", string_to_clsid("EN_WNKP"));
ENUM_TYPE_REGISTER(ibValueEnumHorizontalAlignment, "WindowHorizontalAlignment", string_to_clsid("EN_WHGT"));
ENUM_TYPE_REGISTER(ibValueEnumVerticalAlignment, "WindowVerticalAlignment", string_to_clsid("EN_WAGT"));
ENUM_TYPE_REGISTER(ibValueEnumTitleLocation, "TitleLocation", string_to_clsid("EN_TILC"));
ENUM_TYPE_REGISTER(ibValueEnumRepresentation, "Representation", string_to_clsid("EN_RPRT"));
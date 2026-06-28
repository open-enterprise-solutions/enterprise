////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : value enum  
////////////////////////////////////////////////////////////////////////////

#include "controlEnum.h"


//add new enumeration
ENUM_TYPE_REGISTER(ibValueEnumOrient, "WindowOrient", enum_to_clsid("EN_WORI"));
ENUM_TYPE_REGISTER(ibValueEnumStretch, "WindowStretch", enum_to_clsid("EN_STRH"));
ENUM_TYPE_REGISTER(ibValueEnumOrientNotebookPage, "WindowOrientNotebookPage", enum_to_clsid("EN_WNKP"));
ENUM_TYPE_REGISTER(ibValueEnumHorizontalAlignment, "WindowHorizontalAlignment", enum_to_clsid("EN_WHGT"));
ENUM_TYPE_REGISTER(ibValueEnumVerticalAlignment, "WindowVerticalAlignment", enum_to_clsid("EN_WAGT"));
ENUM_TYPE_REGISTER(ibValueEnumTitleLocation, "TitleLocation", enum_to_clsid("EN_TILC"));
ENUM_TYPE_REGISTER(ibValueEnumRepresentation, "Representation", enum_to_clsid("EN_RPRT"));

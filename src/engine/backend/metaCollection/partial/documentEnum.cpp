#include "documentEnum.h"

wxIMPLEMENT_DYNAMIC_CLASS(CValueEnumDocumentWriteMode, CValue);
wxIMPLEMENT_DYNAMIC_CLASS(CValueEnumDocumentPostingMode, CValue);

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

//add new enumeration
ENUM_TYPE_REGISTER(CValueEnumDocumentWriteMode, "documentWriteMode", string_to_clsid("EN_WRMO"));
ENUM_TYPE_REGISTER(CValueEnumDocumentPostingMode, "documentPostingMode", string_to_clsid("EN_POMO"));
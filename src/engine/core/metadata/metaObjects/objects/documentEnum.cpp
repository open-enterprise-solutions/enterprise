#include "documentEnum.h"

wxIMPLEMENT_DYNAMIC_CLASS(CValueEnumDocumentWriteMode, CValue);
wxIMPLEMENT_DYNAMIC_CLASS(CValueEnumDocumentPostingMode, CValue);

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

//add new enumeration
ENUM_REGISTER(CValueEnumDocumentWriteMode, "documentWriteMode", TEXT2CLSID("EN_WRMO"));
ENUM_REGISTER(CValueEnumDocumentPostingMode, "documentPostingMode", TEXT2CLSID("EN_POMO"));
#include "documentEnum.h"


//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

//add new enumeration
ENUM_TYPE_REGISTER(ibValueEnumDocumentWriteMode, "DocumentWriteMode", enum_to_clsid("EN_WRMO"));
ENUM_TYPE_REGISTER(ibValueEnumDocumentPostingMode, "DocumentPostingMode", enum_to_clsid("EN_POMO"));

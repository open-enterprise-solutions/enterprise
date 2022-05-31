#include "metaResourceObject.h"

wxIMPLEMENT_DYNAMIC_CLASS(CMetaResourceObject, CMetaAttributeObject)

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

METADATA_REGISTER(CMetaResourceObject, "resource", g_metaResourceCLSID);
#include "metaResourceObject.h"

wxIMPLEMENT_DYNAMIC_CLASS(CMetaObjectResource, CMetaObjectAttribute)

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

METADATA_TYPE_REGISTER(CMetaObjectResource, "resource", g_metaResourceCLSID);
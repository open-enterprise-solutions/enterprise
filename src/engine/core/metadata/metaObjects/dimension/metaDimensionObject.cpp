#include "metaDimensionObject.h"

wxIMPLEMENT_DYNAMIC_CLASS(CMetaDimensionObject, CMetaAttributeObject)

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

METADATA_REGISTER(CMetaDimensionObject, "dimension", g_metaDimensionCLSID);
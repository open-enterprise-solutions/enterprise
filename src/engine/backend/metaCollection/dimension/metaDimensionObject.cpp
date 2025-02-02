#include "metaDimensionObject.h"

wxIMPLEMENT_DYNAMIC_CLASS(CMetaObjectDimension, CMetaObjectAttribute)

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

METADATA_TYPE_REGISTER(CMetaObjectDimension, "dimension", g_metaDimensionCLSID);
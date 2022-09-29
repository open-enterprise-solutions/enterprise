#include "metaInterfaceObject.h"

//***********************************************************************
//*                            IntrfaceObject                           *
//***********************************************************************

wxIMPLEMENT_DYNAMIC_CLASS(CMetaInterfaceObject, IMetaObject);

//***********************************************************************
//*                           Metamodule                                *
//***********************************************************************

CMetaInterfaceObject::CMetaInterfaceObject(const wxString& name, const wxString& synonym, const wxString& comment) :
	IMetaObject(name, synonym, comment)
{
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

METADATA_REGISTER(CMetaInterfaceObject, "interface", g_metaInterfaceCLSID);
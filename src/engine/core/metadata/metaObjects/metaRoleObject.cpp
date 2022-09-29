#include "metaRoleObject.h"

//***********************************************************************
//*                            RoleObject                               *
//***********************************************************************

wxIMPLEMENT_DYNAMIC_CLASS(CMetaRoleObject, IMetaObject);

//***********************************************************************
//*                           Metamodule                                *
//***********************************************************************

CMetaRoleObject::CMetaRoleObject(const wxString& name, const wxString& synonym, const wxString& comment) :
	IMetaObject(name, synonym, comment)
{
}

//***********************************************************************
//*                             event object                            *
//***********************************************************************

bool CMetaRoleObject::OnCreateMetaObject(IMetadata* metaData)
{
	return IMetaObject::OnCreateMetaObject(metaData);
}

bool CMetaRoleObject::OnBeforeRunMetaObject(int flags)
{
	return IMetaObject::OnBeforeRunMetaObject(flags);
}

bool CMetaRoleObject::OnAfterCloseMetaObject()
{
	return IMetaObject::OnAfterCloseMetaObject();
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

METADATA_REGISTER(CMetaRoleObject, "role", g_metaRoleCLSID);
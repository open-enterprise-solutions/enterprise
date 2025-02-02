#include "metaRoleObject.h"

//***********************************************************************
//*                            RoleObject                               *
//***********************************************************************

wxIMPLEMENT_DYNAMIC_CLASS(CMetaObjectRole, IMetaObject);

//***********************************************************************
//*                           Metamodule                                *
//***********************************************************************

CMetaObjectRole::CMetaObjectRole(const wxString& name, const wxString& synonym, const wxString& comment) :
	IMetaObject(name, synonym, comment)
{
}

//***********************************************************************
//*                             event object                            *
//***********************************************************************

bool CMetaObjectRole::OnCreateMetaObject(IMetaData* metaData)
{
	return IMetaObject::OnCreateMetaObject(metaData);
}

bool CMetaObjectRole::OnBeforeRunMetaObject(int flags)
{
	return IMetaObject::OnBeforeRunMetaObject(flags);
}

bool CMetaObjectRole::OnAfterCloseMetaObject()
{
	return IMetaObject::OnAfterCloseMetaObject();
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

METADATA_TYPE_REGISTER(CMetaObjectRole, "role", g_metaRoleCLSID);
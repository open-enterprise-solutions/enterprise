#include "metaRoleObject.h"

//***********************************************************************
//*                            RoleObject                               *
//***********************************************************************


//***********************************************************************
//*                           Metamodule                                *
//***********************************************************************

ibValueMetaObjectRole::ibValueMetaObjectRole(const wxString& name, const wxString& synonym, const wxString& comment) :
	ibValueMetaObject(name, synonym, comment)
{
}

//***********************************************************************
//*                             event object                            *
//***********************************************************************

bool ibValueMetaObjectRole::OnCreateMetaObject(ibMetaData* metaData, int flags)
{
	return ibValueMetaObject::OnCreateMetaObject(metaData, flags);
}

bool ibValueMetaObjectRole::OnBeforeRunMetaObject(int flags)
{
	return ibValueMetaObject::OnBeforeRunMetaObject(flags);
}

bool ibValueMetaObjectRole::OnAfterCloseMetaObject()
{
	return ibValueMetaObject::OnAfterCloseMetaObject();
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

METADATA_TYPE_REGISTER(ibValueMetaObjectRole, "Role", g_metaRoleCLSID);
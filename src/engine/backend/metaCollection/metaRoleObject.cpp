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
	// RLS contract: OnAccessRead / OnAccessWrite are FUNCTIONS that return the RESTRICTED source —
	// From(Source).Join(ACL).Where(…) — to narrow access, or nothing to leave this role alone. RLS is
	// a RESTRICTION, not a boolean gate (no True / False; a hard deny is a restriction that admits no
	// rows). Source (FIRST arg) is the source as a QUERYABLE — the module branches on its type and
	// chains off it; Operation is a per-type string ("Read" / "Post" / "Delete" / …).
	(*m_propertyRoleModule)->SetDefaultFunction(wxT("OnAccessRead"),  { wxT("Source"), wxT("Operation") });
	(*m_propertyRoleModule)->SetDefaultFunction(wxT("OnAccessWrite"), { wxT("Source"), wxT("Operation") });
}

//***********************************************************************
//*                             event object                            *
//***********************************************************************

bool ibValueMetaObjectRole::OnCreateMetaObject(ibMetaData* metaData, int flags)
{
	if (!ibValueMetaObject::OnCreateMetaObject(metaData, flags))
		return false;

	return (*m_propertyRoleModule)->OnCreateMetaObject(metaData, flags);
}

bool ibValueMetaObjectRole::OnLoadMetaObject(ibMetaData* metaData)
{
	if (!(*m_propertyRoleModule)->OnLoadMetaObject(metaData))
		return false;

	return ibValueMetaObject::OnLoadMetaObject(metaData);
}

bool ibValueMetaObjectRole::OnSaveMetaObject(int flags)
{
	if (!(*m_propertyRoleModule)->OnSaveMetaObject(flags))
		return false;

	return ibValueMetaObject::OnSaveMetaObject(flags);
}

bool ibValueMetaObjectRole::OnDeleteMetaObject()
{
	if (!(*m_propertyRoleModule)->OnDeleteMetaObject())
		return false;

	return ibValueMetaObject::OnDeleteMetaObject();
}

bool ibValueMetaObjectRole::OnBeforeRunMetaObject(int flags)
{
	// The role module registers itself with the module manager here (as a
	// manager module) — that is what makes it reachable per session for the
	// RLS enforcement point at run time.
	if (!(*m_propertyRoleModule)->OnBeforeRunMetaObject(flags))
		return false;

	return ibValueMetaObject::OnBeforeRunMetaObject(flags);
}

bool ibValueMetaObjectRole::OnBeforeCloseMetaObject()
{
	if (!(*m_propertyRoleModule)->OnBeforeCloseMetaObject())
		return false;

	return ibValueMetaObject::OnBeforeCloseMetaObject();
}

bool ibValueMetaObjectRole::OnAfterCloseMetaObject()
{
	if (!(*m_propertyRoleModule)->OnAfterCloseMetaObject())
		return false;

	return ibValueMetaObject::OnAfterCloseMetaObject();
}

//***********************************************************************
//*                        Save & load metaData                         *
//***********************************************************************

bool ibValueMetaObjectRole::WriteData(ibDataNode& node) const
{
	if (!ibValueMetaObject::WriteData(node))
		return false;

	node.SetProperty(m_propertyRoleModule->GetName(), m_propertyRoleModule->GetNodeValue());
	return true;
}

bool ibValueMetaObjectRole::ReadData(const ibDataNode& node)
{
	if (!ibValueMetaObject::ReadData(node))
		return false;

	m_propertyRoleModule->ReadNodeValue(node.GetProperty(m_propertyRoleModule->GetName()));
	return true;
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

METADATA_TYPE_REGISTER(ibValueMetaObjectRole, "Role", g_metaRoleCLSID);
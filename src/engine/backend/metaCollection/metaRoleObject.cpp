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
	// RLS contract: OnAccessRead / OnAccessWrite are PROCEDURES (the OES idiom — a verdict is returned via
	// a by-ref out-param, like BeforeOpen's `Cancel`, not via a Function return). They FOLD a restriction
	// into `Source` — Source.Where(…) / Source.Join(…) or the `restrict` keyword, a SIDE EFFECT on the
	// source — and then set the by-ref `Allowed` param to True to grant access under that restriction.
	// FAIL-CLOSED at the door: if the handler does NOT set Allowed = True (it fell through, swallowed an
	// exception, or set False), the query is DENIED — a mistake over-restricts (visible), never exposes. A
	// role with NO handler leaves this source alone (allow). Source (FIRST) is the source as a QUERYABLE;
	// Operation is a per-type string ("Read" / "Post" / "Delete" / …); Allowed (THIRD, by-ref) = the verdict.
	(*m_propertyRoleModule)->SetDefaultProcedure(wxT("OnAccessRead"),  ibContentHelper::eProcedureHelper, { wxT("Source"), wxT("Operation"), wxT("Allowed") });
	(*m_propertyRoleModule)->SetDefaultProcedure(wxT("OnAccessWrite"), ibContentHelper::eProcedureHelper, { wxT("Source"), wxT("Operation"), wxT("Allowed") });
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
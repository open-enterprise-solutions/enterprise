#ifndef __META_ROLE_H__
#define __META_ROLE_H__

#include "metaModuleObject.h"   // ibPropertyInnerModule + ibValueMetaObjectManagerModule — the role's policy module
#include "backend/propertyManager/property/propertyEnum.h"

// The MODE itself lives in the role layer (`backend/roleHelper.h`, ibRoleCompositionMode) — it is
// read by the runtime role list and by the rights fold, both of which sit below the metadata. What
// belongs here is only the metaobject's side of it: the enumeration value class (so the mode is a
// script value and an inspector dropdown) and the property that stores it.
#pragma region enumeration
#include "backend/compiler/enumUnit.h"

class ibValueEnumRoleCompositionMode : public ibValueEnumeration<ibRoleCompositionMode> {
public:
	ibValueEnumRoleCompositionMode() : ibValueEnumeration() {}

	virtual void CreateEnumeration() {
		AddEnumeration(ibRoleCompositionMode_Union, wxT("Union"), _("Permitting"));
		AddEnumeration(ibRoleCompositionMode_Intersection, wxT("Intersection"), _("Restricting"));
	}
};
#pragma endregion

class BACKEND_API ibValueMetaObjectRole : public ibValueMetaObject {
	public:
protected:
public:

	ibValueMetaObjectRole(const wxString& name = wxEmptyString, const wxString& synonym = wxEmptyString, const wxString& comment = wxEmptyString);

	//support icons
	virtual wxIcon GetIcon() const;
	static wxIcon GetIconGroup();

	// The role's access-policy module — session-global (manager-module
	// shaped). It is where the OnAccessRead / OnAccessWrite handlers live
	// once the RLS enforcement point is wired; it attaches to the module
	// manager at run time exactly like a manager module. An untouched
	// module adds no restriction (RLS stays off for that role).
	const ibValueMetaObjectManagerModule* GetRoleModule() const { return m_propertyRoleModule->GetMetaObject(); }

	// Union (default) or Intersection — the base answers Union for everything that is not a role, so
	// a caller collecting the user's roles asks this without knowing the Role metatype. Read by the
	// rights fold (ibAccessObject::AccessRight) and by the session's RLS policy.
	virtual ibRoleCompositionMode GetRoleCompositionMode() const override { return m_propertyComposition->GetValueAsEnum(); }

	//events:
	virtual bool OnCreateMetaObject(ibMetaData* metaData, int flags);
	virtual bool OnLoadMetaObject(ibMetaData* metaData);
	virtual bool OnSaveMetaObject(int flags);
	virtual bool OnDeleteMetaObject();

	//module manager is started or exit
	virtual bool OnBeforeRunMetaObject(int flags);
	virtual bool OnBeforeCloseMetaObject();
	virtual bool OnAfterCloseMetaObject();

	//prepare menu for item
	virtual bool CollectContextMenu(std::vector<ibMetaMenuItem>& items);

protected:

	virtual bool ReadData(const ibDataNode& node) override;
	virtual bool WriteData(ibDataNode& node) const override;

private:

	// Same inner-module property DataProcessor uses for its ManagerModule: a
	// nested manager-module metaobject that carries the code editor, serialises
	// itself, and auto-registers with the module manager on run.
	ibPropertyInnerModule<ibValueMetaObjectManagerModule>* m_propertyRoleModule =
		ibPropertyObject::CreateProperty<ibPropertyInnerModule<ibValueMetaObjectManagerModule>>(
			m_categoryContext, wxT("RoleModule"), _("Role module"));

	// Declared BEFORE the property that uses it — members are constructed in declaration order,
	// whatever the initialiser list says (docs/portability.md §1.8).
	ibPropertyCategory* m_categoryAccess = ibPropertyObject::CreatePropertyCategory(wxT("Access"), _("Access"));
	ibPropertyEnum<ibValueEnumRoleCompositionMode>* m_propertyComposition =
		ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumRoleCompositionMode>>(
			m_categoryAccess, wxT("Composition"), _("Composition"), ibRoleCompositionMode_Union);
};

#endif 
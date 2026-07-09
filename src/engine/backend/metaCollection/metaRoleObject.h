#ifndef __META_ROLE_H__
#define __META_ROLE_H__

#include "metaModuleObject.h"   // ibPropertyInnerModule + ibValueMetaObjectManagerModule — the role's policy module

class BACKEND_API ibValueMetaObjectRole : public ibValueMetaObject {
	public:
protected:
	enum
	{
		ID_METATREE_OPEN_ROLE = 19000,
		ID_METATREE_OPEN_MODULE,
	};
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
	virtual bool PrepareContextMenu(wxMenu* defaultMenu);
	virtual void ProcessCommand(unsigned int id);

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
};

#endif 
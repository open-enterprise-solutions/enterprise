#ifndef __META_ROLE_H__
#define __META_ROLE_H__

#include "metaObject.h"

class CMetaRoleObject : public IMetaObject {
	wxDECLARE_DYNAMIC_CLASS(CMetaModuleObject);
protected:
	enum
	{
		ID_METATREE_OPEN_ROLE = 19000,
	};
public:

	CMetaRoleObject(const wxString& name = wxEmptyString, const wxString& synonym = wxEmptyString, const wxString& comment = wxEmptyString);

	virtual wxString GetClassName() const override {
		return wxT("role");
	}

	//support icons
	virtual wxIcon GetIcon();
	static wxIcon GetIconGroup();

	//events:
	virtual bool OnCreateMetaObject(IMetadata* metaData);

	//module manager is started or exit 
	virtual bool OnBeforeRunMetaObject(int flags);
	virtual bool OnAfterCloseMetaObject();

	//prepare menu for item
	virtual bool PrepareContextMenu(wxMenu* defaultMenu);
	virtual void ProcessCommand(unsigned int id);
};

#endif 
#ifndef __META_ROLE_H__
#define __META_ROLE_H__

#include "metaObject.h"

class BACKEND_API ibValueMetaObjectRole : public ibValueMetaObject {
	public:
protected:
	enum
	{
		ID_METATREE_OPEN_ROLE = 19000,
	};
public:

	ibValueMetaObjectRole(const wxString& name = wxEmptyString, const wxString& synonym = wxEmptyString, const wxString& comment = wxEmptyString);

	//support icons
	virtual wxIcon GetIcon() const;
	static wxIcon GetIconGroup();

	//events:
	virtual bool OnCreateMetaObject(ibMetaData* metaData, int flags);

	//module manager is started or exit 
	virtual bool OnBeforeRunMetaObject(int flags);
	virtual bool OnAfterCloseMetaObject();

	//prepare menu for item
	virtual bool PrepareContextMenu(wxMenu* defaultMenu);
	virtual void ProcessCommand(unsigned int id);
};

#endif 
#ifndef _METAGRIDOBJECT_H__
#define _METAGRIDOBJECT_H__

#include "metaObject.h"

class BACKEND_API CMetaObjectGrid : public IMetaObject {
	wxDECLARE_DYNAMIC_CLASS(CMetaObjectGrid);
protected:
	enum
	{
		ID_METATREE_OPEN_TEMPLATE = 19000,
	};
public:

	//support icons
	virtual wxIcon GetIcon() const;
	static wxIcon GetIconGroup();

	//module manager is started or exit 
	virtual bool OnBeforeRunMetaObject(int flags);
	virtual bool OnAfterCloseMetaObject();

	//prepare menu for item
	virtual bool PrepareContextMenu(wxMenu* defaultMenu);
	virtual void ProcessCommand(unsigned int id);

protected:

	virtual bool LoadData(CMemoryReader& reader);
	virtual bool SaveData(CMemoryWriter& writer = CMemoryWriter());
};

class BACKEND_API CMetaObjectCommonGrid : public CMetaObjectGrid {
	wxDECLARE_DYNAMIC_CLASS(CMetaObjectCommonGrid);
public:
};

#endif 
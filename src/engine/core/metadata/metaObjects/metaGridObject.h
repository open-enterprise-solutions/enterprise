#ifndef _METAGRIDOBJECT_H__
#define _METAGRIDOBJECT_H__

#include "metaObject.h"

class CMetaGridObject : public IMetaObject {
	wxDECLARE_DYNAMIC_CLASS(CMetaGridObject);
protected:
	enum
	{
		ID_METATREE_OPEN_TEMPLATE = 19000,
	};
public:

	virtual wxString GetClassName() const override {
		return	wxT("template");
	}

	//support icons
	virtual wxIcon GetIcon();
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

class CMetaCommonGridObject : public CMetaGridObject {
	wxDECLARE_DYNAMIC_CLASS(CMetaCommonGridObject);
public:

	virtual wxString GetClassName() const override {
		return wxT("commonTemplates");
	}
};

#endif 
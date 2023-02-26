#ifndef _ENUMERATIONOBJECT_H__
#define _ENUMERATIONOBJECT_H__

#include "core/metadata/metaObjects/metaObject.h"

class CMetaEnumerationObject : public IMetaObject {
	wxDECLARE_DYNAMIC_CLASS(CMetaEnumerationObject);
public:

	Guid GetGuid() const {
		return m_metaGuid;
	}

	virtual wxString GetClassName() const override {
		return wxT("enum");
	}

	//support icons
	virtual wxIcon GetIcon();
	static wxIcon GetIconGroup();

	//load & save metadata from DB 
	virtual bool LoadData(CMemoryReader& reader);
	virtual bool SaveData(CMemoryWriter& writer = CMemoryWriter());
};

#endif
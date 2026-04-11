#ifndef _ENUMERATIONOBJECT_H__
#define _ENUMERATIONOBJECT_H__

#include "backend/metaCollection/metaObject.h"

class BACKEND_API ibValueMetaObjectEnum : public ibValueMetaObject {
	wxDECLARE_DYNAMIC_CLASS(ibValueMetaObjectEnum);
public:

	ibGuid GetGuid() const {
		return m_metaGuid;
	}

	//support icons
	virtual wxIcon GetIcon() const;
	static wxIcon GetIconGroup();

	//load & save metaData from DB 
	virtual bool LoadData(ibReaderMemory& reader);
	virtual bool SaveData(ibWriterMemory& writer);
};

#endif
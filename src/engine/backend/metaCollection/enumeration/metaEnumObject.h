#ifndef _ENUMERATIONOBJECT_H__
#define _ENUMERATIONOBJECT_H__

#include "backend/metaCollection/metaObject.h"

class BACKEND_API ibValueMetaObjectEnum : public ibValueMetaObject {
	public:

	//support icons
	virtual wxIcon GetIcon() const;
	static wxIcon GetIconGroup();

	//load & save metaData from DB 
};

#endif
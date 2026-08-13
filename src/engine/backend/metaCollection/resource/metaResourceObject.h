#ifndef _RESOURCE_H__
#define _RESOURCE_H__

#include "backend/metaCollection/attribute/metaAttributeObject.h"

class BACKEND_API ibValueMetaObjectResource : public ibValueMetaObjectAttribute {
	public:

	ibValueMetaObjectResource() : ibValueMetaObjectAttribute(ibValueTypes::TYPE_NUMBER) {
	}

	//support icons
	virtual wxIcon GetIcon() const;
	static wxIcon GetIconGroup();

	//get data selector
	virtual ibSelectorDataType GetFilterDataType() const;
};

#endif
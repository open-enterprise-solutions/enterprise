#ifndef _DIMENSION_H__
#define _DIMENSION_H__

#include "core/metadata/metaObjects/attribute/metaAttributeObject.h"

class CMetaDimensionObject : public CMetaAttributeObject {
	wxDECLARE_DYNAMIC_CLASS(CMetaDimensionObject);
public:

	//support icons
	virtual wxIcon GetIcon();
	static wxIcon GetIconGroup();

	//get class name
	virtual wxString GetClassName() const override {
		return wxT("dimension");
	}
};

#endif
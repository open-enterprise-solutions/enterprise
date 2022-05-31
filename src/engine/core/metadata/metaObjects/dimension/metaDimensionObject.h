#ifndef _DIMENSION_H__
#define _DIMENSION_H__

#include "metadata/metaObjects/attributes/metaAttributeObject.h"

class CMetaDimensionObject : public CMetaAttributeObject {
	wxDECLARE_DYNAMIC_CLASS(CMetaDimensionObject);
public:

	CMetaDimensionObject() : CMetaAttributeObject() {
	}

	virtual ~CMetaDimensionObject() {
	}

	//get class name
	virtual wxString GetClassName() const override {
		return wxT("dimension");
	}
};

#endif
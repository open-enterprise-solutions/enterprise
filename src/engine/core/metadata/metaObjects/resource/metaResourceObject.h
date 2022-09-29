#ifndef _RESOURCE_H__
#define _RESOURCE_H__

#include "metadata/metaObjects/attribute/metaAttributeObject.h"

class CMetaResourceObject : public CMetaAttributeObject {
	wxDECLARE_DYNAMIC_CLASS(CMetaResourceObject);
public:

	CMetaResourceObject() : CMetaAttributeObject(eValueTypes::TYPE_NUMBER) {
	}

	//support icons
	virtual wxIcon GetIcon();
	static wxIcon GetIconGroup();

	//get data selector 
	virtual eSelectorDataType GetSelectorDataType() const {
		IMetaObject* metaObject = GetParent();
		wxASSERT(metaObject);
		if (metaObject->GetClsid() == g_metaInformationRegisterCLSID)
			return m_parent->GetSelectorDataType();
		return eSelectorDataType::eSelectorDataType_resource;
	}

	//get class name
	virtual wxString GetClassName() const override {
		return wxT("resource");
	}
};

#endif
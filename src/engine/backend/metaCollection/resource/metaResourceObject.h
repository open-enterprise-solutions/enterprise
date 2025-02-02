#ifndef _RESOURCE_H__
#define _RESOURCE_H__

#include "backend/metaCollection/attribute/metaAttributeObject.h"

class BACKEND_API CMetaObjectResource : public CMetaObjectAttribute {
	wxDECLARE_DYNAMIC_CLASS(CMetaObjectResource);
public:

	CMetaObjectResource() : CMetaObjectAttribute(eValueTypes::TYPE_NUMBER) {
	}

	//support icons
	virtual wxIcon GetIcon() const;
	static wxIcon GetIconGroup();

	//get data selector 
	virtual eSelectorDataType GetSelectorDataType() const {
		IMetaObject* metaObject = GetParent();
		wxASSERT(metaObject);
		if (metaObject->GetClassType() == g_metaInformationRegisterCLSID)
			return m_parent->GetSelectorDataType();
		return eSelectorDataType::eSelectorDataType_resource;
	}
};

#endif
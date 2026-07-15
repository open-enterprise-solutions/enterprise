#ifndef __PROPERTY_TYPE_H__
#define __PROPERTY_TYPE_H__

#include "backend/propertyManager/propertyObject.h"
#include "backend/backend_type.h"

//base property for "type"
class BACKEND_API ibPropertyType : public ibProperty {
	wxVariantData* CreateVariantData(ibPropertyObject* property, const ibValueTypes type) const;
	wxVariantData* CreateVariantData(ibPropertyObject* property, const ibClassID& clsid) const;
	wxVariantData* CreateVariantData(ibPropertyObject* property, const ibTypeDescription& typeDesc) const;
public:

	ibTypeDescription& GetValueAsTypeDesc() const;
	void SetValue(const ibTypeDescription& val);

	ibPropertyType(ibPropertyCategory* cat, const wxString& name, const ibValueTypes type) : ibProperty(cat, name, CreateVariantData(cat->GetPropertyObject(), type)) {}
	ibPropertyType(ibPropertyCategory* cat, const wxString& name, const ibClassID& clsid) : ibProperty(cat, name, CreateVariantData(cat->GetPropertyObject(), clsid)) {}
	ibPropertyType(ibPropertyCategory* cat, const wxString& name, const wxString& label, const ibValueTypes type) : ibProperty(cat, name, label, CreateVariantData(cat->GetPropertyObject(), type)) {}

	ibPropertyType(ibPropertyCategory* cat, const wxString& name, const wxString& label, const ibClassID& clsid) : ibProperty(cat, name, label, CreateVariantData(cat->GetPropertyObject(), clsid)) {}
	ibPropertyType(ibPropertyCategory* cat, const wxString& name, const wxString& label, const wxString& helpString, const ibValueTypes type) : ibProperty(cat, name, label, helpString, CreateVariantData(cat->GetPropertyObject(), type)) {}
	ibPropertyType(ibPropertyCategory* cat, const wxString& name, const wxString& label, const wxString& helpString, const ibClassID& clsid) : ibProperty(cat, name, label, helpString, CreateVariantData(cat->GetPropertyObject(), clsid)) {}

	virtual bool IsEmptyProperty() const { return !GetValueAsTypeDesc().IsOk(); }

	//get property for grid 
	virtual wxObject* GetPGProperty() const {
		if (ms_propertyType != nullptr)
			return ms_propertyType(m_owner, GetFilterDataType(), m_propLabel, m_propName, m_propValue);
		return nullptr;
	}

	//set/get property data
	virtual bool SetDataValue(const ibValue& varPropVal);
	virtual bool GetDataValue(ibValue& pvarPropVal) const;

	//load & save object in control

	// composite node value -> a Child (struct): a "types" Array of clsids + the
	// number / date / string qualifier fields. The "set of values" case.
	virtual bool ReadNodeValue(const ibDataValue& value) override;
	virtual bool WriteNodeValue(ibDataValue& value) const override;

public:

	static wxObject* (*ms_propertyType)(ibPropertyObject*, ibSelectorDataType, const wxString&, const wxString&, const wxVariant&);

	// Which types this property may offer. Public because the FRONT builds the editor now
	// and reads it there — it was protected while the property built its own wxPGProperty.
	ibSelectorDataType GetFilterDataType() const;
};

#endif
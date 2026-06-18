#ifndef __PROPERTY_SOURCE_H__
#define __PROPERTY_SOURCE_H__

#include "backend/propertyManager/propertyObject.h"
#include "backend/backend_type.h"

//////////////////////////////////////////////////////////////////
struct ibTypeDescription;
struct ibSourceDescription;
//////////////////////////////////////////////////////////////////

//base property for "source"
class BACKEND_API ibPropertySource : public ibProperty {
	wxVariantData* CreateVariantData(const ibPropertyObject* property, const ibValueTypes& type) const;
	wxVariantData* CreateVariantData(const ibPropertyObject* property, const ibClassID& id) const;
	wxVariantData* CreateVariantData(const ibPropertyObject* property, const ibTypeDescription& typeDesc) const;
	wxVariantData* CreateVariantData(const ibPropertyObject* property, const ibMetaID& id) const;
	wxVariantData* CreateVariantData(const ibPropertyObject* property, const ibGuid& id, bool fillTypeDesc = true) const;
	wxVariantData* CreateVariantData(const ibPropertyObject* property, const ibSourceDescription& desc) const;
public:

#pragma region _value_
	ibMetaID GetValueAsSource() const;
	ibGuid GetValueAsSourceGuid() const;
	ibTypeDescription& GetValueAsTypeDesc(bool fillTypeDesc = true) const;

	// The binding address — a single ordered metaId path stored in the variant. Returned
	// by reference so serialisation fills it in place (mirrors GetValueAsMetaDesc on the
	// meta-binding properties). The same description is fed to the source object.
	ibSourceDescription& GetValueAsSourceDesc() const;

	// The binding address as a plain metaId path — the identifier a control hands to
	// ibSourceDataObject::GetValueByPath. The property only supplies it; it never reads
	// or writes the value itself.
	const std::vector<ibMetaID>& GetValueAsPath() const;

	void SetValue(const ibMetaID& val);
	void SetValue(const ibGuid& val, bool fillTypeDesc = true);
	void SetValue(const ibTypeDescription& val);
	void SetValue(const ibSourceDescription& val);

	// True when the binding walks one or more reference columns (a dotted path,
	// Source.Ref.Field) instead of a single direct column. CHEAP — just the path
	// length on the variant. A dot-walk binding is READ-ONLY.
	bool IsDotWalk() const;
#pragma endregion

	const class ibValueMetaObjectAttributeBase* GetSourceAttributeObject() const;

	ibPropertySource(ibPropertyCategory* cat, const wxString& name, const ibValueTypes& type = ibValueTypes::TYPE_STRING)
		: ibProperty(cat, name, CreateVariantData(cat->GetPropertyObject(), type))
	{
	}

	ibPropertySource(ibPropertyCategory* cat, const wxString& name, const ibClassID& clsid)
		: ibProperty(cat, name, CreateVariantData(cat->GetPropertyObject(), clsid))
	{
	}

	ibPropertySource(ibPropertyCategory* cat, const wxString& name, const wxString& label, const ibValueTypes& type = ibValueTypes::TYPE_STRING)
		: ibProperty(cat, name, label, CreateVariantData(cat->GetPropertyObject(), type))
	{
	}

	ibPropertySource(ibPropertyCategory* cat, const wxString& name, const wxString& label, const ibClassID& clsid)
		: ibProperty(cat, name, label, CreateVariantData(cat->GetPropertyObject(), clsid))
	{
	}

	ibPropertySource(ibPropertyCategory* cat, const wxString& name, const wxString& label, const wxString& helpString, const ibValueTypes& type = ibValueTypes::TYPE_STRING)
		: ibProperty(cat, name, label, helpString, CreateVariantData(cat->GetPropertyObject(), type))
	{
	}

	ibPropertySource(ibPropertyCategory* cat, const wxString& name, const wxString& label, const wxString& helpString, const ibClassID& clsid)
		: ibProperty(cat, name, label, helpString, CreateVariantData(cat->GetPropertyObject(), clsid))
	{
	}

	virtual bool IsEmptyProperty() const;

	//get property for grid 
	virtual wxObject* GetPGProperty() const {
		if (ms_propertySource != nullptr)
			return ms_propertySource(m_owner, m_propLabel, m_propName, m_propValue);
		return nullptr;
	}

	// set/get property data
	virtual bool SetDataValue(const ibValue& varPropVal);
	virtual bool GetDataValue(ibValue& pvarPropVal) const;

	//load & save object in control 
	virtual bool ReadNodeValue(const ibDataValue& value) override;
	virtual bool WriteNodeValue(ibDataValue& value) const override;

public:

	static wxObject* (*ms_propertySource)(ibPropertyObject*, const wxString&, const wxString&, const wxVariant&);
};

#endif
#ifndef __PROPERTY_PICTURE_H__
#define __PROPERTY_PICTURE_H__

#include "backend/propertyManager/propertyObject.h"
#include "backend/backend_picture.h"

//base property for "picture"
class BACKEND_API ibPropertyPicture : public ibProperty {
	wxVariantData* CreateVariantData(ibPropertyObject* property, const ibPictureDescription& id) const;
public:

#pragma region _value_
	wxBitmap GetValueAsBitmap() const;
	ibPictureDescription& GetValueAsPictureDesc() const;
	void SetValue(const ibPictureDescription& val);
#pragma endregion 

	ibPropertyPicture(ibPropertyCategory* cat, const wxString& name, const ibPictureDescription& id = ibPictureDescription())
		: ibProperty(cat, name, CreateVariantData(cat->GetPropertyObject(), id))
	{
	}

	ibPropertyPicture(ibPropertyCategory* cat, const wxString& name, const wxString& label, const ibPictureDescription& id = ibPictureDescription())
		: ibProperty(cat, name, label, CreateVariantData(cat->GetPropertyObject(), id))
	{
	}

	ibPropertyPicture(ibPropertyCategory* cat, const wxString& name, const wxString& label, const wxString& helpString, const ibPictureDescription& id = ibPictureDescription())
		: ibProperty(cat, name, label, helpString, CreateVariantData(cat->GetPropertyObject(), id))
	{
	}

	virtual bool IsEmptyProperty() const;

	//get property for grid 
	virtual wxObject* GetPGProperty() const {
		if (ms_propertyPicture != nullptr)
			return ms_propertyPicture(m_propLabel, m_propName, m_propValue);
		return nullptr;
	}

	//set/Get property data
	virtual bool SetDataValue(const ibValue& varPropVal);
	virtual bool GetDataValue(ibValue& pvarPropVal) const;

	//load & save object in control 
	virtual bool ReadNodeValue(const ibDataValue& value) override;
	virtual bool WriteNodeValue(ibDataValue& value) const override;

public:

	static wxObject* (*ms_propertyPicture)(const wxString&, const wxString&, const wxVariant&);
};

//base property for "external picture"
class BACKEND_API ibPropertyExternalPicture : public ibProperty {
	wxVariantData* CreateVariantData(const ibExternalPictureDescription& pictureDesc) const;
public:

#pragma region _value_
	wxBitmap GetValueAsBitmap() const;
	ibExternalPictureDescription& GetValueAsPictureDesc() const;
	void SetValue(const ibExternalPictureDescription& val);
#pragma endregion 

	ibPropertyExternalPicture(ibPropertyCategory* cat, const wxString& name, const ibExternalPictureDescription& pictureDesc = ibExternalPictureDescription())
		: ibProperty(cat, name, CreateVariantData(pictureDesc))
	{
	}

	ibPropertyExternalPicture(ibPropertyCategory* cat, const wxString& name, const wxString& label, const ibExternalPictureDescription& pictureDesc = ibExternalPictureDescription())
		: ibProperty(cat, name, label, CreateVariantData(pictureDesc))
	{
	}

	ibPropertyExternalPicture(ibPropertyCategory* cat, const wxString& name, const wxString& label, const wxString& helpString, const ibExternalPictureDescription& pictureDesc = ibExternalPictureDescription())
		: ibProperty(cat, name, label, helpString, CreateVariantData(pictureDesc))
	{
	}

	virtual bool IsEmptyProperty() const;

	//get property for grid 
	virtual wxObject* GetPGProperty() const {
		if (ms_propertyExtPicture != nullptr)
			return ms_propertyExtPicture(m_propLabel, m_propName, m_propValue);
		return nullptr;
	}

	//set/Get property data
	virtual bool SetDataValue(const ibValue& varPropVal);
	virtual bool GetDataValue(ibValue& pvarPropVal) const;

	//load & save object in control 
	virtual bool ReadNodeValue(const ibDataValue& value) override;
	virtual bool WriteNodeValue(ibDataValue& value) const override;

public:

	static wxObject* (*ms_propertyExtPicture)(const wxString&, const wxString&, const wxVariant&);
};

#endif
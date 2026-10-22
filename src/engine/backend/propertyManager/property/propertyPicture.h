#ifndef __PROPERTY_PICTURE_H__
#define __PROPERTY_PICTURE_H__

#include "backend/propertyManager/propertyObject.h"
#include "backend/backend_picture.h"

//base property for "picture"
class BACKEND_API ibPropertyPicture : public ibProperty {
	static wxVariantData* CreateVariantData(ibPropertyObject* property, const ibPictureDescription& id);
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

	//set/Get property data
	virtual bool SetDataValue(const ibValue& varPropVal);
	virtual bool GetDataValue(ibValue& pvarPropVal) const;

	//load & save object in control
	virtual bool ReadNodeValue(const ibDataValue& value) override;
	virtual bool WriteNodeValue(ibDataValue& value) const override;

	// …and the configuration's pictures it may be loaded from, as the desktop's cell listed them (advpropPicture.cpp):
	// Pictures — the picture metaobjects of the owner's configuration, or of the one it extends, each by its Name, its
	// Guid and the Id it is drawn by, not as a Picture: a picture named in an answer is fetched before the answer is drawn.
	virtual bool GetProtocolValue(ibDataNode& row, ibReader reader) override;

public:

};

//base property for "external picture"
class BACKEND_API ibPropertyExternalPicture : public ibProperty {
	static wxVariantData* CreateVariantData(const ibExternalPictureDescription& pictureDesc);
public:

#pragma region _value_
	wxBitmap GetValueAsBitmap() const;
	const ibExternalPictureDescription& GetValueAsPictureDesc() const;
	ibGuid GetValueAsVersion() const;
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

	//set/Get property data
	virtual bool SetDataValue(const ibValue& varPropVal);
	virtual bool GetDataValue(ibValue& pvarPropVal) const;

	//load & save object in control 
	virtual bool ReadNodeValue(const ibDataValue& value) override;
	virtual bool WriteNodeValue(ibDataValue& value) const override;

public:

};

#endif
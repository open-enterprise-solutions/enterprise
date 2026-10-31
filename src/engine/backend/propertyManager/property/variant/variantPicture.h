#ifndef __PICTURE_VARIANT_H__
#define __PICTURE_VARIANT_H__

#include "backend/backend_core.h"
#include "core/variant.h"
#include "backend/propertyManager/propertyObject.h"
#include "backend/backend_picture.h"

class BACKEND_API ibVariantExternalPicture : public ibVariant {
public:

	bool IsEmptyPicture() const { return m_pictureExternalFile.IsEmptyPicture(); }

	const ibExternalPictureDescription& GetExternalPictureDesc() const { return m_pictureExternalFile; }
	// ⭐ WHICH PICTURE IT HOLDS — a guid new with each picture it is given and kept by its copies: what a configuration
	// picture's id carries (ibBackendPicture::SendPicture), so a client asks anew for one changed since. Not written with
	// the picture: one read is given a new one.
	const ibGuid& GetPictureVersion() const { return m_version; }

	ibString GetPictureFileName() const;
	ibPicture GetPicture(const ibSize& size = ibSize(16, 16)) const;

	void SetFromExternalFile(const ibExternalPictureDescription& container) {
		m_pictureExternalFile = container;
		m_version = wxNewUniqueGuid;
	}

	ibVariantExternalPicture(const ibExternalPictureDescription& container = ibExternalPictureDescription())
		: ibVariant(ibValueTypes::TYPE_VALUE), m_pictureExternalFile(container), m_version(wxNewUniqueGuid)
	{
	}

	ibVariantExternalPicture(const ibVariantExternalPicture& src) :
		ibVariant(ibValueTypes::TYPE_VALUE), m_pictureExternalFile(src.m_pictureExternalFile), m_version(src.m_version)
	{
	}

	virtual bool CompareValueEQ(const ibVariant& data) const override {
		const ibVariantExternalPicture* srcData = dynamic_cast<const ibVariantExternalPicture*>(data.GetRef());
		if (srcData != nullptr)
			return m_pictureExternalFile == srcData->m_pictureExternalFile;
		return false;
	}

	virtual ibString GetString() const override;

protected:

	ibExternalPictureDescription m_pictureExternalFile;
	ibGuid m_version;
};

class BACKEND_API ibVariantPicture : public ibVariant {
public:

	bool IsEmptyPicture() const { return m_pictureDesc.IsEmptyPicture(); }

	ibPictureDescription& GetPictureDesc() { return m_pictureDesc; }
	ibPicture GetPicture(const ibSize& size = ibSize(16, 16)) const;

	void SetPictureType(ibPictureType type) { m_pictureDesc.m_type = type; }

	void SetFromBackendPicture(const ibClassID& id) { m_pictureDesc.m_class_identifier = id; }
	void SetFromConfiguraion(const ibGuid& id) { m_pictureDesc.m_meta_guid = id; }
	void SetFromExternalFile(const ibExternalPictureDescription& container) { m_pictureDesc.m_img_data = container; m_externalPicture->SetFromExternalFile(container); }

	//Get owner value
	const ibPropertyObject* GetOwner() { return m_ownerProperty; }

	//set picture data 
	ibVariantPicture(const ibPropertyObject* prop, const ibClassID& id) :
		ibVariant(ibValueTypes::TYPE_VALUE), m_ownerProperty(prop), m_pictureDesc(id)
	{
		m_externalPicture = new ibVariantExternalPicture();
		m_externalPicture->IncrRef();   // this value's own hold on it
	}

	ibVariantPicture(const ibPropertyObject* prop, const ibMetaID& id) :
		ibVariant(ibValueTypes::TYPE_VALUE), m_ownerProperty(prop), m_pictureDesc(id)
	{
		m_externalPicture = new ibVariantExternalPicture();
		m_externalPicture->IncrRef();   // this value's own hold on it
	}

	ibVariantPicture(const ibPropertyObject* prop, const ibExternalPictureDescription& container) :
		ibVariant(ibValueTypes::TYPE_VALUE), m_ownerProperty(prop), m_pictureDesc(container)
	{
		m_externalPicture = new ibVariantExternalPicture(container);
		m_externalPicture->IncrRef();   // this value's own hold on it
	}

	ibVariantPicture(const ibPropertyObject* prop, const ibPictureDescription& pictureDesc) :
		ibVariant(ibValueTypes::TYPE_VALUE), m_ownerProperty(prop), m_pictureDesc(pictureDesc)
	{
		m_externalPicture = new ibVariantExternalPicture(pictureDesc.m_img_data);
		m_externalPicture->IncrRef();   // this value's own hold on it
	}

	ibVariantPicture(const ibVariantPicture& src) :
		ibVariant(ibValueTypes::TYPE_VALUE), m_ownerProperty(src.m_ownerProperty), m_pictureDesc(src.m_pictureDesc)
	{
		m_externalPicture = new ibVariantExternalPicture(*src.m_externalPicture);
		m_externalPicture->IncrRef();   // this value's own hold on it
	}

	virtual ~ibVariantPicture() { m_externalPicture->DecrRef(); }

	virtual bool CompareValueEQ(const ibVariant& data) const override {
		const ibVariantPicture* srcData = dynamic_cast<const ibVariantPicture*>(data.GetRef());
		if (srcData != nullptr)
			return m_pictureDesc == srcData->m_pictureDesc && m_externalPicture->CompareValueEQ(*srcData->m_externalPicture);
		return false;
	}

	virtual ibString GetString() const override;

protected:

	const ibPropertyObject* m_ownerProperty = nullptr;
	ibPictureDescription m_pictureDesc;
	ibVariantExternalPicture* m_externalPicture = nullptr;
};

#endif
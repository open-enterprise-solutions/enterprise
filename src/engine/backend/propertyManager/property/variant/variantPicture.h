#ifndef __PICTURE_VARIANT_H__
#define __PICTURE_VARIANT_H__

#include "backend/backend_core.h"
#include "backend/propertyManager/propertyObject.h"
#include "backend/backend_picture.h"

class BACKEND_API ibVariantDataExternalPicture : public wxVariantData {
	wxString MakeString() const;
public:

	bool IsEmptyPicture() const { return m_pictureExternalFile.IsEmptyPicture(); }

	ibExternalPictureDescription& GetExternalPictureDesc() { return m_pictureExternalFile; }

	wxString GetPictureFileName() const;
	wxBitmap GetPictureBitmap(const wxSize& size = wxSize(16, 16)) const;

	void SetFromExternalFile(const ibExternalPictureDescription& container) { m_pictureExternalFile = container; }

	ibVariantDataExternalPicture(const ibExternalPictureDescription& container = ibExternalPictureDescription())
		: m_pictureExternalFile(container)
	{
	}

	ibVariantDataExternalPicture(const ibVariantDataExternalPicture& src) :
		wxVariantData(), m_pictureExternalFile(src.m_pictureExternalFile)
	{
	}

	virtual ibVariantDataExternalPicture* Clone() const { return new ibVariantDataExternalPicture(*this); }

	virtual bool Eq(wxVariantData& data) const {
		ibVariantDataExternalPicture* srcData = dynamic_cast<ibVariantDataExternalPicture*>(&data);
		if (srcData != nullptr)
			return m_pictureExternalFile == srcData->m_pictureExternalFile;
		return false;
	}

#if wxUSE_STD_IOSTREAM
	virtual bool Write(wxSTD ostream& str) const {
		str << MakeString();
		return true;
	}
#endif

	virtual bool Write(wxString& str) const {
		str = MakeString();
		return true;
	}

	virtual wxString GetType() const { return wxT("ibVariantDataExternalPicture"); }

protected:

	ibExternalPictureDescription m_pictureExternalFile;
};

class BACKEND_API ibVariantDataPicture : public wxVariantData {
	wxString MakeString() const;
public:

	bool IsEmptyPicture() const { return m_pictureDesc.IsEmptyPicture(); }

	ibPictureDescription& GetPictureDesc() { return m_pictureDesc; }
	wxBitmap GetPictureBitmap(const wxSize& size = wxSize(16, 16)) const;

	ibVariantDataExternalPicture* CloneExternalPicture() const { return m_externalPicture->Clone(); }

	void SetPictureType(ibPictureType type) { m_pictureDesc.m_type = type; }

	void SetFromBackendPicture(const ibClassID& id) { m_pictureDesc.m_class_identifier = id; }
	void SetFromConfiguraion(const ibGuid& id) { m_pictureDesc.m_meta_guid = id; }
	void SetFromExternalFile(const ibExternalPictureDescription& container) { m_pictureDesc.m_img_data = container; m_externalPicture->SetFromExternalFile(container); }

	//Get owner value
	const ibPropertyObject* GetOwner() { return m_ownerProperty; }

	//set picture data 
	ibVariantDataPicture(const ibPropertyObject* prop, const ibClassID& id) :
		m_ownerProperty(prop), m_pictureDesc(id)
	{
		m_externalPicture = new ibVariantDataExternalPicture();
	}

	ibVariantDataPicture(const ibPropertyObject* prop, const ibMetaID& id) :
		m_ownerProperty(prop), m_pictureDesc(id)
	{
		m_externalPicture = new ibVariantDataExternalPicture();
	}

	ibVariantDataPicture(const ibPropertyObject* prop, const ibExternalPictureDescription& container) :
		m_ownerProperty(prop), m_pictureDesc(container)
	{
		m_externalPicture = new ibVariantDataExternalPicture(container);
	}

	ibVariantDataPicture(const ibPropertyObject* prop, const ibPictureDescription& pictureDesc) :
		m_ownerProperty(prop), m_pictureDesc(pictureDesc)
	{
		m_externalPicture = new ibVariantDataExternalPicture(pictureDesc.m_img_data);
	}

	ibVariantDataPicture(const ibVariantDataPicture& src) :
		wxVariantData(), m_ownerProperty(src.m_ownerProperty), m_pictureDesc(src.m_pictureDesc)
	{
		m_externalPicture = new ibVariantDataExternalPicture(*src.m_externalPicture);
	}

	virtual ~ibVariantDataPicture() { m_externalPicture->DecRef(); }
	virtual ibVariantDataPicture* Clone() const { return new ibVariantDataPicture(*this); }

	virtual bool Eq(wxVariantData& data) const {
		ibVariantDataPicture* srcData = dynamic_cast<ibVariantDataPicture*>(&data);
		if (srcData != nullptr)
			return m_pictureDesc == srcData->m_pictureDesc && m_externalPicture->Eq(*srcData->m_externalPicture);
		return false;
	}

#if wxUSE_STD_IOSTREAM
	virtual bool Write(wxSTD ostream& str) const {
		str << MakeString();
		return true;
	}
#endif

	virtual bool Write(wxString& str) const {
		str = MakeString();
		return true;
	}

	virtual wxString GetType() const { return wxT("ibVariantDataPicture"); }

protected:

	const ibPropertyObject* m_ownerProperty = nullptr;
	ibPictureDescription m_pictureDesc;
	ibVariantDataExternalPicture* m_externalPicture = nullptr;
};

#endif
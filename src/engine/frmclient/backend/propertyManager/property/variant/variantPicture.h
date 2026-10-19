#ifndef __PICTURE_VARIANT_H__
#define __PICTURE_VARIANT_H__

#include <wx/filename.h>

#include "frmclient/backend/backend_core.h"
#include "frmclient/backend/backend_picture.h"   // ibBackendPicture::CreatePicture — a file's picture, drawn
#include "core/description/pictureDescription.h"

class FRMCLIENT_API ibPropertyObject;

// THE PICTURE A PROPERTY HOLDS — the engine's two variants (backend/propertyManager/property/variant/variantPicture.h):
// a picture of a file, carried whole (ibVariantDataExternalPicture), and a picture by where it comes from — the
// engine's, the configuration's, a file's (ibVariantDataPicture).
class ibVariantDataExternalPicture : public wxVariantData {
	wxString MakeString() const {
		if (IsEmptyPicture())
			return wxEmptyString;

		wxFileName fn(m_pictureExternalFile.m_img_name);
		return fn.GetFullName();
	}
public:

	bool IsEmptyPicture() const { return m_pictureExternalFile.IsEmptyPicture(); }

	ibExternalPictureDescription& GetExternalPictureDesc() { return m_pictureExternalFile; }

	wxString GetPictureFileName() const { return m_pictureExternalFile.m_img_name; }
	wxBitmap GetPictureBitmap(const wxSize& size = wxSize(16, 16)) const {
		return ibBackendPicture::CreatePicture(m_pictureExternalFile, size);
	}

	void SetFromExternalFile(const ibExternalPictureDescription& container) { m_pictureExternalFile = container; }

	ibVariantDataExternalPicture(const ibExternalPictureDescription& container = ibExternalPictureDescription())
		: m_pictureExternalFile(container)
	{
	}

	ibVariantDataExternalPicture(const ibVariantDataExternalPicture& src) :
		wxVariantData(), m_pictureExternalFile(src.m_pictureExternalFile)
	{
	}

	virtual ibVariantDataExternalPicture* Clone() const override { return new ibVariantDataExternalPicture(*this); }

	virtual bool Eq(wxVariantData& data) const override {
		ibVariantDataExternalPicture* srcData = dynamic_cast<ibVariantDataExternalPicture*>(&data);
		if (srcData != nullptr)
			return m_pictureExternalFile == srcData->m_pictureExternalFile;
		return false;
	}

#if wxUSE_STD_IOSTREAM
	virtual bool Write(wxSTD ostream& str) const override {
		str << MakeString();
		return true;
	}
#endif

	virtual bool Write(wxString& str) const override {
		str = MakeString();
		return true;
	}

	virtual wxString GetType() const override { return wxT("ibVariantDataExternalPicture"); }

protected:

	ibExternalPictureDescription m_pictureExternalFile;
};

class ibVariantDataPicture : public wxVariantData {
	wxString MakeString() const {
		if (m_pictureDesc.m_type == ibPictureType::eFromBackend)
			return _("Backend picture");
		else if (m_pictureDesc.m_type == ibPictureType::eFromConfiguration)
			return _("Configuration picture");
		else if (m_pictureDesc.m_type == ibPictureType::eFromFile)
			return _("External picture");

		return wxEmptyString;
	}
public:

	bool IsEmptyPicture() const { return m_pictureDesc.IsEmptyPicture(); }

	ibPictureDescription& GetPictureDesc() { return m_pictureDesc; }

	ibVariantDataExternalPicture* CloneExternalPicture() const { return m_externalPicture->Clone(); }

	void SetPictureType(ibPictureType type) { m_pictureDesc.m_type = type; }

	void SetFromBackendPicture(const ibClassID& id) { m_pictureDesc.m_class_identifier = id; }
	void SetFromConfiguraion(const ibGuid& id) { m_pictureDesc.m_meta_guid = id; }
	void SetFromExternalFile(const ibExternalPictureDescription& container) { m_pictureDesc.m_img_data = container; m_externalPicture->SetFromExternalFile(container); }

	//Get owner value
	const ibPropertyObject* GetOwner() { return m_ownerProperty; }

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

	virtual ~ibVariantDataPicture() override { m_externalPicture->DecRef(); }
	virtual ibVariantDataPicture* Clone() const override { return new ibVariantDataPicture(*this); }

	virtual bool Eq(wxVariantData& data) const override {
		ibVariantDataPicture* srcData = dynamic_cast<ibVariantDataPicture*>(&data);
		if (srcData != nullptr)
			return m_pictureDesc == srcData->m_pictureDesc && m_externalPicture->Eq(*srcData->m_externalPicture);
		return false;
	}

#if wxUSE_STD_IOSTREAM
	virtual bool Write(wxSTD ostream& str) const override {
		str << MakeString();
		return true;
	}
#endif

	virtual bool Write(wxString& str) const override {
		str = MakeString();
		return true;
	}

	virtual wxString GetType() const override { return wxT("ibVariantDataPicture"); }

protected:

	const ibPropertyObject* m_ownerProperty = nullptr;
	ibPictureDescription m_pictureDesc;
	ibVariantDataExternalPicture* m_externalPicture = nullptr;
};

#endif

#include "variantPicture.h"

#include <wx/filename.h>

wxString ibVariantDataExternalPicture::MakeString() const
{
	if (IsEmptyPicture())
		return wxEmptyString;

	wxFileName fn(m_pictureExternalFile.m_img_name);
	return fn.GetFullName();
}

wxString ibVariantDataExternalPicture::GetPictureFileName() const
{
	return m_pictureExternalFile.m_img_name;
}

wxBitmap ibVariantDataExternalPicture::GetPictureBitmap(const wxSize& size) const
{
	return ibBackendPicture::CreatePicture(m_pictureExternalFile, size);
}

wxString ibVariantDataPicture::MakeString() const
{
	if (m_pictureDesc.m_type == ibPictureType::eFromBackend)
		return _("Backend picture");
	else if (m_pictureDesc.m_type == ibPictureType::eFromConfiguration)
		return _("Configuration picture");
	else if (m_pictureDesc.m_type == ibPictureType::eFromFile)
		return _("External picture");

	return wxEmptyString;
}

wxBitmap ibVariantDataPicture::GetPictureBitmap(const wxSize& size) const
{
	return ibBackendPicture::CreatePicture(m_pictureDesc,
		m_ownerProperty != nullptr ? m_ownerProperty->GetMetaData() : nullptr, size);
}


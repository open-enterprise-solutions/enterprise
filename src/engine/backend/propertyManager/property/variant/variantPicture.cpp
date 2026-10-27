#include "variantPicture.h"

#include <wx/filename.h>

wxString ibVariantDataExternalPicture::MakeString() const
{
	if (IsEmptyPicture())
		return ibEmptyString;

	wxFileName fn(m_pictureExternalFile.m_img_name);
	return fn.GetFullName();
}

ibString ibVariantDataExternalPicture::GetPictureFileName() const
{
	return ibString::FromUTF8(m_pictureExternalFile.m_img_name);
}

ibPicture ibVariantDataExternalPicture::GetPicture(const ibSize& size) const
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

	return ibEmptyString;
}

ibPicture ibVariantDataPicture::GetPicture(const ibSize& size) const
{
	return ibBackendPicture::CreatePicture(m_pictureDesc,
		m_ownerProperty != nullptr ? m_ownerProperty->GetMetaData() : nullptr, size);
}


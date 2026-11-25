#include "variantPicture.h"

#include "core/fileSystem.h"   // ibPathOf - the file's name without its folder

ibString ibVariantExternalPicture::GetString() const
{
	if (IsEmptyPicture())
		return ibEmptyString;

	return ibTextOf(ibPathOf(GetPictureFileName()).filename());
}

ibString ibVariantExternalPicture::GetPictureFileName() const
{
	return ibString::FromUTF8(m_pictureExternalFile.m_img_name);
}

ibPicture ibVariantExternalPicture::GetPicture(const ibSize& size) const
{
	return ibBackendPicture::CreatePicture(m_pictureExternalFile, size);
}

ibString ibVariantPicture::GetString() const
{
	if (m_pictureDesc.m_type == ibPictureType::eFromBackend)
		return _("Backend picture");
	else if (m_pictureDesc.m_type == ibPictureType::eFromApplication)
		return _("Application picture");
	else if (m_pictureDesc.m_type == ibPictureType::eFromFile)
		return _("External picture");

	return ibEmptyString;
}

ibPicture ibVariantPicture::GetPicture(const ibSize& size) const
{
	return ibBackendPicture::CreatePicture(m_pictureDesc,
		m_ownerProperty != nullptr ? m_ownerProperty->GetMetaData() : nullptr, size);
}


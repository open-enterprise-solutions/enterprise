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

// THE HASH OF WHAT IT HOLDS — FNV-1a 64 over the image's bytes and its size, the same in every process and every run:
// a client keeps a configuration's picture by an id that carries it. Worked out where the picture is set, so reading
// it only reads; the empty picture is 0.
u64 ibVariantDataExternalPicture::HashContent(const ibExternalPictureDescription& container)
{
	if (container.IsEmptyPicture())
		return 0;
	u64 hash = 14695981039346656037ull;
	const auto add = [&hash](const unsigned char byte) { hash ^= byte; hash *= 1099511628211ull; };
	for (const char byte : container.m_img_buffer)
		add(static_cast<unsigned char>(byte));
	for (const unsigned int size : { container.m_width, container.m_height })
		for (int shift = 0; shift < 32; shift += 8)
			add(static_cast<unsigned char>(size >> shift));
	return hash;
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


#include "backend_picture.h"

#include <wx/filename.h>
#include <wx/image.h>
#include <wx/mstream.h>

#include "core/pictureRegistry.h"   // the engine's pictures, as the core registered them
#include "core/utils/base64.h"      // ibBase64 — a master's PNG, read from its text in the source

bool ibBackendPicture::LoadFromFile(const ibString& strFileName, ibExternalPictureDescription& pictureDesc)
{
	wxFileName filename(strFileName);

	// Cache the image
	if (filename.FileExists()) {

		wxImage image(filename.GetFullPath());
		wxMemoryOutputStream outputStream;

		if (image.SaveFile(outputStream, wxBitmapType::wxBITMAP_TYPE_PNG)) {

			// Get the data from the output stream's buffer
			// The output stream manages the memory buffer internally.
			const size_t buffer_len = outputStream.GetLength();

			pictureDesc.m_img_name = filename.GetFullPath();
			pictureDesc.m_img_buffer.resize(buffer_len, '\0');

			outputStream.CopyTo(pictureDesc.m_img_buffer.data(), buffer_len);

			pictureDesc.m_width = image.GetWidth();
			pictureDesc.m_height = image.GetHeight();

			return true;
		}
	}

	return false;
}

wxBitmap ibBackendPicture::CreatePicture(const ibExternalPictureDescription& pictureDesc, const wxSize& size)
{
	if (!pictureDesc.m_img_buffer.empty()) {
		wxMemoryInputStream inputStream(
			pictureDesc.m_img_buffer.data(),
			pictureDesc.m_img_buffer.size()
		);
		const wxImage image(inputStream);
		if (size != image.GetSize())
			return image.Scale(size.x, size.y, wxIMAGE_QUALITY_HIGH);
		return image;
	}

	return wxNullBitmap;
}

wxImage ibBackendPicture::GetImageFromBase64(const ibString& src, const wxSize& size)
{
	ibMemoryBuffer png;
	const wxImage image = ibBase64::Read(src, png) ? ibProtocolImage(png) : wxImage();
	return size != wxDefaultSize && image.IsOk() && size != image.GetSize() ? image.Scale(size.x, size.y, wxIMAGE_QUALITY_HIGH) : image;
}

std::vector<ibBackendPictureEntry> ibBackendPicture::GetArrayPicture()
{
	// (The engine's list adds the icon of every metadata class its constructors register — the server's; the client
	//  offers the pictures the core registered, drawn as the client has them.)
	std::vector<ibBackendPictureEntry> arrayPicture;
	for (const ibPictureEntry& registered : ibPictureRegistry::GetArrayPicture())
		arrayPicture.push_back({ registered.m_name, GetPicture(registered.m_id), registered.m_id });
	return arrayPicture;
}

#include "backend_picture.h"

#include <wx/filename.h>
#include <wx/image.h>
#include <wx/mstream.h>

#include "core/pictureRegistry.h"   // the engine's pictures, as the core registered them (and ibPicture::FromSvg)

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
	// The bytes as they are kept — a PNG, or a drawing given over MCP — at the size asked, by the core.
	if (pictureDesc.m_img_buffer.empty())
		return wxNullBitmap;
	const ibPicture picture(ibMemoryBuffer(pictureDesc.m_img_buffer.data(), pictureDesc.m_img_buffer.size()));
	const wxImage image = ibProtocolImage(picture.Scale(ibSize(size.x, size.y)).GetData());
	return image.IsOk() ? wxBitmap(image) : wxNullBitmap;
}

wxImage ibBackendPicture::GetImageFromSvg(const ibString& svg, const wxSize& size)
{
	const ibPicture picture = ibPicture::FromSvg(svg, ibSize(size.x, size.y));
	return picture.IsOk() ? ibProtocolImage(picture.GetData()) : wxImage();
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

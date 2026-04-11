#include "backend_picture.h"

#include <wx/base64.h>
#include <wx/mstream.h>

////////////////////////////////////////////////////////////////////////////////////////////////////////

static std::vector<ibBackendPictureEntry> s_arrayPicture;

////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RegisterBackendPicture(const wxString name, const ibPictureID& id, const char* const* data)
{
	if (!ibBackendPicture::IsRegisterPicture(id)) {
		ibBackendPicture::RegisterPicture(name, id, wxImage(data));
		return true;
	}

	wxASSERT_MSG(false, "Picture is register");
	return false;
}

bool RegisterBackendPicture(const wxString name, const ibPictureID& id, const wxString& base64)
{
	if (!ibBackendPicture::IsRegisterPicture(id)) {
		ibBackendPicture::RegisterPicture(name, id, ibBackendPicture::GetImageFromBase64(base64));
		return true;
	}

	wxASSERT_MSG(false, "Picture is register");
	return false;
}

bool RegisterBackendPicture(const wxString name, const ibPictureID& id, const wxBitmap& bitmap)
{
	if (!ibBackendPicture::IsRegisterPicture(id)) {
		ibBackendPicture::RegisterPicture(name, id, bitmap);
		return true;
	}

	wxASSERT_MSG(false, "Picture is register");
	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////

bool ibBackendPicture::LoadFromFile(const wxString& strFileName, ibExternalPictureDescription& pictureDesc)
{
	wxFileName filename = strFileName;

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

bool ibBackendPicture::LoadFromFile(const wxString& strFileName, ibPictureDescription& pictureDesc)
{
	if (ibBackendPicture::LoadFromFile(strFileName, pictureDesc.m_img_data)) {
		pictureDesc.m_type = ibPictureType::eFromFile;
		return true;
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

#include "backend/metadataConfiguration.h"
#include "backend/metaCollection/metaPictureObject.h"

wxBitmap ibBackendPicture::CreatePicture(const ibPictureDescription& pictureDesc, const ibMetaData* metaData, const wxSize& size)
{
	if (pictureDesc.m_type == ibPictureType::eFromBackend) {
		return ibBackendPicture::GetPicture(pictureDesc.m_class_identifier);
	}
	else if (pictureDesc.m_type == ibPictureType::eFromConfiguration) {
		if (metaData != nullptr && pictureDesc.m_meta_guid.isValid()) {
			ibMetaData* metaDataOwner = nullptr;
			if (metaData != nullptr && metaData->GetOwner(metaDataOwner) && metaDataOwner != nullptr) {
				const ibValueMetaObjectPicture* picture =
					metaDataOwner->FindAnyObjectByFilter<ibValueMetaObjectPicture>(pictureDesc.m_meta_guid);
				return picture != nullptr && picture->IsAllowed() ? picture->GetValueAsBitmap() : wxNullBitmap;
			}
			else if (metaData != nullptr) {
				const ibValueMetaObjectPicture* picture =
					metaData->FindAnyObjectByFilter<ibValueMetaObjectPicture>(pictureDesc.m_meta_guid);
				return picture != nullptr && picture->IsAllowed() ? picture->GetValueAsBitmap() : wxNullBitmap;
			}
		}
	}
	else if (pictureDesc.m_type == ibPictureType::eFromFile) {
		return ibBackendPicture::CreatePicture(pictureDesc.m_img_data, size);
	}

	return wxNullBitmap;
}

#pragma region __picture_factory_h__
bool ibBackendPicture::IsRegisterPicture(const ibPictureID& id)
{
	auto iterator = std::find_if(s_arrayPicture.begin(), s_arrayPicture.end(),
		[id](const auto entry) { return entry.m_id == id; });

	if (iterator != s_arrayPicture.end())
		return true;

	const ibCtorAbstractType* so = ibValue::GetAvailableCtor(id);
	if (so != nullptr && (so->GetObjectTypeCtor() == ibCtorObjectType_object_control || so->GetObjectTypeCtor() == ibCtorObjectType_object_metadata))
		return true;

	return false;
}

void ibBackendPicture::RegisterPicture(const wxString name, const ibPictureID& id, const wxBitmap& bitmap)
{
	ibBackendPictureEntry entry;
	entry.m_name = name;
	entry.m_id = id;
	entry.m_data = bitmap;
	s_arrayPicture.push_back(entry);
}

wxBitmap ibBackendPicture::GetPicture(const ibPictureID& id)
{
	auto iterator = std::find_if(s_arrayPicture.begin(), s_arrayPicture.end(),
		[id](const auto entry) { return entry.m_id == id; });

	if (iterator != s_arrayPicture.end())
		return iterator->m_data;

	const ibCtorAbstractType* so = ibValue::GetAvailableCtor(id);
	if (so != nullptr)
		return so->GetClassIcon();
	
	return wxNullBitmap;
}

wxIcon ibBackendPicture::GetPictureAsIcon(const ibPictureID& id)
{
	auto iterator = std::find_if(s_arrayPicture.begin(), s_arrayPicture.end(),
		[id](const auto entry) { return entry.m_id == id; });

	if (iterator != s_arrayPicture.end()) {	
		wxIcon icon; 
		icon.CopyFromBitmap(iterator->m_data);
		return icon;
	}

	const ibCtorAbstractType* so = ibValue::GetAvailableCtor(id);
	if (so != nullptr)
		return so->GetClassIcon();
	
	return wxNullIcon;
}

std::vector<ibBackendPictureEntry> ibBackendPicture::GetArrayPicture()
{
	std::vector<ibBackendPictureEntry> arrayPicture = { s_arrayPicture };

	for (auto so : ibValue::GetListCtorsByType(ibCtorObjectType::ibCtorObjectType_object_metadata)) {
		const wxIcon backend_icon = so->GetClassIcon();
		if (backend_icon.IsOk()) {
			ibBackendPictureEntry entry;
			entry.m_name = so->GetClassName();
			entry.m_id = so->GetClassType();
			entry.m_data = backend_icon;
			arrayPicture.push_back(entry);
		}
	}

	for (auto so : ibValue::GetListCtorsByType(ibCtorObjectType::ibCtorObjectType_object_control)) {
		const wxIcon backend_icon = so->GetClassIcon();
		if (backend_icon.IsOk()) {
			ibBackendPictureEntry entry;
			entry.m_name = so->GetClassName();
			entry.m_id = so->GetClassType();
			entry.m_data = backend_icon;
			arrayPicture.push_back(entry);
		}
	}

	return arrayPicture;
}
#pragma endregion
#pragma region __picture_conv_h__
wxString ibBackendPicture::CreateBase64Image(const wxImage& image) {
	wxMemoryOutputStream outputStream;
	if (image.SaveFile(outputStream, wxBitmapType::wxBITMAP_TYPE_PNG)) {
		const wxStreamBuffer* buffer =
			outputStream.GetOutputStreamBuffer();
		return wxBase64Encode(buffer->GetBufferStart(), buffer->GetLastAccess());
	}
	return wxEmptyString;
}

wxImage ibBackendPicture::GetImageFromBase64(const wxString& src, const wxSize& size) {
	const wxMemoryBuffer buffer = wxBase64Decode(src);
	wxMemoryInputStream stream(buffer.GetData(), buffer.GetDataLen());
	wxImage image(stream);
	return size != wxDefaultSize ? image.Scale(
		size.x, size.y, wxIMAGE_QUALITY_HIGH) : image;
}

wxBitmap ibBackendPicture::GetBitmapFromBase64(const wxString& src, const wxSize& size)
{
	return GetImageFromBase64(src, size);
}

wxIcon ibBackendPicture::GetIconFromBase64(const wxString& src, const wxSize& size)
{
	wxIcon icon;
	icon.CopyFromBitmap(GetBitmapFromBase64(src, size));
	return icon;
}
#pragma endregion 

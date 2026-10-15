#include "backend_picture.h"

#include <map>
#include <mutex>

#include <wx/base64.h>
#include <wx/filename.h>
#include <wx/mstream.h>

#include "core/pictureRegistry.h"   // the registry — each picture kept as the server keeps it

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
		// The string is a MASTER, drawn larger than it is shown (tools/pictures/render.js); a registered picture is
		// handed out as it is kept, and every reader of one shows it at 16 - so it is kept at 16, scaled down once here.
		ibBackendPicture::RegisterPicture(name, id, ibBackendPicture::GetImageFromBase64(base64, wxSize(16, 16)));
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

// THE ENGINE'S OWN PICTURES are the core's, registered as it loads (core/picturePredefined.cpp); here only the image
// handlers that read them.
class ibBackendPictureAutoLoader {
public:

	ibBackendPictureAutoLoader() {

		// Init handlers
		wxInitAllImageHandlers();
	}

	~ibBackendPictureAutoLoader()
	{
		// WHAT THIS OBJECT CREATED, IT LETS GO OF. `wxInitAllImageHandlers` puts one handler per
		// format on wx's own list, and in an application wxApp takes them down at exit - but a test
		// binary has no wxApp, so they stayed, and LeakSanitizer reported them on every process
		// exit (2026-09-22). The empty body was not "nothing to do": it was the half of a pair
		// that nobody had needed to write while the only host cleaned up after us.
		wxImage::CleanUpHandlers();
	}
};

ibBackendPictureAutoLoader m_autoLoader;

////////////////////////////////////////////////////////////////////////////////////////////////////////

bool ibBackendPictureDescriptionMemory::ReadNode(const ibDataValue& value, ibPictureDescription& pictureDesc)
{
	// READ WHOLE, THEN TAKEN — a refusal below leaves the description it was reading into as it was.
	ibPictureDescription read;
	if (!ibPictureDescriptionMemory::ReadNode(value, read))
		return false;

	// 🛑 A PICTURE THE ENGINE HAS, ASKED OF THE PICTURE FACTORY — 0 is the empty picture and is one.
	// Any other id nobody registered was taken without a word and drew nothing (measured 2026-09-22: a
	// rounded Print id, sent back through `value`). On loading it refuses only what could not be drawn
	// here anyway: the pictures register when the DLLs load, before any configuration is read, and a
	// process without the front's controls (the daemon) never writes a configuration back.
	if (read.m_type == ibPictureType::eFromBackend && read.m_class_identifier != 0
		&& !ibBackendPicture::IsRegisterPicture(read.m_class_identifier))
		return false;

	pictureDesc = read;
	return true;
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

ibPicture ibBackendPicture::GetServerPicture(const ibPictureDescription& pictureDesc, const ibMetaData* metaData)
{
	if (pictureDesc.IsEmptyPicture())
		return ibPicture();

	// A backend picture is the process's: made at its first asking at the size it is shown (a registered one is a
	// master), under the lock, and only read after that.
	if (pictureDesc.m_type == ibPictureType::eFromBackend) {
		static std::mutex s_madeMutex;
		static std::map<ibPictureID, ibPicture> s_made;
		const std::lock_guard<std::mutex> lock(s_madeMutex);
		const auto found = s_made.find(pictureDesc.m_class_identifier);
		if (found != s_made.end())
			return found->second;
		const wxBitmap bitmap = GetPicture(pictureDesc.m_class_identifier);
		if (bitmap.IsOk() || metaData == nullptr)
			return s_made.emplace(pictureDesc.m_class_identifier,
				bitmap.IsOk() ? FromImage(bitmap.ConvertToImage()) : ibPicture()).first->second;
		// …a type of the configuration's own (a catalog's reference) — its class's icon, as the configuration's
		// constructor of it gives it: made anew, the configuration's only.
		const ibCtorAbstractType* const ctor = metaData->GetAvailableCtor(pictureDesc.m_class_identifier);
		const wxIcon icon = ctor != nullptr ? ctor->GetClassIcon() : wxNullIcon;
		return icon.IsOk() ? FromImage(wxBitmap(icon).ConvertToImage()) : ibPicture();
	}

	// A configuration's picture and a file's are made from the bytes they keep — anew, shared with nobody.
	const wxBitmap bitmap = CreatePicture(pictureDesc, metaData);
	return bitmap.IsOk() ? FromImage(bitmap.ConvertToImage()) : ibPicture();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma region __picture_factory_h__
bool ibBackendPicture::IsRegisterPicture(const ibPictureID& id)
{
	if (ibPictureRegistry::IsRegisterPicture(id))
		return true;

	const ibCtorAbstractType* so = ibValue::GetAvailableCtor(id);
	if (so != nullptr && (so->GetObjectTypeCtor() == ibCtorObjectType_object_control || so->GetObjectTypeCtor() == ibCtorObjectType_object_metadata))
		return true;

	return false;
}

void ibBackendPicture::RegisterPicture(const wxString name, const ibPictureID& id, const wxBitmap& bitmap)
{
	ibPictureRegistry::RegisterPicture(name, id, FromImage(bitmap.ConvertToImage()));
}

wxBitmap ibBackendPicture::GetPicture(const ibPictureID& id)
{
	const ibPicture registered = ibPictureRegistry::GetPicture(id);
	if (registered.IsOk())
		return GetBitmapFromBase64(registered.GetData(), wxSize(16, 16));

	const ibCtorAbstractType* so = ibValue::GetAvailableCtor(id);
	if (so != nullptr)
		return so->GetClassIcon();
	
	return wxNullBitmap;
}

wxIcon ibBackendPicture::GetPictureAsIcon(const ibPictureID& id)
{
	const ibPicture registered = ibPictureRegistry::GetPicture(id);
	if (registered.IsOk())
		return GetIconFromBase64(registered.GetData(), wxSize(16, 16));

	const ibCtorAbstractType* so = ibValue::GetAvailableCtor(id);
	if (so != nullptr)
		return so->GetClassIcon();
	
	return wxNullIcon;
}

std::vector<ibBackendPictureEntry> ibBackendPicture::GetArrayPicture()
{
	std::vector<ibBackendPictureEntry> arrayPicture;
	for (const ibPictureEntry& registered : ibPictureRegistry::GetArrayPicture())
		arrayPicture.push_back({ registered.m_name, GetBitmapFromBase64(registered.m_picture.GetData(), wxSize(16, 16)), registered.m_id });

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

ibPicture ibBackendPicture::FromImage(const wxImage& image)
{
	return image.IsOk() ? ibPicture(CreateBase64Image(image)) : ibPicture();
}

wxBitmap ibBackendPicture::ToBitmap(const ibPicture& picture)
{
	return picture.IsOk() ? wxBitmap(GetImageFromBase64(picture.GetData())) : wxNullBitmap;
}

wxIcon ibBackendPicture::ToIcon(const ibPicture& picture)
{
	wxIcon icon;
	if (picture.IsOk())
		icon.CopyFromBitmap(ToBitmap(picture));
	return icon;
}
#pragma endregion 

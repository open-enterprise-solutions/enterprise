#include "backend_picture.h"

#include <map>
#include <mutex>

#include <wx/file.h>
#include <wx/filename.h>

#include "core/serialize/dataBuilder.h"     // ibDataValue — a picture as an answer names it

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

bool ibBackendPicture::LoadFromFile(const ibString& strFileName, ibExternalPictureDescription& pictureDesc)
{
	const wxFileName filename(strFileName);
	if (!filename.FileExists())
		return false;

	wxFile file(filename.GetFullPath());
	const wxFileOffset length = file.IsOpened() ? file.Length() : wxInvalidOffset;
	if (length <= 0)
		return false;
	std::string bytes(static_cast<size_t>(length), '\0');
	if (file.Read(bytes.data(), bytes.size()) != static_cast<ssize_t>(bytes.size()))
		return false;

	const ibSize size = ibPicture(ibMemoryBuffer(bytes.data(), bytes.size())).GetSize();
	if (size.m_width <= 0 || size.m_height <= 0)
		return false;

	pictureDesc.m_img_name = filename.GetFullPath();
	pictureDesc.m_img_buffer = std::move(bytes);
	pictureDesc.m_width = size.m_width;
	pictureDesc.m_height = size.m_height;
	return true;
}

bool ibBackendPicture::LoadFromFile(const ibString& strFileName, ibPictureDescription& pictureDesc)
{
	if (ibBackendPicture::LoadFromFile(strFileName, pictureDesc.m_img_data)) {
		pictureDesc.m_type = ibPictureType::eFromFile;
		return true;
	}

	return false;
}

ibPicture ibBackendPicture::CreatePicture(const ibExternalPictureDescription& pictureDesc, const ibSize& size)
{
	if (pictureDesc.m_img_buffer.empty())
		return ibPicture();
	return ibPicture(ibMemoryBuffer(pictureDesc.m_img_buffer.data(), pictureDesc.m_img_buffer.size())).Scale(size);
}

#include "backend/metadataConfiguration.h"
#include "backend/metaCollection/metaPictureObject.h"

// A CONFIGURATION'S PICTURE, where the configuration keeps it: in the one it extends when it extends one, else its own.
static const ibValueMetaObjectPicture* FindConfigurationPicture(const ibMetaData& metaData, const ibGuid& guid)
{
	ibMetaData* metaDataOwner = nullptr;
	const ibMetaData& source = metaData.GetOwner(metaDataOwner) && metaDataOwner != nullptr ? *metaDataOwner : metaData;
	return source.FindAnyObjectByFilter<ibValueMetaObjectPicture>(guid);
}

ibPicture ibBackendPicture::CreatePicture(const ibPictureDescription& pictureDesc, const ibMetaData* metaData, const ibSize& size)
{
	if (pictureDesc.m_type == ibPictureType::eFromBackend) {
		return ibBackendPicture::GetPicture(pictureDesc.m_class_identifier);
	}
	else if (pictureDesc.m_type == ibPictureType::eFromConfiguration) {
		if (metaData != nullptr && pictureDesc.m_meta_guid.isValid()) {
			const ibValueMetaObjectPicture* picture = FindConfigurationPicture(*metaData, pictureDesc.m_meta_guid);
			return picture != nullptr && picture->IsAllowed() ? picture->GetValueAsPicture() : ibPicture();
		}
	}
	else if (pictureDesc.m_type == ibPictureType::eFromFile) {
		return ibBackendPicture::CreatePicture(pictureDesc.m_img_data, size);
	}

	return ibPicture();
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
		const ibPicture picture = GetPicture(pictureDesc.m_class_identifier);
		if (picture.IsOk() || metaData == nullptr)
			return s_made.emplace(pictureDesc.m_class_identifier, picture).first->second;
		// …a type of the configuration's own (a catalog's reference) — its class's icon, as the configuration's
		// constructor of it gives it: made anew, the configuration's only.
		const ibCtorAbstractType* const ctor = metaData->GetAvailableCtor(pictureDesc.m_class_identifier);
		return ctor != nullptr ? ctor->GetClassIcon() : ibPicture();
	}

	// A configuration's picture and a file's are made from the bytes they keep — anew, shared with nobody.
	return CreatePicture(pictureDesc, metaData);
}

ibDataValue ibBackendPicture::SendPicture(const ibPictureDescription& pictureDesc, const ibMetaData* metaData)
{
	if (pictureDesc.IsEmptyPicture())
		return ibDataValue();
	// A picture held as a file's bytes has no name of its own: it goes as itself.
	if (pictureDesc.m_type == ibPictureType::eFromFile) {
		const ibPicture picture = GetServerPicture(pictureDesc, metaData);
		return picture.IsOk() ? ibDataValue::Binary(picture.GetData()) : ibDataValue();
	}

	if (pictureDesc.m_type == ibPictureType::eFromBackend) {
		const ibString id = SendPicture(pictureDesc.m_class_identifier);
		return !id.IsEmpty() ? ibDataValue::String(id) : ibDataValue();
	}

	// A configuration picture's guid and the version of what it holds now: a client keeps a picture by its guid with the
	// version it was given, and asks anew for one named by another. One no configuration has — none given, or the
	// one given has none of it — is no picture.
	const ibValueMetaObjectPicture* const picture =
		metaData != nullptr ? FindConfigurationPicture(*metaData, pictureDesc.m_meta_guid) : nullptr;
	if (picture == nullptr)
		return ibDataValue();
	return ibDataValue::String(pictureDesc.m_meta_guid.str() + ibT('#') + picture->GetValueAsVersion().str());
}

ibString ibBackendPicture::SendPicture(const ibPictureID& id)
{
	return id != 0 ? ibString::Format(ibT("%llu"), static_cast<unsigned long long>(id)) : ibString();
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

ibPicture ibBackendPicture::GetPicture(const ibPictureID& id)
{
	const ibPicture registered = ibPictureRegistry::GetPicture(id);
	if (registered.IsOk())
		return registered.Scale(ibSize(16, 16));

	const ibCtorAbstractType* so = ibValue::GetAvailableCtor(id);
	if (so != nullptr)
		return so->GetClassIcon();

	return ibPicture();
}

std::vector<ibPictureEntry> ibBackendPicture::GetArrayPicture()
{
	std::vector<ibPictureEntry> arrayPicture;
	for (const ibPictureEntry& registered : ibPictureRegistry::GetArrayPicture())
		arrayPicture.push_back({ registered.m_name, registered.m_id, registered.m_picture.Scale(ibSize(16, 16)) });

	for (auto so : ibValue::GetListCtorsByType(ibCtorObjectType::ibCtorObjectType_object_metadata)) {
		const ibPicture icon = so->GetClassIcon();
		if (icon.IsOk())
			arrayPicture.push_back({ so->GetClassName(), so->GetClassType(), icon });
	}

	for (auto so : ibValue::GetListCtorsByType(ibCtorObjectType::ibCtorObjectType_object_control)) {
		const ibPicture icon = so->GetClassIcon();
		if (icon.IsOk())
			arrayPicture.push_back({ so->GetClassName(), so->GetClassType(), icon });
	}

	return arrayPicture;
}
#pragma endregion

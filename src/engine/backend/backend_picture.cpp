#include "backend_picture.h"

#include <fstream>

#include "core/fileSystem.h"                // ibFileExists, ibPathOf - a picture's file
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
	// here anyway: the pictures register when the DLLs load, before any application is read, and a
	// process without the front's controls (the daemon) never writes an application back.
	if (read.m_type == ibPictureType::eFromBackend && read.m_class_identifier != 0
		&& !ibBackendPicture::IsRegisterPicture(read.m_class_identifier))
		return false;

	pictureDesc = read;
	return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////

bool ibBackendPicture::LoadFromFile(const ibString& strFileName, ibExternalPictureDescription& pictureDesc)
{
	if (!ibFileExists(strFileName))
		return false;

	std::filesystem::path path = ibPathOf(strFileName);
	std::ifstream file(path, std::ios::binary);
	file.seekg(0, std::ios::end);
	const std::streamoff length = file ? static_cast<std::streamoff>(file.tellg()) : -1;
	if (length <= 0)
		return false;
	file.seekg(0, std::ios::beg);
	std::string bytes(static_cast<size_t>(length), '\0');
	if (!file.read(bytes.data(), static_cast<std::streamsize>(bytes.size())))
		return false;

	const ibSize size = ibPicture(ibMemoryBuffer(bytes.data(), bytes.size())).GetSize();
	if (size.m_width <= 0 || size.m_height <= 0)
		return false;

	pictureDesc.m_img_name = ibTextOf(path.make_preferred()).ToUtf8();   // the separators this system writes, as wxFileName did
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

#include "backend/metadataApplication.h"
#include "backend/metaCollection/metaPictureObject.h"

// AN APPLICATION'S PICTURE, where the application keeps it: in the one it extends when it extends one, else its own.
static const ibValueMetaObjectPicture* FindApplicationPicture(const ibMetaData& metaData, const ibGuid& guid)
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
	else if (pictureDesc.m_type == ibPictureType::eFromApplication) {
		if (metaData != nullptr && pictureDesc.m_meta_guid.isValid()) {
			const ibValueMetaObjectPicture* picture = FindApplicationPicture(*metaData, pictureDesc.m_meta_guid);
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

	// A backend picture is the process's: the core's drawing, or a class's icon — each kept once where it is made, and
	// handed out as it is.
	if (pictureDesc.m_type == ibPictureType::eFromBackend) {
		const ibPicture picture = GetPicture(pictureDesc.m_class_identifier);
		if (picture.IsOk() || metaData == nullptr)
			return picture;
		// …a type of the application's own (a catalog's reference) — its class's icon, as the application's
		// constructor of it gives it: made anew, the application's only.
		const ibCtorAbstractType* const ctor = metaData->GetAvailableCtor(pictureDesc.m_class_identifier);
		return ctor != nullptr ? ctor->GetClassIcon() : ibPicture();
	}

	// An application's picture and a file's are made from the bytes they keep — anew, shared with nobody.
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

	// An application picture's guid and the version of what it holds now: a client keeps a picture by its guid with the
	// version it was given, and asks anew for one named by another. One no application has — none given, or the
	// one given has none of it — is no picture.
	const ibValueMetaObjectPicture* const picture =
		metaData != nullptr ? FindApplicationPicture(*metaData, pictureDesc.m_meta_guid) : nullptr;
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
		return registered;

	const ibCtorAbstractType* so = ibValue::GetAvailableCtor(id);
	if (so != nullptr)
		return so->GetClassIcon();

	return ibPicture();
}

std::vector<ibPictureEntry> ibBackendPicture::GetArrayPicture()
{
	std::vector<ibPictureEntry> arrayPicture = ibPictureRegistry::GetArrayPicture();

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

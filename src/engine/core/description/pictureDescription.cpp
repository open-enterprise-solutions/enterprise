#include "pictureDescription.h"
#include "core/serialize/dataBuilder.h"   // ibDataNode / ibDataValue — node form

////////////////////////////////////////////////////////////////////////

bool ibExternalPictureDescriptionMemory::ReadNode(const ibDataValue& value, ibExternalPictureDescription& pictureDesc)
{
	const auto root = value.AsChild();
	if (!root)
		return false;
	const ibString name = root->GetValue<ibString>(ibT("Name"));
	pictureDesc.m_img_name = std::string(name.utf8_str());
	const ibMemoryBuffer buf = root->GetValue<ibMemoryBuffer>(ibT("Buffer"));
	pictureDesc.m_img_buffer.assign((const char*)buf.GetData(), buf.GetDataLen());
	pictureDesc.m_width  = (unsigned int)root->GetValue<std::int32_t>(ibT("Width"));
	pictureDesc.m_height = (unsigned int)root->GetValue<std::int32_t>(ibT("Height"));
	return true;
}

bool ibExternalPictureDescriptionMemory::WriteNode(ibDataValue& value, const ibExternalPictureDescription& pictureDesc)
{
	auto root = std::make_shared<ibDataNode>();
	root->SetValue(ibT("Name"), ibString::FromUTF8(pictureDesc.m_img_name.c_str(), pictureDesc.m_img_name.size()));
	ibMemoryBuffer buf;
	if (!pictureDesc.m_img_buffer.empty())
		buf.AppendData(pictureDesc.m_img_buffer.data(), pictureDesc.m_img_buffer.size());
	root->SetValue(ibT("Buffer"), buf);
	root->SetValue(ibT("Width"),  (std::int32_t)pictureDesc.m_width);
	root->SetValue(ibT("Height"), (std::int32_t)pictureDesc.m_height);
	value = ibDataValue::Child(root);
	return true;
}

bool ibPictureDescriptionMemory::ReadNode(const ibDataValue& value, ibPictureDescription& pictureDesc)
{
	const auto root = value.AsChild();
	if (!root)
		return false;

	// READ WHOLE, THEN TAKEN — a refusal below leaves the description it was reading into as it was,
	// rather than with a new Type over the old id.
	ibPictureDescription read;
	read.m_type = (ibPictureType)root->GetValue<std::int32_t>(ibT("Type"));
	if (read.m_type == ibPictureType::eFromBackend) {
		// A NUMBER, both ways. Sixty-four bits do not survive a JSON number, so the MCP door hands the id
		// over as digits in a string and takes it back the same way (mcpTool.h, ibMcpPictureIdAsText /
		// AsNumber) — the door's difficulty, kept at the door: this is the shape a picture is SAVED in,
		// and a transport has no business in it.
		if (auto id = root->FindField(ibT("ClassId")))
			read.m_class_identifier = (ibPictureID)id->AsUInt();
	}
	else if (read.m_type == ibPictureType::eFromConfiguration) {
		read.m_meta_guid = root->GetValue<ibGuid>(ibT("Guid"));
	}
	else if (read.m_type == ibPictureType::eFromFile) {
		ibExternalPictureDescriptionMemory::ReadNode(root->GetProperty(ibT("Image")), read.m_img_data);
	}
	else {
		return false;   // a Type the description does not have
	}

	pictureDesc = read;
	return true;
}

bool ibPictureDescriptionMemory::WriteNode(ibDataValue& value, const ibPictureDescription& pictureDesc)
{
	auto root = std::make_shared<ibDataNode>();
	root->SetValue(ibT("Type"), (std::int32_t)pictureDesc.m_type);
	if (pictureDesc.m_type == ibPictureType::eFromBackend) {
		root->AddField(ibT("ClassId"), ibDataValue::UInt(pictureDesc.m_class_identifier));
	}
	else if (pictureDesc.m_type == ibPictureType::eFromConfiguration) {
		root->SetValue(ibT("Guid"), pictureDesc.m_meta_guid);
	}
	else if (pictureDesc.m_type == ibPictureType::eFromFile) {
		ibDataValue imageVal;
		ibExternalPictureDescriptionMemory::WriteNode(imageVal, pictureDesc.m_img_data);
		root->SetProperty(ibT("Image"), imageVal);
	}
	value = ibDataValue::Child(root);
	return true;
}
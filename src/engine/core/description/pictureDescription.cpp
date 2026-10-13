#include "pictureDescription.h"
#include "core/serialize/dataBuilder.h"   // ibDataNode / ibDataValue — node form

////////////////////////////////////////////////////////////////////////

bool ibExternalPictureDescriptionMemory::ReadNode(const ibDataValue& value, ibExternalPictureDescription& pictureDesc)
{
	const std::shared_ptr<ibDataNode>& root = value.AsChild();
	if (!root)
		return false;
	const wxString name = root->GetValue<wxString>(wxT("Name"));
	pictureDesc.m_img_name = std::string(name.utf8_str());
	const wxMemoryBuffer buf = root->GetValue<wxMemoryBuffer>(wxT("Buffer"));
	pictureDesc.m_img_buffer.assign((const char*)buf.GetData(), buf.GetDataLen());
	pictureDesc.m_width  = (unsigned int)root->GetValue<s32>(wxT("Width"));
	pictureDesc.m_height = (unsigned int)root->GetValue<s32>(wxT("Height"));
	return true;
}

bool ibExternalPictureDescriptionMemory::WriteNode(ibDataValue& value, const ibExternalPictureDescription& pictureDesc)
{
	auto root = std::make_shared<ibDataNode>();
	root->SetValue(wxT("Name"), wxString::FromUTF8(pictureDesc.m_img_name.c_str(), pictureDesc.m_img_name.size()));
	wxMemoryBuffer buf;
	if (!pictureDesc.m_img_buffer.empty())
		buf.AppendData(pictureDesc.m_img_buffer.data(), pictureDesc.m_img_buffer.size());
	root->SetValue(wxT("Buffer"), buf);
	root->SetValue(wxT("Width"),  (s32)pictureDesc.m_width);
	root->SetValue(wxT("Height"), (s32)pictureDesc.m_height);
	value = ibDataValue::Child(root);
	return true;
}

bool ibPictureDescriptionMemory::ReadNode(const ibDataValue& value, ibPictureDescription& pictureDesc)
{
	const std::shared_ptr<ibDataNode>& root = value.AsChild();
	if (!root)
		return false;

	// READ WHOLE, THEN TAKEN — a refusal below leaves the description it was reading into as it was,
	// rather than with a new Type over the old id.
	ibPictureDescription read;
	read.m_type = (ibPictureType)root->GetValue<s32>(wxT("Type"));
	if (read.m_type == ibPictureType::eFromBackend) {
		// A NUMBER, both ways. Sixty-four bits do not survive a JSON number, so the MCP door hands the id
		// over as digits in a string and takes it back the same way (mcpTool.h, ibMcpPictureIdAsText /
		// AsNumber) — the door's difficulty, kept at the door: this is the shape a picture is SAVED in,
		// and a transport has no business in it.
		if (const ibDataValue* id = root->FindField(wxT("ClassId")))
			read.m_class_identifier = (ibPictureID)id->AsUInt();
	}
	else if (read.m_type == ibPictureType::eFromConfiguration) {
		read.m_meta_guid = root->GetValue<ibGuid>(wxT("Guid"));
	}
	else if (read.m_type == ibPictureType::eFromFile) {
		ibExternalPictureDescriptionMemory::ReadNode(root->GetProperty(wxT("Image")), read.m_img_data);
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
	root->SetValue(wxT("Type"), (s32)pictureDesc.m_type);
	if (pictureDesc.m_type == ibPictureType::eFromBackend) {
		root->AddField(wxT("ClassId"), ibDataValue::UInt(pictureDesc.m_class_identifier));
	}
	else if (pictureDesc.m_type == ibPictureType::eFromConfiguration) {
		root->SetValue(wxT("Guid"), pictureDesc.m_meta_guid);
	}
	else if (pictureDesc.m_type == ibPictureType::eFromFile) {
		ibDataValue imageVal;
		ibExternalPictureDescriptionMemory::WriteNode(imageVal, pictureDesc.m_img_data);
		root->SetProperty(wxT("Image"), imageVal);
	}
	value = ibDataValue::Child(root);
	return true;
}
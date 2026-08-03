#ifndef __IMAGE_HELPER_H__
#define __IMAGE_HELPER_H__

#include "backend/compiler/value.h"

enum ibPictureType
{
	eFromBackend = 1,
	eFromConfiguration,
	eFromFile,
};

struct ibExternalPictureDescription {

	bool IsEmptyPicture() const { return m_height == 0 && m_width == 0; }

	bool operator ==(const ibExternalPictureDescription& rhs) const {
		return m_img_buffer == rhs.m_img_buffer
			&& m_width == rhs.m_width
			&& m_height == rhs.m_height;
	}

	std::string m_img_name, m_img_buffer;
	unsigned int m_width = 0, m_height = 0;
};

class BACKEND_API ibExternalPictureDescriptionMemory {
public:
	// node form: a Child { Name, Buffer(image bytes), Width, Height }
	static bool ReadNode(const class ibDataValue& value, ibExternalPictureDescription& pictureDesc);
	static bool WriteNode(class ibDataValue& value, const ibExternalPictureDescription& pictureDesc);
};

struct ibPictureDescription {

	ibPictureType m_type;

	ibPictureDescription() : m_type(eFromBackend) {}
	ibPictureDescription(const ibPictureID& id) : m_type(eFromBackend), m_class_identifier(id) {}
	ibPictureDescription(const ibGuid& id) : m_type(eFromConfiguration), m_meta_guid(id) {}
	ibPictureDescription(const ibExternalPictureDescription& data) : m_type(eFromFile), m_img_data(data) {}
	
	~ibPictureDescription() {}

	bool IsEmptyPicture() const {
		if (m_type == eFromBackend)
			return m_class_identifier == 0;
		else if (m_type == eFromConfiguration)
			return !m_meta_guid.isValid();
		else if (m_type == eFromFile)
			return m_img_data.m_height == 0 && m_img_data.m_width == 0;
		return true;
	}

	bool operator ==(const ibPictureDescription& rhs) const
	{
		if (m_type == rhs.m_type) {
			if (m_type == eFromBackend) {
				return m_class_identifier == rhs.m_class_identifier;
			}
			else if (m_type == eFromConfiguration) {
				return m_meta_guid == rhs.m_meta_guid;
			}
			else if (m_type == eFromFile) {
				return m_img_data.m_img_buffer == rhs.m_img_data.m_img_buffer
					&& m_img_data.m_width == rhs.m_img_data.m_width
					&& m_img_data.m_height == rhs.m_img_data.m_height;
			}
		}

		return false;
	}

	ibPictureID m_class_identifier = 0;
	ibGuid m_meta_guid;
	ibExternalPictureDescription m_img_data;
};

class BACKEND_API ibPictureDescriptionMemory {
public:
	// node form: a Child { Type, then per type ClassId / Guid / Image(Child) }
	static bool ReadNode(const class ibDataValue& value, ibPictureDescription& pictureDesc);
	static bool WriteNode(class ibDataValue& value, const ibPictureDescription& pictureDesc);
};

// Initialize with XPM data
extern BACKEND_API bool RegisterBackendPicture(const wxString name,
	const ibPictureID& id, const char* const* data);

// Initialize with Base-64 + PNG data
extern BACKEND_API bool RegisterBackendPicture(const wxString name,
	const ibPictureID& id, const wxString& base64);

// Initialize with BMP
extern BACKEND_API bool RegisterBackendPicture(const wxString name,
	const ibPictureID& id, const wxBitmap& bitmap);
#endif 

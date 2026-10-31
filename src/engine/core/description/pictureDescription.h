#ifndef __IMAGE_HELPER_H__
#define __IMAGE_HELPER_H__

#include "core/core.h"    // CORE_API, ibPictureID
#include "core/guid.h"    // ibGuid — a configuration's picture

#include <string>

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

class CORE_API ibExternalPictureDescriptionMemory {
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

class CORE_API ibPictureDescriptionMemory {
public:
	// node form: a Child { Type, then per type ClassId / Guid / Image(Child) }. The id as it is held,
	// nothing asked of anyone: whether the engine has that picture is asked by whoever reads it.
	static bool ReadNode(const class ibDataValue& value, ibPictureDescription& pictureDesc);
	static bool WriteNode(class ibDataValue& value, const ibPictureDescription& pictureDesc);
};

#endif

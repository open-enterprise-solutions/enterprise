#ifndef _CATALOG_DATA_H_
#define _CATALOG_DATA_H_

#include "metadata/metadata.h"

struct ownerData_t {
	std::set<meta_identifier_t> m_data;
public:

	bool LoadData(CMemoryReader& dataReader);
	bool LoadFromVariant(const wxVariant& variant);
	bool SaveData(CMemoryWriter& dataWritter);
	void SaveToVariant(wxVariant& variant, IMetadata* metaData) const;
};

#endif
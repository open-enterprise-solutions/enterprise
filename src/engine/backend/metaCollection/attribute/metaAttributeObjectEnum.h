#ifndef __ATTRIBUTE_OBJECT_ENUM_H__
#define __ATTRIBUTE_OBJECT_ENUM_H__

enum ibItemMode {
	ibItemMode_Item,
	ibItemMode_Folder,
	ibItemMode_Folder_Item
};

enum ibSelectMode {
	ibSelectMode_Items = 1,
	ibSelectMode_Folders,
	ibSelectMode_FoldersAndItems
};

// Attribute indexing: a DB-level secondary index on the attribute for faster WHERE / JOIN /
// list filtering. WithAdditionalOrder appends the row reference to the index so list browsing
// (dynamic lists) is ordered too. DontIndex is the default — index only what searches / joins
// on it, not booleans / low-cardinality fields (an index slows writes and grows the DB).
enum ibIndexingMode {
	ibIndexingMode_DontIndex,
	ibIndexingMode_Index,
	ibIndexingMode_IndexWithAdditionalOrder
};

#pragma region enumeration
#include "backend/compiler/enumUnit.h"
class ibValueEnumItemMode : public ibValueEnumeration<ibItemMode> {
	public:
	ibValueEnumItemMode() : ibValueEnumeration() {}
	//ibValueEnumItemMode(const ibItemMode &mode) : ibValueEnumeration(mode) {}

	virtual void CreateEnumeration() {
		AddEnumeration(ibItemMode_Item, wxT("Items"), _("Items"));
		AddEnumeration(ibItemMode_Folder, wxT("Folders"), _("Folders"));
		AddEnumeration(ibItemMode_Folder_Item, wxT("FoldersAndItems"), _("Folders and items"));
	}
};

class ibValueEnumSelectMode : public ibValueEnumeration<ibSelectMode> {
	public:
	ibValueEnumSelectMode() : ibValueEnumeration() {}
	//ibValueEnumSelectMode(const ibSelectMode &mode) : ibValueEnumeration(mode) {}

	virtual void CreateEnumeration() {
		AddEnumeration(ibSelectMode_Items, wxT("Items"), _("Items"));
		AddEnumeration(ibSelectMode_Folders, wxT("Folders"), _("Folders"));
		AddEnumeration(ibSelectMode_FoldersAndItems, wxT("FoldersAndItems"), _("Folders and items"));
	}
};

class ibValueEnumIndexingMode : public ibValueEnumeration<ibIndexingMode> {
	public:
	ibValueEnumIndexingMode() : ibValueEnumeration() {}

	virtual void CreateEnumeration() {
		AddEnumeration(ibIndexingMode_DontIndex, wxT("DontIndex"), _("Don't index"));
		AddEnumeration(ibIndexingMode_Index, wxT("Index"), _("Index"));
		AddEnumeration(ibIndexingMode_IndexWithAdditionalOrder, wxT("IndexWithAdditionalOrder"), _("Index with additional ordering"));
	}
};
#pragma endregion

#endif
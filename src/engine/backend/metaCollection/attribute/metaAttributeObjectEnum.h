#ifndef __ATTRIBUTE_OBJECT_ENUM_H__
#define __ATTRIBUTE_OBJECT_ENUM_H__

enum ibItemMode {
	ibItemMode_Item,
	ibItemMode_Folder,
	ibItemMode_Folder_Item
};

enum ibSelectMode : int {
	ibSelectMode_Items = 1,
	ibSelectMode_Folders,
	ibSelectMode_FoldersAndItems
};

#pragma region enumeration
#include "backend/compiler/enumUnit.h"
class ibValueEnumItemMode : public ibValueEnumeration<ibItemMode> {
	wxDECLARE_DYNAMIC_CLASS(ibValueEnumItemMode);
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
	wxDECLARE_DYNAMIC_CLASS(ibValueEnumSelectMode);
public:
	ibValueEnumSelectMode() : ibValueEnumeration() {}
	//ibValueEnumSelectMode(const ibSelectMode &mode) : ibValueEnumeration(mode) {}

	virtual void CreateEnumeration() {
		AddEnumeration(ibSelectMode_Items, wxT("Items"), _("Items"));
		AddEnumeration(ibSelectMode_Folders, wxT("Folders"), _("Folders"));
		AddEnumeration(ibSelectMode_FoldersAndItems, wxT("FoldersAndItems"), _("Folders and items"));
	}
};
#pragma endregion 

#endif
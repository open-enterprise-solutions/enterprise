#ifndef _COMMON_OBJECT_ENUM_H__
#define _COMMON_OBJECT_ENUM_H__

// WHAT A PARENT MAY BE — the shape of a hierarchy, declared by the metaobject that could have one.
//
// FOUR arrangements, and only two of them are hierarchies. Not a display preference: each answers
// both "is there a parent" and "does the platform navigate one".
//
//   None            — a FLAT list. No parent at all: the field is gone, not merely unused.
//   Subordination   — a parent that is DATA: the field exists, is filled and is shown like any other
//                     attribute. This is a chart of accounts — an account records which account it
//                     sits under ("subordinate to account"). No separate CONTAINER kind is declared,
//                     so a node becomes one by having children rather than by saying so.
//   FoldersAndItems — a catalog. Items live INSIDE folders; a folder is a container and an item is a
//                     leaf. Choosing a parent means choosing a folder.
//   Items           — every element may hold elements. No separate container kind: each node is the
//                     same sort of thing and the platform drills into any of them.
//
// ⭐⭐ THE HIERARCHY IS THE PARENT, and the line that matters is between `None` and the three above
// it. Three arrangements record a parent, and a recorded parent IS a hierarchy: something to walk up,
// something to fold down, something `IN HIERARCHY` and `TOTALS BY … HIERARCHY` can be asked about.
// `None` alone has nothing to ask — the field is gone, not merely unused, and in-hierarchy over such
// a source can only ever answer with the value itself.
//
// ⚠ Corrected 2026-08-13. `GetHierarchyColumn()` used to gate on the two the engine NAVIGATES, which
// made one accessor carry two questions under one null and left a chart of accounts answering "no
// hierarchy" to a query that asked about its parent. What still differs between these three is
// narrower than it was: whether there are FOLDERS (a second kind of node), and whether an item may be
// entered by declaration (`IsItemHierarchy`) rather than by turning out to have children.
//
// ⚠ The property is stored as its INTEGER, so these numbers are the wire. New members append.
enum ibHierarchyType {
	eFoldersAndItems = 0,
	eItems           = 1,
	eNone            = 2,   // the editor lists them in reading order (see CreateEnumeration), which is free
	eSubordination   = 3,
};

#pragma region enumeration
#include "backend/compiler/enumUnit.h"
class ibValueEnumHierarchyType : public ibValueEnumeration<ibHierarchyType> {
	public:
	static ibValue CreateDefEnumValue() {
		return ibValue::CreateEnumObject<ibValueEnumHierarchyType>(ibHierarchyType::eFoldersAndItems);
	}

	ibValueEnumHierarchyType() : ibValueEnumeration() {}

	virtual void CreateEnumeration() {
		// Reading order, not storage order: from no structure at all to the most structure. The labels
		// are ONE FAMILY — four names of the same shape, each naming the arrangement and nothing else.
		// A parenthetical ("subordination (no hierarchy)") is an explainer, and an explainer in one
		// label makes the other three look like they are missing theirs.
		AddEnumeration(ibHierarchyType::eNone, wxT("None"), _("No subordination"));
		AddEnumeration(ibHierarchyType::eSubordination, wxT("Subordination"), _("Subordination without hierarchy"));
		AddEnumeration(ibHierarchyType::eItems, wxT("Items"), _("Hierarchy of items"));
		AddEnumeration(ibHierarchyType::eFoldersAndItems, wxT("FoldersAndItems"), _("Hierarchy of folders and items"));
	}
};
constexpr ibClassID g_enumHierarchyTypeCLSID = enum_to_clsid("EN_HRTP");
#pragma endregion

#endif

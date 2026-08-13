#ifndef _COMMON_OBJECT_ENUM_H__
#define _COMMON_OBJECT_ENUM_H__

// WHAT A PARENT MAY BE — the shape of a hierarchy, declared by the metaobject that could have one.
//
// FOUR arrangements, and only two of them are hierarchies. Not a display preference: each answers
// both "is there a parent" and "does the platform navigate one".
//
//   None            — a FLAT list. No parent at all: the field is gone, not merely unused.
//   Subordination   — a parent that is DATA. The field exists, is filled and is shown like any other
//                     attribute, and the platform builds NOTHING on it: the list stays flat, nothing
//                     drills, nothing folds. Whoever wants the structure asks for it — a query, a
//                     grouping. This is a chart of accounts: an account records which account it
//                     sits under ("subordinate to account"), and that record is not a tree.
//   FoldersAndItems — a catalog. Items live INSIDE folders; a folder is a container and an item is a
//                     leaf. Choosing a parent means choosing a folder.
//   Items           — every element may hold elements. No separate container kind: each node is the
//                     same sort of thing and the platform drills into any of them.
//
// The line that matters is between Subordination and the two below it. All three keep the same
// column; what differs is whether the ENGINE reads it — the level fetch, the drill, a
// TotalBy(…, Hierarchy) unfolding. Recording a parent and navigating one are different claims, and
// a chart of accounts makes only the first.
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

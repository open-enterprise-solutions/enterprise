#ifndef _COMMON_OBJECT_ENUM_H__
#define _COMMON_OBJECT_ENUM_H__

// WHAT A PARENT MAY BE — `ibHierarchyType` (backend_core.h), where every tier can read it.
//
// ⭐⭐ THE HIERARCHY IS THE PARENT, and the line that matters is between `None` and the three above
// it. Three arrangements record a parent, and a recorded parent IS a hierarchy: something to walk up,
// something to fold down, something `IN HIERARCHY` and `TOTALS BY … HIERARCHY` can be asked about.
// `None` alone has nothing to ask — the field is gone, not merely unused, and in-hierarchy over such
// a source can only ever answer with the value itself.
//
// What still differs among the three is narrower: whether there are FOLDERS (a second kind of node),
// whether an item may be entered by declaration rather than by turning out to have children, and
// whether a LIST walks it at all (`Subordination` records a parent and stays flat).
//
// Below is the VALUE side — the enumeration a user picks from in the property editor.
#include "backend/backend_core.h"   // ibHierarchyType — the declaration itself

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

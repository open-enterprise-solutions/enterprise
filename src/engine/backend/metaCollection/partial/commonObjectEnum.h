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
// whether a LIST walks it at all (`ParentOnly`, no longer offered, records a parent and stays flat).
//
// Below is the VALUE side — the enumeration a user picks from in the property editor.
#include "backend/backend_core.h"   // ibHierarchyType — the declaration itself

#pragma region enumeration
#include "backend/compiler/enumUnit.h"
class ibValueEnumHierarchyType : public ibValueEnumeration<ibHierarchyType> {
	public:
	static ibValue CreateDefEnumValue() {
		return ibValue::CreateEnumObject<ibValueEnumHierarchyType>(ibHierarchyType::eFolders);
	}

	ibValueEnumHierarchyType() : ibValueEnumeration() {}

	virtual void CreateEnumeration() {
		// Reading order, not storage order: from no structure at all to the most structure. The labels
		// are ONE FAMILY — each names what the tree is made of and nothing else (Max, 2026-09-29: "the
		// hierarchy mode: folders, items, and no hierarchy"). A parenthetical is an explainer, and an
		// explainer in one label makes the others look like they are missing theirs.
		// (ParentOnly — a parent recorded, nothing built on it — is no longer offered; a stored one reads back
		//  as it was. backend_core.h.)
		AddEnumeration(ibHierarchyType::eNone, wxT("None"), _("No hierarchy"));
		AddEnumeration(ibHierarchyType::eItems, wxT("Items"), _("Items"));
		AddEnumeration(ibHierarchyType::eFolders, wxT("Folders"), _("Folders"));
	}
};
constexpr ibClassID g_enumHierarchyTypeCLSID = enum_to_clsid("EN_HRTP");

// HOW A REFERENCE TO AN ITEM READS, wherever it is shown - a field, a list, a report: by its Description
// or by its Code. A catalog is named by what it is called; a chart of accounts by its number, because
// that is how an accountant names an account (Max, 2026-09-16: "accountants give the account, not its
// name"). One declaration on the kind, read by the one template every presentation is built from.
enum ibDataPresentation {
	ibDataPresentation_Description = 1,
	ibDataPresentation_Code,
};

class ibValueEnumDataPresentation : public ibValueEnumeration<ibDataPresentation> {
	public:
	ibValueEnumDataPresentation() : ibValueEnumeration() {}

	virtual void CreateEnumeration() {
		AddEnumeration(ibDataPresentation_Description, wxT("Description"), wxGETTEXT_IN_CONTEXT("item name", "Description"));
		AddEnumeration(ibDataPresentation_Code, wxT("Code"), _("Code"));
	}
};
#pragma endregion

#endif

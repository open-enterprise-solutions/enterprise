#ifndef __METAOBJECT_ENUM_H__
#define __METAOBJECT_ENUM_H__

////////////////////////////////////////////////////////////////////////////
//	Description : enumerations owned by the BASE metaobject — the ones whose
//	              meaning is platform-wide rather than tied to one metatype.
//
//	Why this file exists. ibSelectMode used to live in attribute/metaAttributeObjectEnum.h,
//	but it is not an attribute concept: ibValueMetaObject::ProcessChoice takes it, and a
//	choice form is something any metaobject can raise. A base naming a type declared by one
//	of its descendants is backwards, and it showed: metaObject.h had to name it through an
//	elaborated `enum ibSelectMode` specifier with nothing declared in scope — accepted by
//	MSVC, rejected outright by GCC.
//
//	Keeping it HERE rather than inside metaObject.h is what keeps consumers cheap: the
//	frontend's type control needs the select mode and nothing else about metaobjects, so it
//	includes this (enumUnit.h only) instead of the whole base-metaobject header.
////////////////////////////////////////////////////////////////////////////

#include "backend/compiler/enumUnit.h"

// What a choice form is allowed to hand back — items, folders, or either. A hierarchical
// source is the one that can tell them apart; a flat one only ever answers with items.
enum ibSelectMode {
	ibSelectMode_Items = 1,
	ibSelectMode_Folders,
	ibSelectMode_FoldersAndItems
};

// The script-visible face of the same enum (registered as "SelectMode" —
// see metaObjectEnum.cpp).
class ibValueEnumSelectMode : public ibValueEnumeration<ibSelectMode> {
	public:
	ibValueEnumSelectMode() : ibValueEnumeration() {}

	virtual void CreateEnumeration() {
		AddEnumeration(ibSelectMode_Items, wxT("Items"), _("Items"));
		AddEnumeration(ibSelectMode_Folders, wxT("Folders"), _("Folders"));
		AddEnumeration(ibSelectMode_FoldersAndItems, wxT("FoldersAndItems"), _("Folders and items"));
	}
};

#endif // !__METAOBJECT_ENUM_H__

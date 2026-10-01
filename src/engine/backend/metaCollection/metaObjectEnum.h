#ifndef __METAOBJECT_ENUM_H__
#define __METAOBJECT_ENUM_H__

////////////////////////////////////////////////////////////////////////////
//	Description : enumerations owned by the BASE metaobject — the ones whose
//	              meaning is platform-wide rather than tied to one metatype.
//
//	The SCRIPT-VISIBLE faces live here; the types themselves live with the
//	subject they belong to. ibSelectMode is part of what a choice is asked
//	with, so it moved to createRequest.h beside the request that carries it —
//	and its script face stayed here, with the other enumerations a module can
//	name (Max, 2026-09-23).
////////////////////////////////////////////////////////////////////////////

#include "backend/compiler/enumUnit.h"
#include "backend/createRequest.h"   // ibSelectMode — the enum this face is of

// The script-visible face of ibSelectMode (registered as "SelectMode" — see metaObjectEnum.cpp).
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

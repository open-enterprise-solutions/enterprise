#ifndef _DECLARED_PRESENTATION_H__
#define _DECLARED_PRESENTATION_H__

////////////////////////////////////////////////////////////////////////////
// HOW A REFERENCE READS WHILE THE CONFIGURATION IS BEING WRITTEN
////////////////////////////////////////////////////////////////////////////
//
// At run time a reference presents the ROW behind it — a description, a document's number and date.
// In the designer there is no row to present: a reference there stands for something the
// configuration DECLARES, and what a person needs to read is what they wrote —
// `CatalogRef.Goods.EmptyRef`, `EnumRef.Kinds.Retail` (Max, 2026-08-28: "the designer sees
// references through the full name").
//
// ⭐ THE PRESENTATION IS THE METAOBJECT'S OWN BUSINESS, so this is a pair of helpers rather than a
// road of its own: each family answers inside its GetDataPresentation, because WHAT it declares
// differs — a catalog has predefined values, an enumeration has members, a document has neither and
// only ever says its empty reference. What they share is the NAME, which is written once here.
////////////////////////////////////////////////////////////////////////////

#include "backend/metaCollection/partial/commonObject.h"
#include "backend/objCtor.h"

// `CatalogRef.Goods` — the REFERENCE type's name, which is what a script writes and what the Type
// column beside the value prints. `Catalog.Goods` is the OBJECT and would read as a different thing.
inline wxString ibDeclaredTypeName(const ibValueMetaObjectRecordDataRef* metaObject)
{
	if (metaObject == nullptr)
		return wxEmptyString;
	// ⚠ THE CTOR'S CLASS NAME IS ALREADY THE WHOLE THING — `EnumerationRef.Enumeration1` — because a
	// dynamic type is named `<kind>Ref.<name>` at registration (objCtor.h). Appending the name again
	// is how `EnumerationRef.Enumeration1.Enumeration1.Enum2` happened.
	const ibCtorMetaValueType* refCtor =
		metaObject->GetTypeCtor(ibCtorObjectMetaType::ibCtorObjectMetaType_Reference);
	return refCtor != nullptr
		? refCtor->GetClassName()
		: metaObject->GetClassName() + wxT(".") + metaObject->GetName();
}

// The one answer EVERY family gives the same way: the empty reference of the type. Empty string when
// the guid names something — then the family says which of its declared values that is.
inline wxString ibDeclaredEmptyRef(const ibValueMetaObjectRecordDataRef* metaObject, const ibGuid& guid)
{
	if (metaObject == nullptr || guid.isValid())
		return wxEmptyString;
	return ibDeclaredTypeName(metaObject) + wxT(".") + ibRefMember::EmptyRef;
}

#endif // _DECLARED_PRESENTATION_H__

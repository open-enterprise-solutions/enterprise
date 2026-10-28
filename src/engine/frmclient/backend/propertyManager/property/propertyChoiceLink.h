#ifndef __PROPERTY_CHOICE_LINK_H__
#define __PROPERTY_CHOICE_LINK_H__

#include "frmclient/backend/propertyManager/propertyObject.h"   // ibPropertyChoiceList

// THE DESKTOP'S HEADER OF THE TWO CHOICE PROPERTIES (backend/propertyManager/property/propertyChoiceLink.h), down to the
// one question their windows ask outside them. The properties themselves are the server's: on the client a row is one
// class (ibPropertySerializable), and what the desktop's ibPropertyChoiceLink::GetValueList offers — the fields that
// may govern this one — comes as that row's choices.
//
// ⭐ THE REFERENCE TYPES OF A FIELD, AS ITS ROW BRINGS THEM. They are read off the field's type description and the
// configuration, on the server, which writes them on the row of a link by type and of the choice parameters
// (ibPropertyInspector — the row's Targets). A row that brings none (an older server) answers none: the link's
// window then asks no question of which type a link decides, and the parameters' window has no target.
//
// ⚠ ASKED OF THE PROPERTY, NOT OF ITS OWNER: the owner holds no configuration here, the row holds the answer.
FRMCLIENT_API void ibFieldReferenceTypes(const ibBackendProperty* property, ibPropertyChoiceList& list);

// The class a reference type is registered under — what a link keeps (the desktop's
// ibMetaData::GetIDObjectFromMetaID) — by the type's metaobject; none: 0.
FRMCLIENT_API ibClassID ibFieldReferenceClass(const ibBackendProperty* property, const ibMetaID& type);

// The fields of one target type a parameter may fill (the desktop's ibPropertyChoiceParameters::GetParameterList),
// by the type's metaobject; and the fields beside this one that may supply a value (…::GetSourceList).
FRMCLIENT_API void ibChoiceParameterList(const ibBackendProperty* property, const ibMetaID& type, ibPropertyChoiceList& list);
FRMCLIENT_API void ibChoiceSourceList(const ibBackendProperty* property, ibPropertyChoiceList& list);

// Whether the row brought the lists the parameters' window is built of — the window is opened only then.
FRMCLIENT_API bool ibChoiceHasLists(const ibBackendProperty* property);

// THE PARAMETERS' WINDOW'S TWO QUESTIONS — the desktop's declarations, each a walk over the configuration from this
// field: what it stands in (the object or the tabular section whose other fields are its neighbours), and which fields
// of what it refers to an attribute could fill, the likeliest first, and the one that is obvious. The server answers
// both on the row (its Sources — ibPropertyInspector), so they are asked of the property; a row without them answers
// none: no holder named, nothing an attribute fills.
FRMCLIENT_API ibString ibChoiceHolderName(const ibBackendProperty* property);
FRMCLIENT_API void ibChoiceParametersForSource(const ibBackendProperty* property, const ibMetaID& sourceId,
	std::vector<ibMetaID>& out, ibMetaID* obvious = nullptr);

#endif

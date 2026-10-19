#ifndef __PROPERTY_CHOICE_LINK_H__
#define __PROPERTY_CHOICE_LINK_H__

#include "frmclient/backend/propertyManager/propertyObject.h"   // ibPropertyChoiceList

// THE DESKTOP'S HEADER OF THE TWO CHOICE PROPERTIES (backend/propertyManager/property/propertyChoiceLink.h), down to the
// one question their windows ask outside them. The properties themselves are the server's: on the client a row is one
// class (ibPropertySerializable), and what the desktop's ibPropertyChoiceLink::GetValueList offers — the fields that
// may govern this one — comes as that row's choices.
//
// ⚠ THE REFERENCE TYPES OF A FIELD ARE NOT BROUGHT YET. They are read off the field's type description and the
// configuration, on the server, and the row does not carry them — so none: the link's window then asks no question
// of which type a link decides (it asks only where there are two), and the parameters' window has no target whose
// fields it could offer.
inline void ibFieldReferenceTypes(const ibPropertyObject* WXUNUSED(owner), ibPropertyChoiceList& WXUNUSED(list)) {}

#endif

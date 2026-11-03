#ifndef __PROPERTY_MEMBERS_H__
#define __PROPERTY_MEMBERS_H__

#include "backend/compiler/value.h"
#include "backend/propertyManager/propertyObject.h"

// ⭐ AN OBJECT'S PROPERTIES AS A SCRIPT READS THEM — one member per property, under the property's name; `writable`
// says whether a script may assign it, `alias` tells these members from the value's others, and each member's data is
// the property's index, read back through GetPropertyByIndex. The one loop a control, a metaobject, a form attribute
// and a form command list their properties with.
inline void ibAppendPropertyMembers(ibValue::ibMemberTable& helper, const ibPropertyObject& object, bool writable,
	const long alias = wxNOT_FOUND)
{
	for (unsigned int idx = 0; idx < object.GetPropertyCount(); idx++) {
		if (const ibProperty* property = object.GetPropertyByIndex(idx))
			helper.AppendProp(property->GetName(), true, writable, idx, alias);
	}
}

#endif

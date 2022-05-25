////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : meta-attribues
////////////////////////////////////////////////////////////////////////////

#include "metaAttributeObject.h"

void CMetaAttributeObject::OnPropertyChanged(Property* property)
{
	IMetaObject* metaObject = GetParent();
	wxASSERT(metaObject);

	if (metaObject->OnReloadMetaObject()) {
		IMetaObject::OnPropertyChanged(property);
	}
}
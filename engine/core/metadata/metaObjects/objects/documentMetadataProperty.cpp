////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : meta-attribues
////////////////////////////////////////////////////////////////////////////

#include "document.h"

void CMetaObjectDocument::OnPropertyChanged(Property* property)
{
	if (CMetaObjectDocument::OnReloadMetaObject()) {
		IMetaObject::OnPropertyChanged(property);
	}
}
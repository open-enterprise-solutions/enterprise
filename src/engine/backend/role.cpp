#include "role.h"
#include "metaCollection/metaObject.h"

void Role::InitRole(IMetaObject* metaObject, const bool& value)
{
	m_object->AddRole(this);
}

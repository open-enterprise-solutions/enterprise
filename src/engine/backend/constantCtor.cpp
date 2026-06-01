#include "backend/metaCollection/partial/constant.h"
#include "backend/metaCollection/partial/constantManager.h"

#include "constantCtor.h"

// Phase 3: GetClassInfo() overrides removed (Category B self-identifies via
// GetClassType()). Only CreateObject() remains.

//const-object class
ibValue* ibCtorMetaValueTypeConstantObject::CreateObject() const
{
	return m_metaObject->CreateRecordDataObjectValue();
}

//const-manager class
ibValue* ibCtorMetaValueTypeConstantManager::CreateObject() const
{
	return ibValue::CreateAndPrepareValueRef<ibValueManagerDataObjectConstant>(m_metaObject);
}

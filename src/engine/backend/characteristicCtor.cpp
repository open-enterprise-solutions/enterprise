#include "backend/metaCollection/partial/chartOfCharacteristicTypes.h"

#include "characteristicCtor.h"

// Phase 3: GetClassInfo() override removed (Category B self-identifies via
// GetClassType()). Only CreateObject() remains.

//characteristic class
ibValue* ibCtorMetaValueTypeCharacteristic::CreateObject() const
{
	return m_metaObject->CreateValueRef();
}

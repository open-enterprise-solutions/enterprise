#ifndef __CHARACTERISTIC_CTOR_H__
#define	__CHARACTERISTIC_CTOR_H__

#include "objCtor.h"

//characteristic class
class ibCtorMetaValueTypeCharacteristic :
	public ibCtorMetaValueType {
public:

	ibCtorMetaValueTypeCharacteristic(ibValueMetaObjectChartOfCharacteristicTypes* recordRef) : ibCtorMetaValueType(), m_metaObject(recordRef) {
		m_classType = characteristic_to_clsid(GetMetaObject()->GetMetaID());
	}

	virtual wxString GetClassName() const {
		return prefixCharacteristic + m_metaObject->GetName();
	}

	virtual ibClassID GetClassType() const { return m_classType; }
	virtual ibValue* CreateObject() const;
	virtual const ibValueMetaObjectRecordDataHierarchyMutableRef* GetMetaObject() const { return m_metaObject; }
	virtual ibCtorObjectMetaType GetMetaTypeCtor() const { return ibCtorObjectMetaType::ibCtorObjectMetaType_Characteristic; }

protected:

	ibClassID m_classType;
	ibValueMetaObjectChartOfCharacteristicTypes* m_metaObject;
};

#define registerCharacteristic()\
	m_metaData->RegisterCtor(new ibCtorMetaValueTypeCharacteristic(this))
#define unregisterCharacteristic()\
	m_metaData->UnRegisterCtor(characteristic_to_clsid(GetMetaID()))

#endif // !__CHAR_CTOR_H__

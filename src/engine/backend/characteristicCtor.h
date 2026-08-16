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

	// ⭐ CAN A VALUE OF THIS CLASS BE ONE OF MINE — the registrar's own question, answered by the
	// CONTOUR: the chart's composition is what a characteristic may be at all, before any kind narrows
	// it. Class ids only, no value built and no metadata walked — one lookup in a vector.
	//
	// Without this the inherited answer is `clsid == GetClassType()`, i.e. "only a value whose class is
	// Characteristic.<chart> passes" — and no real value ever has that class, so the type admitted
	// nothing at all. The NARROWING by kind is a different question and is not asked here: a kind's own
	// `Type` is an ibValueTypeDescription and already knows how to convert (see AdjustValue).
	virtual bool AllowValue(const ibClassID& clsid) const override;

protected:

	ibClassID m_classType;
	ibValueMetaObjectChartOfCharacteristicTypes* m_metaObject;
};

#define registerCharacteristic()\
	m_metaData->RegisterCtor(new ibCtorMetaValueTypeCharacteristic(this))
#define unregisterCharacteristic()\
	m_metaData->UnRegisterCtor(characteristic_to_clsid(GetMetaID()))

#endif // !__CHAR_CTOR_H__

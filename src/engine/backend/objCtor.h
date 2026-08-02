#ifndef _SINGLE_CLASS_H__
#define _SINGLE_CLASS_H__

#include "backend/compiler/typeCtor.h"
#include "backend/objCtorDefs.h"
#include "backend/metaCollection/partial/commonObject.h"

class ibCtorMetaValueType : public ibCtorAbstractType {
public:

	ibCtorMetaValueType() {}
	virtual ~ibCtorMetaValueType() {}

	virtual ibCtorObjectType GetObjectTypeCtor() const final { return ibCtorObjectType::ibCtorObjectType_object_meta_value; }

	template <typename metaType>
	inline bool ConvertToMetaValue(metaType*& refValue) const {
		const ibValueMetaObject* metaObject = GetMetaObject();
		return metaObject ?
			metaObject->ConvertToValue(refValue) : false;
	}

	virtual wxIcon GetClassIcon() const {
		const ibValueMetaObject* metaObject = GetMetaObject();
		return metaObject ?
			metaObject->GetIcon() : wxNullIcon;
	}

	virtual ibCtorObjectMetaType GetMetaTypeCtor() const = 0;
	virtual const ibValueMetaObject* GetMetaObject() const = 0;

	// The dot-walk TARGET queryable of this metadata type — non-null only for a REFERENCE ctor (whose
	// metaobject is a queryable holder). Lets the reference-target resolver reach the queryable by VIRTUAL
	// dispatch, with NO cast on the dot-walk hot path (the concrete ctor already owns the typed metaobject).
	virtual const ibBackendQueryable* GetQueryable() const { return nullptr; }

	// Table trait for metadata types — derived from the meta-kind the metaobject already
	// declares (no per-object flag): a List / TabularSection / RecordSet is a tabular source;
	// a Reference / Object / Manager / Characteristic / RecordKey is a scalar (a table is
	// never a reference). Lets the class factory answer "is this a table" by CLSID for
	// metaobjects too, alongside the value-type ctors' T::IsTableValue.
	virtual bool IsTableValue() const override {
		switch (GetMetaTypeCtor()) {
		case ibCtorObjectMetaType::ibCtorObjectMetaType_List:
		case ibCtorObjectMetaType::ibCtorObjectMetaType_TabularSection:
		case ibCtorObjectMetaType::ibCtorObjectMetaType_TabularSection_String:
		case ibCtorObjectMetaType::ibCtorObjectMetaType_RecordSet:
		case ibCtorObjectMetaType::ibCtorObjectMetaType_RecordSet_String:
			return true;
		default:
			return false;
		}
	}
};

//reference class 
class ibCtorMetaValueTypeReference :
	public ibCtorMetaValueType {
public:

	ibCtorMetaValueTypeReference(ibValueMetaObjectRecordDataRef* recordRef) : ibCtorMetaValueType(), m_metaObject(recordRef) {
		m_classType = reference_to_clsid(m_metaObject->GetMetaID());
	}

	virtual wxString GetClassName() const {
		return m_metaObject->GetClassName() + prefixReference + m_metaObject->GetName();
	}

	virtual ibClassID GetClassType() const { return m_classType; }
	virtual ibValue* CreateObject() const;
	virtual const ibValueMetaObject* GetMetaObject() const { return m_metaObject; }
	virtual ibCtorObjectMetaType GetMetaTypeCtor() const { return ibCtorObjectMetaType::ibCtorObjectMetaType_Reference; }
	// m_metaObject is the TYPED reference target (a queryable holder) — forward its queryable with no cast.
	virtual const ibBackendQueryable* GetQueryable() const override { return m_metaObject != nullptr ? m_metaObject->GetQueryable() : nullptr; }

protected:
	ibClassID m_classType;
	ibValueMetaObjectRecordDataRef* m_metaObject;
};

#define registerReference()\
	m_metaData->RegisterCtor(new ibCtorMetaValueTypeReference(this))
#define unregisterReference()\
	m_metaData->UnRegisterCtor(generate_class_name(prefixReference))

//object class
class ibCtorMetaValueTypeObject :
	public ibCtorMetaValueType {
public:

	ibCtorMetaValueTypeObject(ibValueMetaObjectRecordData* recordRef) : ibCtorMetaValueType(), m_metaObject(recordRef) {
		m_classType = object_to_clsid(m_metaObject->GetMetaID());
	}

	virtual wxString GetClassName() const {
		return m_metaObject->GetClassName() + prefixObject +
			m_metaObject->GetName();
	}

	virtual ibClassID GetClassType() const { return m_classType; }
	virtual ibValue* CreateObject() const;
	virtual const ibValueMetaObject* GetMetaObject() const { return m_metaObject; }
	virtual ibCtorObjectMetaType GetMetaTypeCtor() const { return ibCtorObjectMetaType::ibCtorObjectMetaType_Object; }

protected:
	ibClassID m_classType;
	ibValueMetaObjectRecordData* m_metaObject;
};

class ibCtorMetaValueTypeExternalObject :
	public ibCtorMetaValueTypeObject {
public:

	ibCtorMetaValueTypeExternalObject(ibValueMetaObjectRecordData* recordRef) : ibCtorMetaValueTypeObject(recordRef) {
		m_classType = externalObject_to_clsid(recordRef->GetMetaID());
	}

	virtual ibClassID GetClassType() const { return m_classType; }

private:
	ibClassID m_classType;
	ibValueMetaObjectRecordData* m_metaObject;
};

#define registerObject()\
	m_metaData->RegisterCtor(new ibCtorMetaValueTypeObject(this))
#define registerExternalObject()\
	m_metaData->RegisterCtor(new ibCtorMetaValueTypeExternalObject(this))
#define unregisterObject()\
	m_metaData->UnRegisterCtor(generate_class_name(prefixObject))

//manager class 
class ibCtorMetaValueTypeManager :
	public ibCtorMetaValueType {
public:

	ibCtorMetaValueTypeManager(ibValueMetaObjectGenericData* recordRef) : ibCtorMetaValueType(), m_metaObject(recordRef) {
		m_classType = manager_to_clsid(m_metaObject->GetMetaID());
	}

	virtual wxString GetClassName() const {
		return m_metaObject->GetClassName() + prefixManager + m_metaObject->GetName();
	}

	virtual ibClassID GetClassType() const { return m_classType; }
	virtual ibValue* CreateObject() const;
	virtual const ibValueMetaObject* GetMetaObject() const { return m_metaObject; }
	virtual ibCtorObjectMetaType GetMetaTypeCtor() const { return ibCtorObjectMetaType::ibCtorObjectMetaType_Manager; }

protected:
	ibClassID m_classType;
	ibValueMetaObjectGenericData* m_metaObject;
};

class ibCtorMetaValueTypeExternalManager : public ibCtorMetaValueTypeManager {
public:

	ibCtorMetaValueTypeExternalManager(ibValueMetaObjectGenericData* recordRef) :ibCtorMetaValueTypeManager(recordRef) {
		m_classType = externalManager_to_clsid(recordRef->GetMetaID());
	}

	virtual ibClassID GetClassType() const { return m_classType; }
};

#define registerManager()\
	m_metaData->RegisterCtor(new ibCtorMetaValueTypeManager(this))
#define registerExternalManager()\
	m_metaData->RegisterCtor(new ibCtorMetaValueTypeExternalManager(this))
#define unregisterManager()\
	m_metaData->UnRegisterCtor(generate_class_name(prefixManager))

//selection class
class ibCtorMetaValueTypeSelection :
	public ibCtorMetaValueType {
public:

	ibCtorMetaValueTypeSelection(ibValueMetaObjectGenericData* recordRef) : ibCtorMetaValueType(), m_metaObject(recordRef) {
		m_classType = selection_to_clsid(m_metaObject->GetMetaID());
	}

	virtual wxString GetClassName() const {
		return m_metaObject->GetClassName() + prefixSelection + m_metaObject->GetName();
	}

	virtual ibClassID GetClassType() const { return m_classType; }
	virtual ibValue* CreateObject() const { return nullptr; }
	virtual const ibValueMetaObject* GetMetaObject() const { return m_metaObject; }
	virtual ibCtorObjectMetaType GetMetaTypeCtor() const { return ibCtorObjectMetaType::ibCtorObjectMetaType_Selection; }

protected:
	ibClassID m_classType;
	ibValueMetaObjectGenericData* m_metaObject;
};

#define registerSelection()\
	m_metaData->RegisterCtor(new ibCtorMetaValueTypeSelection(this))
#define unregisterSelection()\
	m_metaData->UnRegisterCtor(generate_class_name(prefixSelection))

//tabular section class
class ibCtorMetaValueTypeTabularSection :
	public ibCtorMetaValueType {
public:

	ibCtorMetaValueTypeTabularSection(ibValueMetaObjectRecordData* recordRef, ibValueMetaObjectTableData* recordTable) : ibCtorMetaValueType(), m_metaTable(recordTable), m_metaObject(recordRef) {
		m_classType = tabularSection_to_clsid(m_metaTable->GetMetaID());
	}

	virtual wxString GetClassName() const {
		return m_metaObject->GetClassName() + prefixTabSection + m_metaObject->GetName() + wxT(".") + m_metaTable->GetName();
	}

	virtual ibClassID GetClassType() const { return m_classType; }
	virtual ibValue* CreateObject() const { return nullptr; }
	virtual const ibValueMetaObject* GetMetaObject() const { return m_metaTable; }
	virtual ibCtorObjectMetaType GetMetaTypeCtor() const { return ibCtorObjectMetaType::ibCtorObjectMetaType_TabularSection; }

protected:
	ibClassID m_classType;
	ibValueMetaObjectTableData* m_metaTable;
	ibValueMetaObjectRecordData* m_metaObject;
};

#define registerTabularSection()\
	m_metaData->RegisterCtor(new ibCtorMetaValueTypeTabularSection(metaObject, this))
#define unregisterTabularSection()\
	m_metaData->UnRegisterCtor(generate_class_table_name(prefixTabSection))

//tabular section reference class — the DB-backed tabular owner (ibValueMetaObjectTableDataRef).
//Same classType/name keying as the RAM variant (one tabular is EITHER RAM or Ref, never both,
//so the "T_<metaID>" key never collides); kept a distinct type so reference-specific behaviour
//(future: CreateObject / primary-key columns) has a home. GetMetaTypeCtor stays TabularSection —
//both variants resolve through ibCtorObjectMetaType_TabularSection.
class ibCtorMetaValueTypeTabularSectionReference :
	public ibCtorMetaValueTypeTabularSection {
public:
	ibCtorMetaValueTypeTabularSectionReference(ibValueMetaObjectRecordData* recordRef, ibValueMetaObjectTableData* recordTable)
		: ibCtorMetaValueTypeTabularSection(recordRef, recordTable) {}
};

#define registerTabularSectionReference()\
	m_metaData->RegisterCtor(new ibCtorMetaValueTypeTabularSectionReference(metaObject, this))
#define unregisterTabularSectionReference()\
	m_metaData->UnRegisterCtor(generate_class_table_name(prefixTabSection))

//tabular section string class
class ibCtorMetaValueTypeTabularSectionString :
	public ibCtorMetaValueType {
public:

	// Mind the order: this class declares m_metaObject before m_metaTable, the opposite of
	// ibCtorMetaValueTypeTabularSection above. The lists differ because the declarations do.
	ibCtorMetaValueTypeTabularSectionString(ibValueMetaObjectRecordData* recordRef, ibValueMetaObjectTableData* recordTable) : ibCtorMetaValueType(), m_metaObject(recordRef), m_metaTable(recordTable) {
		m_classType = tabularSectionString_to_clsid(m_metaTable->GetMetaID());
	}

	virtual wxString GetClassName() const {
		return m_metaObject->GetClassName() + prefixTabSectionStr + m_metaObject->GetName() + wxT(".") + m_metaTable->GetName();
	}

	virtual ibClassID GetClassType() const { return m_classType; }
	virtual ibValue* CreateObject() const { return nullptr; }
	virtual const ibValueMetaObject* GetMetaObject() const { return m_metaTable; }
	virtual ibCtorObjectMetaType GetMetaTypeCtor() const { return ibCtorObjectMetaType::ibCtorObjectMetaType_TabularSection_String; }

protected:
	ibClassID m_classType;
	ibValueMetaObjectRecordData* m_metaObject;
	ibValueMetaObjectTableData* m_metaTable;
};

#define registerTabularSection_String()\
	m_metaData->RegisterCtor(new ibCtorMetaValueTypeTabularSectionString(metaObject, this))
#define unregisterTabularSection_String()\
	m_metaData->UnRegisterCtor(generate_class_table_name(prefixTabSectionStr))

//record key class
class ibCtorMetaValueTypeRecord :
	public ibCtorMetaValueType {
public:

	ibCtorMetaValueTypeRecord(ibValueMetaObjectRegisterData* recordRef) : ibCtorMetaValueType(), m_metaObject(recordRef) {
		m_classType = recordKey_to_clsid(m_metaObject->GetMetaID());
	}

	virtual wxString GetClassName() const {
		return m_metaObject->GetClassName() + prefixRecordKey + m_metaObject->GetName();
	}

	virtual ibClassID GetClassType() const { return m_classType; }
	virtual ibValue* CreateObject() const;
	virtual const ibValueMetaObject* GetMetaObject() const { return m_metaObject; }
	virtual ibCtorObjectMetaType GetMetaTypeCtor() const { return ibCtorObjectMetaType::ibCtorObjectMetaType_RecordKey; }

protected:
	ibClassID m_classType;
	ibValueMetaObjectRegisterData* m_metaObject;
};

#define registerRecordKey()\
	m_metaData->RegisterCtor(new ibCtorMetaValueTypeRecord(this))
#define unregisterRecordKey()\
	m_metaData->UnRegisterCtor(generate_class_name(prefixRecordKey))

//record manager class
class ibCtorMetaValueTypeRecordManager :
	public ibCtorMetaValueType {
public:

	ibCtorMetaValueTypeRecordManager(ibValueMetaObjectRegisterData* recordRef) : ibCtorMetaValueType(), m_metaObject(recordRef) {
		m_classType = recordManager_to_clsid(m_metaObject->GetMetaID());
	}

	virtual wxString GetClassName() const {
		return m_metaObject->GetClassName() + prefixRecordManager + m_metaObject->GetName();
	}

	virtual ibClassID GetClassType() const { return m_classType; }
	virtual ibValue* CreateObject() const;
	virtual const ibValueMetaObject* GetMetaObject() const { return m_metaObject; }
	virtual ibCtorObjectMetaType GetMetaTypeCtor() const { return ibCtorObjectMetaType::ibCtorObjectMetaType_RecordManager; }

protected:
	ibClassID m_classType;
	ibValueMetaObjectRegisterData* m_metaObject;
};

#define registerRecordManager()\
	m_metaData->RegisterCtor(new ibCtorMetaValueTypeRecordManager(this))
#define unregisterRecordManager()\
	m_metaData->UnRegisterCtor(generate_class_name(prefixRecordManager))

//record set class
class ibCtorMetaValueTypeRecordSet :
	public ibCtorMetaValueType {
public:

	ibCtorMetaValueTypeRecordSet(ibValueMetaObjectRegisterData* recordRef) : ibCtorMetaValueType(), m_metaObject(recordRef) {
		m_classType = recordSet_to_clsid(m_metaObject->GetMetaID());
	}

	virtual wxString GetClassName() const {
		return m_metaObject->GetClassName() + prefixRecordSet + m_metaObject->GetName();
	}

	virtual ibClassID GetClassType() const { return m_classType; }
	virtual ibValue* CreateObject() const;
	virtual const ibValueMetaObject* GetMetaObject() const { return m_metaObject; }
	virtual ibCtorObjectMetaType GetMetaTypeCtor() const { return ibCtorObjectMetaType::ibCtorObjectMetaType_RecordSet; }

protected:
	ibClassID m_classType;
	ibValueMetaObjectRegisterData* m_metaObject;
};

#define registerRecordSet()\
	m_metaData->RegisterCtor(new ibCtorMetaValueTypeRecordSet(this))
#define unregisterRecordSet()\
	m_metaData->UnRegisterCtor(generate_class_name(prefixRecordSet))

//record set string class
class ibCtorMetaValueTypeRecordSetString :
	public ibCtorMetaValueType {
public:

	ibCtorMetaValueTypeRecordSetString(ibValueMetaObjectRegisterData* recordRef) : ibCtorMetaValueType(), m_metaObject(recordRef) {
		m_classType = recordSetString_to_clsid(m_metaObject->GetMetaID());
	}

	virtual wxString GetClassName() const {
		return m_metaObject->GetClassName() + prefixRecordSetStr + m_metaObject->GetName();
	}

	virtual ibClassID GetClassType() const { return m_classType; }
	virtual ibValue* CreateObject() const { return nullptr; }
	virtual const ibValueMetaObject* GetMetaObject() const { return m_metaObject; }
	virtual ibCtorObjectMetaType GetMetaTypeCtor() const { return ibCtorObjectMetaType::ibCtorObjectMetaType_RecordSet_String; }

protected:
	ibClassID m_classType;
	ibValueMetaObjectRegisterData* m_metaObject;
};

#define registerRecordSet_String()\
	m_metaData->RegisterCtor(new ibCtorMetaValueTypeRecordSetString(this))
#define unregisterRecordSet_String()\
	m_metaData->UnRegisterCtor(generate_class_name(prefixRecordSetStr))

#endif 

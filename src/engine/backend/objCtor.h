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
	virtual ibValueMetaObject* GetMetaObject() const = 0;
};

//reference class 
class ibCtorMetaValueTypeReference :
	public ibCtorMetaValueType {
public:

	ibCtorMetaValueTypeReference(ibValueMetaObjectRecordDataRef* recordRef) : ibCtorMetaValueType(), m_metaObject(recordRef) {
		m_classType = string_to_clsid(wxT("R_") +
			stringUtils::IntToStr(m_metaObject->GetMetaID()));
	}

	virtual wxString GetClassName() const {
		return m_metaObject->GetClassName() + prefixReference + m_metaObject->GetName();
	}

	virtual ibClassID GetClassType() const { return m_classType; }
	virtual wxClassInfo* GetClassInfo() const;
	virtual ibValue* CreateObject() const;
	virtual ibValueMetaObject* GetMetaObject() const { return m_metaObject; }
	virtual ibCtorObjectMetaType GetMetaTypeCtor() const { return ibCtorObjectMetaType::ibCtorObjectMetaType_Reference; }

protected:
	ibClassID m_classType;
	ibValueMetaObjectRecordDataRef* m_metaObject;
};

#define registerReference()\
	m_metaData->RegisterCtor(new ibCtorMetaValueTypeReference(this))
#define unregisterReference()\
	m_metaData->UnRegisterCtor(generate_class_name(prefixReference))

//list object class 
class ibCtorMetaValueTypeReferenceList :
	public ibCtorMetaValueType {
public:

	ibCtorMetaValueTypeReferenceList(ibValueMetaObjectRecordDataRef* recordRef) : ibCtorMetaValueType(), m_metaObject(recordRef) {
		m_classType = string_to_clsid(wxT("L_") +
			stringUtils::IntToStr(m_metaObject->GetMetaID()));
	}

	virtual wxString GetClassName() const {
		return m_metaObject->GetClassName() + prefixList + m_metaObject->GetName();
	}

	virtual ibClassID GetClassType() const { return m_classType; }
	virtual wxClassInfo* GetClassInfo() const;
	virtual ibValue* CreateObject() const;
	virtual ibValueMetaObject* GetMetaObject() const { return m_metaObject; }
	virtual ibCtorObjectMetaType GetMetaTypeCtor() const { return ibCtorObjectMetaType::ibCtorObjectMetaType_List; }

protected:
	ibClassID m_classType;
	ibValueMetaObjectRecordDataRef* m_metaObject;
};

#define registerRefList()\
	m_metaData->RegisterCtor(new ibCtorMetaValueTypeReferenceList(this))
#define unregisteRefList()\
	m_metaData->UnRegisterCtor(generate_class_name(prefixList))

//list register class
class ibCtorMetaValueTypeRegisterList :
	public ibCtorMetaValueType {
public:

	ibCtorMetaValueTypeRegisterList(ibValueMetaObjectRegisterData* recordRef) : ibCtorMetaValueType(), m_metaObject(recordRef) {
		m_classType = string_to_clsid(wxT("J_") +
			stringUtils::IntToStr(m_metaObject->GetMetaID()));
	}

	virtual wxString GetClassName() const {
		return m_metaObject->GetClassName() + prefixList + m_metaObject->GetName();
	}

	virtual ibClassID GetClassType() const { return m_classType; }
	virtual wxClassInfo* GetClassInfo() const;
	virtual ibValue* CreateObject() const;
	virtual ibValueMetaObject* GetMetaObject() const { return m_metaObject; }
	virtual ibCtorObjectMetaType GetMetaTypeCtor() const { return ibCtorObjectMetaType::ibCtorObjectMetaType_List; }

protected:
	ibClassID m_classType;
	ibValueMetaObjectRegisterData* m_metaObject;
};

#define registerRegList()\
	m_metaData->RegisterCtor(new ibCtorMetaValueTypeRegisterList(this))
#define unregisterRegList()\
	m_metaData->UnRegisterCtor(generate_class_name(prefixList))

//object class
class ibCtorMetaValueTypeObject :
	public ibCtorMetaValueType {
public:

	ibCtorMetaValueTypeObject(ibValueMetaObjectRecordData* recordRef) : ibCtorMetaValueType(), m_metaObject(recordRef) {
		m_classType = string_to_clsid(wxT("O_") +
			stringUtils::IntToStr(m_metaObject->GetMetaID()));
	}

	virtual wxString GetClassName() const {
		return m_metaObject->GetClassName() + prefixObject +
			m_metaObject->GetName();
	}

	virtual ibClassID GetClassType() const { return m_classType; }
	virtual wxClassInfo* GetClassInfo() const;
	virtual ibValue* CreateObject() const;
	virtual ibValueMetaObject* GetMetaObject() const { return m_metaObject; }
	virtual ibCtorObjectMetaType GetMetaTypeCtor() const { return ibCtorObjectMetaType::ibCtorObjectMetaType_Object; }

protected:
	ibClassID m_classType;
	ibValueMetaObjectRecordData* m_metaObject;
};

class ibCtorMetaValueTypeExternalObject :
	public ibCtorMetaValueTypeObject {
public:

	ibCtorMetaValueTypeExternalObject(ibValueMetaObjectRecordData* recordRef) : ibCtorMetaValueTypeObject(recordRef) {
		m_classType = string_to_clsid(wxT("EO_") +
			stringUtils::IntToStr(recordRef->GetMetaID()));
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
		m_classType = string_to_clsid(wxT("M_") +
			stringUtils::IntToStr(m_metaObject->GetMetaID()));
	}

	virtual wxString GetClassName() const {
		return m_metaObject->GetClassName() + prefixManager + m_metaObject->GetName();
	}

	virtual ibClassID GetClassType() const { return m_classType; }
	virtual wxClassInfo* GetClassInfo() const;
	virtual ibValue* CreateObject() const;
	virtual ibValueMetaObject* GetMetaObject() const { return m_metaObject; }
	virtual ibCtorObjectMetaType GetMetaTypeCtor() const { return ibCtorObjectMetaType::ibCtorObjectMetaType_Manager; }

protected:
	ibClassID m_classType;
	ibValueMetaObjectGenericData* m_metaObject;
};

class ibCtorMetaValueTypeExternalManager : public ibCtorMetaValueTypeManager {
public:

	ibCtorMetaValueTypeExternalManager(ibValueMetaObjectGenericData* recordRef) :ibCtorMetaValueTypeManager(recordRef) {
		m_classType = string_to_clsid(wxT("EM_") +
			stringUtils::IntToStr(recordRef->GetMetaID()));
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
		m_classType = string_to_clsid(wxT("S_") +
			stringUtils::IntToStr(m_metaObject->GetMetaID()));
	}

	virtual wxString GetClassName() const {
		return m_metaObject->GetClassName() + prefixSelection + m_metaObject->GetName();
	}

	virtual ibClassID GetClassType() const { return m_classType; }
	virtual wxClassInfo* GetClassInfo() const { return nullptr; }
	virtual ibValue* CreateObject() const { return nullptr; }
	virtual ibValueMetaObject* GetMetaObject() const { return m_metaObject; }
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

	ibCtorMetaValueTypeTabularSection(ibValueMetaObjectRecordData* recordRef, ibValueMetaObjectTableData* recordTable) : ibCtorMetaValueType(), m_metaObject(recordRef), m_metaTable(recordTable) {
		m_classType = string_to_clsid(wxT("T_") +
			stringUtils::IntToStr(m_metaTable->GetMetaID()));
	}

	virtual wxString GetClassName() const {
		return m_metaObject->GetClassName() + prefixTabSection + m_metaObject->GetName() + wxT(".") + m_metaTable->GetName();
	}

	virtual ibClassID GetClassType() const { return m_classType; }
	virtual wxClassInfo* GetClassInfo() const { return nullptr; }
	virtual ibValue* CreateObject() const { return nullptr; }
	virtual ibValueMetaObject* GetMetaObject() const { return m_metaTable; }
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

//tabular section string class
class ibCtorMetaValueTypeTabularSectionString :
	public ibCtorMetaValueType {
public:

	ibCtorMetaValueTypeTabularSectionString(ibValueMetaObjectRecordData* recordRef, ibValueMetaObjectTableData* recordTable) : ibCtorMetaValueType(), m_metaObject(recordRef), m_metaTable(recordTable) {
		m_classType = string_to_clsid(wxT("B_") +
			stringUtils::IntToStr(m_metaTable->GetMetaID()));
	}

	virtual wxString GetClassName() const {
		return m_metaObject->GetClassName() + prefixTabSectionStr + m_metaObject->GetName() + wxT(".") + m_metaTable->GetName();
	}

	virtual ibClassID GetClassType() const { return m_classType; }
	virtual wxClassInfo* GetClassInfo() const { return nullptr; }
	virtual ibValue* CreateObject() const { return nullptr; }
	virtual ibValueMetaObject* GetMetaObject() const { return m_metaTable; }
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
		m_classType = string_to_clsid(wxT("A_") +
			stringUtils::IntToStr(m_metaObject->GetMetaID()));
	}

	virtual wxString GetClassName() const {
		return m_metaObject->GetClassName() + prefixRecordKey + m_metaObject->GetName();
	}

	virtual ibClassID GetClassType() const { return m_classType; }
	virtual wxClassInfo* GetClassInfo() const;
	virtual ibValue* CreateObject() const;
	virtual ibValueMetaObject* GetMetaObject() const { return m_metaObject; }
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
		m_classType = string_to_clsid(wxT("D_") +
			stringUtils::IntToStr(m_metaObject->GetMetaID()));
	}

	virtual wxString GetClassName() const {
		return m_metaObject->GetClassName() + prefixRecordManager + m_metaObject->GetName();
	}

	virtual ibClassID GetClassType() const { return m_classType; }
	virtual wxClassInfo* GetClassInfo() const;
	virtual ibValue* CreateObject() const;
	virtual ibValueMetaObject* GetMetaObject() const { return m_metaObject; }
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
		m_classType = string_to_clsid(wxT("H_") +
			stringUtils::IntToStr(m_metaObject->GetMetaID()));
	}

	virtual wxString GetClassName() const {
		return m_metaObject->GetClassName() + prefixRecordSet + m_metaObject->GetName();
	}

	virtual ibClassID GetClassType() const { return m_classType; }
	virtual wxClassInfo* GetClassInfo() const;
	virtual ibValue* CreateObject() const;
	virtual ibValueMetaObject* GetMetaObject() const { return m_metaObject; }
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
		m_classType = string_to_clsid(wxT("P_") +
			stringUtils::IntToStr(m_metaObject->GetMetaID()));
	}

	virtual wxString GetClassName() const {
		return m_metaObject->GetClassName() + prefixRecordSetStr + m_metaObject->GetName();
	}

	virtual ibClassID GetClassType() const { return m_classType; }
	virtual wxClassInfo* GetClassInfo() const { return nullptr; }
	virtual ibValue* CreateObject() const { return nullptr; }
	virtual ibValueMetaObject* GetMetaObject() const { return m_metaObject; }
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

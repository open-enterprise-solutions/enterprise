#ifndef _SINGLE_CLASS_H__
#define _SINGLE_CLASS_H__

#include "compiler/singleObject.h"

enum eMetaObjectType {
	enReference = 1,
	enList,
	enObject,
	enManager,
	enSelection,
	enTabularSection,
	enRecordSet,
	enRecordKey,
	enRecordManager
};

#define def_offset 25

class IMetaTypeObjectValueSingle : public IObjectValueAbstract {
public:
	virtual wxBitmap GetClassIcon() const;
	virtual eObjectType GetObjectType() const {
		return eObjectType::eObjectType_value_metadata;
	}
	virtual IMetaObject* GetMetaObject() const = 0;
	virtual eMetaObjectType GetMetaType() const = 0;
};

#define clsid_start 95000000000000ll

//reference class 
class CMetaTypeRefObjectValueSingle : public IMetaTypeObjectValueSingle {
	IMetaObjectRecordDataRef* m_metaValue;
public:
	CMetaTypeRefObjectValueSingle(IMetaObjectRecordDataRef* metaRef) : IMetaTypeObjectValueSingle(),
		m_metaValue(metaRef) {}

	virtual wxString GetClassName() const { return m_metaValue->GetClassName() + wxT("Ref.") + m_metaValue->GetName(); }
	virtual CLASS_ID GetClassType() const { return -((clsid_start + m_metaValue->GetMetaID() * def_offset) + 1); }
	virtual wxClassInfo* GetClassInfo() const;

	virtual CValue* CreateObject() const;
	virtual IMetaObject* GetMetaObject() const { return m_metaValue; }
	virtual eMetaObjectType GetMetaType() const { return eMetaObjectType::enReference; }
};

#define registerReference()\
	m_metaData->RegisterObject(GetClassName() + wxT("Ref.") + GetName(), new CMetaTypeRefObjectValueSingle(this))\

#define unregisterReference()\
	m_metaData->UnRegisterObject(GetClassName() + wxT("Ref.") + GetName())\

//list object class 
class CMetaTypeListObjectValueSingle : public IMetaTypeObjectValueSingle {
	IMetaObjectRecordDataRef* m_metaValue;
public:
	CMetaTypeListObjectValueSingle(IMetaObjectRecordDataRef* metaRef) : IMetaTypeObjectValueSingle(),
		m_metaValue(metaRef) {}

	virtual wxString GetClassName() const { return m_metaValue->GetClassName() + wxT("List.") + m_metaValue->GetName(); }
	virtual CLASS_ID GetClassType() const { return -((clsid_start + m_metaValue->GetMetaID() * def_offset) + 2); }
	virtual wxClassInfo* GetClassInfo() const;

	virtual CValue* CreateObject() const;
	virtual IMetaObject* GetMetaObject() const { return m_metaValue; }
	virtual eMetaObjectType GetMetaType() const { return eMetaObjectType::enList; }
};

#define registerRefList()\
	m_metaData->RegisterObject(GetClassName() + wxT("List.") + GetName(), new CMetaTypeListObjectValueSingle(this))\

#define unregisteRefList()\
	m_metaData->UnRegisterObject(GetClassName() + wxT("List.") + GetName())\

//list register class
class CMetaTypeListRegisterValueSingle : public IMetaTypeObjectValueSingle {
	IMetaObjectRegisterData* m_metaValue;
public:
	CMetaTypeListRegisterValueSingle(IMetaObjectRegisterData* metaRef) : IMetaTypeObjectValueSingle(),
		m_metaValue(metaRef) {}

	virtual wxString GetClassName() const { return m_metaValue->GetClassName() + wxT("List.") + m_metaValue->GetName(); }
	virtual CLASS_ID GetClassType() const { return -((clsid_start + m_metaValue->GetMetaID() * def_offset) + 2); }
	virtual wxClassInfo* GetClassInfo() const;

	virtual CValue* CreateObject() const;
	virtual IMetaObject* GetMetaObject() const { return m_metaValue; }
	virtual eMetaObjectType GetMetaType() const { return eMetaObjectType::enList; }
};

#define registerRegList()\
	m_metaData->RegisterObject(GetClassName() + wxT("List.") + GetName(), new CMetaTypeListRegisterValueSingle(this))\

#define unregisterRegList()\
	m_metaData->UnRegisterObject(GetClassName() + wxT("List.") + GetName())\

//object class
class CMetaTypeObjectValueSingle : public IMetaTypeObjectValueSingle {
	IMetaObjectRecordData* m_metaValue;
public:
	CMetaTypeObjectValueSingle(IMetaObjectRecordData* metaRef) : IMetaTypeObjectValueSingle(),
		m_metaValue(metaRef) {}

	virtual wxString GetClassName() const { return m_metaValue->GetClassName() + wxT("Object.") + m_metaValue->GetName(); }
	virtual CLASS_ID GetClassType() const { return -((clsid_start + m_metaValue->GetMetaID() * def_offset) + 3); }
	virtual wxClassInfo* GetClassInfo() const;

	virtual CValue* CreateObject() const;
	virtual IMetaObject* GetMetaObject() const { return m_metaValue; }
	virtual eMetaObjectType GetMetaType() const { return eMetaObjectType::enObject; }
};

#define registerObject()\
	m_metaData->RegisterObject(GetClassName() + wxT("Object.") + GetName(), new CMetaTypeObjectValueSingle(this))\

#define unregisterObject()\
	m_metaData->UnRegisterObject(GetClassName() + wxT("Object.") + GetName())\

class CMetaTypeConstObjectValueSingle : public IMetaTypeObjectValueSingle {
	CMetaConstantObject* m_metaValue;
public:
	CMetaTypeConstObjectValueSingle(CMetaConstantObject* metaRef) : IMetaTypeObjectValueSingle(),
		m_metaValue(metaRef) {}

	virtual wxString GetClassName() const { return ((IMetaObject*)m_metaValue)->GetClassName() + wxT("Object.") + ((IMetaObject*)m_metaValue)->GetName(); }
	virtual CLASS_ID GetClassType() const { return -((clsid_start + ((IMetaObject*)m_metaValue)->GetMetaID() * def_offset) + 3); }
	virtual wxClassInfo* GetClassInfo() const;

	virtual CValue* CreateObject() const;
	virtual IMetaObject* GetMetaObject() const { return (IMetaObject*)m_metaValue; }
	virtual eMetaObjectType GetMetaType() const { return eMetaObjectType::enObject; }
};

#define registerConstObject()\
	m_metaData->RegisterObject(GetClassName() + wxT("Object.") + GetName(), new CMetaTypeConstObjectValueSingle(this))\

#define unregisterConstObject()\
	m_metaData->UnRegisterObject(GetClassName() + wxT("Object.") + GetName())\

//manager class 
class CMetaTypeManagerValueSingle : public IMetaTypeObjectValueSingle {
	IMetaObject* m_metaValue;
public:
	CMetaTypeManagerValueSingle(IMetaObject* metaRef) : IMetaTypeObjectValueSingle(),
		m_metaValue(metaRef) {}

	virtual wxString GetClassName() const { return m_metaValue->GetClassName() + wxT("Manager.") + m_metaValue->GetName(); }
	virtual CLASS_ID GetClassType() const { return -((clsid_start + m_metaValue->GetMetaID() * def_offset) + 4); }
	virtual wxClassInfo* GetClassInfo() const { return NULL; }

	virtual CValue* CreateObject() const { return NULL; }
	virtual IMetaObject* GetMetaObject() const { return m_metaValue; }
	virtual eMetaObjectType GetMetaType() const { return eMetaObjectType::enManager; }
};

#define registerManager()\
	m_metaData->RegisterObject(GetClassName() + wxT("Manager.") + GetName(), new CMetaTypeManagerValueSingle(this))\

#define unregisterManager()\
	m_metaData->UnRegisterObject(GetClassName() + wxT("Manager.") + GetName())\

//selection class
class CMetaTypeSelectionValueSingle : public IMetaTypeObjectValueSingle {
	IMetaObjectWrapperData* m_metaValue;
public:
	CMetaTypeSelectionValueSingle(IMetaObjectWrapperData* metaRef) : IMetaTypeObjectValueSingle(),
		m_metaValue(metaRef) {}

	virtual wxString GetClassName() const { return m_metaValue->GetClassName() + wxT("Selection.") + m_metaValue->GetName(); }
	virtual CLASS_ID GetClassType() const { return -((clsid_start + m_metaValue->GetMetaID() * def_offset) + 5); }
	virtual wxClassInfo* GetClassInfo() const { return NULL; }

	virtual CValue* CreateObject() const { return NULL; }
	virtual IMetaObject* GetMetaObject() const { return m_metaValue; }
	virtual eMetaObjectType GetMetaType() const { return eMetaObjectType::enSelection; }
};

#define registerSelection()\
	m_metaData->RegisterObject(GetClassName() + wxT("Selection.") + GetName(), new CMetaTypeSelectionValueSingle(this))\

#define unregisterSelection()\
	m_metaData->UnRegisterObject(GetClassName() + wxT("Selection.") + GetName())\

//tabular section class
class CMetaTypeTabularSectionValueSingle : public IMetaTypeObjectValueSingle {
	IMetaObjectRecordData* m_metaValue;
	CMetaTableObject* m_metaTable;
public:
	CMetaTypeTabularSectionValueSingle(IMetaObjectRecordData* metaRef, CMetaTableObject* metaTable) : IMetaTypeObjectValueSingle(),
		m_metaValue(metaRef), m_metaTable(metaTable) {}

	virtual wxString GetClassName() const { return m_metaValue->GetClassName() + wxT("TabularSection.") + m_metaValue->GetName() + wxT(".") + m_metaTable->GetName(); }
	virtual CLASS_ID GetClassType() const { return -((clsid_start + (m_metaValue->GetMetaID() + m_metaTable->GetMetaID()) * def_offset) + 6); }
	virtual wxClassInfo* GetClassInfo() const { return NULL; }

	virtual CValue* CreateObject() const { return NULL; }
	virtual IMetaObject* GetMetaObject() const { return m_metaTable; }
	virtual eMetaObjectType GetMetaType() const { return eMetaObjectType::enTabularSection; }
};

#define registerTabularSection()\
	m_metaData->RegisterObject(metaObject->GetClassName() + wxT("TabularSection.") + metaObject->GetName() + wxT(".") + GetName(), new CMetaTypeTabularSectionValueSingle(metaObject, this))\

#define unregisterTabularSection()\
	m_metaData->UnRegisterObject(metaObject->GetClassName() + wxT("TabularSection.") + metaObject->GetName() + wxT(".") + GetName())\

//record key class
class CMetaTypeRecordKeyValueSingle : public IMetaTypeObjectValueSingle {
	IMetaObjectRegisterData* m_metaValue;
public:
	CMetaTypeRecordKeyValueSingle(IMetaObjectRegisterData* metaRef) : IMetaTypeObjectValueSingle(),
		m_metaValue(metaRef) {}

	virtual wxString GetClassName() const { return m_metaValue->GetClassName() + wxT("RecordKey.") + m_metaValue->GetName(); }
	virtual CLASS_ID GetClassType() const { return -((clsid_start + m_metaValue->GetMetaID() * def_offset) + 7); }
	virtual wxClassInfo* GetClassInfo() const;

	virtual CValue* CreateObject() const;
	virtual IMetaObject* GetMetaObject() const { return m_metaValue; }
	virtual eMetaObjectType GetMetaType() const { return eMetaObjectType::enRecordKey; }
};

#define registerRecordKey()\
	m_metaData->RegisterObject(GetClassName() + wxT("RecordKey.") + GetName(), new CMetaTypeRecordKeyValueSingle(this))\

#define unregisterRecordKey()\
	m_metaData->UnRegisterObject(GetClassName() + wxT("RecordKey.") + GetName())\

//record manager class
class CMetaTypeRecordManagerValueSingle : public IMetaTypeObjectValueSingle {
	IMetaObjectRegisterData* m_metaValue;
public:
	CMetaTypeRecordManagerValueSingle(IMetaObjectRegisterData* metaRef) : IMetaTypeObjectValueSingle(),
		m_metaValue(metaRef) {}

	virtual wxString GetClassName() const { return m_metaValue->GetClassName() + wxT("RecordManager.") + m_metaValue->GetName(); }
	virtual CLASS_ID GetClassType() const { return -((clsid_start + m_metaValue->GetMetaID() * def_offset) + 8); }
	virtual wxClassInfo* GetClassInfo() const;

	virtual CValue* CreateObject() const;
	virtual IMetaObject* GetMetaObject() const { return m_metaValue; }
	virtual eMetaObjectType GetMetaType() const { return eMetaObjectType::enRecordManager; }
};

#define registerRecordManager()\
	m_metaData->RegisterObject(GetClassName() + wxT("RecordManager.") + GetName(), new CMetaTypeRecordManagerValueSingle(this))\

#define unregisterRecordManager()\
	m_metaData->UnRegisterObject(GetClassName() + wxT("RecordManager.") + GetName())\

//record set class
class CMetaTypeRecordSetValueSingle : public IMetaTypeObjectValueSingle {
	IMetaObjectRegisterData* m_metaValue;
public:
	CMetaTypeRecordSetValueSingle(IMetaObjectRegisterData* metaRef) : IMetaTypeObjectValueSingle(),
		m_metaValue(metaRef) {}

	virtual wxString GetClassName() const { return m_metaValue->GetClassName() + wxT("RecordSet.") + m_metaValue->GetName(); }
	virtual CLASS_ID GetClassType() const { return -((clsid_start + m_metaValue->GetMetaID() * def_offset) + 9); }
	virtual wxClassInfo* GetClassInfo() const;

	virtual CValue* CreateObject() const;
	virtual IMetaObject* GetMetaObject() const { return m_metaValue; }
	virtual eMetaObjectType GetMetaType() const { return eMetaObjectType::enRecordSet; }
};

#define registerRecordSet()\
	m_metaData->RegisterObject(GetClassName() + wxT("RecordSet.") + GetName(), new CMetaTypeRecordSetValueSingle(this))\

#define unregisterRecordSet()\
	m_metaData->UnRegisterObject(GetClassName() + wxT("RecordSet.") + GetName())\

#endif 

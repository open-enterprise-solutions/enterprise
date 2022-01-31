#ifndef _SINGLE_CLASS_H__
#define _SINGLE_CLASS_H__

#include "compiler/singleObject.h"

enum eMetaObjectType {
	enReference = 1,
	enList,
	enObject,
	enManager,
	enSelection,
	enTabularSection
};

class IMetaTypeObjectValueSingle : public IObjectValueAbstract {
public:

	virtual eObjectType GetObjectType() const { return eObjectType::eObjectType_value_metadata; }
	virtual IMetaObject *GetMetaObject() const = 0;
	virtual eMetaObjectType GetMetaType() const = 0;
};

#define clsid_start 95000000000000ll

//reference class 
class CMetaTypeRefObjectValueSingle : public IMetaTypeObjectValueSingle {
	IMetaObjectRefValue *m_metaValue;
public:
	CMetaTypeRefObjectValueSingle(IMetaObjectRefValue *metaRef) : IMetaTypeObjectValueSingle(),
		m_metaValue(metaRef) {}

	virtual wxString GetClassName() const { return m_metaValue->GetClassName() + wxT("Ref.") + m_metaValue->GetName(); }
	virtual CLASS_ID GetTypeID() const { return -((clsid_start + m_metaValue->GetMetaID() * 10) + 1); }
	virtual wxClassInfo *GetClassInfo() const;

	virtual CValue *CreateObject() const;
	virtual IMetaObject *GetMetaObject() const { return m_metaValue; };
	virtual eMetaObjectType GetMetaType() const { return eMetaObjectType::enReference; };
};

#define registerReference()\
	m_metaData->RegisterObject(GetClassName() + wxT("Ref.") + GetName(), new CMetaTypeRefObjectValueSingle(this))\

#define unregisterReference()\
	m_metaData->UnRegisterObject(GetClassName() + wxT("Ref.") + GetName())\

//list class 
class CMetaTypeListObjectValueSingle : public IMetaTypeObjectValueSingle {
	IMetaObjectRefValue *m_metaValue;
public:
	CMetaTypeListObjectValueSingle(IMetaObjectRefValue *metaRef) : IMetaTypeObjectValueSingle(),
		m_metaValue(metaRef) {}

	virtual wxString GetClassName() const { return m_metaValue->GetClassName() + wxT("List.") + m_metaValue->GetName(); }
	virtual CLASS_ID GetTypeID() const { return -((clsid_start + m_metaValue->GetMetaID() * 10) + 2); }
	virtual wxClassInfo *GetClassInfo() const;

	virtual CValue *CreateObject() const;
	virtual IMetaObject *GetMetaObject() const { return m_metaValue; };
	virtual eMetaObjectType GetMetaType() const { return eMetaObjectType::enList; };
};

#define registerList()\
	m_metaData->RegisterObject(GetClassName() + wxT("List.") + GetName(), new CMetaTypeListObjectValueSingle(this))\

#define unregisterList()\
	m_metaData->UnRegisterObject(GetClassName() + wxT("List.") + GetName())\

//object class
class CMetaTypeObjectValueSingle : public IMetaTypeObjectValueSingle {
	IMetaObjectValue *m_metaValue;
public:
	CMetaTypeObjectValueSingle(IMetaObjectValue *metaRef) : IMetaTypeObjectValueSingle(),
		m_metaValue(metaRef) {}

	virtual wxString GetClassName() const { return m_metaValue->GetClassName() + wxT("Object.") + m_metaValue->GetName(); }
	virtual CLASS_ID GetTypeID() const { return -((clsid_start + m_metaValue->GetMetaID() * 10) + 3); }
	virtual wxClassInfo *GetClassInfo() const;

	virtual CValue *CreateObject() const;
	virtual IMetaObject *GetMetaObject() const { return m_metaValue; };
	virtual eMetaObjectType GetMetaType() const { return eMetaObjectType::enObject; };
};

#define registerObject()\
	m_metaData->RegisterObject(GetClassName() + wxT("Object.") + GetName(), new CMetaTypeObjectValueSingle(this))\

#define unregisterObject()\
	m_metaData->UnRegisterObject(GetClassName() + wxT("Object.") + GetName())\

//manager class 
class CMetaTypeManagerValueSingle : public IMetaTypeObjectValueSingle {
	IMetaObject *m_metaValue;
public:
	CMetaTypeManagerValueSingle(IMetaObject *metaRef) : IMetaTypeObjectValueSingle(),
		m_metaValue(metaRef) {}

	virtual wxString GetClassName() const { return m_metaValue->GetClassName() + wxT("Manager.") + m_metaValue->GetName(); }
	virtual CLASS_ID GetTypeID() const { return -((clsid_start + m_metaValue->GetMetaID() * 10) + 4); }
	virtual wxClassInfo *GetClassInfo() const { return NULL; }

	virtual CValue *CreateObject() const { return NULL; }
	virtual IMetaObject *GetMetaObject() const { return m_metaValue; };
	virtual eMetaObjectType GetMetaType() const { return eMetaObjectType::enManager; };
};

#define registerManager()\
	m_metaData->RegisterObject(GetClassName() + wxT("Manager.") + GetName(), new CMetaTypeManagerValueSingle(this))\

#define unregisterManager()\
	m_metaData->UnRegisterObject(GetClassName() + wxT("Manager.") + GetName())\

//selection class
class CMetaTypeSelectionValueSingle : public IMetaTypeObjectValueSingle {
	IMetaObjectValue *m_metaValue;
public:
	CMetaTypeSelectionValueSingle(IMetaObjectValue *metaRef) : IMetaTypeObjectValueSingle(),
		m_metaValue(metaRef) {}

	virtual wxString GetClassName() const { return m_metaValue->GetClassName() + wxT("Selection.") + m_metaValue->GetName(); }
	virtual CLASS_ID GetTypeID() const { return -((clsid_start + m_metaValue->GetMetaID() * 10) + 5); }
	virtual wxClassInfo *GetClassInfo() const { return NULL; }

	virtual CValue *CreateObject() const { return NULL; }
	virtual IMetaObject *GetMetaObject() const { return m_metaValue; };
	virtual eMetaObjectType GetMetaType() const { return eMetaObjectType::enSelection; };
};

#define registerSelection()\
	m_metaData->RegisterObject(GetClassName() + wxT("Selection.") + GetName(), new CMetaTypeSelectionValueSingle(this))\

#define unregisterSelection()\
	m_metaData->UnRegisterObject(GetClassName() + wxT("Selection.") + GetName())\

//tabular section class
class CMetaTypeTabularSectionValueSingle : public IMetaTypeObjectValueSingle {
	IMetaObjectValue *m_metaValue;
	CMetaTableObject *m_metaTable;
public:
	CMetaTypeTabularSectionValueSingle(IMetaObjectValue *metaRef, CMetaTableObject *metaTable) : IMetaTypeObjectValueSingle(),
		m_metaValue(metaRef), m_metaTable(metaTable) {}

	virtual wxString GetClassName() const { return m_metaValue->GetClassName() + wxT("TabularSection.") + m_metaValue->GetName() + wxT(".") + m_metaTable->GetName(); }
	virtual CLASS_ID GetTypeID() const { return -((clsid_start + m_metaValue->GetMetaID() * 10) + 6); }
	virtual wxClassInfo *GetClassInfo() const { return NULL; }

	virtual CValue *CreateObject() const { return NULL; }
	virtual IMetaObject *GetMetaObject() const { return m_metaValue; };
	virtual eMetaObjectType GetMetaType() const { return eMetaObjectType::enTabularSection; };
};

#define registerTabularSection()\
	m_metaData->RegisterObject(metaObject->GetClassName() + wxT("TabularSection.") + metaObject->GetName() + wxT(".") + GetName(), new CMetaTypeTabularSectionValueSingle(metaObject, this))\

#define unregisterTabularSection()\
	m_metaData->UnRegisterObject(metaObject->GetClassName() + wxT("TabularSection.") + metaObject->GetName() + wxT(".") + GetName())\

#endif 

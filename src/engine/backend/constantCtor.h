#ifndef __CONST_CTOR_H__
#define __CONST_CTOR_H__

#include "objCtor.h"

//const-object class
class ibCtorMetaValueTypeConstantObject :
	public ibCtorMetaValueType {
public:

	ibCtorMetaValueTypeConstantObject(ibValueMetaObjectConstant* recordRef) : ibCtorMetaValueType(), m_metaObject(recordRef) {
		m_classType = string_to_clsid(wxT("C_") +
			stringUtils::IntToStr(m_metaObject->GetMetaID()));
	}

	wxString GetClassName() const {
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
	ibValueMetaObjectConstant* m_metaObject;
};

#define registerConstObject()\
	m_metaData->RegisterCtor(new ibCtorMetaValueTypeConstantObject(this))
#define unregisterConstObject()\
	m_metaData->UnRegisterCtor(generate_class_name(prefixObject))

//const-manager class 
class ibCtorMetaValueTypeConstantManager :
	public ibCtorMetaValueType {
public:

	ibCtorMetaValueTypeConstantManager(class ibValueMetaObjectConstant* recordRef) : ibCtorMetaValueType(), m_metaObject(recordRef) {
		m_classType = string_to_clsid(wxT("G_") +
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
	ibValueMetaObjectConstant* m_metaObject;
};

#define registerConstManager()\
	m_metaData->RegisterCtor(new ibCtorMetaValueTypeConstantManager(this))
#define unregisterConstManager()\
	m_metaData->UnRegisterCtor(generate_class_name(prefixManager))

#endif 
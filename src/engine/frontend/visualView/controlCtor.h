#ifndef _CONTROL_CLASS_
#define _CONTROL_CLASS_

#include "backend/compiler/typeCtor.h"

// object with non-create object
class IControlTypeCtor : public IValueTypeCtor {
	wxString m_classType;
public:

	IControlTypeCtor(const wxString& className, const wxString& classType, wxClassInfo* classInfo, const class_identifier_t& clsid)
		: IValueTypeCtor(className, classInfo, clsid), m_classType(classType) {
	}

	virtual wxString GetTypeControlName() const {
		return m_classType;
	}

	virtual bool IsControlSystem() const {
		return false;
	}
};

// object with non-create object
template <class T>
class CControlTypeCtor : public IControlTypeCtor {
public:

	CControlTypeCtor(const wxString& className, const wxString& classType, const class_identifier_t& clsid)
		: IControlTypeCtor(className, classType, CLASSINFO(T), clsid) {
	}

	virtual wxIcon GetClassIcon() const {
		return T::GetIconGroup();
	}

	virtual eCtorObjectType GetObjectTypeCtor() const {
		return eCtorObjectType::eCtorObjectType_object_control;
	}

	virtual void CallAsEvent(eCtorObjectTypeEvent event) {
		if (event == eCtorObjectTypeEvent::eCtorObjectTypeEvent_Register)
			T::OnRegisterObject(GetClassName(), this);
		else if (event == eCtorObjectTypeEvent::eCtorObjectTypeEvent_UnRegister)
			T::OnUnRegisterObject(GetClassName());
	}

	virtual CValue* CreateObject() const {
		return new T();
	}
};

template <class T>
class CSystemControlTypeCtor : public CControlTypeCtor<T> {
public:
	CSystemControlTypeCtor(const wxString& className, const wxString& classType, const class_identifier_t& clsid)
		: CControlTypeCtor(className, classType, clsid) {
	}
	virtual bool IsControlSystem() const {
		return true;
	}
};

#define CONTROL_TYPE_REGISTER(class_info, class_name, class_type, clsid)\
GENERATE_REGISTER(wxT(class_name), wxMAKE_UNIQUE_NAME(s_cs_reg_c_), new CControlTypeCtor<class_info>(wxT(class_name), wxT(class_type), clsid))

#define S_CONTROL_TYPE_REGISTER(class_info, class_name, class_type, clsid)\
GENERATE_REGISTER(wxT(class_name), wxMAKE_UNIQUE_NAME(s_cs_reg_sc_), new CSystemControlTypeCtor<class_info>(wxT(class_name), wxT(class_type), clsid))

#endif 
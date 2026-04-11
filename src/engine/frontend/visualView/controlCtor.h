#ifndef _CONTROL_CLASS_
#define _CONTROL_CLASS_

#include "backend/compiler/typeCtor.h"

// object with non-create object
class ibCtorControlTypeBase : public ibCtorValueTypeBase {
	wxString m_classType;
public:

	ibCtorControlTypeBase(const wxString& className, const wxString& classType, wxClassInfo* classInfo, const ibClassID& clsid)
		: ibCtorValueTypeBase(className, classInfo, clsid), m_classType(classType) {
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
class ibCtorControlType : public ibCtorControlTypeBase {
public:

	ibCtorControlType(const wxString& className, const wxString& classType, const ibClassID& clsid)
		: ibCtorControlTypeBase(className, classType, CLASSINFO(T), clsid) {
	}

	virtual wxIcon GetClassIcon() const {
		return T::GetIconGroup();
	}

	virtual ibCtorObjectType GetObjectTypeCtor() const {
		return ibCtorObjectType::ibCtorObjectType_object_control;
	}

	virtual void CallAsEvent(ibCtorObjectTypeEvent event) {
		if (event == ibCtorObjectTypeEvent::ibCtorObjectTypeEvent_Register)
			T::OnRegisterObject(GetClassName(), this);
		else if (event == ibCtorObjectTypeEvent::ibCtorObjectTypeEvent_UnRegister)
			T::OnUnRegisterObject(GetClassName());
	}

	virtual ibValue* CreateObject() const {
		return new T();
	}
};

template <class T>
class ibCtorSystemControlType : public ibCtorControlType<T> {
public:
	ibCtorSystemControlType(const wxString& className, const wxString& classType, const ibClassID& clsid)
		: ibCtorControlType<T>(className, classType, clsid) {
	}
	virtual bool IsControlSystem() const {
		return true;
	}
};

#define CONTROL_TYPE_REGISTER(class_info, class_name, class_type, clsid)\
GENERATE_REGISTER(wxT(class_name), wxMAKE_UNIQUE_NAME(s_cs_reg_c_), new ibCtorControlType<class_info>(wxT(class_name), wxT(class_type), clsid))

#define S_CONTROL_TYPE_REGISTER(class_info, class_name, class_type, clsid)\
GENERATE_REGISTER(wxT(class_name), wxMAKE_UNIQUE_NAME(s_cs_reg_sc_), new ibCtorSystemControlType<class_info>(wxT(class_name), wxT(class_type), clsid))

#endif 
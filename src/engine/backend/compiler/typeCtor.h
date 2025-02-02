#ifndef _TYPE_CTOR_H__
#define _TYPE_CTOR_H__

enum eCtorObjectType {
	eCtorObjectType_object_primitive = 1,
	eCtorObjectType_object_value,
	eCtorObjectType_object_control,
	eCtorObjectType_object_system,
	eCtorObjectType_object_enum,
	eCtorObjectType_object_context,

	eCtorObjectType_object_metadata,
	eCtorObjectType_object_meta_value
};

enum eCtorObjectTypeEvent {
	eCtorObjectTypeEvent_Register,
	eCtorObjectTypeEvent_UnRegister,
};

/////////////////////////////////////////////////////////////////////////

template<typename typeCtor>
class value_register {
	class IAbstractTypeCtor* m_so;
public:
	value_register(typeCtor* so) : m_so(so) {
		try {
			if (m_so != nullptr) {
				CValue::RegisterCtor(m_so);
			}
		}
		catch (...) {
#ifdef DEBUG
			wxLogDebug("! failed to register class: %s", m_so->GetClassName());
#endif
			m_so = nullptr;
		}
	}
	~value_register() {
		try {
			if (m_so != nullptr) {
				CValue::UnRegisterCtor(m_so);
			}
		}
		catch (...) {
#ifdef DEBUG
			wxLogDebug("! failed to unregister class: %s", m_so->GetClassName());
#endif
			m_so = nullptr;
		}
	}
};

/////////////////////////////////////////////////////////////////////////

#define GENERATE_REGISTER(class_name, class_type, class_so)\
	static const value_register class_type = class_so;

/////////////////////////////////////////////////////////////////////////

class IAbstractTypeCtor {
public:

	virtual ~IAbstractTypeCtor() {}

	virtual wxString GetClassName() const = 0;
	virtual wxClassInfo* GetClassInfo() const = 0;
	virtual class_identifier_t GetClassType() const = 0;

	virtual wxIcon GetClassIcon() const {
		return wxNullIcon;
	}

	virtual eCtorObjectType GetObjectTypeCtor() const = 0;
	virtual void CallEvent(eCtorObjectTypeEvent event) {};
	virtual CValue* CreateObject() const = 0;
};

class IValueTypeCtor : public IAbstractTypeCtor {
	wxString m_className;
	wxClassInfo* m_classInfo;
	class_identifier_t m_clsid;
public:

	virtual ~IValueTypeCtor() {}

	virtual wxString GetClassName() const {
		return m_className;
	}

	virtual wxClassInfo* GetClassInfo() const {
		return m_classInfo;
	}

	virtual class_identifier_t GetClassType() const {
		return m_clsid;
	}

	IValueTypeCtor(const wxString& className, wxClassInfo* classInfo, const class_identifier_t& clsid)
		: m_className(className), m_classInfo(classInfo), m_clsid(clsid) {
	}

	virtual eCtorObjectType GetObjectTypeCtor() const = 0;
	virtual CValue* CreateObject() const = 0;
};

class IPrimitiveTypeCtor : public IValueTypeCtor {
public:

	IPrimitiveTypeCtor(const wxString& className, wxClassInfo* classInfo, const class_identifier_t& clsid)
		: IValueTypeCtor(className, classInfo, clsid)
	{
	}

	virtual eValueTypes GetValueType() const = 0;
};

//simple types
template <class T>
class CPrimitiveTypeCtor : public IPrimitiveTypeCtor {
	eValueTypes m_valType;

public:

	CPrimitiveTypeCtor(const wxString& className, eValueTypes valType, const class_identifier_t& clsid) :
		IPrimitiveTypeCtor(className, CLASSINFO(T), clsid), m_valType(valType) {
	}

	virtual wxIcon GetClassIcon() const {
		return T::GetIconGroup();
	}

	virtual eValueTypes GetValueType() const {
		return m_valType;
	}

	virtual eCtorObjectType GetObjectTypeCtor() const {
		return eCtorObjectType::eCtorObjectType_object_primitive;
	}

	virtual void CallEvent(eCtorObjectTypeEvent event) {
		if (event == eCtorObjectTypeEvent::eCtorObjectTypeEvent_Register)
			T::OnRegisterObject(GetClassName(), this);
		else if (event == eCtorObjectTypeEvent::eCtorObjectTypeEvent_UnRegister)
			T::OnUnRegisterObject(GetClassName());
	}

	virtual CValue* CreateObject() const {
		return new T(m_valType);
	}
};

#define PRIMITIVE_TYPE_REGISTER(class_info, class_name, class_type, clsid)\
GENERATE_REGISTER(wxT(class_name), wxMAKE_UNIQUE_NAME(s_cs_reg_s_), new CPrimitiveTypeCtor<class_info>(wxT(class_name), class_type, clsid))

// object value register - array, struct, etc.. 
template <class T>
class CValueTypeCtor : public IValueTypeCtor {

public:

	CValueTypeCtor(const wxString& className, const class_identifier_t& clsid) :
		IValueTypeCtor(className, CLASSINFO(T), clsid) {
	}

	virtual wxIcon GetClassIcon() const {
		return T::GetIconGroup();
	}

	virtual eCtorObjectType GetObjectTypeCtor() const {
		return eCtorObjectType::eCtorObjectType_object_value;
	}

	virtual void CallEvent(eCtorObjectTypeEvent event) {
		if (event == eCtorObjectTypeEvent::eCtorObjectTypeEvent_Register)
			T::OnRegisterObject(GetClassName(), this);
		else if (event == eCtorObjectTypeEvent::eCtorObjectTypeEvent_UnRegister)
			T::OnUnRegisterObject(GetClassName());
	}

	virtual CValue* CreateObject() const {
		return new T();
	}
};

#define VALUE_TYPE_REGISTER(class_info, class_name, clsid)\
GENERATE_REGISTER(wxT(class_name), wxMAKE_UNIQUE_NAME(s_cs_reg_val_), new CValueTypeCtor<class_info>(wxT(class_name), clsid))

// object with non-create object
template <class T>
class CSystemTypeCtor : public IValueTypeCtor {

public:

	CSystemTypeCtor(const wxString& className, const class_identifier_t& clsid) :
		IValueTypeCtor(className, CLASSINFO(T), clsid) {
	}

	virtual wxIcon GetClassIcon() const {
		return T::GetIconGroup();
	}

	virtual eCtorObjectType GetObjectTypeCtor() const {
		return eCtorObjectType::eCtorObjectType_object_system;
	}

	virtual void CallEvent(eCtorObjectTypeEvent event) {
		if (event == eCtorObjectTypeEvent::eCtorObjectTypeEvent_Register)
			T::OnRegisterObject(GetClassName(), this);
		else if (event == eCtorObjectTypeEvent::eCtorObjectTypeEvent_UnRegister)
			T::OnUnRegisterObject(GetClassName());
	}

	virtual CValue* CreateObject() const {
		return nullptr;
	}
};

#define SYSTEM_TYPE_REGISTER(class_info, class_name, clsid)\
GENERATE_REGISTER(wxT(class_name), wxMAKE_UNIQUE_NAME(s_cs_reg_so_), new CSystemTypeCtor<class_info>(wxT(class_name), clsid))

//enumeration register - windowOrient, etc...
template <class T>
class CEnumTypeCtor : public IPrimitiveTypeCtor {

public:
	CEnumTypeCtor(const wxString& className, const class_identifier_t& clsid) :
		IPrimitiveTypeCtor(className, CLASSINFO(T), clsid) {
	}

	virtual wxIcon GetClassIcon() const {
		return T::GetIconGroup();
	}

	virtual eValueTypes GetValueType() const {
		return eValueTypes::TYPE_ENUM;
	};

	virtual eCtorObjectType GetObjectTypeCtor() const {
		return eCtorObjectType::eCtorObjectType_object_enum;
	};

	virtual void CallEvent(eCtorObjectTypeEvent event) {
		if (event == eCtorObjectTypeEvent::eCtorObjectTypeEvent_Register)
			T::OnRegisterObject(GetClassName(), this);
		else if (event == eCtorObjectTypeEvent::eCtorObjectTypeEvent_UnRegister)
			T::OnUnRegisterObject(GetClassName());
	}

	virtual CValue* CreateObject() const {
		return new T();
	};
};

#define ENUM_TYPE_REGISTER(class_info, class_name, clsid)\
GENERATE_REGISTER(wxT(class_name), wxMAKE_UNIQUE_NAME(s_cs_reg_e_), new CEnumTypeCtor<class_info>(wxT(class_name), clsid))

template <class T>
class CContextTypeCtor : public IPrimitiveTypeCtor {
	T* m_innerObject = nullptr;
public:
	CContextTypeCtor(const wxString& className, const class_identifier_t& clsid) :
		IPrimitiveTypeCtor(className, CLASSINFO(T), clsid) {
	}

	virtual wxIcon GetClassIcon() const {
		return T::GetIconGroup();
	}

	virtual eValueTypes GetValueType() const {
		return eValueTypes::TYPE_VALUE;
	};

	virtual eCtorObjectType GetObjectTypeCtor() const {
		return eCtorObjectType::eCtorObjectType_object_context;
	};

	virtual void CallEvent(eCtorObjectTypeEvent event) {
		switch (event)
		{
		case eCtorObjectTypeEvent_Register:
			m_innerObject = new T();
			m_innerObject->IncrRef();
			T::OnRegisterObject(GetClassName(), this);
			break;
		case eCtorObjectTypeEvent_UnRegister:
			T::OnUnRegisterObject(GetClassName());
			m_innerObject->DecrRef();
			break;
		}
	}

	virtual CValue* CreateObject() const {
		return m_innerObject;
	};
};

#define CONTEXT_TYPE_REGISTER(class_info, class_name, clsid)\
GENERATE_REGISTER(wxT(class_name), wxMAKE_UNIQUE_NAME(s_cs_reg_ctx_), new CContextTypeCtor<class_info>(wxT(class_name), clsid))

#endif // !_SINGLE_OBJECT_H__

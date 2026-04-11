#ifndef _TYPE_CTOR_H__
#define _TYPE_CTOR_H__

class ibValue;
class ibCtorAbstractType;

enum ibCtorObjectType {
	ibCtorObjectType_object_primitive = 1,
	ibCtorObjectType_object_value,
	ibCtorObjectType_object_control,
	ibCtorObjectType_object_system,
	ibCtorObjectType_object_enum,
	ibCtorObjectType_object_context,

	ibCtorObjectType_object_metadata,
	ibCtorObjectType_object_meta_value
};

enum ibCtorObjectTypeEvent {
	ibCtorObjectTypeEvent_Register,
	ibCtorObjectTypeEvent_UnRegister,
};

/////////////////////////////////////////////////////////////////////////

template<typename typeCtor>
class value_register {
	class ibCtorAbstractType* m_so;
public:
	value_register(typeCtor* so);
	~value_register();
};

/////////////////////////////////////////////////////////////////////////

#define GENERATE_REGISTER(class_name, class_type, class_so)\
	const value_register class_type = class_so;

/////////////////////////////////////////////////////////////////////////

class ibCtorAbstractType {
public:

	virtual ~ibCtorAbstractType() {}

	virtual wxString GetClassName() const = 0;
	virtual wxClassInfo* GetClassInfo() const = 0;
	virtual ibClassID GetClassType() const = 0;

	virtual wxIcon GetClassIcon() const { return wxNullIcon; }

	virtual ibCtorObjectType GetObjectTypeCtor() const = 0;
	virtual void CallEvent(ibCtorObjectTypeEvent event) {}
	virtual ibValue* CreateObject() const = 0;
};

class ibCtorValueTypeBase : public ibCtorAbstractType {
	wxString m_className;
	wxClassInfo* m_classInfo;
	ibClassID m_clsid;
public:

	virtual ~ibCtorValueTypeBase() {}

	virtual wxString GetClassName() const { return m_className; }
	virtual wxClassInfo* GetClassInfo() const { return m_classInfo; }
	virtual ibClassID GetClassType() const { return m_clsid; }

	ibCtorValueTypeBase(const wxString& className, wxClassInfo* classInfo, const ibClassID& clsid)
		: m_className(className), m_classInfo(classInfo), m_clsid(clsid) {
	}

	virtual ibCtorObjectType GetObjectTypeCtor() const = 0;
	virtual ibValue* CreateObject() const = 0;
};

class ibCtorSingleType : public ibCtorValueTypeBase {
public:

	ibCtorSingleType(const wxString& className, wxClassInfo* classInfo, const ibClassID& clsid)
		: ibCtorValueTypeBase(className, classInfo, clsid)
	{
	}

	virtual ibValueTypes GetValueType() const = 0;
};

//simple types
template <class T>
class ibCtorPrimitiveType : public ibCtorSingleType {
	ibValueTypes m_valType;

public:

	ibCtorPrimitiveType(const wxString& className, ibValueTypes valType, const ibClassID& clsid) :
		ibCtorSingleType(className, CLASSINFO(T), clsid), m_valType(valType) {
	}

	virtual wxIcon GetClassIcon() const { return T::GetIconGroup(); }
	virtual ibValueTypes GetValueType() const { return m_valType; }
	virtual ibCtorObjectType GetObjectTypeCtor() const { return ibCtorObjectType::ibCtorObjectType_object_primitive; }
	virtual void CallEvent(ibCtorObjectTypeEvent event) {
		if (event == ibCtorObjectTypeEvent::ibCtorObjectTypeEvent_Register)
			T::OnRegisterObject(GetClassName(), this);
		else if (event == ibCtorObjectTypeEvent::ibCtorObjectTypeEvent_UnRegister)
			T::OnUnRegisterObject(GetClassName());
	}

	virtual ibValue* CreateObject() const { return new T(m_valType); }
};

#define PRIMITIVE_TYPE_REGISTER(class_info, class_name, class_type, clsid)\
GENERATE_REGISTER(wxT(class_name), wxMAKE_UNIQUE_NAME(s_cs_reg_s_), new ibCtorPrimitiveType<class_info>(wxT(class_name), class_type, clsid))

// object value register - array, struct, etc.. 
template <class T>
class ibCtorValueType : public ibCtorValueTypeBase {

public:

	ibCtorValueType(const wxString& className, const ibClassID& clsid) :
		ibCtorValueTypeBase(className, CLASSINFO(T), clsid) {
	}

	virtual wxIcon GetClassIcon() const { return T::GetIconGroup(); }
	virtual ibCtorObjectType GetObjectTypeCtor() const { return ibCtorObjectType::ibCtorObjectType_object_value; }
	virtual void CallEvent(ibCtorObjectTypeEvent event) {
		if (event == ibCtorObjectTypeEvent::ibCtorObjectTypeEvent_Register)
			T::OnRegisterObject(GetClassName(), this);
		else if (event == ibCtorObjectTypeEvent::ibCtorObjectTypeEvent_UnRegister)
			T::OnUnRegisterObject(GetClassName());
	}

	virtual ibValue* CreateObject() const { return new T(); }
};

#define VALUE_TYPE_REGISTER(class_info, class_name, clsid)\
GENERATE_REGISTER(wxT(class_name), wxMAKE_UNIQUE_NAME(s_cs_reg_val_), new ibCtorValueType<class_info>(wxT(class_name), clsid))

// object with non-create object
template <class T>
class ibCtorSystemType : public ibCtorValueTypeBase {

public:

	ibCtorSystemType(const wxString& className, const ibClassID& clsid) :
		ibCtorValueTypeBase(className, CLASSINFO(T), clsid) {
	}

	virtual wxIcon GetClassIcon() const { return T::GetIconGroup(); }
	virtual ibCtorObjectType GetObjectTypeCtor() const { return ibCtorObjectType::ibCtorObjectType_object_system; }
	virtual void CallEvent(ibCtorObjectTypeEvent event) {
		if (event == ibCtorObjectTypeEvent::ibCtorObjectTypeEvent_Register)
			T::OnRegisterObject(GetClassName(), this);
		else if (event == ibCtorObjectTypeEvent::ibCtorObjectTypeEvent_UnRegister)
			T::OnUnRegisterObject(GetClassName());
	}

	virtual ibValue* CreateObject() const { return nullptr; }
};

#define SYSTEM_TYPE_REGISTER(class_info, class_name, clsid)\
GENERATE_REGISTER(wxT(class_name), wxMAKE_UNIQUE_NAME(s_cs_reg_so_), new ibCtorSystemType<class_info>(wxT(class_name), clsid))

//enumeration register - windowOrient, etc...
template <class T>
class ibCtorEnumType : public ibCtorSingleType {

public:
	ibCtorEnumType(const wxString& className, const ibClassID& clsid) :
		ibCtorSingleType(className, CLASSINFO(T), clsid) {
	}

	virtual wxIcon GetClassIcon() const { return T::GetIconGroup(); }
	virtual ibValueTypes GetValueType() const { return ibValueTypes::TYPE_ENUM; }
	virtual ibCtorObjectType GetObjectTypeCtor() const { return ibCtorObjectType::ibCtorObjectType_object_enum; }
	virtual void CallEvent(ibCtorObjectTypeEvent event) {
		if (event == ibCtorObjectTypeEvent::ibCtorObjectTypeEvent_Register)
			T::OnRegisterObject(GetClassName(), this);
		else if (event == ibCtorObjectTypeEvent::ibCtorObjectTypeEvent_UnRegister)
			T::OnUnRegisterObject(GetClassName());
	}

	virtual ibValue* CreateObject() const {
		T* _ptr = new T();
		_ptr->CreateEnumeration();
		return _ptr;
	};
};

#define ENUM_TYPE_REGISTER(class_info, class_name, clsid)\
GENERATE_REGISTER(wxT(class_name), wxMAKE_UNIQUE_NAME(s_cs_reg_e_), new ibCtorEnumType<class_info>(wxT(class_name), clsid))

template <class T>
class ibCtorContextType : public ibCtorSingleType {
	T* m_innerObject = nullptr;
public:
	ibCtorContextType(const wxString& className, const ibClassID& clsid) :
		ibCtorSingleType(className, CLASSINFO(T), clsid) {
	}

	virtual wxIcon GetClassIcon() const { return T::GetIconGroup(); }
	virtual ibValueTypes GetValueType() const { return ibValueTypes::TYPE_VALUE; }
	virtual ibCtorObjectType GetObjectTypeCtor() const { return ibCtorObjectType::ibCtorObjectType_object_context; }
	virtual void CallEvent(ibCtorObjectTypeEvent event) {
		switch (event)
		{
		case ibCtorObjectTypeEvent_Register:
			m_innerObject = new T();
			m_innerObject->IncrRef();
			T::OnRegisterObject(GetClassName(), this);
			break;
		case ibCtorObjectTypeEvent_UnRegister:
			T::OnUnRegisterObject(GetClassName());
			m_innerObject->DecrRef();
			break;
		}
	}

	virtual ibValue* CreateObject() const { return m_innerObject; }
};

#define CONTEXT_TYPE_REGISTER(class_info, class_name, clsid)\
GENERATE_REGISTER(wxT(class_name), wxMAKE_UNIQUE_NAME(s_cs_reg_ctx_), new ibCtorContextType<class_info>(wxT(class_name), clsid))

#endif // !_SINGLE_OBJECT_H__

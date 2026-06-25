#ifndef _TYPE_CTOR_H__
#define _TYPE_CTOR_H__

#include "backend/clsid.h"      // ibClassID, ib_clsid_hash
#include <typeinfo>             // std::type_info — Phase 3 pilot (typeid registry)

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
// Variadic-macro plumbing for *_TYPE_REGISTER overload-by-arity.
// Lets the same macro name accept either an explicit CLSID
// (legacy 3-arg / 4-arg form) or auto-hash from class_name
// (new 2-arg / 3-arg form, CLSID = ib_clsid_hash(class_name)).

#define IB_EXPAND(x) x
#define IB_CONCAT2(a, b) a##b
#define IB_CONCAT(a, b) IB_CONCAT2(a, b)
#define IB_VA_PICK5(_1, _2, _3, _4, _5, N, ...) N
#define IB_VA_COUNT(...) IB_EXPAND(IB_VA_PICK5(__VA_ARGS__, 5, 4, 3, 2, 1))
#define IB_DISPATCH(prefix, ...) IB_EXPAND(IB_CONCAT(prefix, IB_VA_COUNT(__VA_ARGS__))(__VA_ARGS__))

/////////////////////////////////////////////////////////////////////////

class ibCtorAbstractType {
public:

	virtual ~ibCtorAbstractType() {}

	virtual wxString GetClassName() const = 0;
	// std::type_info replaces the former wxClassInfo* GetClassInfo() — it is the
	// registry's runtime type key (matched against typeid(*liveValue)).
	// Default = typeid(void) for ctors that carry no concrete C++ type
	// (meta/control ctors that derive this base directly); their objects
	// override GetClassType() and never reach the typeid resolution path.
	// See docs/value-audit.md Phase 3.
	virtual const std::type_info& GetTypeInfo() const { return typeid(void); }
	virtual ibClassID GetClassType() const = 0;

	virtual wxIcon GetClassIcon() const { return wxNullIcon; }

	virtual ibCtorObjectType GetObjectTypeCtor() const = 0;
	virtual void CallEvent(ibCtorObjectTypeEvent event) {}
	virtual ibValue* CreateObject() const = 0;

	// Class-factory table trait (see ibValue::IsTableValue): does this TYPE create tabular
	// sources? Answered by CLSID, no instance needed. Default = false; value-type ctors
	// forward to their T, metadata ctors derive it from their meta-kind (List / TabularSection
	// / RecordSet). Lets selection / form-build ask the factory instead of a source explorer.
	virtual bool IsTableValue() const { return false; }
};

class ibCtorValueTypeBase : public ibCtorAbstractType {
	wxString m_className;
	const std::type_info* m_typeInfo;   // typeid(T) — registry runtime type key
	ibClassID m_clsid;
public:

	virtual ~ibCtorValueTypeBase() {}

	virtual wxString GetClassName() const { return m_className; }
	virtual const std::type_info& GetTypeInfo() const { return *m_typeInfo; }
	virtual ibClassID GetClassType() const { return m_clsid; }

	ibCtorValueTypeBase(const wxString& className, const std::type_info& typeInfo, const ibClassID& clsid)
		: m_className(className), m_typeInfo(&typeInfo), m_clsid(clsid) {
	}

	virtual ibCtorObjectType GetObjectTypeCtor() const = 0;
	virtual ibValue* CreateObject() const = 0;
};

class ibCtorSingleType : public ibCtorValueTypeBase {
public:

	ibCtorSingleType(const wxString& className, const std::type_info& typeInfo, const ibClassID& clsid)
		: ibCtorValueTypeBase(className, typeInfo, clsid)
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
		ibCtorSingleType(className, typeid(T), clsid), m_valType(valType) {
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

// 4-arg (legacy): explicit clsid.
#define PRIMITIVE_TYPE_REGISTER_4(class_info, class_name, class_type, clsid)\
GENERATE_REGISTER(wxT(class_name), wxMAKE_UNIQUE_NAME(s_cs_reg_s_), new ibCtorPrimitiveType<class_info>(wxT(class_name), class_type, clsid))
// 3-arg (new): clsid = ib_clsid_hash(class_name).
#define PRIMITIVE_TYPE_REGISTER_3(class_info, class_name, class_type)\
PRIMITIVE_TYPE_REGISTER_4(class_info, class_name, class_type, ib_clsid_hash(class_name))
#define PRIMITIVE_TYPE_REGISTER(...) IB_DISPATCH(PRIMITIVE_TYPE_REGISTER_, __VA_ARGS__)

// object value register - array, struct, etc.. 
template <class T>
class ibCtorValueType : public ibCtorValueTypeBase {

public:

	ibCtorValueType(const wxString& className, const ibClassID& clsid) :
		ibCtorValueTypeBase(className, typeid(T), clsid) {
	}

	virtual wxIcon GetClassIcon() const { return T::GetIconGroup(); }
	virtual ibCtorObjectType GetObjectTypeCtor() const { return ibCtorObjectType::ibCtorObjectType_object_value; }
	// Forward the table trait to the concrete type — T::IsTableValue() resolves (via name
	// lookup) to ibValueModel's gate for models, to ibValue's default otherwise. Pure
	// compile-time: a non-model T never needs ibValueModel visible in its TU.
	virtual bool IsTableValue() const override { return T::IsTableValue(); }
	virtual void CallEvent(ibCtorObjectTypeEvent event) {
		if (event == ibCtorObjectTypeEvent::ibCtorObjectTypeEvent_Register)
			T::OnRegisterObject(GetClassName(), this);
		else if (event == ibCtorObjectTypeEvent::ibCtorObjectTypeEvent_UnRegister)
			T::OnUnRegisterObject(GetClassName());
	}

	virtual ibValue* CreateObject() const { return new T(); }
};

// 3-arg (legacy): explicit clsid.
#define VALUE_TYPE_REGISTER_3(class_info, class_name, clsid)\
GENERATE_REGISTER(wxT(class_name), wxMAKE_UNIQUE_NAME(s_cs_reg_val_), new ibCtorValueType<class_info>(wxT(class_name), clsid))
// 2-arg (new): clsid = ib_clsid_hash(class_name).
#define VALUE_TYPE_REGISTER_2(class_info, class_name)\
VALUE_TYPE_REGISTER_3(class_info, class_name, ib_clsid_hash(class_name))
#define VALUE_TYPE_REGISTER(...) IB_DISPATCH(VALUE_TYPE_REGISTER_, __VA_ARGS__)

// object with non-create object
template <class T>
class ibCtorSystemType : public ibCtorValueTypeBase {

public:

	ibCtorSystemType(const wxString& className, const ibClassID& clsid) :
		ibCtorValueTypeBase(className, typeid(T), clsid) {
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

// 3-arg (legacy): explicit clsid.
#define SYSTEM_TYPE_REGISTER_3(class_info, class_name, clsid)\
GENERATE_REGISTER(wxT(class_name), wxMAKE_UNIQUE_NAME(s_cs_reg_so_), new ibCtorSystemType<class_info>(wxT(class_name), clsid))
// 2-arg (new): clsid = ib_clsid_hash(class_name).
#define SYSTEM_TYPE_REGISTER_2(class_info, class_name)\
SYSTEM_TYPE_REGISTER_3(class_info, class_name, ib_clsid_hash(class_name))
#define SYSTEM_TYPE_REGISTER(...) IB_DISPATCH(SYSTEM_TYPE_REGISTER_, __VA_ARGS__)

//enumeration register - windowOrient, etc...
template <class T>
class ibCtorEnumType : public ibCtorSingleType {

public:
	ibCtorEnumType(const wxString& className, const ibClassID& clsid) :
		ibCtorSingleType(className, typeid(T), clsid) {
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

// 3-arg (legacy): explicit clsid.
#define ENUM_TYPE_REGISTER_3(class_info, class_name, clsid)\
GENERATE_REGISTER(wxT(class_name), wxMAKE_UNIQUE_NAME(s_cs_reg_e_), new ibCtorEnumType<class_info>(wxT(class_name), clsid))
// 2-arg (new): clsid = ib_clsid_hash(class_name).
#define ENUM_TYPE_REGISTER_2(class_info, class_name)\
ENUM_TYPE_REGISTER_3(class_info, class_name, ib_clsid_hash(class_name))
#define ENUM_TYPE_REGISTER(...) IB_DISPATCH(ENUM_TYPE_REGISTER_, __VA_ARGS__)

template <class T>
class ibCtorContextType : public ibCtorSingleType {
	T* m_innerObject = nullptr;
public:
	ibCtorContextType(const wxString& className, const ibClassID& clsid) :
		ibCtorSingleType(className, typeid(T), clsid) {
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

// 3-arg (legacy): explicit clsid.
#define CONTEXT_TYPE_REGISTER_3(class_info, class_name, clsid)\
GENERATE_REGISTER(wxT(class_name), wxMAKE_UNIQUE_NAME(s_cs_reg_ctx_), new ibCtorContextType<class_info>(wxT(class_name), clsid))
// 2-arg (new): clsid = ib_clsid_hash(class_name).
#define CONTEXT_TYPE_REGISTER_2(class_info, class_name)\
CONTEXT_TYPE_REGISTER_3(class_info, class_name, ib_clsid_hash(class_name))
#define CONTEXT_TYPE_REGISTER(...) IB_DISPATCH(CONTEXT_TYPE_REGISTER_, __VA_ARGS__)

#endif // !_SINGLE_OBJECT_H__

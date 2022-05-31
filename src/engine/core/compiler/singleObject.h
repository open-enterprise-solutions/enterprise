#ifndef _SINGLE_OBJECT_H__
#define _SINGLE_OBJECT_H__

enum eObjectType
{
	eObjectType_simple = 1,
	eObjectType_object,
	eObjectType_object_control,
	eObjectType_object_system,
	eObjectType_enum,
	eObjectType_metadata,
	eObjectType_value_metadata
};

class IObjectValueAbstract {
public:
	virtual wxString GetClassName() const = 0;
	virtual wxClassInfo* GetClassInfo() const = 0;
	virtual CLASS_ID GetClassType() const = 0;

	virtual wxBitmap GetClassIcon() const {
		return wxNullBitmap;
	}

	virtual eObjectType GetObjectType() const = 0;
	virtual CValue* CreateObject() const = 0;
};

class IObjectValueSingle : public IObjectValueAbstract {
	wxString m_className;
	wxClassInfo* m_classInfo;
	CLASS_ID m_clsid;
public:

	virtual wxString GetClassName() const { return m_className; }
	virtual wxClassInfo* GetClassInfo() const { return m_classInfo; }
	virtual CLASS_ID GetClassType() const { return m_clsid; }

	IObjectValueSingle(const wxString& className, wxClassInfo* classInfo, const CLASS_ID& clsid)
		: m_className(className), m_classInfo(classInfo), m_clsid(clsid)
	{
	}

	virtual eObjectType GetObjectType() const = 0;
	virtual CValue* CreateObject() const = 0;
};

class ISimpleObjectValueSingle : public IObjectValueSingle {
public:

	ISimpleObjectValueSingle(const wxString& className, wxClassInfo* classInfo, const CLASS_ID& clsid)
		: IObjectValueSingle(className, classInfo, clsid)
	{
	}

	virtual eValueTypes GetValueType() const = 0;
};

//simple types
template <class T>
class CSimpleObjectValueSingle : public ISimpleObjectValueSingle {
	eValueTypes m_valType;
	wxBitmap m_classBMP;
public:

	CSimpleObjectValueSingle(const wxString& className, eValueTypes valType, const CLASS_ID& clsid) : ISimpleObjectValueSingle(className, CLASSINFO(T), clsid), m_valType(valType), m_classBMP(T::GetIconGroup()) {}

	virtual wxBitmap GetClassIcon() const {
		return m_classBMP;
	}

	virtual eValueTypes GetValueType() const 
	{
		return m_valType; 
	}

	virtual eObjectType GetObjectType() const
	{ 
		return eObjectType::eObjectType_simple; 
	}

	virtual CValue* CreateObject() const { 
		return new T(m_valType);
	}
};

// object value register - array, struct, etc.. 
template <class T>
class CObjectValueSingle : public IObjectValueSingle {
	wxBitmap m_classBMP;
public:

	CObjectValueSingle(const wxString& className, const CLASS_ID& clsid) : IObjectValueSingle(className, CLASSINFO(T), clsid), m_classBMP(T::GetIconGroup()) {}

	virtual wxBitmap GetClassIcon() const {
		return m_classBMP;
	}

	virtual eObjectType GetObjectType() const { 
		return eObjectType::eObjectType_object; 
	}
	
	virtual CValue* CreateObject() const { 
		return new T();
	}
};

// object with non-create object
template <class T>
class CSystemObjectValueSingle : public IObjectValueSingle {
	wxBitmap m_classBMP;
public:

	CSystemObjectValueSingle(const wxString& className, const CLASS_ID& clsid) : IObjectValueSingle(className, CLASSINFO(T), clsid), m_classBMP(T::GetIconGroup()) {}

	virtual wxBitmap GetClassIcon() const {
		return m_classBMP;
	}

	virtual eObjectType GetObjectType() const { 
		return eObjectType::eObjectType_object_system; 
	}
	
	virtual CValue* CreateObject() const { 
		return new T(); 
	}
};

// object with non-create object
class IControlValueAbstract : public IObjectValueSingle {
	wxString m_classType;
	wxBitmap m_classBMP;
	bool m_classSystem;
public:

	IControlValueAbstract(const wxString& className, const wxString& classType, char* imageData[], wxClassInfo* classInfo, const CLASS_ID& clsid)
		: IObjectValueSingle(className, classInfo, clsid), m_classSystem(false), m_classType(classType)
	{
		if (imageData) {
			m_classBMP = wxBitmap(imageData);
		}
	}

	IControlValueAbstract(const wxString& className, const wxString& classType, char* imageData[], wxClassInfo* classInfo, bool classSystem, const CLASS_ID& clsid)
		: IObjectValueSingle(className, classInfo, clsid), m_classSystem(classSystem), m_classType(classType)
	{
		if (imageData) {
			m_classBMP = wxBitmap(imageData);
		}
	}

	virtual wxBitmap GetClassIcon() const {
		return GetControlImage();
	}

	wxString GetControlType() const {
		return m_classType;
	}

	wxBitmap GetControlImage() const {
		return m_classBMP;
	}

	bool IsControlSystem() const {
		return m_classSystem;
	}
};

// object with non-create object
template <class T>
class CControlObjectValueSingle : public IControlValueAbstract {
public:

	CControlObjectValueSingle(const wxString& className, const wxString& classType, char* imageData[], const CLASS_ID& clsid)
		: IControlValueAbstract(className, classType, imageData, CLASSINFO(T), clsid) {}

	CControlObjectValueSingle(const wxString& className, const wxString& classType, char* imageData[], bool classSystem, const CLASS_ID& clsid)
		: IControlValueAbstract(className, classType, imageData, CLASSINFO(T), classSystem, clsid) {}

	virtual eObjectType GetObjectType() const { 
		return eObjectType::eObjectType_object_control; 
	}
	
	virtual CValue* CreateObject() const { 
		return new T(); 
	}
};

//enumeration register - windowOrient, etc...
template <class T>
class CEnumObjectValueSingle : public ISimpleObjectValueSingle {
	wxBitmap m_classBMP;
public:
	CEnumObjectValueSingle(const wxString& className, const CLASS_ID& clsid) : ISimpleObjectValueSingle(className, CLASSINFO(T), clsid), m_classBMP(T::GetIconGroup()) {}

	virtual wxBitmap GetClassIcon() const {
		return m_classBMP;
	}

	virtual eValueTypes GetValueType() const { 
		return eValueTypes::TYPE_ENUM; 
	};
	
	virtual eObjectType GetObjectType() const { 
		return eObjectType::eObjectType_enum; 
	}
	
	virtual CValue* CreateObject() const { 
		return new T();
	}
};

//metaobject register document, form, etc ... 
template <class T>
class CMetaObjectValueSingle : public IObjectValueSingle {
	wxBitmap m_classBMP;
public:

	CMetaObjectValueSingle(const wxString& className, const CLASS_ID& clsid) : IObjectValueSingle(className, CLASSINFO(T), clsid), m_classBMP(T::GetIconGroup()) {}

	virtual wxBitmap GetClassIcon() const {
		return m_classBMP;
	}

	virtual eObjectType GetObjectType() const { 
		return eObjectType::eObjectType_metadata; 
	}
	
	virtual CValue* CreateObject() const { 
		return new T();
	}
};

#endif // !_SINGLE_OBJECT_H__

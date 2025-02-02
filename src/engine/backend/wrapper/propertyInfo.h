#ifndef _OBJECT_BASE_H__
#define _OBJECT_BASE_H__

#include <set>

#include "backend/backend_core.h"

#include "backend/fontcontainer.h"
#include "backend/types.h"

///////////////////////////////////////////////////////////////////

class BACKEND_API PropertyCategory;
class BACKEND_API Property;
class BACKEND_API Event;

class BACKEND_API IPropertyObject;
///////////////////////////////////////////////////////////////////

#include "backend/fileSystem/fs.h"

class BACKEND_API OptionList {

	struct OptionVariant {
		wxString m_name;
		wxString m_label;
		wxString m_help;
		int		 m_intVal;
	public:

		operator int() const {
			return m_intVal;
		}

	public:

		OptionVariant(const wxString& name, const int& l) :
			m_name(name), m_label(name), m_intVal(l)
		{
		}

		OptionVariant(const wxString& name, const wxString& label, const int& l) :
			m_name(name), m_label(label), m_intVal(l)
		{
		}

		friend class OptionList;
	};

	std::vector< OptionVariant > m_options;
public:

	void AddOption(const wxString& name, const int& l) {
		(void)m_options.emplace_back(name, l);
	}

	void AddOption(const wxString& name, const wxString& label, const int& l) {
		(void)m_options.emplace_back(name, label, l);
	}

	bool HasValue(const int& l) const {
		auto& it = std::find_if(m_options.begin(), m_options.end(),
			[l](const OptionVariant& opt) {return l == opt; }
		);
		if (it != m_options.end())
			return true;
		return false;
	}

	unsigned int GetOptionCount() const {
		return (unsigned int)m_options.size();
	}

	const std::vector< OptionVariant >& GetOptions() {
		return m_options;
	}
};

///////////////////////////////////////////////////////////////////////////////

class BACKEND_API PropertyCategory {
	wxString m_name;
	wxString m_label;
	wxString m_helpStr;
	std::vector<wxString> m_properties;
	std::vector<wxString> m_events;
	std::vector< PropertyCategory* > m_categories;
	IPropertyObject* m_object;

private:

	PropertyCategory(IPropertyObject* object) :
		m_name("propertyEvents"),
		m_label(_("property and events")),
		m_helpStr(wxEmptyString),
		m_object(object)
	{
	}

	PropertyCategory(const wxString& name, const wxString& label, const wxString& helpString, IPropertyObject* object, PropertyCategory* ownerCat = nullptr) :
		m_name(name),
		m_label(label),
		m_helpStr(helpString),
		m_object(object)
	{
		if (ownerCat != nullptr) ownerCat->AddCategory(this);
	}

public:

	~PropertyCategory() {
		for (auto& category : m_categories) wxDELETE(category);
	}

	void AddProperty(Property* property);
	void AddEvent(Event* event);
	void AddCategory(PropertyCategory* cat);

	wxString GetName() const {
		return m_name;
	}

	wxString GetLabel() const {
		return (m_label.IsEmpty()) ? m_name : m_label;
	}

	wxString GetHelp() const {
		return m_helpStr;
	}

	wxString GetPropertyName(unsigned int index) const {
		if (index < m_properties.size()) return m_properties[index];
		return wxEmptyString;
	}

	wxString GetEventName(unsigned int index) const {
		if (index < m_events.size()) return m_events[index];
		return wxEmptyString;
	}

	PropertyCategory* GetCategory(unsigned int index) const {
		if (index < m_categories.size()) return m_categories[index];
		return new PropertyCategory(m_object);
	}

	IPropertyObject* GetObject() const {
		return m_object;
	}

	unsigned int GetPropertyCount() const {
		return m_properties.size();
	}

	unsigned int GetEventCount() const {
		return m_events.size();
	}

	unsigned int GetCategoryCount() const {
		return m_categories.size();
	}

	friend class IPropertyObject;
};

///////////////////////////////////////////////////////////////////////////////

class BACKEND_API Property {
	wxString        m_name;
	wxString		m_label;
	wxString		m_helpStr;
	PropertyType    m_type;
	IPropertyObject* m_object; // pointer to the owner object
	wxVariant       m_value;
	friend class IPropertyObject;
private:
	void InitProperty(PropertyCategory* cat, const wxVariant& value = wxNullVariant);
protected:
	Property(const wxString& name, const wxString& label, const wxString& helpString,
		PropertyType type, PropertyCategory* cat, const wxVariant& value = wxNullVariant) :
		m_name(name),
		m_label(label),
		m_helpStr(helpString),
		m_type(type),
		m_object(cat->GetObject())
	{
		InitProperty(cat, value);
	}
public:

	virtual bool IsOk() const {
		if (m_value.IsNull())
			return false;
		if (m_type == PropertyType::PT_WXNAME ||
			m_type == PropertyType::PT_WXSTRING ||
			m_type == PropertyType::PT_TEXT) {
			const wxString& value = m_value.GetString();
			if (value.IsEmpty())
				return false;
		}
		return true;
	}

	bool IsEditable() const;

	IPropertyObject* GetObject() const {
		return m_object;
	}

	wxString GetName() const {
		return m_name;
	}

	PropertyType GetType() const {
		return m_type;
	}

	wxString GetLabel() const {
		return m_label.IsEmpty() ?
			m_name : m_label;
	}

	wxString GetHelp() const {
		return m_helpStr;
	}

	wxVariant& GetValue() {
		return m_value;
	}

	////////////////////

	void SetValue(const wxVariant& val) {
		m_value = val;
	}

	void SetValue(wxString& val) {
		m_value = val;
	}

	void SetValue(const wxChar* val) {
		m_value = val;
	}

	////////////////////

	Property& operator =(const wxVariant& val) {
		SetValue(val);
		return *this;
	}

	Property& operator =(const wxString& val) {
		SetValue(val);
		return *this;
	}

	Property& operator =(const wxChar& val) {
		SetValue(val);
		return *this;
	}

	Property& operator =(const wxArrayString& str) {
		SetValue(str);
		return *this;
	}

	Property& operator =(const wxFontContainer& font) {
		SetValue(font);
		return *this;
	}

	Property& operator =(const wxColour& colour) {
		SetValue(colour);
		return *this;
	}

	Property& operator =(const wxBitmap& bmp) {
		SetValue(bmp);
		return *this;
	}

	Property& operator =(const wxPoint& point) {
		SetValue(point);
		return *this;
	}

	Property& operator =(const wxSize& size) {
		SetValue(size);
		return *this;
	}

	Property& operator =(const bool boolean) {
		SetValue(boolean);
		return *this;
	}

	Property& operator =(const int integer) {
		SetValue(integer);
		return *this;
	}

	Property& operator =(const long long integer) {
		SetValue(integer);
		return *this;
	}

	Property& operator =(const number_t& val) {
		SetValue(val);
		return *this;
	}

	////////////////////

	void SetValue(const wxArrayString& str);
	void SetValue(const wxFontContainer& font);
	void SetValue(const wxColour& colour);
	void SetValue(const wxBitmap& bmp);
	void SetValue(const wxString& str, bool format = false);
	void SetValue(const wxPoint& point);
	void SetValue(const wxSize& size);
	void SetValue(const bool boolean);
	void SetValue(const signed int integer);
	void SetValue(const unsigned int integer);
	void SetValue(const long long integer);
	void SetValue(const number_t& val);

	wxFontContainer GetValueAsFont() const;
	wxColour GetValueAsColour() const;
	wxPoint  GetValueAsPoint() const;
	wxSize   GetValueAsSize() const;
	bool     GetValueAsBoolean() const;
	int      GetValueAsInteger() const;
	wxString GetValueAsString() const;
	wxBitmap GetValueAsBitmap() const;
	wxString GetValueAsText() const;   // sustituye los ('\n',...) por ("\\n",...)
	wxArrayString GetValueAsArrayString() const;
	number_t GetValueAsNumber() const;

	////////////////////

	bool IsNull() const;

	////////////////////

	virtual OptionList* GetOptionList() {
		return nullptr;
	}

	//load & save object in control 
	virtual bool LoadData(CMemoryReader& reader);
	virtual bool SaveData(CMemoryWriter& writer);

	/**
	* Set/Get property data
	*/
	virtual void SetDataValue(const CValue& srcValue);
	virtual CValue GetDataValue() const;
};

class BACKEND_API PropertyOption : public Property {

	class PropertyFunctor {
	public:
		virtual OptionList* Invoke(PropertyOption* property) = 0;
	};
	template <typename optClass>
	class ObjectPropertyFunctor : public PropertyFunctor {
		OptionList* (optClass::* m_funcHandler)(PropertyOption*);
		optClass* m_handler;
	public:
		ObjectPropertyFunctor(OptionList* (optClass::* funcHandler)(PropertyOption*), optClass* handler)
		: m_funcHandler(funcHandler), m_handler(handler)
		{
		}
		virtual OptionList* Invoke(PropertyOption* property) override {
			return (m_handler->*m_funcHandler)(property);
		}
	};

	PropertyFunctor* m_functor;

	friend class IPropertyObject;

protected:

	template <typename optClass>
	PropertyOption(const wxString& name, const wxString& label, const wxString& helpString,
		OptionList* (optClass::* funcHandler)(PropertyOption*), PropertyCategory* cat, const wxVariant& value = wxNullVariant)
		: Property(name, label, helpString, PropertyType::PT_OPTION, cat, value)
	{
		m_functor = new ObjectPropertyFunctor<optClass>(funcHandler, (optClass*)cat->GetObject());
	}

	virtual class IEnumerationWrapper* CreateEnum() const {
		return nullptr;
	}

public:

	/**
	* Set/Get property data
	*/
	virtual void SetDataValue(const CValue& srcValue);
	virtual CValue GetDataValue() const;

public:

	virtual OptionList* GetOptionList() {
		if (m_functor != nullptr)
			return m_functor->Invoke(this);
		return nullptr;
	}
};

template <typename enumClass>
class PropertyEnumOption : public PropertyOption {

	template <typename optClass>
	PropertyEnumOption(const wxString& name, const wxString& label, const wxString& helpString,
		OptionList* (optClass::* funcHandler)(PropertyEnumOption< enumClass>*), PropertyCategory* cat, const wxVariant& value = wxNullVariant) :
		PropertyOption(name, label, helpString, (OptionList* (optClass::*)(PropertyOption*))funcHandler, cat, value)
	{
	}

public:

	virtual IEnumerationWrapper* CreateEnum() const {
		return new enumClass();
	}

private:

	friend class IPropertyObject;
};

class BACKEND_API PropertyBitlist : public Property {

	class PropertyFunctor {
	public:
		virtual OptionList* Invoke(PropertyBitlist* property) = 0;
	};
	template <typename optClass>
	class ObjectPropertyFunctor : public PropertyFunctor {
		OptionList* (optClass::* m_funcHandler)(PropertyBitlist*);
		optClass* m_handler;
	public:
		ObjectPropertyFunctor(OptionList* (optClass::* funcHandler)(PropertyBitlist*), optClass* handler)
			: m_funcHandler(funcHandler), m_handler(handler)
		{
		}
		virtual OptionList* Invoke(PropertyBitlist* property) override {
			return (m_handler->*m_funcHandler)(property);
		}
	};

	PropertyFunctor* m_functor;

	friend class IPropertyObject;

private:

	template <typename optClass>
	PropertyBitlist(const wxString& name, const wxString& label, const wxString& helpString,
		OptionList* (optClass::* funcHandler)(PropertyBitlist*), PropertyCategory* cat, const wxVariant& value = wxNullVariant)
		: Property(name, label, helpString, PropertyType::PT_BITLIST, cat, value)
	{
		m_functor = new ObjectPropertyFunctor<optClass>(funcHandler, (optClass*)cat->GetObject());
	}

public:

	virtual OptionList* GetOptionList() {
		if (m_functor != nullptr)
			return m_functor->Invoke(this);
		return nullptr;
	}
};

class BACKEND_API Event {
	wxString m_name;
	EventType m_type;
	wxString m_label;
	wxString m_helpStr;
	std::vector<wxString> m_args;
	IPropertyObject* m_object; // pointer to the owner object
	wxString m_value;  // handler function name

	friend class IPropertyObject;

private:
	void InitEvent(PropertyCategory* cat);
protected:

	Event(const wxString& eventName, const wxString& descr, const wxString& helpString,
		std::vector<wxString>& args,
		PropertyCategory* cat, const wxString& value = wxEmptyString) :
		m_name(eventName),
		m_label(descr),
		m_helpStr(helpString),
		m_type(EventType::ET_EVENT),
		m_args(args),
		m_value(value),
		m_object(cat->GetObject())
	{
		InitEvent(cat);
	}

	Event(const wxString& eventName, const wxString& descr, const wxString& helpString,
		EventType type, PropertyCategory* cat, const wxString& value = wxEmptyString) :
		m_name(eventName),
		m_label(descr),
		m_helpStr(helpString),
		m_type(type),
		m_args(),
		m_value(value),
		m_object(cat->GetObject())
	{
		InitEvent(cat);
	}

	Event(const wxString& eventName, const wxString& descr, const wxString& helpString,
		EventType type, std::vector<wxString>& args, PropertyCategory* cat, const wxString& value = wxEmptyString) :
		m_name(eventName),
		m_label(descr),
		m_helpStr(helpString),
		m_type(type),
		m_args(args),
		m_value(value),
		m_object(cat->GetObject())
	{
		InitEvent(cat);
	}

public:

	void SetValue(const wxString& value) {
		m_value = value;
	}

	wxString GetValue() const {
		return m_value;
	}

	std::vector<wxString>& GetArgs() {
		return m_args;
	}

	wxString GetName() const {
		return m_name;
	}

	IPropertyObject* GetObject() const {
		return m_object;
	}

	wxString GetLabel() const {
		return m_label.IsEmpty() ?
			m_name : m_label;
	}

	EventType GetType() const {
		return m_type;
	}

	wxString GetHelp() const {
		return m_helpStr;
	}

	virtual OptionList* GetOptionList() {
		return nullptr;
	}

	//load & save object in control 
	virtual bool LoadData(CMemoryReader& reader);
	virtual bool SaveData(CMemoryWriter& writer);
};

class BACKEND_API EventAction : public Event {
	class EventFunctor {
	public:
		virtual OptionList* Invoke(EventAction* event) = 0;
	};
	template <typename optClass>
	class ObjectEventFunctor : public EventFunctor {
		OptionList* (optClass::* m_funcHandler)(EventAction*);
		optClass* m_handler;
	public:
		ObjectEventFunctor(OptionList* (optClass::* funcHandler)(EventAction*), optClass* handler)
			: m_funcHandler(funcHandler), m_handler(handler)
		{
		}
		virtual OptionList* Invoke(EventAction* event) override {
			return (m_handler->*m_funcHandler)(event);
		}
	};

	EventFunctor* m_functor;

	friend class IPropertyObject;

private:

	template <typename optClass>
	EventAction(const wxString& name, const wxString& label, const wxString& helpString,
		OptionList* (optClass::* funcHandler)(EventAction*), PropertyCategory* cat, const wxString& value = wxEmptyString)
		: Event(name, label, helpString, EventType::ET_ACTION, cat, value)
	{
		m_functor = new ObjectEventFunctor<optClass>(funcHandler, (optClass*)cat->GetObject());
	}

	template <typename optClass>
	EventAction(const wxString& name, const wxString& label, const wxString& helpString,
		std::vector<wxString>& args, OptionList* (optClass::* funcHandler)(EventAction*), PropertyCategory* cat, const wxString& value = wxEmptyString)
		: Event(name, label, helpString, EventType::ET_ACTION, args, cat, value)
	{
		m_functor = new ObjectEventFunctor<optClass>(funcHandler, (optClass*)cat->GetObject());
	}

public:

	virtual OptionList* GetOptionList() {
		if (m_functor != nullptr)
			return m_functor->Invoke(this);
		return nullptr;
	}
};

///////////////////////////////////////////////////////////////////////////////

enum eSelectorDataType {
	eSelectorDataType_any,
	eSelectorDataType_reference,
	eSelectorDataType_table,
	eSelectorDataType_resource,
};

enum eSourceDataType {
	eSourceDataVariant_table,
	eSourceDataVariant_tableColumn,
	eSourceDataVariant_attribute,
};

///////////////////////////////////////////////////////////////////////////////

class BACKEND_API IPropertyObject {
	std::map<wxString, Property*> m_properties;
	std::map<wxString, Event*> m_events;
protected:

	struct propertyName_t {
		wxString m_propName;
		wxString m_propLabel;
		wxString m_propHelpStr;
		propertyName_t(const wxString& strPropName) : m_propName(strPropName) {}
		propertyName_t(const wxString& strPropName, const wxString& propLabel) : m_propName(strPropName), m_propLabel(propLabel) {}
		propertyName_t(const wxString& strPropName, const wxString& propLabel, const wxString& propHelpStr) : m_propName(strPropName), m_propLabel(propLabel), m_propHelpStr(propHelpStr) {}
		propertyName_t(const char* strPropName) : m_propName(strPropName) {}
	};

	Property* CreateProperty(const propertyName_t& name, PropertyType type, const wxVariant& defValue = wxNullVariant) {
		return IPropertyObject::CreateProperty(m_category, name, type, defValue);
	}

	template<typename valtype>
	Property* CreateProperty(const propertyName_t& name, PropertyType type, const valtype& defValue) {
		return IPropertyObject::CreateProperty(m_category, name, type, defValue);
	}

	Property* CreateTypeProperty(const propertyName_t& name) {
		return IPropertyObject::CreateTypeProperty(m_category, name);
	}

	Property* CreateSourceProperty(const propertyName_t& name) {
		return IPropertyObject::CreateSourceProperty(m_category, name);
	}

	Property* CreateRecordProperty(const propertyName_t& name) {
		return IPropertyObject::CreateRecordProperty(m_category, name);
	}

	Property* CreateOwnerProperty(const propertyName_t& name) {
		return IPropertyObject::CreateOwnerProperty(m_category, name);
	}

	Property* CreateGenerationProperty(const propertyName_t& name) {
		return IPropertyObject::CreateGenerationProperty(m_category, name);
	}

	Property* CreateProperty(PropertyCategory* cat, const propertyName_t& name, PropertyType type, const wxVariant& defValue = wxNullVariant) {
		if (type == PropertyType::PT_TYPE ||
			type == PropertyType::PT_SOURCE ||
			type == PropertyType::PT_RECORD ||
			type == PropertyType::PT_OWNER ||
			type == PropertyType::PT_GENERATION)
			return nullptr;
		return new Property(name.m_propName, name.m_propLabel, name.m_propHelpStr, type, cat, defValue);
	}

	Property* CreateTypeProperty(PropertyCategory* cat, const propertyName_t& name) {
		return new Property(name.m_propName, name.m_propLabel, name.m_propHelpStr, PropertyType::PT_TYPE, cat);
	}

	Property* CreateSourceProperty(PropertyCategory* cat, const propertyName_t& name) {
		return new Property(name.m_propName, name.m_propLabel, name.m_propHelpStr, PropertyType::PT_SOURCE, cat);
	}

	Property* CreateRecordProperty(PropertyCategory* cat, const propertyName_t& name) {
		return new Property(name.m_propName, name.m_propLabel, name.m_propHelpStr, PropertyType::PT_RECORD, cat);
	}

	Property* CreateOwnerProperty(PropertyCategory* cat, const propertyName_t& name) {
		return new Property(name.m_propName, name.m_propLabel, name.m_propHelpStr, PropertyType::PT_OWNER, cat);
	}

	Property* CreateGenerationProperty(PropertyCategory* cat, const propertyName_t& name) {
		return new Property(name.m_propName, name.m_propLabel, name.m_propHelpStr, PropertyType::PT_GENERATION, cat);
	}

	Property* CreateProperty(PropertyCategory* cat, const propertyName_t& name, PropertyType type, const wxArrayString& str);
	Property* CreateProperty(PropertyCategory* cat, const propertyName_t& name, PropertyType type, const wxFontContainer& font);
	Property* CreateProperty(PropertyCategory* cat, const propertyName_t& name, PropertyType type, const wxColour& colour);
	Property* CreateProperty(PropertyCategory* cat, const propertyName_t& name, PropertyType type, const wxBitmap& bmp);
	Property* CreateProperty(PropertyCategory* cat, const propertyName_t& name, PropertyType type, const wxString& str, bool format = false);
	Property* CreateProperty(PropertyCategory* cat, const propertyName_t& name, PropertyType type, const wxPoint& point);
	Property* CreateProperty(PropertyCategory* cat, const propertyName_t& name, PropertyType type, const wxSize& size);
	Property* CreateProperty(PropertyCategory* cat, const propertyName_t& name, PropertyType type, const bool boolean);
	Property* CreateProperty(PropertyCategory* cat, const propertyName_t& name, PropertyType type, const int integer);
	Property* CreateProperty(PropertyCategory* cat, const propertyName_t& name, PropertyType type, const long long integer);
	Property* CreateProperty(PropertyCategory* cat, const propertyName_t& name, PropertyType type, const number_t& val);

	template <typename optClass>
	Property* CreateProperty(PropertyCategory* cat, const propertyName_t& name, OptionList* (optClass::* funcHandler)(PropertyOption*), const wxVariant& defValue = wxNullVariant) {
		return new PropertyOption(name.m_propName, name.m_propLabel, name.m_propHelpStr, funcHandler, cat, defValue);
	}

	template <typename optClass, typename valClass>
	Property* CreateProperty(PropertyCategory* cat, const propertyName_t& name, OptionList* (optClass::* funcHandler)(PropertyEnumOption<valClass>*), const wxVariant& defValue = wxNullVariant) {
		return new PropertyEnumOption(name.m_propName, name.m_propLabel, name.m_propHelpStr, funcHandler, cat, defValue);
	}

	template <typename optClass>
	Property* CreateProperty(PropertyCategory* cat, const propertyName_t& name, OptionList* (optClass::* funcHandler)(PropertyBitlist*), const wxVariant& defValue = wxNullVariant) {
		return new PropertyBitlist(name.m_propName, name.m_propLabel, name.m_propHelpStr, funcHandler, cat, defValue);
	}

	Event* CreateEvent(const propertyName_t& name, std::vector<wxString> args = {}) {
		return CreateEvent(m_category, name, args);
	}

	template <typename optClass>
	Event* CreateEvent(const propertyName_t& name, OptionList* (optClass::* funcHandler)(EventAction*), const wxString& defValue = wxEmptyString) {
		return CreateEvent(m_category, name, funcHandler, defValue);
	}

	template <typename optClass>
	Event* CreateEvent(const propertyName_t& name, std::vector<wxString> args, OptionList* (optClass::* funcHandler)(EventAction*), const wxString& defValue = wxEmptyString) {
		return CreateEvent(m_category, name, args, funcHandler, defValue);
	}

	Event* CreateEvent(PropertyCategory* cat, const propertyName_t& name, std::vector<wxString> args = {}) {
		return new Event(name.m_propName, name.m_propLabel, name.m_propHelpStr, args, cat);
	}

	template <typename optClass>
	Event* CreateEvent(PropertyCategory* cat, const propertyName_t& name, OptionList* (optClass::* funcHandler)(EventAction*), const wxString& defValue = wxEmptyString) {
		return new EventAction(name.m_propName, name.m_propLabel, name.m_propHelpStr, funcHandler, cat, defValue);
	}

	template <typename optClass>
	Event* CreateEvent(PropertyCategory* cat, const propertyName_t& name, std::vector<wxString> args, OptionList* (optClass::* funcHandler)(EventAction*), const wxString& defValue = wxEmptyString) {
		return new EventAction(name.m_propName, name.m_propLabel, name.m_propHelpStr, args, funcHandler, cat, defValue);
	}

	PropertyCategory* CreatePropertyCategory(const propertyName_t& name, PropertyCategory* ownerCat = nullptr) {
		return new PropertyCategory(name.m_propName, name.m_propLabel, name.m_propHelpStr, this, ownerCat ? ownerCat : m_category);
	}

protected:

	bool m_propEnabled = true;

	IPropertyObject* m_parent = nullptr;

	PropertyCategory* m_category;
	std::vector<IPropertyObject*> m_children;

protected:

	// utilites for implementing the tree
	static const int INDENT;  // size of indent
	wxString GetIndentString(int indent) const; // obtiene la cadena con el indentado

	std::vector<IPropertyObject*>& GetChildren() {
		return m_children;
	}

	std::map<wxString, Property*>& GetProperties() {
		return m_properties;
	}

	// devuelve el puntero "this"
	IPropertyObject* GetThis() {
		return this;
	}

	IPropertyObject() : m_parent(nullptr) {
		m_category = new PropertyCategory(this);
	}

	//friend class CObjectInspector;

	friend class Property;
	friend class Event;

	/**
	* Añade una propiedad al objeto.
	*
	* Este método será usado por el registro de descriptores para crear la
	* instancia del objeto.
	* Los objetos siempre se crearán a través del registro de descriptores.
	*/
	void AddProperty(Property* property);
	void AddEvent(Event* event);

public:

	virtual ~IPropertyObject();

	virtual bool IsEditable() const {
		return m_propEnabled;
	}

	virtual void SetReadOnly(bool enabled = true) {
		m_propEnabled = enabled;
	}

	/**
	* Obtiene el nombre del objeto.
	*
	* @note No confundir con la propiedad nombre que tienen algunos objetos.
	*       Cada objeto tiene un nombre, el cual será el mismo que el usado
	*       como clave en el registro de descriptores.
	*/
	virtual wxString GetClassName() const = 0;

	/// Gets the parent object
	IPropertyObject* GetParent() const {
		return m_parent;
	}

	/// Links the object to a parent
	virtual void SetParent(IPropertyObject* parent) {
		m_parent = parent;
	}

	/**
	* Devuelve la posicion del hijo o GetParentPosition() en caso de no encontrarlo
	*/
	virtual unsigned int GetParentPosition() const;

	/**
	* Obtiene la propiedad identificada por el nombre.
	*
	* @note Notar que no existe el método SetProperty, ya que la modificación
	*       se hace a través de la referencia.
	*/
	Property* GetProperty(const wxString& nameParam) const;
	Event* GetEvent(const wxString& nameParam) const;

	/**
	* Obtiene el número de propiedades del objeto.
	*/
	unsigned int GetPropertyCount() const {
		return (unsigned int)m_properties.size();
	}

	unsigned int GetEventCount() const {
		return m_events.size();
	}

	Property* GetProperty(unsigned int idx) const; // throws ...;
	Event* GetEvent(unsigned int idx) const; // throws ...;

	/**
	* Devuelve el primer antecesor cuyo tipo coincida con el que se pasa
	* como parámetro.
	*
	* Será útil para encontrar el widget padre.
	*/
	IPropertyObject* FindNearAncestor(const wxString& type) const;
	IPropertyObject* FindNearAncestorByBaseClass(const wxString& type) const;

	/**
	* Añade un hijo al objeto.
	* Esta función es virtual, debido a que puede variar el comportamiento
	* según el tipo de objeto.
	*
	* @return true si se añadió el hijo con éxito y false en caso contrario.
	*/
	virtual bool AddChild(IPropertyObject*);
	virtual bool AddChild(unsigned int idx, IPropertyObject* obj);

	/**
	* Devuelve la posicion del hijo o GetChildCount() en caso de no encontrarlo
	*/
	virtual unsigned int GetChildPosition(IPropertyObject* obj) const;
	virtual bool ChangeChildPosition(IPropertyObject* obj, unsigned int pos);

	/**
	* Elimina un hijo del objeto.
	*/
	virtual void RemoveChild(IPropertyObject* obj);
	virtual void RemoveChild(unsigned int idx);

	virtual void RemoveAllChildren() {
		m_children.clear();
	}

	/**
	* Obtiene un hijo del objeto.
	*/
	IPropertyObject* GetChild(unsigned int idx) const;
	IPropertyObject* GetChild(unsigned int idx, const wxString& type) const;

	/**
	* Obtiene el número de hijos del objeto.
	*/
	unsigned int GetChildCount() const {
		return (unsigned int)m_children.size();
	}

	unsigned int GetPropertyIndex(const wxString& nameParam) const;

	Property* GetPropertyByIndex(unsigned int idx) const {
		if (m_properties.size() < idx)
			return nullptr;

		auto properties_iterator = m_properties.begin();
		std::advance(properties_iterator, idx);
		return (*properties_iterator).second;
	}

	/**
	* Devuelve el tipo de objeto.
	*
	* Deberá ser redefinida en cada clase derivada.
	*/
	virtual wxString GetObjectTypeName() const = 0;

	/**
	* Devuelve la profundidad  del objeto en el arbol.
	*/
	virtual int GetComponentType() const = 0;

	/**
	* Property events
	*/
	virtual void OnPropertyCreated() {}
	virtual void OnPropertyCreated(Property* property) {}
	virtual void OnPropertyRefresh(class wxPropertyGridManager* pg, class wxPGProperty* pgProperty, Property* property) {}
	virtual void OnPropertySelected(Property* property) {}
	virtual bool OnPropertyChanging(Property* property, const wxVariant& newValue) {
		return true;
	}

	virtual void OnPropertyChanged(Property* property, const wxVariant& oldValue, const wxVariant& newValue) {}

	virtual void OnEventCreated() {}
	virtual void OnEventCreated(Event* event) {}
	virtual void OnEventRefresh(class wxPropertyGridManager* pg, class wxPGProperty* pgProperty, Event* event) {}
	virtual void OnEventSelected(Event* event) {}

	virtual bool OnEventChanging(Event* event, const wxVariant& newValue) {
		return true;
	}

	virtual void OnEventChanged(Event* property, const wxVariant& oldValue, const wxVariant& newValue) {}

	/**
	* Comprueba si el tipo es derivado del que se pasa como parámetro.
	*/

	bool IsSubclassOf(const wxString& clsName) const;

	PropertyCategory* GetCategory() const {
		return m_category;
	}

	// get metaData from object
	virtual class IMetaData* GetMetaData() const {
		return nullptr;
	}

	//get data selector 
	virtual eSelectorDataType GetSelectorDataType() const {
		return eSelectorDataType::eSelectorDataType_reference;
	}

	IPropertyObject* GetChildPtr(unsigned int idx) const {
		return GetChild(idx);
	}

	virtual void DeleteRecursive();

	wxDECLARE_ABSTRACT_CLASS(IPropertyObject);
};

#endif
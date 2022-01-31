#ifndef _OBJECTBASE_H__
#define _OBJECTBASE_H__

#include "formdefs.h"
#include "types.h"
#include "fontcontainer.h"

struct Option
{
	wxString m_option;
	wxString m_description;
	long m_intVal;

	Option(const wxString strOpt, long l) : m_option(strOpt), m_intVal(l) {}

	operator long() { return m_intVal; }
};

#include <set>

class OptionList
{
	std::vector< Option > m_options;

public:

	void AddOption(const wxString &option, long l) { m_options.emplace_back(option, l); }
	unsigned int GetOptionCount() { return (unsigned int)m_options.size(); }
	const std::vector< Option >& GetOptions() { return m_options; }
};

///////////////////////////////////////////////////////////////////////////////

class Property
{
	class PropertyFunctor
	{
	public:

		virtual OptionList* Invoke(Property *property) = 0;
	};

	template <typename optClass>
	class ObjectPropertyFunctor : public PropertyFunctor {

		OptionList*(optClass::*m_funcHandler)(Property *);
		optClass *m_handler;

	public:

		ObjectPropertyFunctor(OptionList*(optClass::*funcHandler)(Property *), optClass *handler)
			: m_funcHandler(funcHandler), m_handler(handler)
		{
		}

		virtual OptionList *Invoke(Property *property) override
		{
			return (m_handler->*m_funcHandler)(property);
		}
	};

	wxString        m_name;
	PropertyType    m_type;
	wxString		m_description;
	IObjectBase*    m_object; // pointer to the owner object
	wxString        m_value;

	PropertyFunctor *m_functor;

public:

	Property() {}

	Property(const wxString &name, PropertyType type, IObjectBase* obj)
		: m_name(name), m_type(type), m_object(obj)
	{
		m_functor = NULL;
	}

	template <typename optClass>
	Property(const wxString &name, PropertyType type, OptionList*(optClass::*funcHandler)(Property *), optClass* obj)
		: m_name(name), m_type(type), m_object(obj)
	{
		m_functor = new ObjectPropertyFunctor<optClass>(funcHandler, obj);
	}

public:

	bool IsEditable();

	IObjectBase* GetObject() { return m_object; }
	wxString GetName() { return m_name; }
	wxString GetValue() { return m_value; }
	PropertyType GetType() { return m_type; }
	wxString GetDescription() { return m_description; }

	////////////////////

	void SetValue(wxString& val) { m_value = val; }
	void SetValue(const wxChar* val) { m_value = val; }

	////////////////////

	void SetValue(const wxArrayString &str);
	void SetValue(const wxFontContainer &font);
	void SetValue(const wxColour &colour);
	void SetValue(const wxBitmap &bmp);
	void SetValue(const wxString &str, bool format = false);
	void SetValue(const wxPoint &point);
	void SetValue(const wxSize &size);
	void SetValue(const int integer);
	void SetValue(const long long integer);
	void SetValue(const double val);

	wxFontContainer GetValueAsFont();
	wxColour GetValueAsColour();
	wxPoint  GetValueAsPoint();
	wxSize   GetValueAsSize();
	int      GetValueAsInteger();
	wxString GetValueAsString();
	wxBitmap GetValueAsBitmap();
	wxString GetValueAsText();   // sustituye los ('\n',...) por ("\\n",...)
	wxArrayString GetValueAsArrayString();
	double GetValueAsFloat();

	////////////////////

	OptionList *GetOptionList()
	{
		if (m_functor)
			return m_functor->Invoke(this);

		return NULL;
	}

	OptionList *GetTypelist();

	bool IsNull();
};

class Event
{
	wxString m_name;
	wxString m_description;

	std::vector<wxString> m_args;

	IObjectBase *m_object; // pointer to the owner object
	wxString m_value;  // handler function name

public:

	Event() {}
	Event(const wxString &eventName, std::vector<wxString> &args,
		const wxString &description,
		IObjectBase* obj) :
		m_name(eventName), m_args(args),
		m_description(description),
		m_object(obj)
	{
	}

public:

	void SetValue(const wxString &value) { m_value = value; }
	wxString GetValue() { return m_value; }
	std::vector<wxString> &GetArgs() { return m_args; }

	wxString GetName() { return m_name; }
	IObjectBase *GetObject() { return m_object; }
	wxString GetDescription() { return m_description; }
};

///////////////////////////////////////////////////////////////////////////////

class IObjectBase
{
	std::map<wxString, Property *> m_properties;
	std::map<wxString, Event *> m_events;

protected:

	class PropertyContainer
	{
		wxString m_name;
		wxString m_description;
		wxString m_helpString;

		std::vector<wxString> m_properties;
		std::set<wxString> m_propertiesVisible;
		std::vector<wxString> m_events;
		std::set<wxString> m_eventsVisible;

		std::vector< PropertyContainer* > m_categories;

		IObjectBase *m_ownerCategory;

	protected:

		PropertyContainer(IObjectBase *ownerCategory) : m_name("propertyEvents"), m_description("Property and events"), m_helpString("Property and events"),
			m_ownerCategory(ownerCategory) {}

		PropertyContainer(const wxString &name, IObjectBase *ownerCategory) : m_name(name), m_ownerCategory(ownerCategory) {}
		PropertyContainer(const wxString &name, const wxString &description, IObjectBase *ownerCategory) : m_name(name), m_description(description), m_ownerCategory(ownerCategory) {}
		PropertyContainer(const wxString &name, const wxString &description, const wxString &helpString, IObjectBase *ownerCategory) : m_name(name), m_description(description), m_helpString(helpString), m_ownerCategory(ownerCategory) {}

	public:

		void AddProperty(const wxString &name, PropertyType type, bool visible = true)
		{
			m_ownerCategory->AddProperty(new Property(name, type, m_ownerCategory));

			if (visible) {
				m_propertiesVisible.insert(name);
			}

			m_properties.push_back(name);
		}

		template <typename optClass>
		void AddProperty(const wxString &name, PropertyType type, OptionList*(optClass::*oFunc)(Property *property), bool visible = true)
		{
			m_ownerCategory->AddProperty(new Property(name, type, oFunc, dynamic_cast<optClass *>(m_ownerCategory)));

			if (visible) {
				m_propertiesVisible.insert(name);
			}

			m_properties.push_back(name);
		}

		void ShowProperty(const wxString &name) { m_propertiesVisible.insert(name); }
		void HideProperty(const wxString &name) { m_propertiesVisible.erase(name); }

		bool IsVisibleProperty(const wxString &name) { return m_propertiesVisible.count(name) != 0; }

		void AddEvent(const wxString &name, std::vector<wxString> args = {},
			const wxString &description = wxEmptyString, bool visible = true)
		{
			m_ownerCategory->AddEvent(new Event(name, args, description, m_ownerCategory));

			if (visible) {
				m_eventsVisible.insert(name);
			}

			m_events.push_back(name);
		}

		void ShowEvent(const wxString &name) { m_eventsVisible.insert(name); }

		void HideEvent(const wxString &name) { m_eventsVisible.erase(name); }

		bool IsVisibleEvent(const wxString &name) { return m_eventsVisible.count(name) != 0; }

		void AddCategory(PropertyContainer* category) { m_categories.push_back(category); }

		wxString GetName() { return m_name; }

		wxString GetDescription() { return (m_description.IsEmpty()) ? m_name : m_description; }

		wxString GetPropertyName(unsigned int index)
		{
			if (index < m_properties.size()) {
				return m_properties[index];
			}

			return wxEmptyString;
		}

		wxString GetEventName(unsigned int index)
		{
			if (index < m_events.size()) {
				return m_events[index];
			}

			return wxEmptyString;
		}

		PropertyContainer* GetCategory(unsigned int index)
		{
			if (index < m_categories.size()) {
				return m_categories[index];
			}

			return new PropertyContainer(m_ownerCategory);
		}

		unsigned int GetPropertyCount() { return m_properties.size(); }
		unsigned int GetEventCount() { return m_events.size(); }
		unsigned int GetCategoryCount() { return m_categories.size(); }

		friend class IObjectBase;
	};

	PropertyContainer *CreatePropertyContainer(const wxString &name) { return new PropertyContainer(name, this); }
	PropertyContainer *CreatePropertyContainer(const wxString &name, const wxString &description) { return new PropertyContainer(name, description, this); }
	PropertyContainer *CreatePropertyContainer(const wxString &name, const wxString &description, const wxString &helpString) { return new PropertyContainer(name, description, helpString, this); }

protected:

	bool m_enabled = true;

	IObjectBase  *m_parent = NULL;
	PropertyContainer *m_category;

	std::vector<IObjectBase *> m_children;

protected:

	// utilites for implementing the tree
	static const int INDENT;  // size of indent
	wxString GetIndentString(int indent); // obtiene la cadena con el indentado

	std::vector<IObjectBase *>& GetChildren() { return m_children; };
	std::map<wxString, Property *>& GetProperties() { return m_properties; };

	// devuelve el puntero "this"
	IObjectBase* GetThis() { return this; }

	IObjectBase() : m_parent(NULL) {
		m_category = new PropertyContainer(this);
	}

	friend class CObjectInspector;

public:

	~IObjectBase();

	/**
	* Obtiene el nombre del objeto.
	*
	* @note No confundir con la propiedad nombre que tienen algunos objetos.
	*       Cada objeto tiene un nombre, el cual será el mismo que el usado
	*       como clave en el registro de descriptores.
	*/
	virtual wxString GetClassName() const = 0;

	/// Gets the parent object
	IObjectBase* GetParent() const { return m_parent; }

	/// Links the object to a parent
	virtual void SetParent(IObjectBase* parent) { m_parent = parent; }

	/**
	* Obtiene la propiedad identificada por el nombre.
	*
	* @note Notar que no existe el método SetProperty, ya que la modificación
	*       se hace a través de la referencia.
	*/
	Property* GetProperty(const wxString &nameParam) const;
	Event* GetEvent(const wxString &nameParam) const;

	/**
	* Añade una propiedad al objeto.
	*
	* Este método será usado por el registro de descriptores para crear la
	* instancia del objeto.
	* Los objetos siempre se crearán a través del registro de descriptores.
	*/
	void AddProperty(Property* value);
	void AddEvent(Event* event);

	/**
	* Obtiene el número de propiedades del objeto.
	*/
	unsigned int GetPropertyCount() const { return (unsigned int)m_properties.size(); }
	unsigned int GetEventCount() const { return m_events.size(); }

	Property* GetProperty(unsigned int idx) const; // throws ...;
	Event* GetEvent(unsigned int idx) const; // throws ...;

	/**
	* Devuelve el primer antecesor cuyo tipo coincida con el que se pasa
	* como parámetro.
	*
	* Será útil para encontrar el widget padre.
	*/
	IObjectBase* FindNearAncestor(const wxString &type);
	IObjectBase* FindNearAncestorByBaseClass(const wxString &type);

	/**
	* Añade un hijo al objeto.
	* Esta función es virtual, debido a que puede variar el comportamiento
	* según el tipo de objeto.
	*
	* @return true si se añadió el hijo con éxito y false en caso contrario.
	*/
	virtual bool AddChild(IObjectBase*);
	virtual bool AddChild(unsigned int idx, IObjectBase* obj);

	/**
	* Devuelve la posicion del hijo o GetChildCount() en caso de no encontrarlo
	*/
	unsigned int GetChildPosition(IObjectBase* obj);
	bool ChangeChildPosition(IObjectBase* obj, unsigned int pos);

	/**
	* Elimina un hijo del objeto.
	*/
	void RemoveChild(IObjectBase* obj);
	void RemoveChild(unsigned int idx);
	void RemoveAllChildren() { m_children.clear(); }

	/**
	* Obtiene un hijo del objeto.
	*/
	IObjectBase* GetChild(unsigned int idx);
	IObjectBase* GetChild(unsigned int idx, const wxString& type);

	/**
	* Obtiene el número de hijos del objeto.
	*/
	unsigned int GetChildCount()
	{
		return (unsigned int)m_children.size();
	}

	unsigned int GetPropertyIndex(const wxString &paramName) const
	{
		return std::distance(m_properties.begin(), m_properties.find(paramName.Lower()));
	}

	Property *GetPropertyByIndex(unsigned int idx)
	{
		if (m_properties.size() < idx)
			return NULL;

		auto m_properties_iterator = m_properties.begin();
		std::advance(m_properties_iterator, idx);
		return (*m_properties_iterator).second;
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
	virtual int GetComponentType() = 0;

	virtual void ReadProperty() = 0;
	virtual void SaveProperty() = 0;

	virtual bool IsEditable() const { return m_enabled; }
	virtual void SetReadOnly(bool enabled = true) { m_enabled = enabled; }

	virtual bool IsItem() { return false; }

	/**
	* Property events
	*/
	virtual void OnPropertyCreated() {}

	virtual void OnPropertyCreated(Property *property) {}
	virtual void OnPropertySelected(Property *property) {}

	virtual bool OnPropertyChanging(Property *property, const wxString &oldValue) { return true; }
	virtual void OnPropertyChanged(Property *property) {}

	/**
	* Comprueba si el tipo es derivado del que se pasa como parámetro.
	*/

	bool IsSubclassOf(wxString classname);

	PropertyContainer *GetCategory() const { return m_category; }

	// get metadata from object
	virtual IMetadata *GetMetaData() const { return NULL; }

	//get typelist from metadata 
	virtual OptionList *GetTypelist() const { return NULL; }

	////////////////////////////////////////////////////////////////////////////

	bool IsNull(const wxString& propertyName);

	bool SetPropertyValue(const wxString& propertyName, const wxArrayString &str);
	bool SetPropertyValue(const wxString& propertyName, const wxFontContainer &font);
	bool SetPropertyValue(const wxString& propertyName, const wxFont &font);
	bool SetPropertyValue(const wxString& propertyName, const wxColour &colour);
	bool SetPropertyValue(const wxString& propertyName, const wxBitmap &bmp);
	bool SetPropertyValue(const wxString& propertyName, const wxString &str, bool format = false);
	bool SetPropertyValue(const wxString& propertyName, const wxPoint &point);
	bool SetPropertyValue(const wxString& propertyName, const wxSize &size);
	bool SetPropertyValue(const wxString& propertyName, const bool integer);
	bool SetPropertyValue(const wxString& propertyName, const int integer);
	bool SetPropertyValue(const wxString& propertyName, const long integer);
	bool SetPropertyValue(const wxString& propertyName, const double val);

	template<class convType>
	bool SetPropertyValue(const wxString& propertyName, const convType &val, bool needConvert)
	{
		return SetPropertyValue(propertyName, (int)val);
	}

	bool GetPropertyValue(const wxString& propertyName, wxArrayInt &ineger);
	bool GetPropertyValue(const wxString& propertyName, wxArrayString &str);
	bool GetPropertyValue(const wxString& propertyName, wxFontContainer &font);
	bool GetPropertyValue(const wxString& propertyName, wxFont &font);
	bool GetPropertyValue(const wxString& propertyName, wxColour &colour);
	bool GetPropertyValue(const wxString& propertyName, wxBitmap &bmp);
	bool GetPropertyValue(const wxString& propertyName, wxString &str);
	bool GetPropertyValue(const wxString& propertyName, wxPoint &point);
	bool GetPropertyValue(const wxString& propertyName, wxSize &size);
	bool GetPropertyValue(const wxString& propertyName, bool &integer);
	bool GetPropertyValue(const wxString& propertyName, int &integer);
	bool GetPropertyValue(const wxString& propertyName, long &integer);
	bool GetPropertyValue(const wxString& propertyName, double &val);

	template<class convType>
	bool GetPropertyValue(const wxString& propertyName, convType &val, bool needConvert)
	{
		int conv_value = 0;
		if (GetPropertyValue(propertyName, conv_value)) {
			val = convType(conv_value); return true;
		}
		return false;
	}

	int GetPropertyAsInteger(const wxString& pname);
	wxFontContainer GetPropertyAsFont(const wxString& pname);
	wxColour GetPropertyAsColour(const wxString& pname);
	wxString GetPropertyAsString(const wxString& pname);
	wxPoint GetPropertyAsPoint(const wxString& pname);
	wxSize GetPropertyAsSize(const wxString& pname);
	wxBitmap GetPropertyAsBitmap(const wxString& pname);
	double GetPropertyAsFloat(const wxString& pname);

	wxArrayInt GetPropertyAsArrayInt(const wxString& pname);
	wxArrayString GetPropertyAsArrayString(const wxString& pname);

	IObjectBase* GetChildPtr(unsigned int idx)
	{
		return GetChild(idx);
	}

	virtual void DeleteRecursive();

	wxDECLARE_ABSTRACT_CLASS(IObjectBase);
};

///////////////////////////////////////////////////////////////////////////////

/**
* Clase que guarda un conjunto de plantillas de código.
*/
class CodeInfo
{
private:
	typedef std::map<wxString, wxString> TemplateMap;
	TemplateMap m_templates;
public:
	wxString GetTemplate(wxString name);
	void AddTemplate(wxString name, wxString _template);
	void Merge(CodeInfo* merger);
};

#endif
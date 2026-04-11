#ifndef __PROPERTY_OBJECT_H__
#define __PROPERTY_OBJECT_H__

#include "backend/backend_core.h"

///////////////////////////////////////////////////////////////////////////////

class BACKEND_API ibPropertyObject;

///////////////////////////////////////////////////////////////////////////////

class BACKEND_API ibProperty;
class BACKEND_API ibEvent;

///////////////////////////////////////////////////////////////////////////////

class BACKEND_API ibMetaData;

///////////////////////////////////////////////////////////////////////////////

#include "backend/fileSystem/fs.h"

///////////////////////////////////////////////////////////////////////////////

#define propertyDefName		wxT("propertyEvent")
#define propertyDefLabel	_("property and event")

///////////////////////////////////////////////////////////////////////////////

class BACKEND_API ibPropertyCategory final {
public:

	~ibPropertyCategory() {
		for (auto& category : m_categories) wxDELETE(category);
	}

	void AddProperty(ibProperty* property);
	void AddEvent(ibEvent* event);
	void AddCategory(ibPropertyCategory* cat);

	wxString GetName() const { return m_catName; }
	wxString GetLabel() const { return (m_catLabel.IsEmpty()) ? m_catName : m_catLabel; }
	wxString GetHelp() const { return m_catHelp; }

	wxString GetPropertyName(unsigned int index) const {
		if (index < m_properties.size()) return m_properties[index];
		return wxEmptyString;
	}

	wxString GetEventName(unsigned int index) const {
		if (index < m_events.size()) return m_events[index];
		return wxEmptyString;
	}

	ibPropertyCategory* GetCategory(unsigned int index) const {
		if (index < m_categories.size()) return m_categories[index];
		return new ibPropertyCategory(m_owner);
	}

	ibPropertyObject* GetPropertyObject() const { return m_owner; }

	unsigned int GetPropertyCount() const { return m_properties.size(); }
	unsigned int GetEventCount() const { return m_events.size(); }
	unsigned int GetCategoryCount() const { return m_categories.size(); }

	friend class ibPropertyObject;

private:

	ibPropertyCategory(ibPropertyObject* object) :
		m_catName(propertyDefName),
		m_catLabel(propertyDefLabel),
		m_catHelp(wxEmptyString),
		m_owner(object)
	{
	}
	ibPropertyCategory(const wxString& name, ibPropertyObject* object, ibPropertyCategory* ownerCat = nullptr) :
		m_catName(name),
		m_catLabel(wxEmptyString),
		m_catHelp(wxEmptyString),
		m_owner(object)
	{
		if (ownerCat != nullptr) ownerCat->AddCategory(this);
	}
	ibPropertyCategory(const wxString& name, const wxString& label, ibPropertyObject* object, ibPropertyCategory* ownerCat = nullptr) :
		m_catName(name),
		m_catLabel(label),
		m_catHelp(wxEmptyString),
		m_owner(object)
	{
		if (ownerCat != nullptr) ownerCat->AddCategory(this);
	}
	ibPropertyCategory(const wxString& name, const wxString& label, const wxString& helpString, ibPropertyObject* object, ibPropertyCategory* ownerCat = nullptr) :
		m_catName(name),
		m_catLabel(label),
		m_catHelp(helpString),
		m_owner(object)
	{
		if (ownerCat != nullptr) ownerCat->AddCategory(this);
	}

	wxString m_catName;
	wxString m_catLabel;
	wxString m_catHelp;
	std::vector<wxString> m_properties;
	std::vector<wxString> m_events;
	std::vector< ibPropertyCategory* > m_categories;
	ibPropertyObject* m_owner;
};

///////////////////////////////////////////////////////////////////////////////

class BACKEND_API ibBackendProperty {
protected:

	ibBackendProperty(ibPropertyCategory* cat, const wxString& name,
		const wxVariant& value) :
		m_propName(name),
		m_propLabel(name),
		m_propHelp(wxEmptyString),
		m_owner(cat->GetPropertyObject())
	{
	}
	ibBackendProperty(ibPropertyCategory* cat, const wxString& name,
		const wxString& label, const wxVariant& value) :
		m_propName(name),
		m_propLabel(label.IsEmpty() ? name : label),
		m_propHelp(wxEmptyString),
		m_owner(cat->GetPropertyObject())
	{
	}
	ibBackendProperty(ibPropertyCategory* cat, const wxString& name,
		const wxString& label, const wxString& helpString, const wxVariant& value) :
		m_propName(name),
		m_propLabel(label.IsEmpty() ? name : label),
		m_propHelp(helpString),
		m_owner(cat->GetPropertyObject())
	{
	}

public:
	virtual ~ibBackendProperty() {}
public:

	bool IsEditable() const;

	////////////////////

	ibPropertyObject* GetPropertyObject() const { return m_owner; }

	////////////////////

	const wxString& GetName() const { return m_propName; }
	void GetName(wxString& result) const { result = m_propName; }
	const wxString& GetLabel() const { return m_propLabel; }
	void GetLabel(wxString& result) const { result = m_propLabel; }
	const wxString& GetHelp() const { return m_propHelp; }
	void GetHelp(wxString& result) const { result = m_propHelp; }

	////////////////////

	const wxVariant& GetValue() const { return m_propValue; }

	////////////////////
	void SetValue(const wxVariant& val) { DoSetValue(val); }
	////////////////////

	ibBackendProperty& operator =(const wxVariant& val) {
		SetValue(val);
		return *this;
	}

	////////////////////

	template <typename cast_type = wxVariantData>
	inline cast_type* get_cell_variant(const wxVariant& val) const {
		cast_type* ret_type = dynamic_cast<cast_type*>(val.GetRefData());
		wxASSERT(ret_type != nullptr);
		return ret_type;
	}

	template <typename cast_type = wxVariantData>
	inline cast_type* get_cell_variant() const {
		return get_cell_variant<cast_type>(m_propValue);
	}

	////////////////////

	virtual bool IsOk() const { return !m_propValue.IsNull(); }
	virtual bool IsEmptyProperty() const { return false; }

	//get property for grid 
	virtual wxObject* GetPGProperty() const = 0;
	virtual void RefreshPGProperty(wxPGProperty* pg) {};

	//load & save object in control 
	virtual bool LoadData(ibReaderMemory& reader) = 0;
	virtual bool SaveData(ibWriterMemory& writer) = 0;

	//copy & paste object in control 
	virtual bool PasteData(ibReaderMemory& reader) { return LoadData(reader); }
	virtual bool CopyData(ibWriterMemory& writer) { return SaveData(writer); }

	//Set/Get property data
	virtual bool SetDataValue(const ibValue& varPropVal) = 0;
	virtual bool GetDataValue(ibValue& pvarPropVal) const = 0;

protected:
	virtual void DoSetValue(const wxVariant& val) { m_propValue = val; }
protected:

	wxString        m_propName;
	wxString		m_propLabel;
	wxString		m_propHelp;
	ibPropertyObject* m_owner; // pointer to the owner object
	wxVariant       m_propValue;
};

class BACKEND_API ibProperty : public ibBackendProperty {
	void InitProperty(ibPropertyCategory* cat, const wxVariant& value = wxNullVariant);
protected:
	ibProperty(ibPropertyCategory* cat, const wxString& name, const wxVariant& value) : ibBackendProperty(cat, name, value) { InitProperty(cat, value); }
	ibProperty(ibPropertyCategory* cat, const wxString& name, const wxString& label, const wxVariant& value) : ibBackendProperty(cat, name, label, value) { InitProperty(cat, value); }
	ibProperty(ibPropertyCategory* cat, const wxString& name, const wxString& label, const wxString& helpString, const wxVariant& value) : ibBackendProperty(cat, name, label, helpString, value) { InitProperty(cat, value); }
public:
	virtual bool PasteData(ibReaderMemory& reader);
};

class BACKEND_API ibEvent : public ibBackendProperty {
	void InitEvent(ibPropertyCategory* cat, const wxVariant& value = wxNullVariant);
protected:
	ibEvent(ibPropertyCategory* cat, const wxString& name, const wxVariant& value) : ibBackendProperty(cat, name, value) { InitEvent(cat, value); }
	ibEvent(ibPropertyCategory* cat, const wxString& name, const wxString& label, const wxVariant& value) : ibBackendProperty(cat, name, label, value) { InitEvent(cat, value); }
	ibEvent(ibPropertyCategory* cat, const wxString& name, const wxString& label, const wxString& helpString, const wxVariant& value) : ibBackendProperty(cat, name, label, helpString, value) { InitEvent(cat, value); }
	ibEvent(ibPropertyCategory* cat, const wxString& name, const wxArrayString& args, const wxVariant& value) : ibBackendProperty(cat, name, value), m_args(args) { InitEvent(cat, value); }
	ibEvent(ibPropertyCategory* cat, const wxString& name, const wxString& label, const wxArrayString& args, const wxVariant& value) : ibBackendProperty(cat, name, label, value), m_args(args) { InitEvent(cat, value); }
	ibEvent(ibPropertyCategory* cat, const wxString& name, const wxString& label, const wxString& helpString, const wxArrayString& args, const wxVariant& value) :ibBackendProperty(cat, name, label, helpString, value), m_args(args) { InitEvent(cat, value); }
public:
	const wxArrayString& GetArgs() const { return m_args; }
protected:
	wxArrayString m_args;
};

///////////////////////////////////////////////////////////////////////////////

#define property_cast(val, type) dynamic_cast<type*>(val.GetData())

///////////////////////////////////////////////////////////////////////////////

class BACKEND_API ibPropertyObject {
protected:

	template <typename typeProp, typename... Args>
	inline typeProp* CreateProperty(ibPropertyCategory* cat, Args&&... args) {
		return new typeProp(cat, std::forward<Args>(args)...);
	}
	template <typename typeEvent, typename... Args>
	inline typeEvent* CreateEvent(ibPropertyCategory* cat, Args&&... args) {
		return new typeEvent(cat, std::forward<Args>(args)...);
	}
	inline ibPropertyCategory* CreatePropertyCategory(const wxString& catName) {
		return new ibPropertyCategory(catName, this, m_category);
	}
	inline ibPropertyCategory* CreatePropertyCategory(const wxString& catName, const wxString& catLabel) {
		return new ibPropertyCategory(catName, catLabel, this, m_category);
	}
	inline ibPropertyCategory* CreatePropertyCategory(const wxString& catName, const wxString& catLabel, const wxString& catHelp) {
		return new ibPropertyCategory(catName, catLabel, catHelp, this, m_category);
	}
	inline ibPropertyCategory* CreatePropertyCategory(ibPropertyCategory* ownerCat, const wxString& catName) {
		return new ibPropertyCategory(catName, this, ownerCat);
	}
	inline ibPropertyCategory* CreatePropertyCategory(ibPropertyCategory* ownerCat, const wxString& catName, const wxString& catLabel) {
		return new ibPropertyCategory(catName, catLabel, this, ownerCat);
	}
	inline ibPropertyCategory* CreatePropertyCategory(ibPropertyCategory* ownerCat, const wxString& catName, const wxString& catLabel, const wxString& catHelp) {
		return new ibPropertyCategory(catName, catLabel, catHelp, this, ownerCat);
	}

protected:

	wxString GetIndentString(int indent) const; // obtiene la cadena con el indentado

	ibPropertyObject() /*: m_parent(nullptr)*/ { m_category = new ibPropertyCategory(this); }

	friend class ibProperty;
	friend class ibEvent;

	/**
	* Añade una propiedad al objeto.
	*
	* Este método será usado por el registro de descriptores para crear la
	* instancia del objeto.
	* Los objetos siempre se crearán a través del registro de descriptores.
	*/
	void AddProperty(ibProperty* property);
	void AddEvent(ibEvent* event);

public:

	virtual ~ibPropertyObject();

	/**
	* Obtiene el nombre del objeto.
	*
	* @note No confundir con la propiedad nombre que tienen algunos objetos.
	*       Cada objeto tiene un nombre, el cual será el mismo que el usado
	*       como clave en el registro de descriptores.
	*/
	virtual wxString GetClassName() const = 0;

	/// Gets the owner object
	virtual ibPropertyObject* GetOwner() const { return nullptr; }

	/// Gets the metadata object
	virtual ibMetaData* GetMetaData() const { return nullptr; }

	/**
	* Obtiene la propiedad identificada por el nombre.
	*
	* @note Notar que no existe el método SetProperty, ya que la modificación
	*       se hace a través de la referencia.
	*/
	ibProperty* GetProperty(const wxString& nameParam) const;
	ibEvent* GetEvent(const wxString& nameParam) const;

	/**
	* Obtiene el número de propiedades del objeto.
	*/
	unsigned int GetPropertyCount() const { return (unsigned int)m_properties.size(); }
	unsigned int GetEventCount() const { return m_events.size(); }

	ibProperty* GetProperty(unsigned int idx) const; // throws ...;
	ibEvent* GetEvent(unsigned int idx) const; // throws ...;

	/**
	* Obtiene el número de hijos del objeto.
	*/
	unsigned int GetPropertyIndex(const wxString& nameParam) const;

	ibProperty* GetPropertyByIndex(unsigned int idx) const {
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
	virtual int GetComponentType() const { return COMPONENT_TYPE_ABSTRACT; }

	/**
	* ibProperty events
	*/
	virtual void OnPropertyCreated() {}
	virtual void OnPropertyCreated(ibProperty* property) {}
	virtual void OnPropertyRefresh(class wxPropertyGridManager* pg, class wxPGProperty* pgProperty, ibProperty* property) {}
	virtual void OnPropertySelected(ibProperty* property) {}
	virtual bool OnPropertyChanging(ibProperty* property, const wxVariant& newValue) { return true; }
	virtual void OnPropertyChanged(ibProperty* property, const wxVariant& oldValue, const wxVariant& newValue) {}

	/**
	* ibEvent events
	*/
	virtual void OnEventCreated() {}
	virtual void OnEventCreated(ibEvent* event) {}
	virtual void OnEventRefresh(class wxPropertyGridManager* pg, class wxPGProperty* pgProperty, ibEvent* event) {}
	virtual void OnEventSelected(ibEvent* event) {}
	virtual bool OnEventChanging(ibEvent* event, const wxVariant& newValue) { return true; }
	virtual void OnEventChanged(ibEvent* property, const wxVariant& oldValue, const wxVariant& newValue) {}

	/**
	* Comprueba si el tipo es derivado del que se pasa como parámetro.
	*/

	ibPropertyCategory* GetCategory() const { return m_category; }
	virtual bool IsEditable() const = 0;

protected:

	//copy & paste property 
	bool CopyProperty(ibWriterMemory& writer) const;
	bool PasteProperty(ibReaderMemory& reader);

	ibPropertyCategory* m_category;

private:

	std::map<wxString, ibProperty*> m_properties;
	std::map<wxString, ibEvent*> m_events;
};

template <typename T>
class ibPropertyObjectHelper : public ibPropertyObject {
	void RemovePropertyObject(const ibPropertyObject* obj) {
		typename std::vector< propertyType* >::iterator it = m_children.begin();
		while (it != m_children.end() && *it != obj) it++;
		if (it != m_children.end()) m_children.erase(it);
	}
protected:
	ibPropertyObjectHelper() : m_parent(nullptr) {}
public:

	using propertyType = T;
	using vectorType = std::vector<propertyType*>;

	virtual ~ibPropertyObjectHelper() {
		// remove the reference in the parent
		if (m_parent != nullptr) m_parent->RemovePropertyObject(this);
		for (auto& child : m_children) child->SetParent(nullptr);
	}

	/// Gets the owner object
	virtual ibPropertyObjectHelper* GetOwner() const { return GetParent(); }

	// Gets the parent object
	propertyType* GetParent() const { return m_parent; }

	/// Links the object to a parent
	void SetParent(propertyType* parent) { m_parent = parent; }

	/**
	* Devuelve la posicion del hijo o GetParentPosition() en caso de no encontrarlo
	*/
	unsigned int GetParentPosition() const {
		if (m_parent == nullptr)
			return 0;
		unsigned int pos = 0;
		while (pos < m_parent->GetChildCount() && m_parent->m_children[pos] != this)
			pos++;
		return pos;
	}

	/**
	* Devuelve el primer antecesor cuyo tipo coincida con el que se pasa
	* como parámetro.
	*
	* Será útil para encontrar el widget padre.
	*/
	propertyType* FindNearAncestor(const wxString& type) const {
		propertyType* result = nullptr;
		propertyType* parent = GetParent();
		if (parent != nullptr) {
			if (stringUtils::CompareString(parent->GetObjectTypeName(), type))
				result = parent;
			else
				result = parent->FindNearAncestor(type);
		}

		return result;
	}

	propertyType* FindNearAncestorByBaseClass(const wxString& type) const {
		propertyType* result = nullptr;
		propertyType* parent = GetParent();
		if (parent != nullptr) {
			if (stringUtils::CompareString(parent->GetObjectTypeName(), type))
				result = parent;
			else
				result = parent->FindNearAncestorByBaseClass(type);
		}

		return result;
	}

	/**
	* Añade un hijo al objeto.
	* Esta función es virtual, debido a que puede variar el comportamiento
	* según el tipo de objeto.
	*
	* @return true si se añadió el hijo con éxito y false en caso contrario.
	*/
	bool AddChild(propertyType* obj) {
		m_children.emplace_back(obj);
		return true;
	}

	bool AddChild(unsigned int idx, propertyType* obj) {
		m_children.emplace(m_children.begin() + idx, obj);
		return true;
	}

	/**
	* Devuelve la posicion del hijo o GetChildCount() en caso de no encontrarlo
	*/
	unsigned int GetChildPosition(propertyType* obj) const {
		unsigned int pos = 0;
		while (pos < GetChildCount() && m_children[pos] != obj)
			pos++;
		return pos;
	}

	bool ChangeChildPosition(propertyType* obj, unsigned int pos) {

		unsigned int obj_pos = GetChildPosition(obj);

		if (obj_pos == GetChildCount() || pos >= GetChildCount())
			return false;

		if (pos == obj_pos)
			return true;

		// Procesamos el cambio de posición
		RemoveChild(obj);
		AddChild(pos, obj);
		return true;
	}

	/**
	* Elimina un hijo del objeto.
	*/
	void RemoveChild(propertyType* obj) { RemovePropertyObject(obj); }
	void RemoveChild(unsigned int idx) {
		assert(idx < m_children.size());
		m_children.erase(m_children.begin() + idx);
	}

	void RemoveAllChildren() { m_children.clear(); }

	/**
	* Obtiene un hijo del objeto.
	*/
	propertyType* GetChild(unsigned int idx) const {
		assert(idx < m_children.size());
		return m_children[idx];
	}

	/**
	* Obtiene el número de hijos del objeto.
	*/
	unsigned int GetChildCount() const { return (unsigned int)m_children.size(); }

	/**
	* Comprueba si el tipo es derivado del que se pasa como parámetro.
	*/

	bool IsSubclassOf(const wxString& className) const {
		bool found = false;
		if (stringUtils::CompareString(className, GetClassName())) {
			found = true;
		}
		else {
			propertyType* parent = GetParent();
			while (parent != nullptr) {
				found = parent->IsSubclassOf(className);
				if (found)
					break;
				else
					parent = parent->GetParent();
			}
		}
		return found;
	}

	void DeleteRecursive() {
		for (auto objChild : m_children) {
			objChild->SetParent(nullptr);
			objChild->DeleteRecursive();
			wxDELETE(objChild);
		}
		m_children.clear();
	}

protected:

	propertyType* m_parent = nullptr;
	vectorType m_children;
};

#include "backend/compiler/value.h"

#endif
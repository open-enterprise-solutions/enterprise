#ifndef __PROPERTY_OBJECT_H__
#define __PROPERTY_OBJECT_H__

#include "backend/backend_core.h"

///////////////////////////////////////////////////////////////////////////////

class BACKEND_API ibPropertyObject;
class BACKEND_API ibEventDispatcher;   // backend/eventDispatcher.h — the facet a concrete event exposes

///////////////////////////////////////////////////////////////////////////////

class BACKEND_API ibProperty;
class BACKEND_API ibEvent;
class BACKEND_API ibDataNode;   // serialize/dataBuilder.h — universal structure node
class BACKEND_API ibDataValue;  // serialize/dataBuilder.h — a node value (scalar / Child)

///////////////////////////////////////////////////////////////////////////////

class BACKEND_API ibMetaData;

// owning smart pointer over ibValue-derived types (defined in value_ptr.h,
// pulled in transitively via value.h at the bottom of this header). Forward
// declared here so m_children can be a vector of owning handles.
template <class T> class ibValuePtr;

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

	ibPropertyCategory* GetCategory(unsigned int index) const;

	ibPropertyObject* GetPropertyObject() const { return m_owner; }

	unsigned int GetPropertyCount() const { return m_properties.size(); }
	unsigned int GetEventCount() const { return m_events.size(); }
	unsigned int GetCategoryCount() const;

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

// The choices a property offers — label + id + optional bitmap, in backend terms.
//
// This is what a list / enum / event-action property HANDS OVER; turning it into the
// editor's own choice type is the front's job. It exists because the three of them used
// to build a wxPGChoices right here and pass it through the ms_property* slot, which the
// seam does not allow (wxObject* is the most derived type backend may name — §4 of
// docs/property-system.md) and which dragged propgrid into every backend TU. The data
// was already ours; only the conversion sat on the wrong side.
class BACKEND_API ibPropertyChoiceList {

	struct ibPropertyChoiceItem {
		ibPropertyChoiceItem(const wxString& label, long id, const wxBitmap& bmp)
			: m_label(label), m_id(id), m_bmp(bmp) {}
		wxString m_label;
		long m_id;
		wxBitmap m_bmp;
	};

	std::vector<ibPropertyChoiceItem> m_items;

public:

	void Add(const wxString& label, long id, const wxBitmap& bmp = wxNullBitmap) {
		m_items.emplace_back(label, id, bmp);
	}

	unsigned int GetCount() const { return (unsigned int)m_items.size(); }
	wxString GetLabel(unsigned int idx) const { return m_items[idx].m_label; }
	long GetId(unsigned int idx) const { return m_items[idx].m_id; }
	const wxBitmap& GetBitmap(unsigned int idx) const { return m_items[idx].m_bmp; }
};

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


	// Property <-> node value — the ONLY per-property serialization (no byte
	// SaveData/LoadData anymore). Read/Write pair: bool + out-param + const on the
	// writer, PARALLEL to the runtime GetDataValue/SetDataValue but over ibDataValue.
	// WriteNodeValue yields the property's value into `value`: a typed scalar, or a
	// Child sub-node (a SET of values) for a composite; the sub-node is shared via
	// shared_ptr, so the owner can place the SAME value under one or several named
	// areas cheaply. ReadNodeValue sets the property from a value found by name:
	//     ibDataValue v; prop->WriteNodeValue(v); node.SetProperty(name, v);
	virtual bool ReadNodeValue(const ibDataValue& value) = 0;
	virtual bool WriteNodeValue(ibDataValue& value) const = 0;

	// Non-virtual by-value convenience over WriteNodeValue, for owners that "get the
	// value as is" inline (yields null when the writer declines). A DIFFERENT name
	// from the virtual, so an override does NOT hide it — no `using` needed:
	//     node.SetProperty(name, prop->GetNodeValue());
	ibDataValue GetNodeValue() const;

	// copy & paste over the NODE value (clipboard hook, no bytes). Default = the node
	// value (GetNodeValue / ReadNodeValue); a type whose clipboard shape differs (the
	// form pulls its LIVE data) overrides. The byte transport lives once at the
	// owner's CopyProperty / PasteProperty boundary, through the binary provider.
	virtual bool CopyNodeValue(ibDataValue& value) const;
	virtual bool PasteNodeValue(const ibDataValue& value);

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

	// THE dispatcher this event fires through — PURE: every concrete event decides HOW it dispatches (ibEventControl
	// vends a named-event / lambda value; a plain no-dispatch event returns nullptr). CallAsEvent asks for this and
	// calls Dispatch, staying agnostic. Typed on the base -> no cast at the fire site.
	virtual ibEventDispatcher* GetDispatcher() const = 0;

protected:
	wxArrayString m_args;
};

///////////////////////////////////////////////////////////////////////////////

#define property_cast(val, type) dynamic_cast<type*>(val.GetData())

///////////////////////////////////////////////////////////////////////////////

// ---------------------------------------------------------
// ibPropertyObjectNotifier
// ---------------------------------------------------------

// The object's presentation channel to whatever is showing it. PURE PUSH, like
// ibDataViewModelNotifier: the object says WHAT CHANGED about one of ITS OWN
// properties, in ibProperty terms; the front owns the widget and decides how to
// apply it. Nothing here pulls, so the editor's toolkit stays out of the object's
// vtable — and, because an object only ever pushes for a property it declared, a
// composite's editor keeps full control of its own sub-properties.
class BACKEND_API ibPropertyObjectNotifier {
public:

	ibPropertyObjectNotifier() { m_owner = nullptr; }
	virtual ~ibPropertyObjectNotifier() { m_owner = nullptr; }

	virtual bool PropertyHidden(const ibProperty* property, bool hide) = 0;

	// Null once the object we were registered on is gone (its dtor clears it) — so the
	// front can tell "still showing a live object" from "holding a corpse" without the
	// object having to announce anything.
	void SetOwner(ibPropertyObject* owner) { m_owner = owner; }
	ibPropertyObject* GetOwner() const { return m_owner; }

private:

	ibPropertyObject* m_owner;
};

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

	wxString GetIndentString(int indent) const; // gets the string with the indentation

	ibPropertyObject() /*: m_parent(nullptr)*/ { m_category = new ibPropertyCategory(this); }

	friend class ibProperty;
	friend class ibEvent;

	/**
	* Adds a property to the object.
	*
	* This method is used by the descriptor registry to create the
	* instance of the object.
	* Objects are always created through the descriptor registry.
	*/
	void AddProperty(ibProperty* property);
	void AddEvent(ibEvent* event);

public:

	virtual ~ibPropertyObject();

	/**
	* Gets the name of the object.
	*
	* @note Not to be confused with the name property that some objects have.
	*       Every object has a name, which is the same as the one used
	*       as the key in the descriptor registry.
	*/
	virtual wxString GetClassName() const = 0;

	/// Gets the owner object
	virtual ibPropertyObject* GetOwner() const { return nullptr; }

	/// A child bound to its parent for life: a parent reset (RemoveAllChildren
	/// with keepPinned) preserves it; only the parent's destruction drops it.
	/// Default false; ibValueMetaObject pins predefined children.
	virtual bool IsPinnedToParent() const { return false; }

	/// Check is enabled property?
	virtual bool IsEditable() const = 0;

	/// Gets the metadata object. The CONST overload is the one types actually implement
	/// (a control resolves it through its owning form; ibValueMetaObject returns m_metaData).
	virtual const ibMetaData* GetMetaData() const { return nullptr; }
	/// The non-const overload is for MUTABLE metadata owners only (ibValueMetaObject). A type
	/// that overrides just the const one (e.g. a form control) inherits THIS default — and a
	/// call through a non-const ibPropertyObject* then silently yields null. That is the exact
	/// "binding loses its source" trap: a property serialising against a null metaData writes
	/// null GUIDs. Reach the const overload through a const pointer unless you truly own a
	/// mutable metaData. Guard so the trap surfaces in debug instead of corrupting data.
	virtual ibMetaData* GetMetaData() {
		wxFAIL_MSG(wxT("ibPropertyObject::GetMetaData(): non-const overload not overridden — ")
			wxT("call the const one (through a const pointer); this returns null."));
		return nullptr;
	}

	/**
	* Gets the property identified by name.
	*
	* @note Note that there is no SetProperty method, since modification
	*       is done through the reference.
	*/
	ibProperty* GetProperty(const wxString& nameParam) const;
	ibEvent* GetEvent(const wxString& nameParam) const;

	/**
	* Gets the number of properties of the object.
	*/
	unsigned int GetPropertyCount() const { return (unsigned int)m_properties.size(); }
	unsigned int GetEventCount() const { return m_events.size(); }

	ibProperty* GetProperty(unsigned int idx) const; // throws ...;
	ibEvent* GetEvent(unsigned int idx) const; // throws ...;

	/**
	* Gets the number of children of the object.
	*/
	unsigned int GetPropertyIndex(const wxString& nameParam) const;

	ibProperty* GetPropertyByIndex(unsigned int idx) const {
		if (idx >= m_properties.size())
			return nullptr;
		auto properties_iterator = m_properties.begin();
		std::advance(properties_iterator, idx);
		return (*properties_iterator).second;
	}

	/**
	* Returns the type of the object.
	*
	* Must be overridden in each derived class.
	*/
	virtual wxString GetObjectTypeName() const = 0;

	/**
	* Returns the depth of the object in the tree.
	*/
	virtual int GetComponentType() const { return COMPONENT_TYPE_ABSTRACT; }

	// Central VIRTUAL entry for object-level node save/load. A type OVERRIDES it to do its
	// OWN data: meta / controls call their LoadNode/SaveNode inside (their per-type
	// ReadData/WriteData stays THEIR method, NOT on this base); the dynamic list writes
	// its Source + settings. Base default routes through attached objects (their data is
	// part of us) — an override calls the base to include attached.
	virtual bool ReadProperty(const ibDataNode& node);
	virtual bool WriteProperty(ibDataNode& node) const;

	// The front's ONE refresh entry — sent per render and after an edit, NOT per property.
	// Default fans out to both halves; override a half to push what that kind's state
	// decides, or this to do both at once.
	virtual void OnRefresh() { OnPropertyRefresh(); OnEventRefresh(); }

	/**
	* ibProperty events
	*/
	virtual void OnPropertyCreated() {}
	virtual void OnPropertyCreated(ibProperty* property) {}
	// An override calls HideProperty() for the properties whose visibility its current state
	// decides (a Type change flips SelectMode) and stays silent about the rest, so nothing it
	// does not own is ever touched.
	virtual void OnPropertyRefresh() {}
	virtual void OnPropertySelected(ibProperty* property) {}
	virtual bool OnPropertyChanging(ibProperty* property, const wxVariant& newValue) { return true; }
	virtual void OnPropertyChanged(ibProperty* property, const wxVariant& oldValue, const wxVariant& newValue) {}

	/**
	* ibEvent events
	*/
	virtual void OnEventCreated() {}
	virtual void OnEventCreated(ibEvent* event) {}
	virtual void OnEventRefresh() {}
	virtual void OnEventSelected(ibEvent* event) {}
	virtual bool OnEventChanging(ibEvent* event, const wxVariant& newValue) { return true; }
	virtual void OnEventChanged(ibEvent* property, const wxVariant& oldValue, const wxVariant& newValue) {}

	// A CHILD changed (an attached object, or one that named us its attach-owner). Mirrors wxPGProperty's
	// ChildChanged: the parent hears it and passes it up — default bubbles along the attach-owner chain;
	// a frontend holder overrides to refresh its editor. Deliberately carries NO payload — a refresh
	// signal, not the property event: reusing OnPropertyChanged here would make a holder re-forward to
	// the property's owner and fire the child's handler a second time.
	virtual void OnChildChanged() { if (m_attachOwner != nullptr) m_attachOwner->OnChildChanged(); }

#pragma region __notifier_h__

	// The front registers itself once the object's properties are built, and drops out
	// when it stops showing it. Non-owning — the notifier outlives nothing here.
	void AddNotifier(ibPropertyObjectNotifier* notifier);
	void RemoveNotifier(ibPropertyObjectNotifier* notifier);

	int GetViewCount() const { return (int)m_notifiers.size(); }

protected:

	// The push. An OnPropertyRefresh override calls this for a property IT declared;
	// silence means "not mine, leave it alone".
	bool HideProperty(const ibProperty* property, bool hide = true);

public:

#pragma endregion

	/**
	* Checks whether the type is derived from the one passed as a parameter.
	*/

	ibPropertyCategory* GetCategory() const { return m_category; }
	const std::vector<ibPropertyObject*>& GetAttachedObjects() const { return m_attachedObjects; }

	// Attach ANOTHER property-object so ITS properties appear as part of THIS one in
	// the inspector (a single whole), while GetProperty routes them back to it — edits
	// and OnPropertyChanged fire on the REAL owner (the attached object), not us.
	// Non-owning: the other object lives elsewhere (e.g. an attribute's value).
	void AttachPropertyObject(ibPropertyObject* other);
	void DetachAllPropertyObjects();

	// The attach graph's UPWARD edge — distinct from the tree m_parent on ibPropertyObjectHelper
	// (that is the children hierarchy). This is "who attached me", set on AttachPropertyObject or by
	// a structural owner (a value-table sets it on its column-infos). Null when standalone. A change
	// here bubbles up via OnChildChanged until a holder that can react is reached.
	ibPropertyObject* GetAttachOwner() const { return m_attachOwner; }
	
	void SetAttachOwner(ibPropertyObject* owner) { m_attachOwner = owner; }
	void RemoveAttachedObject(ibPropertyObject* other);


protected:

	//copy & paste property
	bool CopyProperty(ibWriterMemory& writer) const;
	bool PasteProperty(ibReaderMemory& reader);

	ibPropertyCategory* m_category;

private:

	std::map<wxString, ibProperty*> m_properties;
	std::map<wxString, ibEvent*> m_events;
	std::vector<ibPropertyObject*> m_attachedObjects;   // non-owning — GetProperty routes to them
	ibPropertyObject* m_attachOwner = nullptr;          // upward back-link to whoever attached us (see GetAttachOwner)

	//property notifier
	std::vector<ibPropertyObjectNotifier*> m_notifiers; // non-owning — the front adds and removes itself
};

template <typename T>
class ibPropertyObjectHelper : public ibPropertyObject {
	void RemovePropertyObject(const ibPropertyObject* obj) {
		typename vectorType::iterator it = m_children.begin();
		// cast through the owning handle to the raw child, then to the
		// ibPropertyObject base — m_children stores ibValue* internally, which is
		// a sibling base of ibPropertyObject, so direct comparison is ill-formed.
		while (it != m_children.end() &&
			static_cast<const ibPropertyObject*>(static_cast<propertyType*>(*it)) != obj) it++;
		if (it != m_children.end()) {
			// Hold a ref across erase(): if this is the child's last owner, releasing
			// it inside erase() runs the child's destructor, which calls back
			// RemovePropertyObject(this) → a re-entrant erase on the vector we're
			// already erasing from (UB). The local drops the ref only after erase()
			// returns, by which point the re-entrant lookup finds nothing.
			ibValuePtr<propertyType> dying(*it);
			m_children.erase(it);
		}
	}
protected:
	ibPropertyObjectHelper() : m_parent(nullptr) {}
public:

	using propertyType = T;
	// Owning children: each handle holds a ref (IncrRef on AddChild, DecrRef on
	// erase / clear / destruction). The container is the structural owner; the
	// parent link (m_parent) stays a raw, non-owning back-pointer.
	using vectorType = std::vector<ibValuePtr<propertyType>>;

	virtual ~ibPropertyObjectHelper() {
		// remove the reference in the parent
		if (m_parent != nullptr) m_parent->RemovePropertyObject(this);
		// Teardown reuses the canonical reset: orphan + release every child, the
		// owning handles cascade destruction down the subtree.
		RemoveAllChildren();
	}

	/// Gets the owner object
	virtual ibPropertyObjectHelper* GetOwner() const { return GetParent(); }

	// Gets the parent object
	propertyType* GetParent() const { return m_parent; }

	/// Links the object to a parent
	void SetParent(propertyType* parent) { m_parent = parent; }

	/**
	* Returns the position of the child, or GetParentPosition() if not found
	*/
	unsigned int GetParentPosition() const {
		if (m_parent == nullptr)
			return 0;
		unsigned int pos = 0;
		while (pos < m_parent->GetChildCount() &&
			static_cast<propertyType*>(m_parent->m_children[pos]) != this)
			pos++;
		return pos;
	}

	/**
	* Returns the first ancestor whose type matches the one passed
	* as a parameter.
	*
	* Useful for finding the parent widget.
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

	/**
	* Adds a child to the object.
	* This function is virtual, since the behaviour may vary
	* depending on the type of object.
	*
	* @return true if the child was added successfully and false otherwise.
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
	* Returns the position of the child, or GetChildCount() if not found
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

		// Process the position change. Anchor a ref across remove→re-add:
		// RemoveChild drops the owning handle, which would destroy a sole-owned
		// child before AddChild can re-insert it.
		ibValuePtr<propertyType> keep(obj);
		RemoveChild(obj);
		AddChild(pos, obj);
		return true;
	}

	/**
	* Removes a child from the object.
	*/
	void RemoveChild(propertyType* obj) { RemovePropertyObject(obj); }
	void RemoveChild(unsigned int idx) {
		assert(idx < m_children.size());
		// Hold a ref across erase() — see RemovePropertyObject (re-entrant erase guard).
		ibValuePtr<propertyType> dying(m_children[idx]);
		m_children.erase(m_children.begin() + idx);
	}

	// Canonical "reset children". Orphan first so a child's destructor doesn't
	// re-enter RemovePropertyObject on this vector mid-clear() (re-entrant erase =
	// UB), then clear: the owning handles release and a last-ref child is destroyed,
	// cascading destruction down its subtree. Used for reload-reset and teardown.
	//
	// keepPinned: preserve children bound to this parent for life
	// (IsPinnedToParent — predefined attributes / inner modules on a reused root).
	// A reload reset passes true so the predefined infrastructure created in the
	// owner's ctor survives; teardown (destructor) passes false to drop everything.
	void RemoveAllChildren(bool keepPinned = false) {
		if (!keepPinned) {
			for (auto& child : m_children) child->SetParent(nullptr);
			m_children.clear();
			return;
		}
		// Keep pinned children in place; orphan the rest first (so their dtors
		// don't re-enter RemovePropertyObject), then release them via swap: the
		// old vector holds every handle, dropping it destroys the last-ref
		// non-pinned children while the pinned ones survive in m_children.
		vectorType kept;
		for (auto& child : m_children) {
			if (child->IsPinnedToParent())
				kept.emplace_back(child);
			else
				child->SetParent(nullptr);
		}
		m_children.swap(kept);
	}

	/**
	* Gets a child of the object.
	*/
	propertyType* GetChild(unsigned int idx) const {
		assert(idx < m_children.size());
		return m_children[idx];
	}

	/**
	* Gets the number of children of the object.
	*/
	unsigned int GetChildCount() const { return (unsigned int)m_children.size(); }

	/**
	* Checks whether the type is derived from the one passed as a parameter.
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
		// Owning children: clearing releases each handle, and the destructor of a
		// last-ref child recurses this teardown down the subtree. No wxDELETE /
		// explicit recursion — the cascade does it.
		for (auto& objChild : m_children) objChild->SetParent(nullptr);
		m_children.clear();
	}

protected:

	propertyType* m_parent = nullptr;
	vectorType m_children;
};

#include "backend/compiler/value.h"

#endif
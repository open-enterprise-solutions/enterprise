#ifndef __META_COMMON_ATTRIBUTE_OBJECT_H__
#define __META_COMMON_ATTRIBUTE_OBJECT_H__

// A COMMON ATTRIBUTE — one declaration, carried by many objects.
//
// It is an ATTRIBUTE, for the same reason a session parameter is one: an attribute
// already IS "a declared name with a type, its qualifiers and an editor", which is
// the whole of what is being declared. Deriving it means the type description, the
// property page, the serialisation and AdjustValue arrive finished.
//
// WHAT MAKES IT DIFFERENT is that it does not belong to one owner. A catalog's
// attribute is a column of that catalog. This one is declared under Common and then
// APPEARS IN EVERY OBJECT checked into its composition — as a real child, with a
// metaID of its own, a column of its own, and a place in that object's restructuring.
// Nothing about it is virtual or resolved late: by the time the table is built it is
// an attribute like any other.
//
// TWO CLASSES, because there are two things:
//
//   ibValueMetaObjectCommonAttribute      the DECLARATION — lives under Common, one per name
//   ibValueMetaObjectCommonAttributeColumn   the COPY — lives inside an object, one per member
//
// The copy DELEGATES its name and its type to the declaration and keeps only its own
// identity. That is the design decision worth stating: a rename or a type change on
// the declaration needs no propagation pass, because the copies never held a second
// answer to begin with. What the copy does hold is its metaID — and the physical
// column follows the metaID (fld<metaId>), so each object's column is its own.
//
// Membership is NOT stored here, and NOT in the section mechanism either. It lives on
// the object, in ibCompositionObject (compositionHelper.h) — a set of its own, with a
// chunk of its own on disk. That mechanism is a copy of the interface one in shape and
// deliberately separate in identity: ibInterfaceObject belongs to SECTIONS (it is their
// older name) and carries their vocabulary, so putting a second kind of membership into
// the same set would leave one container holding two meanings with no way to tell them
// apart.

#include "attribute/metaAttributeObject.h"

class BACKEND_API ibValueMetaObjectCommonAttribute : public ibValueMetaObjectAttribute {
	protected:

	enum
	{
		ID_METATREE_OPEN_COMPOSITION = 19100,
	};

	public:

	ibValueMetaObjectCommonAttribute(const ibValueTypes& valType = ibValueTypes::TYPE_STRING)
		: ibValueMetaObjectAttribute(valType) {}

	//support icons
	virtual wxIcon GetIcon() const;
	static wxIcon GetIconGroup();

	// Context menu — "Open composition", the same gesture a section offers for its own.
	virtual bool PrepareContextMenu(class wxMenu* defaultMenu) override;
	virtual void ProcessCommand(unsigned int id) override;

	// THE COMPOSITION, asked of the objects rather than kept here. Returns the
	// metaobjects that are checked into this declaration.
	std::vector<class ibValueMetaObject*> GetCompositionArrayObject() const;

	// Check an object in or out. Checking in marks the object (ibCompositionObject) AND
	// creates the copy inside it; checking out undoes both. The object never owns the
	// decision, which is what makes the copy safe to show and impossible to remove locally.
	//
	// Named …CompositionObject, not SetComposition: this class is itself an
	// ibCompositionObject, and an overload taking a metaobject would hide the primitive's
	// SetComposition(ibMetaID) from every caller of this type.
	bool SetCompositionObject(class ibValueMetaObject* metaObject, bool set = true);
	bool IsCompositionObject(const class ibValueMetaObject* metaObject) const;

	// Every copy this declaration has put into an object. Used when the declaration
	// itself goes away.
	std::vector<class ibValueMetaObjectCommonAttributeColumn*> GetCommonAttributeColumnArrayObject() const;
	// The copies inside ONE object — normally one, and more only if a configuration was
	// edited into an inconsistent state, which is why removal iterates rather than picks.
	std::vector<class ibValueMetaObjectCommonAttributeColumn*> FindCommonAttributeColumnIn(
		const class ibValueMetaObject* metaObject) const;

	// A change here — the type above all — has to reach every object that carries a copy.
	virtual void OnPropertyChanged(class ibProperty* property,
		const wxVariant& oldValue, const wxVariant& newValue) override;
	virtual bool OnRenameMetaObject(const wxString& sNewName) override;
	virtual bool OnDeleteMetaObject() override;
};

//////////////////////////////////////////////////////////////////////
//  The copy that lives inside an object
//////////////////////////////////////////////////////////////////////
//
// A real child of the object, and an ordinary column to everything downstream: the
// table builder, the index contributor, save and read, the query generator and the
// form all see an attribute and ask it the usual questions.
//
// It answers two of them by asking the declaration (name, type) and the rest by
// itself (identity, physical column). It cannot be renamed, retyped or deleted where
// it sits — the declaration is the only place any of that happens.

// Derives the CONCRETE attribute rather than its base: an attribute base is abstract
// (GetTypeDesc, FillCheck), and re-implementing those here would be a second attribute
// implementation kept in step with the first by hand. Deriving the real one and
// overriding what is DELEGATED leaves its own storage unused — which is correct, since
// nothing may write it.
class BACKEND_API ibValueMetaObjectCommonAttributeColumn : public ibValueMetaObjectAttribute {
	protected:

	enum
	{
		ID_METATREE_OPEN_SOURCE = 19110,
	};

	public:

	// Defaulted for the type registry; the source is bound when the copy is created
	// and re-bound by metaID after a configuration is loaded.
	ibValueMetaObjectCommonAttributeColumn(ibValueMetaObjectCommonAttribute* source = nullptr);

	ibValueMetaObjectCommonAttribute* GetSource() const { return m_source; }
	ibMetaID GetSourceMetaID() const { return m_sourceMetaId; }
	// Bound when the declaration creates the copy, and again by metaID after a load.
	void BindSource(ibValueMetaObjectCommonAttribute* source);

	// THE TYPE comes from the declaration — never a copy of it, which is why a type
	// change needs no propagation pass. An unbound copy (its declaration deleted while
	// the configuration was closed) answers an empty description: "no types" everywhere,
	// contributing no column.
	//
	// THE NAME does not work that way, and the difference is not a preference:
	// ibValueMetaObject::GetName is NOT virtual, so the designer tree and the property
	// grid read the stored one directly. The copy therefore STORES its name, set when it
	// is created and rewritten by the declaration's OnRenameMetaObject.
	virtual ibTypeDescription& GetTypeDesc() const override;

	// ALIVE ONLY WHILE ITS DECLARATION IS. A copy exists because a declaration put it
	// there, so it cannot outlive it: mark the declaration deleted and every copy stops
	// being allowed in the same instant — no pass over the configuration, no list to keep.
	//
	// This is what makes the column disappear too. Every walk that builds anything —
	// the schema snapshot, the designer tree, the query columns — filters on IsAllowed
	// (FillArrayObjectByFilter does it first), so a copy whose declaration is gone stops
	// contributing a column, and the next restructuring drops it exactly as it drops a
	// deleted attribute.
	//
	// A copy with NO declaration at all (one pasted in from another configuration) is the
	// same case and answers the same way, which is why no orphan sweep is needed.
	virtual bool IsAllowed() const override {
		const ibValueMetaObjectCommonAttribute* const src = GetSource();
		return ibValueMetaObjectAttribute::IsAllowed() && src != nullptr && src->IsAllowed();
	}

	//support icons
	virtual wxIcon GetIcon() const;
	static wxIcon GetIconGroup();

	// NOT REMOVABLE WHERE IT SITS — refused in the DELETE event, not the close one (the
	// close phase runs on every metaobject when a configuration closes; refusing there
	// stops the configuration from closing).
	//
	// The refusal lives in the metaobject rather than in the tree: delete is reachable
	// from the context menu, the toolbar and the keyboard, and a guarantee that holds
	// only in the UI is not a guarantee.
	virtual bool OnDeleteMetaObject() override;
	// Called by the declaration immediately before it removes this copy.
	void AllowRemoval() { m_removalAllowed = true; }

	// The designer offers no New / Remove on a copy — only a jump to where it is edited.
	virtual bool PrepareContextMenu(class wxMenu* defaultMenu) override;
	virtual void ProcessCommand(unsigned int id) override;

	virtual bool OnLoadMetaObject(class ibMetaData* metaData) override;
	// BOUND HERE, not on load. Load walks the tree in stored order, so an object can be
	// read before the Common branch that holds the declarations — binding on load leaves
	// those copies with no source, hence no type, and the source explorer copies a node's
	// type BY VALUE at build time (srcDataObject.h), so a form built over such a copy has
	// a column with no type at all. By the run phase the whole configuration is in memory.
	virtual bool OnBeforeRunMetaObject(int flags) override;

	protected:

	virtual bool ReadData(const class ibDataNode& node) override;
	virtual bool WriteData(class ibDataNode& node) const override;

	private:

	// The declaration, and its id. The id is what survives serialisation; the pointer
	// is bound on load, because a metaobject is only reachable once the whole
	// configuration is in memory.
	ibValuePtr<ibValueMetaObjectCommonAttribute> m_source;
	ibMetaID m_sourceMetaId = 0;

	// Answered when the declaration is gone — never a null dereference, and never a
	// silently wrong type.
	static ibTypeDescription s_emptyTypeDesc;

	// Set for the one call that is allowed to take this copy away.
	bool m_removalAllowed = false;
};

#endif // !__META_COMMON_ATTRIBUTE_OBJECT_H__

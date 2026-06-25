#ifndef __FORM_ATTRIBUTE_H__
#define __FORM_ATTRIBUTE_H__

// -----------------------------------------------------------------------
// ibValueFormAttribute — a FORM-LOCAL, typed source attribute.
//
// A form no longer works with a single hard-wired data object. Instead it
// owns a REGISTRY of attributes; each attribute is a typed slot that a
// source object is assigned into, and that controls bind to. The attribute
// is two things by inheritance:
//
//   * runtime value   — ibValueDynamicMembers (an ibValue): export-bound
//                        into the form module by Name, so script reaches it
//                        through `ThisForm.<Name>`.
//   * property object  — ibPropertyObject: Name + Type properties; lives
//                        ONLY on the front and is serialized together with
//                        the control tree, never in the data metadata.
//
// The source itself is NOT a base — a real ibSourceDataObject "flies into"
// the attribute (m_assignedSource). The MAIN attribute (registry owner) is
// where the source object passed to the form on open lands; from then on
// the value is read off this attribute. A simple-type attribute instead
// holds its own value cell.
//
// The attribute's TYPE drives what kind of source it accepts (object of a
// document / catalog, a list, a dynamic list, or a plain reference → read-
// only). Type-driven dispatch is staged; the first slice gates assignment
// by type and forwards reads to the assigned source.
// -----------------------------------------------------------------------

#include "frontend/frontend.h"           // FRONTEND_API
#include "backend/compiler/value.h"
#include "backend/propertyManager/propertyManager.h"
#include "backend/backend_type.h"       // ibBackendFormAttribute (backend wrapper)
#include "backend/uniqueKey.h"

class BACKEND_API ibSourceDataObject;
class BACKEND_API ibDataNode;
class BACKEND_API ibMetaData;
class FRONTEND_API ibValueForm;


// -----------------------------------------------------------------------
// ibFormAttributeValue — the form's per-attribute registry entry AND the runtime VALUE the
// attribute manages. It CREATES its own description (ibValueFormAttribute: the amorphous
// name / type / id) internally — outside the form only holders exist, never a bare description.
// Being a runtime value it is ref-counted; the form keeps these by ibValuePtr.
// -----------------------------------------------------------------------
class FRONTEND_API ibFormAttributeValue :
	// ibValueDynamicMembers FIRST keeps ibValue at offset 0 (see reference_ibvalue_first_base_pmf).
	public ibValueDynamicMembers,
	public ibBackendFormAttributeValue,
	public ibPropertyObject {

	friend class ibValueForm;   // the form builds / configures the nested description via the holder

	// ── ibFormAttribute — PRIVATE nested Impl ────────────────────────────────────────────────
	// The amorphous description (Name / Type / FillCheck) — the holder's OWN entity, invisible
	// outside (like ibValueModelColumnInfo / ibVariantDataValueImpl). A no-register internal value
	// type: never handed out bare, never serialized by its own clsid; the holder owns and drives it.
	class ibFormAttribute :
		public ibValueDynamicMembers,
		public ibPropertyObject,
		public ibBackendTypeConfigFactory {
	public:

		ibFormAttribute(ibValueForm* ownerForm = nullptr);
		virtual ~ibFormAttribute();

		virtual wxString GetClassName() const override { return ibValue::GetClassName(); }
		virtual wxString GetObjectTypeName() const override { return GetAttributeName(); }
		virtual bool IsEditable() const override { return true; }

		// ibBackendTypeConfigFactory = the Type-config factory the Type property's variant resolves
		// through (internal — the holder's FAÇADE re-exposes name / id / type, never this object):
		virtual ibTypeDescription& GetTypeDesc() const override { return m_propertyType->GetValueAsTypeDesc(); }
		virtual ibSelectorDataType GetFilterDataType() const override { return ibSelectorDataType::ibSelectorDataType_any; }
		virtual const ibMetaData* GetMetaData() const override;

		// the description's OWN name / id / main — the holder's facade forwards to these:
		wxString GetAttributeName() const { return m_propertyName->GetValueAsString(); }
		bool     IsMainAttribute() const { return m_isMain; }
		ibMetaID GetAttributeId() const { return m_attributeId; }

		// Holder-only (NOT on the interface) — reached through friendship from the holder / form:
		ibSourceDataType GetSourceDataType() const; // table (List type) / attribute
		bool IsTypeProperty(const ibProperty* prop) const { return prop == m_propertyType; }
		void SetAttributeName(const wxString& name) { m_propertyName->SetValue(name); }
		void SetMainAttribute(bool main) { m_isMain = main; }
		ibValueForm* GetOwnerForm() const { return m_ownerForm; }
		void SetOwnerForm(ibValueForm* ownerForm) { m_ownerForm = ownerForm; }
		void FillMembers(ibMemberTable& helper) const;   // bound in ctor
		virtual bool ReadProperty(const ibDataNode& node) override;
		virtual bool WriteProperty(ibDataNode& node) const override;
		void SetAttributeId(const ibMetaID& id) { m_attributeId = id; }
		int  GetFillCheck() const { return m_propertyFillCheck->GetValueAsInteger(); }

	protected:

		bool FillFillCheck(ibPropertyList* prop);

		ibValueForm* m_ownerForm;
		bool         m_isMain = false;
		ibMetaID     m_attributeId = wxNOT_FOUND;

		ibPropertyCategory* m_categoryCommon = ibPropertyObject::CreatePropertyCategory(wxT("Common"), _("General"));
		ibPropertyUString* m_propertyName = ibPropertyObject::CreateProperty<ibPropertyUString>(m_categoryCommon, wxT("Name"), _("Name"), _("Attribute name"), wxT(""));
		ibPropertyType* m_propertyType = ibPropertyObject::CreateProperty<ibPropertyType>(m_categoryCommon, wxT("Type"), _("Type"), ibValueTypes::TYPE_STRING);
		ibPropertyList* m_propertyFillCheck = ibPropertyObject::CreateProperty<ibPropertyList>(m_categoryCommon, wxT("FillCheck"), _("Fill check"), &ibFormAttribute::FillFillCheck, 0);
	};

public:

	// The holder IS the inspector's property-object — a pure ACCUMULATOR. It owns no properties
	// of its own; it attaches the attribute (its standard Name/Type/FillCheck) and, if the held
	// value supports properties (a dynamic list → Source/Settings), that value too — both show
	// as a single whole. The attribute stays amorphous; the value lives here. The description is
	// BUILT internally from the owner form (no external attribute object is ever passed in).
	explicit ibFormAttributeValue(ibValueForm* ownerForm = nullptr);
	ibFormAttributeValue(const ibFormAttributeValue&) = delete;
	ibFormAttributeValue& operator=(const ibFormAttributeValue&) = delete;
	~ibFormAttributeValue();   // source is not held separately (lives in m_value) — trivial

	// --- ibPropertyObject (accumulator: attaches attribute + value) ---------
	virtual wxString GetClassName() const override { return ibValue::GetClassName(); }   // registered value type
	virtual wxString GetObjectTypeName() const override { return m_attribute->GetObjectTypeName(); }
	virtual bool IsEditable() const override { return true; }
	// Single interception point (the holder is what the inspector selects): forward the change
	// to the property's REAL owner (attribute / value — each reacts to its own), and on the
	// attribute's Type change re-materialise the value + re-accumulate.
	virtual void OnPropertyChanged(ibProperty* property, const wxVariant& oldValue, const wxVariant& newValue) override;
	// Serialization: the attribute's own set, then the held value's own (its clsid's Source /
	// Settings). The attribute is read FIRST so its Type is known before the value materialises.
	virtual bool ReadProperty(const ibDataNode& node) override;
	virtual bool WriteProperty(ibDataNode& node) const override;

	// Re-accumulate: detach all, attach the attribute, then the held value (if it is a
	// property-object). Called on construction and whenever the value / Type changes.
	void Refresh();

	// ibBackendFormAttributeValue FAÇADE — outside code reads name / id / type through the HOLDER;
	// the concrete description (ibFormAttribute) is never exposed. The holder forwards internally.
	wxString GetAttributeName() const override { return m_attribute->GetAttributeName(); }
	ibMetaID GetAttributeId() const override { return m_attribute->GetAttributeId(); }
	bool     IsMainAttribute() const override { return m_attribute->IsMainAttribute(); }
	const ibTypeDescription& GetTypeDesc() const override { return m_attribute->GetTypeDesc(); }

	// The value this entry manages — kept in sync with the attribute's Type on every get/set
	// via AdjustValue (it carries the metadata): a value whose type still matches is kept; a
	// stale one (Type changed) comes back EMPTY of the new type. The attribute is amorphous —
	// the value lives HERE, not on it.
	bool GetValue(ibValue& value) const { m_value = m_attribute->AdjustValue(m_value); value = m_value; return true; }
	// PLAIN set — the source must seat as-is (AddMainAttribute seats it BEFORE the Type is set;
	// adjusting against the still-empty Type would drop it). The sync to the Type happens in
	// Refresh (after the Type is known / on a Type change).
	bool SetHeldValue(const ibValue& value) { m_value = value; return true; }   // not SetValue: ibValue::SetValue clashes

	// Backing cell for the form-module local bind (the script variable's value) — the
	// LIVE slot address (BindLocalVariable stores the pointer; must stay valid).
	ibValue* GetBindValue() { return &m_value; }

	// The held value CONVERTED to a property-object / source — NOT stored separately; the value
	// (m_value) IS the source, kept alive by its own refcount. Both sync to the attribute's Type
	// first (AdjustValue). Null when the value is not that kind.
	ibPropertyObject*   GetValueAsProperty() const;   // → Source / Settings (a dynamic list)
	ibSourceDataObject* GetValueAsSource()   const;   // → the form's data source

	// ibBackendFormAttributeValue — the source is just the value as a source (not a stored field).
	ibSourceDataObject* GetSourceValue() const override { return GetValueAsSource(); }
	void SetSourceValue(ibSourceDataObject* source);

	// True when the value is a REFERENCE (everything read THROUGH it is read-only).
	bool IsReferenceValue() const;

	// Read down a binding tail (relative to this entry): empty → the value itself;
	// deeper → step into the value (as a source object) and walk. Encapsulates the
	// source cross-cast so the form just delegates.
	bool GetValueByPath(const std::vector<ibSourceId>& tail, ibValue& result) const;
	bool SetValueByPath(const std::vector<ibSourceId>& tail, const ibValue& value);

	// Clipboard copy / paste of a form attribute through OUR serialization (own clipboard
	// format id) — designer-only (no-op on web). Copy serializes the attribute's description;
	// Paste deserializes it as a fresh NON-main entry on `form` (via ibValueForm::PasteAttribute).
	static bool CopyToClipboard(const ibFormAttributeValue* entry);
	static ibFormAttributeValue* PasteFromClipboard(ibValueForm* form);

private:
	ibValuePtr<ibFormAttribute> m_attribute;   // the nested description (private Impl), created in the ctor
	mutable ibValue m_value;                         // the value the holder manages — IS the source/property; synced to Type via AdjustValue
};

#endif

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

class FRONTEND_API ibValueFormAttribute :
	// ibValueDynamicMembers FIRST keeps ibValue at offset 0 (member-pmf
	// binds need no MI this-adjustment — see reference_ibvalue_first_base_pmf).
	public ibValueDynamicMembers,
	public ibPropertyObject,
	// Backend wrapper — IS-A ibBackendTypeConfigFactory (carries the attribute's
	// Type + metadata), which the Type property (ibPropertyType) requires and the
	// picker / property source read without any cross-cast.
	public ibBackendFormAttribute {
public:

	ibValueFormAttribute(ibValueForm* ownerForm = nullptr);
	virtual ~ibValueFormAttribute();

	// ---- identity / property-object surface ------------------------------
	// ibPropertyObject declares GetClassName pure; resolve it (and the clsid)
	// through the value-type registry — the type is registered via
	// SYSTEM_TYPE_REGISTER in the .cpp, so no hand-rolled clsid here.
	virtual wxString GetClassName() const override { return ibValue::GetClassName(); }
	virtual wxString GetObjectTypeName() const override { return GetAttributeName(); }
	virtual bool IsEditable() const override { return true; }

	// ---- ibBackendFormAttribute (backend wrapper) ------------------------
	virtual wxString GetAttributeName() const override { return m_propertyName->GetValueAsString(); }
	// ibBackendTypeConfigFactory: the type description (from the Type property) +
	// metadata (via the owner form) — the Type property's variant resolves the
	// selector through these.
	virtual ibTypeDescription& GetTypeDesc() const override { return m_propertyType->GetValueAsTypeDesc(); }
	// A form attribute accepts ANY type (primitive / reference / table / dynamic
	// list), not just references like the default config factory.
	virtual ibSelectorDataType GetFilterDataType() const override { return ibSelectorDataType::ibSelectorDataType_any; }
	// "Main attribute" — the form owner. The source object passed on open is
	// assigned into THE main attribute; only one is main per form. A plain
	// flag (not shown in the property grid), set by the registry / build.
	virtual bool IsMainAttribute() const override { return m_isMain; }

	// Kind of source this attribute represents (table when its Type is a list /
	// collection, attribute otherwise) — the form filters its attributes by this.
	// NOT on the backend interface: only the FORM (which holds concrete attributes)
	// uses it.
	ibSourceDataType GetSourceDataType() const; // table (List type) / attribute

	void SetAttributeName(const wxString& name) { m_propertyName->SetValue(name); }
	void SetMainAttribute(bool main) { m_isMain = main; }

	ibValueForm* GetOwnerForm() const { return m_ownerForm; }
	void SetOwnerForm(ibValueForm* ownerForm) { m_ownerForm = ownerForm; }

	// Metadata via the owner form — needed to materialise a reference / object
	// Type value (and by the Type property's variant).
	virtual const ibMetaData* GetMetaData() const override;

	// Property events. The attribute is pure DEFINITION — it has NO runtime value; the
	// value (and AssignSource / GetValue / IsReferenceValue / bind) lives on the owning
	// ibFormAttributeValue wrapper.
	virtual void OnPropertyChanged(ibProperty* property, const wxVariant& oldValue, const wxVariant& newValue) override;

	// ---- runtime member surface (export-bound into the form module) ------
	void FillMembers(ibMemberTable& helper) const;   // bound in ctor

	// ---- serialization (packed alongside the control tree) ---------------
	bool ReadData(const ibDataNode& node);
	bool WriteData(ibDataNode& node) const;

	// the attribute's own id — addresses the simple-type value cell.
	virtual ibMetaID GetAttributeId() const override { return m_attributeId; }
	void     SetAttributeId(const ibMetaID& id) { m_attributeId = id; }

	// "Fill check" — how the attribute's value is validated on save:
	// 0 = don't check, 1 = show error.
	int GetFillCheck() const { return m_propertyFillCheck->GetValueAsInteger(); }

protected:

	bool FillFillCheck(ibPropertyList* prop);   // list options for the Fill check property

	ibValueForm* m_ownerForm;

	bool     m_isMain = false;
	ibMetaID m_attributeId = wxNOT_FOUND;

	ibPropertyCategory* m_categoryCommon = ibPropertyObject::CreatePropertyCategory(wxT("Common"), _("General"));
	ibPropertyUString* m_propertyName = ibPropertyObject::CreateProperty<ibPropertyUString>(m_categoryCommon, wxT("Name"), _("Name"), _("Attribute name"), wxT(""));
	ibPropertyType* m_propertyType = ibPropertyObject::CreateProperty<ibPropertyType>(m_categoryCommon, wxT("Type"), _("Type"), ibValueTypes::TYPE_EMPTY);
	ibPropertyList* m_propertyFillCheck = ibPropertyObject::CreateProperty<ibPropertyList>(m_categoryCommon, wxT("FillCheck"), _("Fill check"), &ibValueFormAttribute::FillFillCheck, 0);
};

// -----------------------------------------------------------------------
// ibFormAttributeValue — the form's per-attribute registry entry: OWNS the attribute
// (its DEFINITION: name / type / id) and HOLDS, separately, the runtime VALUE the
// attribute manages, plus the value behaviour (assign / read / reference test). The
// attribute itself is pure definition. Non-copyable; the form keeps these by unique_ptr
// so a held pointer stays valid across vector growth.
// -----------------------------------------------------------------------
class FRONTEND_API ibFormAttributeValue {
public:

	explicit ibFormAttributeValue(ibValueFormAttribute* attr) : m_attribute(attr) {}
	ibFormAttributeValue(const ibFormAttributeValue&) = delete;
	ibFormAttributeValue& operator=(const ibFormAttributeValue&) = delete;
	~ibFormAttributeValue();   // releases the held source (SourceDecrRef)

	ibValueFormAttribute* GetAttribute() const { return m_attribute; }

	// Facades over the wrapped attribute's description — avoid GetAttribute()->X chains.
	wxString GetAttributeName() const { return m_attribute->GetAttributeName(); }
	bool     IsMainAttribute() const { return m_attribute->IsMainAttribute(); }
	ibMetaID GetAttributeId() const { return m_attribute->GetAttributeId(); }

	// The runtime value this entry manages.
	bool GetValue(ibValue& value) const { value = m_value; return true; }
	bool SetValue(const ibValue& value) { m_value = value; return true; }

	// Backing cell for the form-module local bind (the script variable's value) — the
	// LIVE slot address (BindLocalVariable stores the pointer; must stay valid).
	ibValue* GetBindValue() { return &m_value; }

	// The form's data source. Held as a refcounted owner (SourceIncrRef) in m_sourceData,
	// SEPARATELY from m_value, so it survives even if the script reassigns the bound value
	// cell. SetSourceValue also seats it into m_value (for the bind / dot-walk). nullptr
	// clears it (the attribute goes empty — e.g. when it stops being the main).
	ibSourceDataObject* GetSourceValue() const;
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
	static bool CopyToClipboard(const ibValueFormAttribute* attr);
	static ibFormAttributeValue* PasteFromClipboard(ibValueForm* form);

private:
	ibValuePtr<ibValueFormAttribute> m_attribute;   // owns the attribute (definition)
	ibValue m_value;                                 // the value the attribute manages (bound cell)
	ibSourceDataObject* m_sourceData = nullptr;      // the source, held via SourceIncrRef (stable owner)
};

#endif

////////////////////////////////////////////////////////////////////////////
//	Description : form-local typed source attribute (registry slot on a form)
////////////////////////////////////////////////////////////////////////////

#include "formAttribute.h"
#include "form.h"

#include "backend/metaCollection/partial/commonObject.h"   // ibSourceDataObject
#include "backend/serialize/dataBuilder.h"
#include "backend/system/value/valueType.h"                 // ibValueTypeDescription::AdjustValue
#include "backend/composition/listFilter.h"                 // ibValueListSettings (list settings form)
#include "backend/tableInfo.h"                               // ibValueModel (table-source check)
#include "backend/typeDescription.h"                          // ibTypeDescription::GetFirstClsid (value materialise)
#include "backend/metaData.h"                                // GetTypeCtor
#include "backend/objCtor.h"                                 // ibCtorMetaValueType / ibCtorObjectMetaType
#include "backend/appData.h"                                 // ibApplicationData::GetActiveMetaData (metadata fallback)
#include "backend/metadataConfiguration.h"                   // ibMetaDataConfigurationBase : ibMetaData (upcast)
#include "backend/clsid.h"
#include "backend/backend_core.h"                            // oes_clipboard_attribute
#include "backend/fileSystem/fs.h"                           // ibWriterMemory / ibReaderMemory (clipboard serialize)
#ifndef OES_USE_WEB
#include "frontend/visualView/visualHost.h"                  // g_visualHostContext / RefreshEditor (designer refresh)
#include <wx/clipbrd.h>
#include <wx/dataobj.h>
#endif

//*********************************************************************************************
//*   ibValueForm attribute store / path resolve (relocated here from form.cpp)              *
//*********************************************************************************************

bool ibValueForm::ReadAttributes(const ibDataNode& node)
{
	// The MAIN attribute is NEVER serialized — it is always reconstructed from the form's
	// source in the ctor (its name List/Object and Type derive from the source; its id is
	// stable). Only the user-added (non-main) attributes are persisted. So: KEEP the ctor's
	// main, REPLACE the non-main set from the "Attributes" section, and DROP any main-flagged
	// entry from an old blob (the ctor's main wins — never a second "Object").
	for (auto it = m_attributes.begin(); it != m_attributes.end(); ) {
		if ((*it)->IsMain()) ++it;
		else { DropAttributeBinds(*it); it = m_attributes.erase(it); }
	}

	if (const ibDataNode* attrsNode = node.FindChild(wxT("Attributes"))) {
		for (const ibDataNode& attrNode : attrsNode->Children()) {
			if (attrNode.GetValue<bool>(wxT("Main")))
				continue;   // the main belongs to the ctor — never load one from the blob
			ibValuePtr<ibFormAttributeValue> holder(new ibFormAttributeValue(this));   // builds its description
			holder->m_attribute->SetMainAttribute(false);
			if (!holder->ReadProperty(attrNode))   // facade: reads the attribute + the generated value
				return false;
			m_attributes.emplace_back(std::move(holder));
		}
	}
	return true;
}

bool ibValueForm::WriteAttributes(ibDataNode& node) const
{
	// Persist ONLY the user-added (non-main) attributes. The MAIN is reconstructed from the
	// source in the ctor, so writing it would just produce a duplicate main on copy / load.
	ibDataNode& attrsNode = node.Child(wxT("Attributes"));
	for (const auto& av : m_attributes) {
		if (av->IsMain())
			continue;
		ibDataNode& attrNode = attrsNode.AddChild(av->GetClassType(), av->GetId());
		// WriteProperty returns false when the attribute is INCOMPLETE (a dynamic list with no
		// source picked) — FAIL the whole write so a broken definition is never persisted (the
		// form's save is refused, not silently partial).
		if (!av->WriteProperty(attrNode))
			return false;
	}
	return true;
}

ibMetaID ibValueForm::NextAttributeId() const
{
	ibMetaID nextId = 1;   // form-unique = max existing + 1
	for (const auto& av : m_attributes)
		if (av->GetId() >= nextId)
			nextId = av->GetId() + 1;
	return nextId;
}

void ibValueForm::WireAttribute(ibFormAttributeValue* entry)
{
	// Designer-time add (module already live) wires the variable immediately so script/editor see
	// it; initial + load adds happen before the module exists (InitializeFormModule binds the set).
	// The member surface (ThisForm.<attr>) is refreshed either way.
	if (GetCompileModule() != nullptr)
		BindAttributeVariable(entry);

	InvalidateNames();
}

ibFormAttributeValue* ibValueForm::AddAttribute(const wxString& name, const ibClassID& type, const ibValue& value)
{
	// Name + Type are mandatory — an attribute is never empty. Make + attach in one step.
	return AttachAttribute(MakeAttribute(name, type, value));
}

ibMetaID ibValueForm::AddAutoAttribute(const wxString& name, const ibTypeDescription& typeDesc)
{
	// Attribute name = the control's name, made UNIQUE among the form's attributes (a pre-existing
	// same-named attribute gets a numeric suffix). Type = the control's default type desc (already
	// produced by the source-type generator — checkbox Boolean, tablebox value table, else String).
	wxString uniqueName = name;
	for (unsigned int index = 0; GetAttribute(uniqueName) != nullptr; )
		uniqueName = wxString::Format(wxT("%s%u"), name, ++index);
	ibFormAttributeValue* entry = AddAttribute(uniqueName, typeDesc.GetFirstClsid(), ibValue());
	return entry != nullptr ? entry->GetId() : ibMetaID(wxNOT_FOUND);
}

ibValuePtr<ibFormAttributeValue> ibValueForm::MakeAttribute(const wxString& name, const ibClassID& type, const ibValue& value)
{
	// Build a holder UNOWNED by the form (no registry slot, no module bind yet) — the caller (the
	// visual-editor InsertAttribute command) decides when to attach it. The holder builds its own
	// description; we just configure it.
	ibValuePtr<ibFormAttributeValue> holder(new ibFormAttributeValue(this));
	holder->m_attribute->SetAttributeName(name);   // friend access to the private description
	if (type != 0)   // empty → keep the attribute's own default (String); the variant resolves it
		holder->m_attribute->SetDefaultMetaType(type);

	holder->SetHeldValue(value);
	holder->Refresh();   // facade accumulates the value's own properties (if it has any)
	return holder;
}

ibFormAttributeValue* ibValueForm::AttachAttribute(ibValuePtr<ibFormAttributeValue> holder)
{
	if (holder == nullptr)
		return nullptr;
	ibFormAttributeValue* entry = holder;
	m_attributes.emplace_back(std::move(holder));   // form co-owns it (ref-counted)
	WireAttribute(entry);                            // module bind (if live) + InvalidateNames
	return entry;
}

ibValuePtr<ibFormAttributeValue> ibValueForm::DetachAttribute(ibFormAttributeValue* entry)
{
	for (auto it = m_attributes.begin(); it != m_attributes.end(); ++it) {
		if (*it == entry) {
			DropAttributeBinds(*it);                          // unbind before it leaves the form
			ibValuePtr<ibFormAttributeValue> holder = std::move(*it);
			m_attributes.erase(it);
			InvalidateNames();
			return holder;                                          // ref handed to the caller (undo stack co-owns)
		}
	}
	return nullptr;
}

ibFormAttributeValue* ibValueForm::AddMainAttribute(const wxString& name, const ibClassID& type, ibSourceDataObject* value)
{
	wxASSERT(GetMainAttribute() == nullptr);   // only ONE main per form

	// Order matters: id + name + MAIN flag + register + SEAT THE SOURCE, and only THEN set the Type.
	// A form with no creator (auto-built list form) resolves its metadata THROUGH the source object;
	// SetDefaultMetaType triggers a Type refresh that needs it, so seating the source AFTER it would
	// be too late → null metadata → crash in DoRefreshTypeDesc.
	ibValuePtr<ibFormAttributeValue> holder(new ibFormAttributeValue(this));   // builds its description
	holder->m_attribute->SetAttributeName(name);   // friend access to the private description
	holder->m_attribute->SetMainAttribute(true);

	holder->SetSourceValue(value);                 // seat the source BEFORE the Type (metadata flows through it)
	holder->m_attribute->SetDefaultMetaType(type); // safe — the Type refresh sees the source's metadata
	holder->Refresh();                             // facade accumulates the value's own properties (if any)
	return AttachAttribute(std::move(holder));     // registers + wires
}

void ibValueForm::DeleteAttribute(const wxString& name)
{
	for (auto it = m_attributes.begin(); it != m_attributes.end(); ++it)
		if ((*it)->GetName() == name) {
			DropAttributeBinds(*it);
			m_attributes.erase(it);
			InvalidateNames();
			return;
		}
}

void ibValueForm::DeleteAttribute(unsigned int idx)
{
	if (idx < m_attributes.size()) {
		DropAttributeBinds(m_attributes[idx]);
		m_attributes.erase(m_attributes.begin() + idx);
		InvalidateNames();
	}
}

void ibValueForm::BindAttributeVariable(ibFormAttributeValue* entry)
{
	if (entry == nullptr)
		return;
	const wxString name = entry->GetName();
	if (!name.IsEmpty())
		BindLocalVariable(name, entry->GetBindValue());   // <name> / ThisForm.<name>
	// The MAIN attribute additionally drives the exported DataSource (the form's source).
	if (entry->IsMain())
		BindExportVariable(wxT("DataSource"), entry->GetBindValue());
}

void ibValueForm::DropAttributeBinds(ibFormAttributeValue* entry)
{
	// Before the wrapper dies, remove the form-module binds that point INTO it — the local
	// (the attribute's own name) and, for the main, the exported DataSource (both seeded with
	// &m_value). Left in place they dangle and the next compile (syntax-check on close) reads
	// freed memory — see the close-after-delete-main crash. The source the wrapper held is
	// released by its dtor (SourceDecrRef); nothing else owns it, which is fine.
	if (entry == nullptr)
		return;
	UnbindVariable(entry->GetName());
	if (entry->IsMain())
		UnbindVariable(wxT("DataSource"));
}

bool ibValueForm::IsAttributeNameUnique(const wxString& name, const ibFormAttributeValue* except) const
{
	for (const auto& av : m_attributes)
		if (av != except && av->GetName() == name)
			return false;
	return true;
}

wxString ibValueForm::MakeUniqueAttributeName(const wxString& base) const
{
	const wxString stem = base.IsEmpty() ? wxT("Attribute") : base;
	if (IsAttributeNameUnique(stem))
		return stem;
	for (unsigned int n = 1; ; ++n) {
		const wxString candidate = wxString::Format(wxT("%s%u"), stem, n);
		if (IsAttributeNameUnique(candidate))
			return candidate;
	}
}

bool ibValueForm::RenameAttribute(ibFormAttributeValue* entry, const wxString& newName)
{
	if (entry == nullptr || newName.IsEmpty() || !IsAttributeNameUnique(newName, entry))
		return false;
	// Re-bind under the new name: drop the old local (and DataSource if main), rename, re-wire.
	DropAttributeBinds(entry);
	entry->m_attribute->SetAttributeName(newName);
	WireAttribute(entry);
	return true;
}

ibFormAttributeValue* ibValueForm::PasteAttribute(const ibDataNode& node)
{
	ibValuePtr<ibFormAttributeValue> holder(new ibFormAttributeValue(this));   // builds its description
	auto& attr = *holder->m_attribute;   // friend access to the private description
	attr.ReadProperty(node);
	attr.SetMainAttribute(false);   // a paste is never the main — only one main per form
	attr.SetAttributeId(NextAttributeId());   // fresh id so its bindings never collide
	attr.SetAttributeName(MakeUniqueAttributeName(attr.GetName()));
	return AttachAttribute(std::move(holder));
}

ibFormAttributeValue* ibValueForm::GetAttribute(const wxString& name) const
{
	for (const auto& av : m_attributes)
		if (av->GetName() == name)
			return av;
	return nullptr;
}

ibFormAttributeValue* ibValueForm::GetAttribute(unsigned int idx) const
{
	if (idx >= m_attributes.size())
		return nullptr;
	return m_attributes[idx];
}

ibFormAttributeValue* ibValueForm::GetMainAttribute() const
{
	for (const auto& av : m_attributes)
		if (av->IsMain())
			return av;
	return nullptr;
}

void ibValueForm::SetMainAttribute(ibFormAttributeValue* entry)
{
	// The source flows to whoever is main. entry == nullptr CLEARS main entirely (no attribute is
	// the data source) — the toggle-off path. SetSourceValue IncrRefs the new before the old
	// releases, so it never drops to zero mid-move; re-selecting the same main is a no-op.
	ibFormAttributeValue* oldMain = GetMainAttribute();
	// Hand the demoted main's seated source to the new main — but ONLY into an EMPTY slot. An attribute
	// that already holds its own value (a value-table with its designed columns) keeps it: promoting it
	// to main must NOT overwrite that value with the inherited source (which is empty at design time, so
	// the columns silently vanished — the reported bug). Refresh/AdjustValue below is the sole type-sync.
	if (entry != nullptr && entry != oldMain && !entry->HasHeldValue())
		entry->SetSourceValue(oldMain != nullptr ? oldMain->GetSourceValue() : nullptr);

	// Sole-main invariant: `entry` becomes main; every other loses it. Each holder RE-MATERIALISES its
	// value from its own Type via Refresh(), whose AdjustValue KEEPS a value whose Type still matches and
	// empties a stale one — so it is the sole type-sync. A holder that owns its value (a value-table with
	// its designed columns) keeps it; only a genuinely EMPTY slot has its seated source cleared before the
	// re-materialise. Forcing SetSourceValue(nullptr) on an owned value was what silently dropped a
	// value-table's columns on set/unset-main; AdjustValue already handles the stale case without it.
	for (const auto& av : m_attributes) {
		const bool isMain = (entry != nullptr && av == entry);
		av->m_attribute->SetMainAttribute(isMain);
		if (!isMain && !av->HasHeldValue())
			av->SetSourceValue(nullptr);   // clear ONLY an empty slot; an owned value (value-table columns) survives
		av->Refresh();                     // AdjustValue keeps a type-matching value, empties a stale one
	}
	// Re-point the exported DataSource at the new main's cell (BindExport overwrites the single
	// "DataSource" slot, so the old main's ownership is dropped — no per-old unbind). Only when
	// the module is already live AND there is a main; toggle-off leaves the last bind harmless
	// (its cell is now empty).
	if (GetCompileModule() != nullptr && entry != nullptr)
		BindExportVariable(wxT("DataSource"), entry->GetBindValue());
}

ibFormAttributeValue* ibValueForm::FindAttributeById(const ibMetaID& id) const
{
	for (const auto& av : m_attributes)
		if (av->GetId() == id)
			return av;
	return nullptr;
}

bool ibValueForm::GetSourceList(ibSourceDataType kind, std::vector<ibBackendFormAttributeValue*>& out) const
{
	// tableColumn sources from its parent table, not the form's attributes.
	if (kind == ibSourceDataType::ibSourceDataType_tableColumn)
		return false;

	// A TABLE control reaches the MAIN attribute regardless of the main's own kind: an
	// OBJECT main descends into its tabular sections, a LIST main IS the table. A SCALAR
	// control must NOT see a list/table main — its "fields" are table COLUMNS that belong
	// to a tablebox, not scalar bindings (a scalar control only sees attributes whose own
	// kind is scalar). Auxiliary attributes always match by kind. The binding resolve
	// walks from the attribute (path[0] = its id, the gate) — see WalkPath.
	for (const auto& av : m_attributes) {
		auto& a = *av->m_attribute;   // friend access to the private description
		bool include = (a.GetSourceDataType() == kind);
		if (kind == ibSourceDataType::ibSourceDataType_table && !include) {
			// A TABLE control also reaches the MAIN (any kind) AND any COMPOSITE attribute (an object /
			// reference — NOT a scalar primitive), so the picker can DESCEND into its tabular sections.
			// Before, only the MAIN object descended; a plain object attribute on a common form was
			// invisible to the tablebox picker (and its section-path head wouldn't resolve).
			const ibTypeDescription& td = a.GetTypeDesc();
			const bool scalar = td.ContainType(ibValueTypes::TYPE_STRING) || td.ContainType(ibValueTypes::TYPE_NUMBER)
				|| td.ContainType(ibValueTypes::TYPE_DATE) || td.ContainType(ibValueTypes::TYPE_BOOLEAN);
			include = a.IsMain() || !scalar;
		}
		if (include)
			out.push_back((ibFormAttributeValue*)av);   // the HOLDER (IS-A ibBackendFormAttributeValue)
	}
	return !out.empty();
}

bool ibValueForm::GetValueByAttributePath(const ibSourceDescription& desc, ibValue& result) const
{
	const std::vector<ibSourceId>& path = desc.GetPath();
	if (path.empty())
		return false;

	// Head selects an entry from the registry → it reads the rest of the path itself.
	if (ibFormAttributeValue* attr = FindAttributeById(path.front())) {
		const std::vector<ibSourceId> tail(path.begin() + 1, path.end());
		return attr->GetValueByPath(tail, result);
	}

	// path[0] is ALWAYS a form-local attribute id — if it resolves to no attribute
	// the binding is stale (e.g. an attribute removed). NEVER feed the form-local
	// head to the source (it is not a record field → GetValueByMetaID asserts).
	return false;
}

bool ibValueForm::IsWritableBinding(const ibSourceDescription& desc) const
{
	const std::vector<ibSourceId>& path = desc.GetPath();
	if (path.empty())
		return false;
	if (ibFormAttributeValue* attr = FindAttributeById(path.front())) {
		if (attr->IsReferenceValue())
			return false;            // anything read through a reference is read-only
		return path.size() <= 2;     // [attr] or [attr, directField]
	}
	// path[0] is always a form-local attribute (the gate) — a non-attribute head is a stale binding.
	return false;
}

bool ibValueForm::SetValueByAttributePath(const ibSourceDescription& desc, const ibValue& value)
{
	if (!IsWritableBinding(desc))
		return false;   // dot-walk binding is read-only

	const std::vector<ibSourceId>& path = desc.GetPath();
	if (ibFormAttributeValue* attr = FindAttributeById(path.front())) {
		const std::vector<ibSourceId> tail(path.begin() + 1, path.end());
		return attr->SetValueByPath(tail, value);
	}

	// path[0] is always a form-local attribute (the gate) — a non-attribute head is a stale binding.
	return false;
}

//*********************************************************************************************
//*                                   construction                                          *
//*********************************************************************************************

ibFormAttributeValue::ibFormAttribute::ibFormAttribute(ibValueForm* ownerForm)
	: ibValueDynamicMembers(ibValueTypes::TYPE_VALUE, true), m_ownerForm(ownerForm), m_attributeId(ownerForm ? ownerForm->NextAttributeId() : 0)   // true = no-register internal value
{
	m_members.Bind(this, &ibFormAttributeValue::ibFormAttribute::FillMembers);
}

ibFormAttributeValue::ibFormAttribute::~ibFormAttribute()
{
}

const ibMetaData* ibFormAttributeValue::ibFormAttribute::GetMetaData() const
{
	const ibMetaData* md = m_ownerForm != nullptr ? m_ownerForm->GetMetaData() : nullptr;
	// A queryable-based source (a dynamic list) carries NO metaobject and an auto-built form has
	// no creator → the form yields null; fall back to the ACTIVE configuration metadata so the
	// Type property still resolves (was a null-deref crash in DoRefreshTypeDesc).
	return md != nullptr ? md : ibApplicationData::GetActiveMetaData();
}

//*********************************************************************************************
//*  value entity — the wrapper holds the value; the attribute only describes its Type        *
//*********************************************************************************************

ibFormAttributeValue::ibFormAttributeValue(ibValueForm* ownerForm)
	: ibValueDynamicMembers(ibValueTypes::TYPE_VALUE), m_attribute(new ibFormAttribute(ownerForm))
{
	// The holder BUILDS and owns its description (the amorphous Name / Type / FillCheck shell): attach
	// it so those standard properties surface through the holder. The generated value is attached by
	// Refresh (when it is set, or its Type changes).
	AttachPropertyObject(m_attribute);
}

ibFormAttributeValue::~ibFormAttributeValue()
{
	// The source is NOT held separately — it lives in m_value (released with it). Nothing to do.
}

void ibFormAttributeValue::Refresh()
{
	// Sync the value to the CURRENT Type here (the single sync point — the converters stay pure
	// to avoid the GetMetaData recursion). A stale value (Type changed) becomes empty of the new
	// type / a fresh instance.
	m_value = m_attribute->AdjustValue(m_value);
	// Re-accumulate: the attribute (always) + the held value if it supports properties (a
	// dynamic list surfaces Source / Settings). Detach first so a Type change drops the old
	// value's properties.
	DetachAllPropertyObjects();
	AttachPropertyObject(m_attribute);
	if (ibPropertyObject* po = GetValueAsProperty())
		AttachPropertyObject(po);
}

void ibFormAttributeValue::OnPropertyChanged(ibProperty* property, const wxVariant& oldValue, const wxVariant& newValue)
{
	// The holder is what the inspector selects — forward the change to the property's REAL
	// owner (the hidden attribute, or the held value) so each reacts to its own.
	ibPropertyObject* owner = property != nullptr ? property->GetPropertyObject() : nullptr;
	if (owner != nullptr && owner != this)
		owner->OnPropertyChanged(property, oldValue, newValue);

	// The attribute's Type drives the value's type: re-materialise + re-accumulate, so the
	// inspector shows the new type's properties.
	if (owner == static_cast<ibPropertyObject*>(&*m_attribute) && m_attribute->IsTypeProperty(property))
		Refresh();

#ifndef OES_USE_WEB

	// g_visualHostContext = designer's editor notebook; web has no
	// designer context, so property changes just update the backing
	// ibProperty model without notifying a live editor.
	if (ibFrontendVisualEditorNotebook* editor = ibFrontendVisualEditorNotebook::FindEditorByForm(m_attribute->GetOwnerForm())) {
		editor->ModifyProperty(property, oldValue, newValue);
		editor->RefreshEditor();
	}

#else
	(void)property; (void)oldValue; (void)newValue;
#endif
}

void ibFormAttributeValue::OnChildChanged()
{
	// A nested value changed (a value-table column-info edited in the inspector). The holder is the
	// frontend end of the attach chain — re-render the bound control live. Keep bubbling up too (the
	// base is a no-op once there is no further owner).
	ibPropertyObject::OnChildChanged();

#ifndef OES_USE_WEB
	if (ibFrontendVisualEditorNotebook* editor = ibFrontendVisualEditorNotebook::FindEditorByForm(m_attribute->GetOwnerForm()))
		editor->RefreshEditor();
#endif
}

bool ibFormAttributeValue::WriteProperty(ibDataNode& node) const
{
	// The facade saves BOTH: the hidden attribute (Name / Type / FillCheck / id) and the value
	// it generates (its clsid's own data — a dynamic list's Source / Settings). The value's
	// verdict PROPAGATES: a value that refuses (a dynamic list with no source picked) makes the
	// whole attribute non-serializable — type-agnostic, the holder asks the value, not "is it a
	// dynamic list". On false the caller (WriteAttributes → WriteData) FAILS the whole form write.
	m_attribute->WriteProperty(node);
	if (ibPropertyObject* po = GetValueAsProperty())
		return po->WriteProperty(node);
	return true;
}

bool ibFormAttributeValue::ReadProperty(const ibDataNode& node)
{
	// Attribute FIRST — its Type must be known before the value materialises.
	m_attribute->ReadProperty(node);
	Refresh();   // materialises the value of the loaded Type and re-accumulates
	if (ibPropertyObject* po = GetValueAsProperty())
		po->ReadProperty(node);   // the value reads its own Source / Settings
	return true;
}

ibPropertyObject* ibFormAttributeValue::GetValueAsProperty() const
{
	ibPropertyObject* ptr = nullptr;;

	// PURE convert — NO AdjustValue here. GetSourceValue → GetValueAsSource is reached during
	// metadata resolution (form GetMetaData → GetSourceObject → GetSourceValue), and AdjustValue
	// needs that metadata → infinite recursion. The value is synced at set / Refresh time.
	m_value.ConvertToValue<ibPropertyObject>(ptr);
	return ptr;
}

ibSourceDataObject* ibFormAttributeValue::GetValueAsSource() const
{
	ibSourceDataObject* ptr = nullptr;

	// PURE convert — see GetValueAsProperty: syncing here recurses through GetMetaData.
	m_value.ConvertToValue<ibSourceDataObject>(ptr);
	return ptr;
}

void ibFormAttributeValue::SetSourceValue(ibSourceDataObject* source)
{
	// The source is NOT stored separately — it IS the held value. Seat it into the value cell
	// (the source is an ibValue, kept alive by the value's own refcount); GetValueAsSource
	// converts it back. nullptr → empty.
	ibValue* srcVal = source != nullptr ? dynamic_cast<ibValue*>(source) : nullptr;
	SetHeldValue(srcVal != nullptr ? *srcVal : ibValue());
}

bool ibFormAttributeValue::IsReferenceValue() const
{
	// Read from the ALREADY-ASSIGNED value's actual type — not the declared Type. For a
	// COMPOSITE Type only the concrete value tells which one it is. No value → not a ref.
	// The kind is read straight from the value's clsid (clsid.h) — no metadata lookup.
	if (m_value.IsEmpty())
		return false;
	return ::IsReference(m_value.GetClassType());
}

// Walk the tail through the VALUE's runtime members (FindProp / GetPropVal) — the same
// member dispatch the script uses — instead of a source-object cross-cast. Each hop id is
// resolved to a field name via metadata, then read off the live value.
bool ibFormAttributeValue::GetValueByPath(const std::vector<ibSourceId>& tail, ibValue& result) const
{
	if (tail.empty()) { result = m_value; return true; }
	// The value IS a source object (stored in the attribute). Read it by metaID PATH straight through
	// the source — BYPASSING metadata entirely (no metaID→metaobject→name→FindProp round-trip). Every
	// source kind (record / reference / constant / …) maps the id to its slot via GetValueByMetaID.
	ibSourceDataObject* src = GetValueAsSource();
	return src != nullptr && src->GetValueByPath(tail, result);
}

bool ibFormAttributeValue::SetValueByPath(const std::vector<ibSourceId>& tail, const ibValue& value)
{
	if (tail.empty()) { m_value = value; return true; }
	// Only a DIRECT field is writable (the resolve gates deeper dot-walks read-only). The value IS a
	// source object — write it by metaID straight through the source, BYPASSING metadata (no name
	// lookup). Every source maps the id to its slot via SetValueByMetaID — a record by the attribute's
	// metaID, a constant by its own.
	ibSourceDataObject* src = GetValueAsSource();
	return src != nullptr && src->SetValueByMetaID(tail.front(), value);
}

ibSourceDataType ibFormAttributeValue::ibFormAttribute::GetSourceDataType() const
{
	// A table-kind Type → table; everything else (object, reference, primitive) → attribute.
	// (Column kind is parent-derived, not a form attr.) Answered by the class factory's
	// IsTableValue through the Type's CLSID — the metaobject ctor registry first, then the
	// global value registry (ibMetaData::GetAvailableCtor's own fallback). One gate covers
	// both a metatype List and a system-registered ibValueModel (the dynamic list), with no
	// dynamic_cast and no value materialisation — the same answer ibSourceDataObject::
	// IsTableSource gives a live source.
	const ibTypeDescription& typeDesc = GetTypeDesc();
	if (typeDesc.GetClsidCount() == 1) {
		const ibMetaData* metaData = GetMetaData();
		const ibClassID clsid = typeDesc.GetFirstClsid();
		const ibCtorAbstractType* typeCtor = metaData != nullptr
			? metaData->GetAvailableCtor(clsid)
			: ibValue::GetAvailableCtor(clsid);
		if (typeCtor != nullptr && typeCtor->IsTableValue())
			return ibSourceDataType::ibSourceDataType_table;
	}
	return ibSourceDataType::ibSourceDataType_attribute;
}

//*********************************************************************************************
//*                              runtime member surface                                      *
//*********************************************************************************************

bool ibFormAttributeValue::ibFormAttribute::FillFillCheck(ibPropertyList* prop)
{
	prop->AppendItem(_("Don't check"), 0, wxBitmap());
	prop->AppendItem(_("Show error"), 1, wxBitmap());
	return true;
}

void ibFormAttributeValue::ibFormAttribute::FillMembers(ibMemberTable& helper) const
{
	// A simple-type attribute surfaces its own value; an assigned source
	// surfaces the source's members. Member dispatch is staged with the
	// type-driven dispatch — left minimal for the first slice.
}

//*********************************************************************************************
//*                                    serialization                                         *
//*********************************************************************************************

bool ibFormAttributeValue::ibFormAttribute::ReadProperty(const ibDataNode& node)
{
	m_attributeId = (ibMetaID)node.GetValue<s32>(wxT("AttributeId"));
	m_isMain = node.GetValue<bool>(wxT("Main"));
	m_propertyName->ReadNodeValue(node.GetProperty(m_propertyName->GetName()));
	m_propertyCaption->ReadNodeValue(node.GetProperty(m_propertyCaption->GetName()));
	m_propertyType->ReadNodeValue(node.GetProperty(m_propertyType->GetName()));
	m_propertyFillCheck->ReadNodeValue(node.GetProperty(m_propertyFillCheck->GetName()));
	// Pure definition — no value here. The facade (ibFormAttributeValue::ReadProperty) reads
	// this attribute FIRST (Type now known), then materialises + reads the generated value.
	return ibPropertyObject::ReadProperty(node);
}

bool ibFormAttributeValue::ibFormAttribute::WriteProperty(ibDataNode& node) const
{
	node.SetValue(wxT("AttributeId"), (s32)m_attributeId);
	node.SetValue(wxT("Main"), m_isMain);
	node.SetProperty(m_propertyName->GetName(), m_propertyName->GetNodeValue());
	node.SetProperty(m_propertyCaption->GetName(), m_propertyCaption->GetNodeValue());
	node.SetProperty(m_propertyType->GetName(), m_propertyType->GetNodeValue());
	node.SetProperty(m_propertyFillCheck->GetName(), m_propertyFillCheck->GetNodeValue());

	// The attached value (a dynamic list) writes its own data through the base entry.
	return ibPropertyObject::WriteProperty(node);
}

//*********************************************************************************************
//*                          clipboard copy / paste (designer-only)                          *
//*********************************************************************************************

bool ibFormAttributeValue::CopyToClipboard(const ibFormAttributeValue* entry)
{
#ifndef OES_USE_WEB
	if (entry == nullptr)
		return false;
	// Serialize the description through OUR format (the same Read/WriteProperty used on save),
	// then put the bytes on the clipboard under our own format id — analogous to control copy.
	ibDataNode node;
	entry->m_attribute->WriteProperty(node);   // static method → access to the private description
	ibWriterMemory writer;
	if (!ibBinaryProvider().Write(node, writer))
		return false;
	if (wxTheClipboard->Open()) {
		wxCustomDataObject* custom = new wxCustomDataObject(oes_clipboard_attribute);
		custom->SetData(writer.size(), writer.pointer());
		wxTheClipboard->SetData(custom);
		wxTheClipboard->Close();
		return true;
	}
#else
	(void)entry;
#endif
	return false;
}

ibFormAttributeValue* ibFormAttributeValue::PasteFromClipboard(ibValueForm* form)
{
#ifndef OES_USE_WEB
	if (form == nullptr)
		return nullptr;
	ibFormAttributeValue* entry = nullptr;
	if (wxTheClipboard->Open()) {
		if (wxTheClipboard->IsSupported(oes_clipboard_attribute)) {
			wxCustomDataObject data(oes_clipboard_attribute);
			if (wxTheClipboard->GetData(data)) {
				ibReaderMemory reader(data.GetData(), data.GetDataSize());
				ibDataNode node;
				if (ibBinaryProvider().Read(reader, node))
					entry = form->PasteAttribute(node);   // fresh NON-main entry, wired in
			}
		}
		wxTheClipboard->Close();
	}
	return entry;
#else
	(void)form;
	return nullptr;
#endif
}

//*********************************************************************************************
//*                              system type registration                                    *
//*********************************************************************************************

// Registered as a system runtime value type (clsid + name come from the registry;
// GetClassName / GetClassType resolve through it — see formAttribute.h).
SYSTEM_TYPE_REGISTER(ibFormAttributeValue, "FormAttributeValue");

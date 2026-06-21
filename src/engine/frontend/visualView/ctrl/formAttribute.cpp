////////////////////////////////////////////////////////////////////////////
//	Description : form-local typed source attribute (registry slot on a form)
////////////////////////////////////////////////////////////////////////////

#include "formAttribute.h"
#include "form.h"

#include "backend/metaCollection/partial/commonObject.h"   // ibSourceDataObject
#include "backend/serialize/dataBuilder.h"
#include "backend/system/value/valueType.h"                 // ibValueTypeDescription::AdjustValue
#include "backend/metaData.h"                                // GetTypeCtor
#include "backend/objCtor.h"                                 // ibCtorMetaValueType / ibCtorObjectMetaType
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

void ibValueForm::ReadAttributes(const ibDataNode& node)
{
	// The MAIN attribute is NEVER serialized — it is always reconstructed from the form's
	// source in the ctor (its name List/Object and Type derive from the source; its id is
	// stable). Only the user-added (non-main) attributes are persisted. So: KEEP the ctor's
	// main, REPLACE the non-main set from the "Attributes" section, and DROP any main-flagged
	// entry from an old blob (the ctor's main wins — never a second "Object").
	for (auto it = m_attributes.begin(); it != m_attributes.end(); ) {
		if ((*it)->IsMainAttribute()) ++it;
		else { DropAttributeBinds(it->get()); it = m_attributes.erase(it); }
	}

	if (const ibDataNode* attrsNode = node.FindChild(wxT("Attributes"))) {
		for (const ibDataNode& attrNode : attrsNode->Children()) {
			if (attrNode.GetValue<bool>(wxT("Main")))
				continue;   // the main belongs to the ctor — never load one from the blob
			ibValueFormAttribute* attr = new ibValueFormAttribute(this);
			attr->ReadData(attrNode);
			attr->SetMainAttribute(false);
			m_attributes.emplace_back(std::make_unique<ibFormAttributeValue>(attr));
		}
	}
}

void ibValueForm::WriteAttributes(ibDataNode& node) const
{
	// Persist ONLY the user-added (non-main) attributes. The MAIN is reconstructed from the
	// source in the ctor, so writing it would just produce a duplicate main on copy / load.
	ibDataNode& attrsNode = node.Child(wxT("Attributes"));
	for (const auto& av : m_attributes) {
		if (av->IsMainAttribute())
			continue;
		ibValueFormAttribute* attr = av->GetAttribute();
		ibDataNode& attrNode = attrsNode.AddChild(attr->GetClassType(), attr->GetAttributeId());
		attr->WriteData(attrNode);
	}
}

ibMetaID ibValueForm::NextAttributeId() const
{
	ibMetaID nextId = 1;   // form-unique = max existing + 1
	for (const auto& av : m_attributes)
		if (av->GetAttributeId() >= nextId)
			nextId = av->GetAttributeId() + 1;
	return nextId;
}

ibFormAttributeValue* ibValueForm::RegisterAttribute(ibValueFormAttribute* attr)
{
	// The wrapper OWNS the attribute; unique_ptr keeps the returned pointer stable across growth.
	m_attributes.emplace_back(std::make_unique<ibFormAttributeValue>(attr));
	return m_attributes.back().get();
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
	// Name + Type are mandatory — an attribute is never empty.
	ibValueFormAttribute* attr = new ibValueFormAttribute(this);
	attr->SetAttributeId(NextAttributeId());
	attr->SetAttributeName(name);
	attr->SetDefaultMetaType(type);

	ibFormAttributeValue* entry = RegisterAttribute(attr);
	entry->SetValue(value);
	WireAttribute(entry);
	return entry;
}

ibFormAttributeValue* ibValueForm::AddMainAttribute(const wxString& name, const ibClassID& type, ibSourceDataObject* value)
{
	wxASSERT(GetMainAttribute() == nullptr);   // only ONE main per form

	// Order matters: id + name + MAIN flag + register + SEAT THE SOURCE, and only THEN set the Type.
	// A form with no creator (auto-built list form) resolves its metadata THROUGH the source object;
	// SetDefaultMetaType triggers a Type refresh that needs it, so seating the source AFTER it would
	// be too late → null metadata → crash in DoRefreshTypeDesc.
	ibValueFormAttribute* attr = new ibValueFormAttribute(this);
	attr->SetAttributeId(NextAttributeId());
	attr->SetAttributeName(name);
	attr->SetMainAttribute(true);

	ibFormAttributeValue* entry = RegisterAttribute(attr);
	entry->SetSourceValue(value);     // metadata now reachable via GetSourceObject()
	attr->SetDefaultMetaType(type);   // safe — the Type refresh sees the source's metadata
	WireAttribute(entry);
	return entry;
}

void ibValueForm::DeleteAttribute(const wxString& name)
{
	for (auto it = m_attributes.begin(); it != m_attributes.end(); ++it)
		if ((*it)->GetAttributeName() == name) {
			DropAttributeBinds(it->get());
			m_attributes.erase(it);
			InvalidateNames();
			return;
		}
}

void ibValueForm::DeleteAttribute(unsigned int idx)
{
	if (idx < m_attributes.size()) {
		DropAttributeBinds(m_attributes[idx].get());
		m_attributes.erase(m_attributes.begin() + idx);
		InvalidateNames();
	}
}

void ibValueForm::BindAttributeVariable(ibFormAttributeValue* entry)
{
	if (entry == nullptr)
		return;
	const wxString name = entry->GetAttributeName();
	if (!name.IsEmpty())
		BindLocalVariable(name, entry->GetBindValue());   // <name> / ThisForm.<name>
	// The MAIN attribute additionally drives the exported DataSource (the form's source).
	if (entry->IsMainAttribute())
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
	UnbindVariable(entry->GetAttributeName());
	if (entry->IsMainAttribute())
		UnbindVariable(wxT("DataSource"));
}

bool ibValueForm::IsAttributeNameUnique(const wxString& name, const ibFormAttributeValue* except) const
{
	for (const auto& av : m_attributes)
		if (av.get() != except && av->GetAttributeName() == name)
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
	entry->GetAttribute()->SetAttributeName(newName);
	WireAttribute(entry);
	return true;
}

ibFormAttributeValue* ibValueForm::PasteAttribute(const ibDataNode& node)
{
	ibValueFormAttribute* attr = new ibValueFormAttribute(this);
	attr->ReadData(node);
	attr->SetMainAttribute(false);   // a paste is never the main — only one main per form
	attr->SetAttributeId(NextAttributeId());   // fresh id so its bindings never collide
	attr->SetAttributeName(MakeUniqueAttributeName(attr->GetAttributeName()));

	ibFormAttributeValue* entry = RegisterAttribute(attr);
	WireAttribute(entry);
	return entry;
}

ibFormAttributeValue* ibValueForm::GetAttribute(const wxString& name) const
{
	for (const auto& av : m_attributes)
		if (av->GetAttributeName() == name)
			return av.get();
	return nullptr;
}

ibFormAttributeValue* ibValueForm::GetAttribute(unsigned int idx) const
{
	if (idx >= m_attributes.size())
		return nullptr;
	return m_attributes[idx].get();
}

ibFormAttributeValue* ibValueForm::GetMainAttribute() const
{
	for (const auto& av : m_attributes)
		if (av->IsMainAttribute())
			return av.get();
	return nullptr;
}

void ibValueForm::SetMainAttribute(ibFormAttributeValue* entry)
{
	if (entry == nullptr)
		return;
	// The source flows to whoever is main: take it off the current main and seat it into
	// `entry` (SetSourceValue IncrRefs the new before the old releases, so it never drops to
	// zero mid-move). Re-selecting the same main is a no-op (SetSourceValue self-guards).
	ibFormAttributeValue* oldMain = GetMainAttribute();
	entry->SetSourceValue(oldMain != nullptr ? oldMain->GetSourceValue() : nullptr);
	// Sole-main invariant: entry becomes main; every other loses main AND its source (goes
	// empty — its controls render nothing until activity returns to it).
	for (const auto& av : m_attributes) {
		const bool isMain = (av.get() == entry);
		av->GetAttribute()->SetMainAttribute(isMain);
		if (!isMain)
			av->SetSourceValue(nullptr);
	}
	// Re-point the exported DataSource at the new main's cell (BindExport overwrites the single
	// "DataSource" slot, so the old main's ownership is dropped — no per-old unbind). Only when
	// the module is already live; the initial bind in InitializeFormModule reads the main flag.
	if (GetCompileModule() != nullptr)
		BindExportVariable(wxT("DataSource"), entry->GetBindValue());
}

ibFormAttributeValue* ibValueForm::FindAttributeById(const ibMetaID& id) const
{
	for (const auto& av : m_attributes)
		if (av->GetAttributeId() == id)
			return av.get();
	return nullptr;
}

bool ibValueForm::GetSourceList(ibSourceDataType kind, std::vector<ibBackendFormAttribute*>& out) const
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
		ibValueFormAttribute* a = av->GetAttribute();
		const bool mainForTable = (kind == ibSourceDataType::ibSourceDataType_table) && a->IsMainAttribute();
		if (mainForTable || a->GetSourceDataType() == kind)
			out.push_back(a);   // ibValueFormAttribute IS-A ibBackendFormAttribute (no cast)
	}
	return !out.empty();
}

bool ibValueForm::GetValueByAttributePath(const std::vector<ibSourceId>& path, ibValue& result) const
{
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

bool ibValueForm::IsWritableBinding(const std::vector<ibSourceId>& path) const
{
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

bool ibValueForm::SetValueByAttributePath(const std::vector<ibSourceId>& path, const ibValue& value)
{
	if (!IsWritableBinding(path))
		return false;   // dot-walk binding is read-only

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

ibValueFormAttribute::ibValueFormAttribute(ibValueForm* ownerForm)
	: ibValueDynamicMembers(ibValueTypes::TYPE_VALUE), m_ownerForm(ownerForm)
{
	m_members.Bind(this, &ibValueFormAttribute::FillMembers);
}

ibValueFormAttribute::~ibValueFormAttribute()
{
}

const ibMetaData* ibValueFormAttribute::GetMetaData() const
{
	return m_ownerForm != nullptr ? m_ownerForm->GetMetaData() : nullptr;
}

//*********************************************************************************************
//*    value — the attribute stores only a Type; a source is ASSIGNED at runtime (adjusted)  *
//*********************************************************************************************

void ibValueFormAttribute::OnPropertyChanged(ibProperty* property, const wxVariant& /*oldValue*/, const wxVariant& /*newValue*/)
{
	// The attribute holds only its Type; nothing to materialise here. But a Type change can
	// invalidate the control bindings that walk this attribute (their path leaf type no longer
	// matches), so refresh the form editor — the broken links then render as empty/invalid
	// instead of looking connected. Designer-only (no editor on web).
#ifndef OES_USE_WEB
	if (property == m_propertyType) {
		// FindVisualEditor() (g_visualHostContext) is an ibValueFrame member — the attribute is
		// not a control, so reach the editor by the owner form instead.
		if (ibFrontendVisualEditorNotebook* editor = ibFrontendVisualEditorNotebook::FindEditorByForm(m_ownerForm))
			editor->RefreshEditor();
	}
#endif
}

//*********************************************************************************************
//*  value entity — the wrapper holds the value; the attribute only describes its Type        *
//*********************************************************************************************

ibFormAttributeValue::~ibFormAttributeValue()
{
	// Release the held source (paired with the SourceIncrRef in SetSourceValue).
	if (m_sourceData != nullptr)
		m_sourceData->SourceDecrRef();
}

ibSourceDataObject* ibFormAttributeValue::GetSourceValue() const
{
	return m_sourceData;
}

void ibFormAttributeValue::SetSourceValue(ibSourceDataObject* source)
{
	if (m_sourceData == source)
		return;
	// Refcount the new source BEFORE releasing the old, so a move between attributes
	// (main switch) never drops the object to zero in between.
	if (source != nullptr)
		source->SourceIncrRef();
	if (m_sourceData != nullptr)
		m_sourceData->SourceDecrRef();
	m_sourceData = source;
	// Seat it into the bound value cell (the source IS an ibValue); nullptr → empty, so the
	// attribute reads as empty and its controls render nothing until a source is set again.
	ibValue* srcVal = m_sourceData != nullptr ? dynamic_cast<ibValue*>(m_sourceData) : nullptr;
	m_value = srcVal != nullptr ? *srcVal : ibValue();
}

bool ibFormAttributeValue::IsReferenceValue() const
{
	// Read from the ALREADY-ASSIGNED value's actual type — not the declared Type. For a
	// COMPOSITE Type only the concrete value tells which one it is. No value → not a ref.
	const ibMetaData* metaData = m_attribute != nullptr ? m_attribute->GetMetaData() : nullptr;
	if (metaData == nullptr || m_value.IsEmpty())
		return false;
	const ibCtorMetaValueType* typeCtor = metaData->GetTypeCtor(m_value.GetClassType());
	return typeCtor != nullptr && typeCtor->GetMetaTypeCtor() == ibCtorObjectMetaType::ibCtorObjectMetaType_Reference;
}

// Walk the tail through the VALUE's runtime members (FindProp / GetPropVal) — the same
// member dispatch the script uses — instead of a source-object cross-cast. Each hop id is
// resolved to a field name via metadata, then read off the live value.
bool ibFormAttributeValue::GetValueByPath(const std::vector<ibSourceId>& tail, ibValue& result) const
{
	if (tail.empty()) { result = m_value; return true; }
	const ibMetaData* metaData = m_attribute != nullptr ? m_attribute->GetMetaData() : nullptr;
	if (metaData == nullptr)
		return false;
	ibValue current = m_value;
	for (const ibSourceId& id : tail) {
		const ibValueMetaObject* field = metaData->FindAnyObjectByFilter(id, true);
		if (field == nullptr)
			return false;
		const long propNum = current.FindProp(field->GetName());
		if (propNum == wxNOT_FOUND)
			return false;
		ibValue next;
		if (!current.GetPropVal(propNum, next))
			return false;
		current = next;
	}
	result = current;
	return true;
}

bool ibFormAttributeValue::SetValueByPath(const std::vector<ibSourceId>& tail, const ibValue& value)
{
	if (tail.empty()) { m_value = value; return true; }
	// Only a DIRECT field is writable (the resolve gates deeper dot-walks read-only).
	const ibMetaData* metaData = m_attribute != nullptr ? m_attribute->GetMetaData() : nullptr;
	const ibValueMetaObject* field = metaData != nullptr ? metaData->FindAnyObjectByFilter(tail.front(), true) : nullptr;
	if (field == nullptr)
		return false;
	const long propNum = m_value.FindProp(field->GetName());
	return propNum != wxNOT_FOUND && m_value.SetPropVal(propNum, value);
}

ibSourceDataType ibValueFormAttribute::GetSourceDataType() const
{
	// A list / collection Type → table; everything else (object, reference,
	// primitive) → attribute. (Column kind is parent-derived, not a form attr.)
	const ibMetaData* metaData = GetMetaData();
	const ibTypeDescription& typeDesc = GetTypeDesc();
	if (metaData != nullptr && typeDesc.GetClsidCount() == 1) {
		const ibCtorMetaValueType* typeCtor = metaData->GetTypeCtor(typeDesc.GetFirstClsid());
		if (typeCtor != nullptr && typeCtor->GetMetaTypeCtor() == ibCtorObjectMetaType::ibCtorObjectMetaType_List)
			return ibSourceDataType::ibSourceDataType_table;
	}
	return ibSourceDataType::ibSourceDataType_attribute;
}

//*********************************************************************************************
//*                              runtime member surface                                      *
//*********************************************************************************************

bool ibValueFormAttribute::FillFillCheck(ibPropertyList* prop)
{
	prop->AppendItem(_("Don't check"), 0, wxBitmap());
	prop->AppendItem(_("Show error"), 1, wxBitmap());
	return true;
}

void ibValueFormAttribute::FillMembers(ibMemberTable& helper) const
{
	// A simple-type attribute surfaces its own value; an assigned source
	// surfaces the source's members. Member dispatch is staged with the
	// type-driven dispatch — left minimal for the first slice.
}

//*********************************************************************************************
//*                                    serialization                                         *
//*********************************************************************************************

bool ibValueFormAttribute::ReadData(const ibDataNode& node)
{
	m_attributeId = (ibMetaID)node.GetValue<s32>(wxT("AttributeId"));
	m_isMain = node.GetValue<bool>(wxT("Main"));
	m_propertyName->ReadNodeValue(node.GetProperty(m_propertyName->GetName()));
	m_propertyType->ReadNodeValue(node.GetProperty(m_propertyType->GetName()));
	m_propertyFillCheck->ReadNodeValue(node.GetProperty(m_propertyFillCheck->GetName()));
	// The attribute stores only its Type; the runtime value is assigned later. (The MAIN is
	// never serialized — reconstructed from the source in the ctor — so no copy-aware Type
	// remap is needed on read.)
	return true;
}

bool ibValueFormAttribute::WriteData(ibDataNode& node) const
{
	node.SetValue(wxT("AttributeId"), (s32)m_attributeId);
	node.SetValue(wxT("Main"), m_isMain);
	node.SetProperty(m_propertyName->GetName(), m_propertyName->GetNodeValue());
	node.SetProperty(m_propertyType->GetName(), m_propertyType->GetNodeValue());
	node.SetProperty(m_propertyFillCheck->GetName(), m_propertyFillCheck->GetNodeValue());
	// (The MAIN is never serialized — see WriteAttributes — so no copy-aware "Copied" flag.)
	return true;
}


//*********************************************************************************************
//*                              system type registration                                    *
//*********************************************************************************************

// Registered as a system runtime value type (clsid + name come from the registry;
// GetClassName / GetClassType resolve through it — see formAttribute.h).
SYSTEM_TYPE_REGISTER(ibValueFormAttribute, "FormAttribute");

//*********************************************************************************************
//*                          clipboard copy / paste (designer-only)                          *
//*********************************************************************************************

bool ibFormAttributeValue::CopyToClipboard(const ibValueFormAttribute* attr)
{
#ifndef OES_USE_WEB
	if (attr == nullptr)
		return false;
	// Serialize the attribute through OUR format (the same ReadData/WriteData used on save),
	// then put the bytes on the clipboard under our own format id — analogous to control copy.
	ibDataNode node;
	attr->WriteData(node);
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
	(void)attr;
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
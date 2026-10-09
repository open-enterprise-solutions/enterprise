////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : base control
////////////////////////////////////////////////////////////////////////////

#include "control.h"
#include "form.h"
#include "backend/compiler/procUnit.h"
#include "core/serialize/dataBuilder.h"   // ibDataNode / ibDataBuilder / ibBinaryProvider
#include "backend/metaCollection/metaFormObject.h"   // ibValueMetaObjectFormBase — IsCopyMode / IsPasteMode mark


//*************************************************************************
//*                          ValueControl		                          *
//*************************************************************************

ibValueFrame::ibValueFrame() : ibValueDynamicMembers(ibValueTypes::TYPE_VALUE),
m_controlId(0), m_controlGuid(ibGuid::newGuid()),
m_valEventContainer(new ibValueEventContainer(this))
{
	m_members.Bind(this, &ibValueFrame::FillMembers);
}

ibValueFrame::~ibValueFrame()
{
}

wxString ibValueFrame::GetClassName() const
{
	const ibClassID& clsid = GetClassType();
	if (clsid == 0)
		return _("Class not registered");

	const ibCtorAbstractType* typeCtor = ibValue::GetAvailableCtor(clsid);
	if (typeCtor != nullptr)
		return typeCtor->GetClassName();
	return _("Class not registered");
}

wxString ibValueFrame::GetObjectTypeName() const
{
	const ibCtorControlTypeBase* typeCtor =
		static_cast<const ibCtorControlTypeBase*>(ibValue::GetAvailableCtor(typeid(*this)));

	if (typeCtor != nullptr)
		return typeCtor->GetTypeControlName();
	return _("Class not registered");
}

#define	headerBlock 0x012230
#define	dataBlock 0x012250
#define	childBlock 0x012270
#define	frameBlock 0x012290

bool ibValueFrame::IsEditable() const
{
	const ibValueForm* handler = GetOwnerForm();
	if (handler != nullptr)
		return handler->IsEditable();
	return false;
}

bool ibValueFrame::IsReadOnly() const
{
	// A control is read-only when its form is in view-only mode. The form is the root frame, so every control
	// reaches it through GetOwnerForm(); the form's own case resolves to IsViewOnly() through this same path
	// (GetOwnerForm returns the form itself). Each control reads this at build time to render read-only.
	const ibValueForm* handler = GetOwnerForm();
	return handler != nullptr && handler->IsViewOnly();
}

// The control is serialized as a node tree (header + per-type WriteData), rendered to
// the form buffer by the binary provider — the same path metaobjects use.
bool ibValueFrame::LoadControl(const ibValueMetaObjectFormBase* metaForm, ibReaderMemory& dataReader)
{
	ibDataBuilder builder;
	if (!builder.Load(ibBinaryProvider(), dataReader))
		return false;
	// Route by the blob's OWN format tag (SELF-DESCRIBING), not a transient paste mark: a copy blob — a pasted form,
	// possibly saved to disk and reloaded long after the paste mark cleared — re-homes its source hops (guid→id) onto
	// the pasted objects; a raw blob (incl. every OLD config, no tag) loads plainly. No copy→raw normalization anywhere.
	if (builder.Root().GetValue<bool>(wxT("PasteFormat")))
		return PasteNode(builder.Root());
	return LoadNode(builder.Root());
}

bool ibValueFrame::SaveControl(const ibValueMetaObjectFormBase* metaForm, ibWriterMemory& dataWritter) const
{
	ibDataBuilder builder;
	// While the form's metaobject is marked for copy (ibControlCopyGuard) the clipboard blob rides guids — route to
	// the copy cascade; otherwise the plain raw save.
	const bool copy = (metaForm != nullptr && metaForm->IsCopyMode());
	const bool ok = copy ? CopyNode(builder.Root()) : SaveNode(builder.Root());
	if (!ok)
		return false;
	// SELF-DESCRIBING tag: a copy blob stamps its root so LoadControl re-homes it (PasteNode) by CONTENT, independent of
	// any live paste mark. Absent on a raw save (and on every old config) → back-compat load as raw. A pasted form keeps
	// its copy blob on disk and re-homes on each load; editing+saving it writes raw (no tag) — self-healing.
	if (copy)
		builder.Root().SetValue(wxT("PasteFormat"), true);
	return builder.Save(ibBinaryProvider(), dataWritter);
}

// control header (id / name / expanded as fields) + per-type Read/WriteData, then the
// sub-controls recurse as node CHILDREN — so a control's node IS its whole subtree and
// the form is one transparent node tree (the provider frames each child in kChildBlock).
bool ibValueFrame::LoadNode(const ibDataNode& node)
{
	m_controlId = (ibFormID)node.GetValue<s32>(wxT("ControlId"));
	SetControlName(node.GetValue<wxString>(wxT("Name")));
	m_expanded = node.GetValue<bool>(wxT("Expanded"));
	if (!ReadData(node))
		return false;

	// Re-create each sub-control through the owning form's factory (NewObject attaches it
	// to this parent and stamps the owner form), then recurse into its own subtree.
	ibValueForm* ownerForm = GetOwnerForm();
	if (ownerForm != nullptr) {
		for (const ibDataNode& childNode : node.Children()) {
			ibValueFrame* child = ownerForm->NewObject(childNode.GetClsid(), this, false);
			// 🛑 A CONTROL THAT WAS SAVED AND DOES NOT COME BACK IS SAID OUT LOUD. The form still
			// opens — the rest of it is intact, and losing the whole form over one control is the
			// worse trade — but "the table is simply not there" must never again be the only
			// symptom (Max, 2026-08-20). NewObject has already reported WHY when it had a reason;
			// this names WHAT and WHERE, which the reason alone does not carry.
			if (child == nullptr) {
				ibJournalError(wxT("ui.form"), _("Control '%s' of '%s' could not be restored (class id %lld)"),
					childNode.GetValue<wxString>(wxT("Name")), GetControlName(), (long long)childNode.GetClsid());
				continue;
			}
			if (!child->LoadNode(childNode))
				return false;
		}
	}
	return true;
}

bool ibValueFrame::SaveNode(ibDataNode& node) const
{
	node.SetValue(wxT("ControlId"), (s32)m_controlId);
	node.SetValue(wxT("Name"),      GetControlName());
	node.SetValue(wxT("Expanded"),  m_expanded);
	if (!WriteData(node))
		return false;

	// clsid = control type, metaId = control id — the load side recreates by clsid.
	for (unsigned int idx = 0; idx < GetChildCount(); idx++) {
		ibValueFrame* child = GetChild(idx);
		if (child == nullptr)
			continue;
		ibDataNode& childNode = node.AddChild(child->GetClassType(), child->GetControlID());
		if (!child->SaveNode(childNode))
			return false;
	}
	return true;
}

bool ibValueFrame::OnClientEvent(ibProtocolEvent event, const ibDataNode& args)
{
	if (event != ibProtocolEvent::Command || !HasCommandBar())
		return false;
	const ibActionID id = args.GetValue<s32>(wxT("Id"));
	if (args.FindField(wxT("Member")) != nullptr)
		GetCommandBar()->ExecuteCommand(id, static_cast<size_t>(args.GetValue<s32>(wxT("Member"))));
	else
		GetCommandBar()->ExecuteCommand(id, GetOwnerForm());
	return true;
}

// COPY / PASTE node — the control's OWN copy-paste serialization (routed to from SaveControl / LoadControl while the
// form's metaobject is marked). It mirrors SaveNode / LoadNode but drives every property AND event through the
// Copy / PasteNodeValue pair: default == Write / ReadNodeValue, so a plain control round-trips unchanged, while a
// source property rides its hops on guids and re-homes them onto the pasted object. Children recurse the same pair.
bool ibValueFrame::CopyNode(ibDataNode& node) const
{
	node.SetValue(wxT("ControlId"), (s32)m_controlId);
	node.SetValue(wxT("Name"),      GetControlName());
	node.SetValue(wxT("Expanded"),  m_expanded);

	for (unsigned int idx = 0; idx < ibPropertyObject::GetPropertyCount(); idx++)
		if (ibProperty* prop = ibPropertyObject::GetProperty(idx)) {
			ibDataValue value;
			if (!prop->CopyNodeValue(value))
				return false;
			node.SetProperty(prop->GetName(), value);
		}
	for (unsigned int idx = 0; idx < ibPropertyObject::GetEventCount(); idx++)
		if (ibEvent* event = ibPropertyObject::GetEvent(idx)) {
			ibDataValue value;
			if (!event->CopyNodeValue(value))
				return false;
			node.SetProperty(event->GetName(), value);
		}

	if (!CopyData(node))
		return false;

	for (unsigned int idx = 0; idx < GetChildCount(); idx++) {
		ibValueFrame* child = GetChild(idx);
		if (child == nullptr)
			continue;
		ibDataNode& childNode = node.AddChild(child->GetClassType(), child->GetControlID());
		if (!child->CopyNode(childNode))
			return false;
	}
	return true;
}

bool ibValueFrame::PasteNode(const ibDataNode& node)
{
	m_controlId = (ibFormID)node.GetValue<s32>(wxT("ControlId"));
	SetControlName(node.GetValue<wxString>(wxT("Name")));
	m_expanded = node.GetValue<bool>(wxT("Expanded"));

	for (unsigned int idx = 0; idx < ibPropertyObject::GetPropertyCount(); idx++)
		if (ibProperty* prop = ibPropertyObject::GetProperty(idx))
			if (!prop->PasteNodeValue(node.GetProperty(prop->GetName())))
				return false;
	for (unsigned int idx = 0; idx < ibPropertyObject::GetEventCount(); idx++)
		if (ibEvent* event = ibPropertyObject::GetEvent(idx))
			if (!event->PasteNodeValue(node.GetProperty(event->GetName())))
				return false;

	if (!PasteData(node))
		return false;

	ibValueForm* ownerForm = GetOwnerForm();
	if (ownerForm != nullptr) {
		for (const ibDataNode& childNode : node.Children()) {
			ibValueFrame* child = ownerForm->NewObject(childNode.GetClsid(), this, false);
			// Same rule on the paste road — a control silently missing from what was pasted reads
			// as "the clipboard lost it", which is the one thing the user cannot investigate.
			if (child == nullptr) {
				ibJournalError(wxT("ui.form"), _("Control '%s' of '%s' could not be pasted (class id %lld)"),
					childNode.GetValue<wxString>(wxT("Name")), GetControlName(), (long long)childNode.GetClsid());
				continue;
			}
			if (!child->PasteNode(childNode))
				return false;
		}
	}
	return true;
}

//*******************************************************************

bool ibValueFrame::Init()
{
	// always false
	return false;
}

bool ibValueFrame::Init(ibValue** paParams, const long lSizeArray)
{
	// THREE params are read below (form, parent, generateId) — the gate said 2, so a
	// two-argument caller walked off the end of paParams on the generateId read. The
	// only live caller (ibValueForm::NewObject) always passes 3, which is why this
	// never fired; it is still an out-of-bounds read waiting for a second caller.
	if (lSizeArray < 3)
		return false;
	ibValueForm* ownerForm = nullptr;
	ibValueFrame* controlParent = nullptr;
	if (paParams[0]->ConvertToValue(ownerForm) &&
		paParams[1]->ConvertToValue(controlParent)) {
		if (controlParent != nullptr) {
			controlParent->AddChild(this);
			SetParent(controlParent);
		}
		SetOwnerForm(ownerForm);
		ownerForm->ResolveNameConflict(this);
		if (paParams[2]->GetBoolean()) {
			GenerateNewID();
		}
		return true;
	}
	return false;
}

//*******************************************************************

bool ibValueFrame::ChangeChildPosition(ibValueFrame* obj, unsigned int pos)
{
	OnChangeChildPosition(obj, pos);
	return ibPropertyObjectHelper::ChangeChildPosition(obj, pos);
}

//*******************************************************************

ibValueFrame* ibValueFrame::CreatePasteObject(const ibReaderMemory& reader,
	ibValueForm* dstForm, ibValueFrame* dstParent)
{
	std::shared_ptr <ibReaderMemory>readerHeaderMemory(reader.open_chunk(headerBlock));
	if (readerHeaderMemory == nullptr)
		return nullptr;

	readerHeaderMemory->r_s32();   // version — reserved; READ, because it advances the cursor
	const ibClassID& clsid = readerHeaderMemory->r_u64();

	return dstForm->NewObject(clsid, dstParent);
}

//*******************************************************************

bool ibValueFrame::CopyObject(ibWriterMemory& writer) const
{
	ibWriterMemory writerHeaderMemory;
	writerHeaderMemory.w_s32(0); //reserved
	writerHeaderMemory.w_u64(GetClassType()); //get class type 
	writer.w_chunk(headerBlock, writerHeaderMemory.pointer(), writerHeaderMemory.size());
	ibWriterMemory writerDataMemory;
	if (!CopyProperty(writerDataMemory))
		return false;
	writer.w_chunk(dataBlock, writerDataMemory.pointer(), writerDataMemory.size());
	ibWriterMemory writerChildMemory;
	for (unsigned int idx = 0; idx < GetChildCount(); idx++) {
		ibWriterMemory writerMemory;
		ibValueFrame* obj = GetChild(idx);
		if (!obj->CopyObject(writerMemory))
			return false;
		writerChildMemory.w_chunk(obj->GetClassType(), writerMemory.pointer(), writerMemory.size());
	}
	writer.w_chunk(childBlock, writerChildMemory.pointer(), writerChildMemory.size());
	return true;
}

bool ibValueFrame::PasteObject(ibReaderMemory& reader)
{
	ibValueForm* valueForm = GetOwnerForm();
	if (valueForm == nullptr) return false;

	std::shared_ptr <ibReaderMemory>readerHeaderMemory(reader.open_chunk(headerBlock));

	// Both are READ, not used: the header's two fields have to leave the cursor past them.
	readerHeaderMemory->r_s32();   // version — reserved
	readerHeaderMemory->r_u64();   // clsid — the form below is resolved by name, not by this

	std::shared_ptr <ibReaderMemory> readerChildMemory(reader.open_chunk(childBlock));
	if (readerChildMemory != nullptr) {
		ibReaderMemory* prevReaderMemory = nullptr;
		do {
			ibClassID founded_clsid = 0;
			ibReaderMemory* readerMemory = readerChildMemory->open_chunk_iterator(founded_clsid, &*prevReaderMemory);
			if (readerMemory == nullptr)
				break;
			if (founded_clsid > 0) {
				ibValueFrame* valueFrame = valueForm->NewObject(founded_clsid, this, false);
				if (valueFrame != nullptr && !valueFrame->PasteObject(*readerMemory))
					return false;
			}
			prevReaderMemory = readerMemory;
		} while (true);
	}

	std::shared_ptr <ibReaderMemory>readerDataMemory(reader.open_chunk(dataBlock));
	if (!PasteProperty(*readerDataMemory))
		return false;

	valueForm->ResolveNameConflict(this);
	return true;
}

std::shared_ptr<ibProcUnit> ibValueFrame::GetFormProcUnit() const
{
	const ibValueForm* valueForm =
		GetOwnerForm();

	if (valueForm == nullptr) 
		return nullptr;

	return valueForm->GetProcUnit();
}

//*******************************************************************

#include "backend/metaData.h"
#include "backend/objCtor.h"

// A primitive edits itself, an enumeration offers its finite registered members, a metadata type answers
// through its metaobject; anything else has no short list to offer. Declared in frame.h.
bool HasQuickChoice(const ibCtorAbstractType* typeCtor)
{
	if (typeCtor == nullptr)
		return false;

	switch (typeCtor->GetObjectTypeCtor()) {
	case ibCtorObjectType::ibCtorObjectType_object_primitive:
	case ibCtorObjectType::ibCtorObjectType_object_enum:
		return true;
	case ibCtorObjectType::ibCtorObjectType_object_meta_value: {
		const ibCtorMetaValueType* metaCtor = dynamic_cast<const ibCtorMetaValueType*>(typeCtor);
		const ibValueMetaObjectRecordDataRef* metaObject = metaCtor != nullptr
			? dynamic_cast<const ibValueMetaObjectRecordDataRef*>(metaCtor->GetMetaObject()) : nullptr;
		return metaObject != nullptr && metaObject->HasQuickChoice();
	}
	default:
		return false;
	}
}

bool ibValueFrame::HasQuickChoice() const {
	const ibMetaData* metaData = GetMetaData();
	if (metaData == nullptr)
		return false;
	ibValue selValue; GetControlValue(selValue);
	// ASK THE TYPE — one question, one answer, and it lives on the ctor (ibCtorAbstractType::
	// HasQuickChoice). This used to be a hand-written walk over the ctor KINDS, and the same walk was
	// written a second time in the filter dialog; the two disagreed about enumerations, so one and the
	// same account type dropped its member list in a filter and refused to on a form.
	const ibCtorAbstractType* so = metaData->GetAvailableCtor(selValue.GetClassType());
	return ::HasQuickChoice(so);
}

//*******************************************************************

ibBackendValueForm* ibValueFrame::GetBackendForm() const
{
	return GetOwnerForm();
}

//*******************************************************************

ibFormVisualDocument* ibValueFrame::GetVisualDocument() const
{
	ibValueForm* const valueForm = GetOwnerForm();
	if (valueForm == nullptr)
		return nullptr;
	return valueForm->GetVisualDocument();
}

#include "backend/metaCollection/partial/commonObject.h"

//****************************************************************************
//*                              Support methods                             *
//****************************************************************************

void ibValueFrame::FillMembers(ibMemberTable& helper) const
{
	{
		wxString propertyName;
		for (unsigned int idx = 0; idx < ibPropertyObject::GetPropertyCount(); idx++) {
			ibProperty* property = ibPropertyObject::GetProperty(idx);
			if (property == nullptr)
				continue;
			property->GetName(propertyName);
			helper.AppendProp(propertyName, idx, eProperty);
		}
		//if we have sizerItem then call him
		ibValueFrame* sizeritem = GetParent();
		if (sizeritem != nullptr && sizeritem->GetComponentType() == COMPONENT_TYPE_SIZERITEM) {
			for (unsigned int idx = 0; idx < sizeritem->GetPropertyCount(); idx++) {
				ibProperty* property = sizeritem->GetProperty(idx);
				if (property == nullptr)
					continue;
				property->GetName(propertyName);
				helper.AppendProp(propertyName, idx, eSizerItem);
			}
		}
	}

	helper.AppendProp(wxT("Events"), true, false, 0, eEvent);
}

bool ibValueFrame::SetPropVal(const long lPropNum, const ibValue& varPropVal)
{
	const long lPropAlias = m_members.GetPropAlias(lPropNum);
	if (lPropAlias == eProperty) {
		unsigned int idx = m_members.GetPropData(lPropNum);
		ibProperty* property = GetPropertyByIndex(idx);
		if (property != nullptr) {
			// Through the property's change, as the inspector and MCP write it: the control hears it (OnPropertyChanged).
			const wxVariant oldValue = property->GetValue();
			property->SetDataValue(varPropVal);
			OnPropertyChanged(property, oldValue, property->GetValue());
		}
	}
	else if (lPropAlias == eSizerItem) {
		//if we have sizerItem then call him savepropery
		ibValueFrame* sizerItem = GetParent();
		if (sizerItem != nullptr &&
			sizerItem->GetComponentType() == COMPONENT_TYPE_SIZERITEM) {
			// The member number is NOT the property index: FillMembers appends the
			// sizerItem's properties AFTER our own, so the index it stored lives in
			// the member's DATA. Reading it back is what GetPropVal does; writing
			// must do the same or it lands on a different property.
			unsigned int idx = m_members.GetPropData(lPropNum);
			ibProperty* property = sizerItem->GetPropertyByIndex(idx);
			if (property != nullptr) {
				const wxVariant oldValue = property->GetValue();
				property->SetDataValue(varPropVal);
				sizerItem->OnPropertyChanged(property, oldValue, property->GetValue());
			}
		}
	}

	// Nothing is poked: the next frame the form writes reads the property as it now is, and the client
	// receives the difference.
	return GetOwnerForm() != nullptr;
}

bool ibValueFrame::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	const long lPropAlias = m_members.GetPropAlias(lPropNum);
	if (lPropAlias == eSizerItem) {
		//if we have sizerItem then call him savepropery 
		ibValueFrame* sizerItem = GetParent();
		if (sizerItem != nullptr &&
			sizerItem->GetComponentType() == COMPONENT_TYPE_SIZERITEM) {
			unsigned int idx = m_members.GetPropData(lPropNum);
			ibProperty* property = sizerItem->GetPropertyByIndex(idx);
			if (property != nullptr)
				return property->GetDataValue(pvarPropVal);
			return false;
		}
	}
	else if (lPropAlias == eEvent) {
		pvarPropVal = m_valEventContainer;
		return true;
	}
	else {
		unsigned int idx = m_members.GetPropData(lPropNum);
		ibProperty* property = GetPropertyByIndex(idx);
		if (property != nullptr)
			return property->GetDataValue(pvarPropVal);
		return false;
	}

	return false;
}
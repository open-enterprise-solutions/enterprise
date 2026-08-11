////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : common object source-command interface (record / register base)
////////////////////////////////////////////////////////////////////////////

// The source-command SURFACE of the base record / register metaobjects — the command set + execution + column
// fill + select value that the templated source descriptor (queryableFactory.h) forwards to. Lives HERE, with the
// metaobjects it implements (commonObject.h), NOT in the legacy list TU it was lifted from — the object lists get
// deleted once the dynamic list fully replaces them; this stays. Document adds Post in documentAction.cpp.

#include "commonObject.h"
#include "backend/backend_form.h"             // ibBackendControlFrame COMPLETE — the dynamic_cast target in ShowFormValue
#include "backend/srcDataObject.h"            // ibSourceDataObject::ibSourceExplorer — FillSourceExplorer's out-param
#include "backend/system/systemManager.h"     // ibValueSystemFunction::Alert
#include "backend/picturePredefined.h"        // g_picAdd/Copy/Edit/Delete/MarkAsDelete/AddFolderCLSID — the command band
#include "backend/rowValues.h"                // ibRowMetaValues::find — reading the reference cell for GetSelectValue
#include "backend/metaCollection/partial/reference/reference.h"   // ibValueReferenceDataObject::GetGuid — GetItemKey's row guid

// ---------------------------------------------------------------------------------------------------------
// Reference-record BASE — column fill + the reference select value (shared by catalog / document / enum / charts).
// ---------------------------------------------------------------------------------------------------------

// The default-column fill has FOUR variants, one per metatype, each spelling out its OWN visible set directly by
// its OWN predicates (no shared "system column" abstraction — the rule genuinely differs: an enum SHOWS its
// reference, a catalog HIDES it). Three record chains (enum / document / catalog) below + register further down.

// ENUM variant (reference-record base) — an enum's value IS its reference, so the reference column is the ONLY one
// shown; everything else (order …) is hidden. The writeable / hierarchy metatypes override with their own set.
void ibValueMetaObjectRecordDataRef::FillSourceExplorer(ibSourceDataObject::ibSourceExplorer& explorer) const
{
	for (const ibValueMetaObjectAttributeBase* a : GetGenericAttributeArrayObject())
		if (a != nullptr)
			explorer.AppendColumn(a, /*enabled*/true, /*visible*/ IsDataReference(a->GetMetaID()));
}

// DOCUMENT variant — reference / deletion mark / data version hidden; number / date / posted / attributes visible.
void ibValueMetaObjectRecordDataMutableRef::FillSourceExplorer(ibSourceDataObject::ibSourceExplorer& explorer) const
{
	for (const ibValueMetaObjectAttributeBase* a : GetGenericAttributeArrayObject()) {
		if (a == nullptr) continue;
		const ibMetaID id = a->GetMetaID();
		const bool hidden = IsDataReference(id) || IsDataDeletionMark(id) || IsDataVersion(id);
		explorer.AppendColumn(a, /*enabled*/true, /*visible*/ !hidden);
	}

	// ⭐ THE MOMENT, APPENDED BY HAND — and that is the whole point of it. It is deliberately absent
	// from the generic list (the list that becomes columns, object members and form fields), so this
	// is the ONE place it surfaces: the field tree a query is written against.
	explorer.AppendColumn(GetPointInTime(), /*enabled*/true, /*visible*/true);
}

// CATALOG variant — reference / deletion mark / data version / predefined name / parent / is-folder hidden;
// code / description / attributes visible.
void ibValueMetaObjectRecordDataHierarchyMutableRef::FillSourceExplorer(ibSourceDataObject::ibSourceExplorer& explorer) const
{
	for (const ibValueMetaObjectAttributeBase* a : GetGenericAttributeArrayObject()) {
		if (a == nullptr) continue;
		const ibMetaID id = a->GetMetaID();
		const bool hidden = IsDataReference(id) || IsDataDeletionMark(id) || IsDataVersion(id)
		                 || IsDataPredefinedName(id) || IsDataParent(id) || IsDataFolder(id);
		explorer.AppendColumn(a, /*enabled*/true, /*visible*/ !hidden);
	}

	// Same as the flat variant above — the moment reaches the query and nothing else.
	explorer.AppendColumn(GetPointInTime(), /*enabled*/true, /*visible*/true);
}

// SELECT value — a record's picker value = its REFERENCE cell, read from the row's DEFAULT columns by the
// reference attribute's metaID (the metaobject knows it; the reference is always among the default columns). On the
// ref base, so catalog / document / enum / charts all get it. Lifted from the list models' GetItemSelectValue.
ibValue ibValueMetaObjectRecordDataRef::GetSelectValue(const ibRowMetaValues& rowValues) const
{
	ibValue refVal;
	if (const ibValueMetaObjectAttributePredefined* ref = GetDataReference()) {
		const ibRowMetaValues::const_iterator it = rowValues.find(ref->GetMetaID());
		if (it != rowValues.end())
			refVal = it->second;
	}
	return refVal;
}

// ITEM KEY — a record's identity = its REFERENCE guid, read from the same reference cell GetSelectValue reads. On
// the ref base, so catalog / document / enum / charts all share it. A register overrides with the composite key.
ibUniqueKey ibValueMetaObjectRecordDataRef::GetItemKey(const ibRowMetaValues& rowValues) const
{
	if (const ibValueMetaObjectAttributePredefined* ref = GetDataReference()) {
		const ibRowMetaValues::const_iterator it = rowValues.find(ref->GetMetaID());
		if (it != rowValues.end()) {
			ibValueReferenceDataObject* refObj = nullptr;
			if (it->second.ConvertToValue(refObj) && refObj != nullptr)
				return ibUniqueKey(refObj->GetGuid());
		}
	}
	return ibUniqueKey();
}

// ---------------------------------------------------------------------------------------------------------
// Writeable-record BASE (MutableRef) — the command set + execution of the ref list models, lifted onto the
// metaobject so a metadata-blind dynamic list reaches it through the source descriptor. Execute is BY KEY (the
// front-owned row's handle) + srcForm (parent / to refresh). Hierarchy adds AddFolder, Document adds Post.
// ---------------------------------------------------------------------------------------------------------
void ibValueMetaObjectRecordDataMutableRef::GetCommandCollection(const ibFormID& /*formType*/, std::vector<ibCommandItem>& commands) const
{
	commands.emplace_back(eAddValue,     wxT("Add"),          _("Add"),           g_picAddCLSID,          true);
	commands.emplace_back(eCopyValue,    wxT("Copy"),         _("Copy"),          g_picCopyCLSID);
	commands.emplace_back(eEditValue,    wxT("Edit"),         _("Edit"),          g_picEditCLSID);
	commands.emplace_back(eDeleteValue,  wxT("Delete"),       _("Delete"),        g_picDeleteCLSID);
	commands.emplace_back(eMarkAsDeleteValue, wxT("MarkAsDelete"), _("Mark as delete"), g_picMarkAsDeleteCLSID, true);
}

void ibValueMetaObjectRecordDataMutableRef::CallAsCommand(ibActionID id, const ibUniqueKey& /*anchor*/, const ibUniqueKey& key, ibBackendValueForm* srcForm) const
{
	try {
		switch (id)
		{
		case eAddValue: {
			ibValuePtr<ibValueRecordDataObjectRef> obj(CreateObjectValue());
			if (obj != nullptr) obj->ShowFormValue(wxEmptyString, dynamic_cast<ibBackendControlFrame*>(srcForm));
			break;
		}
		case eCopyValue: {
			if (!key.IsOk()) return;
			ibValuePtr<ibValueRecordDataObjectRef> obj(CopyObjectValue(key));
			if (obj != nullptr) obj->ShowFormValue(wxEmptyString, dynamic_cast<ibBackendControlFrame*>(srcForm));
			break;
		}
		case eEditValue:
			ShowValueByKey(key, srcForm);
			break;
		case eDeleteValue: {
			if (!key.IsOk()) return;
			ibValuePtr<ibValueRecordDataObjectRef> obj(CreateObjectValue(key));
			if (obj != nullptr) obj->DeleteObject();
			if (srcForm != nullptr) srcForm->UpdateForm();
			break;
		}
		case eMarkAsDeleteValue: {
			if (!key.IsOk()) return;
			ibValuePtr<ibValueRecordDataObjectRef> obj(CreateObjectValue(key));
			if (obj != nullptr) obj->SetDeletionMark(true);
			if (srcForm != nullptr) srcForm->UpdateForm();
			break;
		}
		}
	}
	// The BASE, not ibBackendCoreException — an access deny / lock conflict is an ibBackendException
	// as well, and catching only Core sent those into the catch(...) below: the command failed and
	// the user was told nothing. (Same fix in every command / show entry point in this file.)
	catch (const ibBackendInterruptException&) {}   // the user stopped it — nothing to report
	catch (const ibBackendException& err) { ibValueSystemFunction::Alert(err.GetErrorDescription()); }
	catch (...) { wxLogError(wxT("ibValueMetaObjectRecordDataMutableRef::CallAsCommand: unhandled non-ibBackend exception swallowed")); }
}

void ibValueMetaObjectRecordDataMutableRef::ShowValueByKey(const ibUniqueKey& key, ibBackendValueForm* srcForm) const
{
	if (!key.IsOk()) return;
	try {
		ibValueRecordDataObjectRef* obj(CreateObjectValue(key));
		if (obj != nullptr) obj->ShowFormValue(wxEmptyString, dynamic_cast<ibBackendControlFrame*>(srcForm));
	}
	catch (const ibBackendInterruptException&) {}
	catch (const ibBackendException& err) { ibValueSystemFunction::Alert(err.GetErrorDescription()); }
	catch (...) { wxLogError(wxT("ibValueMetaObjectRecordDataMutableRef::ShowValueByKey: unhandled non-ibBackend exception swallowed")); }
}

// Hierarchy override — the writeable base set PLUS AddFolder, gated by the source actually having folders (a folder
// column), so a flat catalog reaching here shows none. eAddFolder opens a NEW folder form; the rest goes to the
// base. NOTE: parent-nesting (create inside the selected folder) is model-side (ResolveParentForNew reads the
// node) — the key-only command interface creates at root for now; the opened form lets the user set parent.
void ibValueMetaObjectRecordDataHierarchyMutableRef::GetCommandCollection(const ibFormID& formType, std::vector<ibCommandItem>& commands) const
{
	// AddFolder goes FIRST (as in the legacy folder list), then the writeable base set. A folders+items hierarchy
	// has the IsFolder attribute — the metaobject knows it directly (no queryable / GetFolderColumn indirection).
	if (GetDataIsFolder() != nullptr)
		commands.emplace_back(eAddFolder, wxT("AddFolder"), _("Add folder"), g_picAddFolderCLSID, true);
	ibValueMetaObjectRecordDataMutableRef::GetCommandCollection(formType, commands);   // Add/Copy/Edit/Delete/MarkAsDelete
}

void ibValueMetaObjectRecordDataHierarchyMutableRef::CallAsCommand(ibActionID id, const ibUniqueKey& anchor, const ibUniqueKey& key, ibBackendValueForm* srcForm) const
{
	if (id == eAddFolder || id == eAddValue) {
		// CREATE always anchors on the ANCHOR (never the selected row): the front computed it per view mode — a
		// hierarchy list gives the top element, a tree gives the folder the user is standing in, a flat list gives
		// none. A folder anchor → the new value lands INSIDE it (parent = the folder itself); an item anchor → the
		// new value is its SIBLING (parent = the item's own parent); an empty anchor → the catalog root. The command
		// interface is by-KEY, so the anchor's own IsFolder / Parent / Reference come from one point load of it.
		const ibUniqueKey& ctxKey = anchor;
		try {
			ibValue parent;   // empty → root
			if (ctxKey.IsOk()) {
				ibValuePtr<ibValueRecordDataObjectHierarchyRef> sel(CreateObjectValue(ibObjectMode::OBJECT_ITEM, ctxKey.GetGuid()));
				if (sel != nullptr) {
					ibValue isFolder;
					sel->GetValueByMetaID(GetDataIsFolder()->GetMetaID(), isFolder);
					// folder → its own reference (nest inside); item → its parent (sibling).
					sel->GetValueByMetaID(isFolder.GetBoolean() ? GetDataReference()->GetMetaID() : GetDataParent()->GetMetaID(), parent);
				}
			}
			const ibObjectMode mode = (id == eAddFolder) ? ibObjectMode::OBJECT_FOLDER : ibObjectMode::OBJECT_ITEM;
			ibValuePtr<ibValueRecordDataObjectHierarchyRef> obj(CreateObjectValue(mode));
			if (obj != nullptr) {
				if (!parent.IsEmpty())
					obj->SetValueByMetaID(GetDataParent()->GetMetaID(), parent);   // pre-fill the parent from the browsed folder
				obj->ShowFormValue(wxEmptyString, dynamic_cast<ibBackendControlFrame*>(srcForm));
			}
		}
		catch (const ibBackendInterruptException&) {}
		catch (const ibBackendException& err) { ibValueSystemFunction::Alert(err.GetErrorDescription()); }
		catch (...) { wxLogError(wxT("ibValueMetaObjectRecordDataHierarchyMutableRef::CallAsCommand: unhandled non-ibBackend exception swallowed")); }
		return;
	}
	ibValueMetaObjectRecordDataMutableRef::CallAsCommand(id, anchor, key, srcForm);   // Copy/Edit/Delete/MarkAsDelete
}

// ---------------------------------------------------------------------------------------------------------
// Register BASE — Add/Copy/Edit/Delete for an INDEPENDENT register (no recorder); a recorder-based register
// (document movements) shows none. Execute / open run through the RECORD MANAGER (by the row's key-pair). Select =
// the composite record key. Lifted from ibValueListRegisterObject.
// ---------------------------------------------------------------------------------------------------------
void ibValueMetaObjectRegisterData::GetCommandCollection(const ibFormID& /*formType*/, std::vector<ibCommandItem>& commands) const
{
	if (HasRecorder())   // recorder-based register (document movements) — no independent commands
		return;
	commands.emplace_back(eAddValue,    wxT("Add"),    _("Add"),    g_picAddCLSID,    true);
	commands.emplace_back(eCopyValue,   wxT("Copy"),   _("Copy"),   g_picCopyCLSID);
	commands.emplace_back(eEditValue,   wxT("Edit"),   _("Edit"),   g_picEditCLSID);
	commands.emplace_back(eDeleteValue, wxT("Delete"), _("Delete"), g_picDeleteCLSID);
}

void ibValueMetaObjectRegisterData::CallAsCommand(ibActionID id, const ibUniqueKey& /*anchor*/, const ibUniqueKey& key, ibBackendValueForm* srcForm) const
{
	if (!HasRecordManager())
		return;
	try {
		switch (id)
		{
		case eAddValue: {
			ibValuePtr<ibValueRecordManagerObject> obj(CreateRecordManagerObjectValue());
			if (obj != nullptr) obj->ShowFormValue(wxEmptyString, dynamic_cast<ibBackendControlFrame*>(srcForm));
			break;
		}
		case eCopyValue: {
			if (!key.IsOk()) return;
			ibValuePtr<ibValueRecordManagerObject> obj(CopyRecordManagerObjectValue(ibUniqueKeyPair(key.GetKeyValues())));
			if (obj != nullptr) obj->ShowFormValue(wxEmptyString, dynamic_cast<ibBackendControlFrame*>(srcForm));
			break;
		}
		case eEditValue: {
			if (!key.IsOk()) return;
			ibValuePtr<ibValueRecordManagerObject> obj(CreateRecordManagerObjectValue(ibUniqueKeyPair(key.GetKeyValues())));
			if (obj != nullptr) obj->ShowFormValue(wxEmptyString, dynamic_cast<ibBackendControlFrame*>(srcForm));
			break;
		}
		case eDeleteValue: {
			if (!key.IsOk()) return;
			ibValuePtr<ibValueRecordManagerObject> obj(CreateRecordManagerObjectValue(ibUniqueKeyPair(key.GetKeyValues())));
			if (obj != nullptr) obj->DeleteRegister();
			if (srcForm != nullptr) srcForm->UpdateForm();
			break;
		}
		}
	}
	catch (const ibBackendInterruptException&) {}
	catch (const ibBackendException& err) { ibValueSystemFunction::Alert(err.GetErrorDescription()); }
	catch (...) { wxLogError(wxT("ibValueMetaObjectRegisterData::CallAsCommand: unhandled non-ibBackend exception swallowed")); }
}

// OPEN a register record by key (double-click) — through the record manager, mirroring the eEditValue command.
//
// A RECORDER-BASED REGISTER OPENS ITS RECORDER. Its rows are not editable on their own — they belong to the document
// that wrote them, which is why there is no record manager to raise a form from. What the user is pointing at in that
// case is the document, and the recorder reference is PART of the row's identity, so it is already in the key: no
// second lookup, no row node to reach back for.
void ibValueMetaObjectRegisterData::ShowValueByKey(const ibUniqueKey& key, ibBackendValueForm* srcForm) const
{
	if (!key.IsOk())
		return;

	if (!HasRecordManager()) {
		if (!HasRecorder())
			return;                       // neither a record of its own nor a recorder — nothing to open
		try {
			const ibValueMetaObjectAttributePredefined* metaRecorder = GetRegisterRecorder();
			if (metaRecorder == nullptr)
				return;
			const ibRowMetaValues& keyValues = key.GetKeyValues();
			auto it = keyValues.find(metaRecorder->GetMetaID());
			if (it != keyValues.end()) {
				ibValue recorderVal = it->second;
				recorderVal.ShowValue();
			}
		}
		catch (const ibBackendInterruptException&) {}
		catch (const ibBackendException& err) { ibValueSystemFunction::Alert(err.GetErrorDescription()); }
		catch (...) { wxLogError(wxT("ibValueMetaObjectRegisterData::ShowValueByKey: unhandled non-ibBackend exception swallowed")); }
		return;
	}

	try {
		ibValuePtr<ibValueRecordManagerObject> obj(CreateRecordManagerObjectValue(ibUniqueKeyPair(key.GetKeyValues())));
		if (obj != nullptr) obj->ShowFormValue(wxEmptyString, dynamic_cast<ibBackendControlFrame*>(srcForm));
	}
	catch (const ibBackendInterruptException&) {}
	catch (const ibBackendException& err) { ibValueSystemFunction::Alert(err.GetErrorDescription()); }
	catch (...) { wxLogError(wxT("ibValueMetaObjectRegisterData::ShowValueByKey: unhandled non-ibBackend exception swallowed")); }
}

// SELECT value for a register — no single reference; the picker value IS the composite RECORD KEY built from the
// row's dimension cells (the whole value map). Lifted from ibValueListRegisterObject::GetItemSelectValue.
ibValue ibValueMetaObjectRegisterData::GetSelectValue(const ibRowMetaValues& rowValues) const
{
	return CreateRecordKeyObjectValue(rowValues);
}

// ITEM KEY for a register — no single reference; a register has SEVERAL key columns, so the identity is the
// COMPOSITE record key built from the row's dimension cells. CreateUniqueKeyPair filters the row to this register's
// dimensions and seeds the pair (ibUniqueKeyPair IS-A ibUniqueKey — the composite lives in the base, survives by value).
ibUniqueKey ibValueMetaObjectRegisterData::GetItemKey(const ibRowMetaValues& rowValues) const
{
	return CreateUniqueKeyPair(rowValues);
}

// REGISTER variant — all columns (dimensions / resources / period / recorder …) visible by default.
void ibValueMetaObjectRegisterData::FillSourceExplorer(ibSourceDataObject::ibSourceExplorer& explorer) const
{
	for (const ibValueMetaObjectAttributeBase* a : GetGenericAttributeArrayObject())
		if (a != nullptr)
			explorer.AppendColumn(a, /*enabled*/true, /*visible*/ true);
}

// (Default presentation sort REMOVED from here — it is now set at LIST CREATION (CreateSourceObject / GetListForm)
//  as an ordinary serialised, user-editable sort, not a runtime method that re-applies every fetch. — Max 2026-07-11)

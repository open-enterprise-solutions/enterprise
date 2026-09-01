////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : metaData
////////////////////////////////////////////////////////////////////////////

#include "metaData.h"

#include "backend/metaCollection/metaModuleObject.h"
#include "backend/metaCollection/metaFormObject.h"
#include "backend/query/queryableFactory.h"   // ibQueryableFactory::Register/Unregister — the per-config source registry
#include "backend/query/schemaSnapshot.h"     // ibSchemaSnapshot — BuildSchemaSnapshot

#include <algorithm>
#include <cwctype>
#include <string>
#include <unordered_set>

//**************************************************************************************************
//*                          copy-aware identity resolve (metaId <-> guid)                          *
//**************************************************************************************************

ibGuid ibMetaData::GuidByMetaId(const ibMetaID& id) const
{
	const ibValueMetaObject* meta = FindAnyObjectByFilter(id, true);
	return meta != nullptr && meta->IsAllowed() ? meta->GetCommonGuid() : wxNullGuid;
}

ibMetaID ibMetaData::MetaIdByGuid(const ibGuid& guid) const
{
	if (!guid.isValid())
		return wxNOT_FOUND;
	const ibValueMetaObject* meta = FindAnyObjectByFilter(guid, true);
	return meta != nullptr && meta->IsAllowed() ? meta->GetMetaID() : wxNOT_FOUND;
}

// The whole structure this configuration declares — see the header. The walk
// itself belongs to the metaobjects (ibValueMetaObject::ContributeTables descends
// its children); this only names WHERE it starts, which is the one thing every
// caller was repeating.
ibSchemaSnapshot ibMetaData::BuildSchemaSnapshot() const
{
	ibSchemaSnapshot snapshot;
	if (const ibValueMetaObject* common = GetCommonMetaObject())
		common->ContributeTables(snapshot);
	return snapshot;
}

// Register the source into THIS config's OWN factory (the metaobject calls it on run: GetMetaData()->RegisterSource).
// Only metadata-backed sources register now; each config keeps its own set. No factory (closed) → no-op.
void ibMetaData::RegisterSource(ibQueryableSourceDescriptor* descriptor) const
{
	if (ibQueryableFactory* factory = GetSourceFactory())
		factory->Register(descriptor);
}

void ibMetaData::UnregisterSource(ibQueryableSourceDescriptor* descriptor) const
{
	if (ibQueryableFactory* factory = GetSourceFactory())
		factory->Unregister(descriptor);
}

//**************************************************************************************************
//*                                       ibModuleStorage                                          *
//**************************************************************************************************

bool ibModuleStorage::AddCommonModule(ibValueMetaObjectCommonModule* commonModule)
{
	if (commonModule == nullptr)
		return false;
	auto it = std::find(m_initModules.begin(), m_initModules.end(), commonModule);
	if (it == m_initModules.end())
		m_initModules.emplace_back(commonModule);
	return true;
}

bool ibModuleStorage::RenameCommonModule(ibValueMetaObjectCommonModule* /*commonModule*/, const wxString& /*newName*/)
{
	// Descriptor name is owned by the descriptor itself; storage indexes
	// by raw pointer, so rename is a no-op. Kept on the API as a single
	// notification point — runtime mm picks up name changes on its own
	// during compile.
	return true;
}

bool ibModuleStorage::RemoveCommonModule(ibValueMetaObjectCommonModule* commonModule)
{
	if (commonModule == nullptr)
		return false;
	auto it = std::find(m_initModules.begin(), m_initModules.end(), commonModule);
	if (it != m_initModules.end())
		m_initModules.erase(it);
	return true;
}

//**************************************************************************************************
//*                                    ibCompileValueCache                                         *
//**************************************************************************************************

// Out-of-line — the complete ibValueModuleManagerDesigner type is available here
// (moduleManager.h fully included), so the ibValuePtr<ibValueModuleManagerDesigner>
// assign/convert (a ref-counting static_cast through ibValue*) compiles. In
// metaData.h the type is only forward-visible under the moduleManager.h cycle.
ibCompileValueCache::ibCompileValueCache(ibValueModuleManagerDesigner* moduleManager)
	: m_moduleManager(moduleManager)
{
}

ibValueModuleManagerDesigner* ibCompileValueCache::GetModuleManager() const
{
	return m_moduleManager;
}

void ibCompileValueCache::SetModuleManager(ibValueModuleManagerDesigner* moduleManager)
{
	m_moduleManager = moduleManager;
}

bool ibCompileValueCache::AddCompileModule(const ibValueMetaObject* moduleObject, ibValue* object)
{
	if (object == nullptr)
		return true;
	auto it = m_cache.find(moduleObject);
	if (it == m_cache.end()) {
		ibCompileEntry entry;
		entry.m_value = ibValuePtr<ibValue>(object);
		m_cache.emplace(moduleObject, std::move(entry));
		return true;
	}
	return false;
}

bool ibCompileValueCache::AddCompileModule(const ibValueMetaObject* moduleObject, std::function<ibValue*()> builder)
{
	if (moduleObject == nullptr)
		return false;
	// Insert/replace — second registration for the same descriptor (e.g.
	// after a designer rename + re-run) overrides the prior rebuilder
	// and drops any built value so the next Find rebuilds from scratch.
	ibCompileEntry entry;
	entry.m_deferred = std::move(builder);
	m_cache.insert_or_assign(moduleObject, std::move(entry));
	return true;
}

bool ibCompileValueCache::RemoveCompileModule(const ibValueMetaObject* moduleObject)
{
	auto it = m_cache.find(moduleObject);
	if (it != m_cache.end()) {
		m_cache.erase(it);
		return true;
	}

	return false;
}

bool ibCompileValueCache::InvalidateCompileModule(const ibValueMetaObject* moduleObject)
{
	auto it = m_cache.find(moduleObject);
	if (it == m_cache.end()) return false;

	// No rebuilder — entry was registered as an already-built value
	// (e.g. catalog/document module). Nothing to invalidate; rebuild
	// requires a fresh AddCompileModule(meta, value) by the registering
	// side.
	if (!it->second.m_deferred)
		return false;

	// Drop the cached built value; rebuilder stays so the next Find
	// triggers a Construct.
	it->second.m_value = ibValuePtr<ibValue>();
	return true;
}

ibValue* ibCompileValueCache::FindCompileModuleRef(const ibValueMetaObject* moduleObject) const
{
	auto it = m_cache.find(moduleObject);
	if (it == m_cache.end())
		return nullptr;

	// Cached built value — return directly.
	if (it->second.m_value)
		return &(*it->second.m_value);

	// Pending or invalidated — try the rebuilder. mm should be ready by
	// the time the first lookup arrives; on Construct failure the cache
	// entry is dropped to avoid retrying on every subsequent lookup.
	if (!it->second.m_deferred)
		return nullptr;

	// Re-entrancy guard: Construct internally calls
	// CreateObjectForm → CreateAndBuildForm → back into FindCompileModule
	// for the same creator. Without this guard the lookup would trigger
	// another Construct on the still-empty entry and recurse forever
	// (stack-overflow on first new-form add).
	if (it->second.m_constructing)
		return nullptr;
	it->second.m_constructing = true;

	ibValue* value = it->second.m_deferred();

	// Re-find: Construct may have called Invalidate / erase along the way.
	it = m_cache.find(moduleObject);
	if (it == m_cache.end())
		return value;  // cache cleared; just return what we built (may be null)

	it->second.m_constructing = false;
	if (value == nullptr) {
		m_cache.erase(it);
		return nullptr;
	}
	it->second.m_value = ibValuePtr<ibValue>(value);
	return value;
}

ibValue* ibCompileValueCache::FindParentCompileModuleRef(const ibValueMetaObject* moduleObject) const
{
	ibValueMetaObject* parent = moduleObject ? moduleObject->GetParent() : nullptr;
	return parent ? FindCompileModuleRef(parent) : nullptr;
}

//**************************************************************************************************
//*                                          ibMetaData											   *
//**************************************************************************************************

//ID's 
ibMetaID ibMetaData::GenerateNewID() const
{
	// SEED ONCE, THEN COUNT. The walk still decides where the numbering starts — a configuration
	// just loaded must continue past its highest existing id — but it runs once per open image
	// instead of once per new object, and what it produces is a floor, not an answer.
	if (m_nextMetaId == 0) {
		const ibValueMetaObject* commonObject = GetCommonMetaObject();
		wxASSERT(commonObject);
		ibMetaID id = commonObject->GetMetaID() + 1;
		DoGenerateNewID(id, commonObject);
		m_nextMetaId = id;

		// The seed is the whole question: it is the walk's answer about what the tree CURRENTLY holds,
		// and an id already given to something the walk cannot see will be handed out a second time.
	}

	// ⚠ NEVER BACKWARDS. An id whose object was deleted does NOT return to the pool, and that is the
	// whole point: the physical column is named `fld<id>`, so re-issuing an id re-issues a column
	// NAME the database may still be holding. See the note on the declaration.
	return m_nextMetaId++;
}

void ibMetaData::DoGenerateNewID(ibMetaID& id, const ibValueMetaObject* top) const
{
	for (unsigned int idx = 0; idx < top->GetChildCount(); idx++) {
		ibValueMetaObject* child = top->GetChild(idx);
		wxASSERT(child);
		ibMetaID newID = child->GetMetaID() + 1;
		if (newID > id) {
			id = newID;
		}
		DoGenerateNewID(id, child);
	}
}

ibValueMetaObject* ibMetaData::CreateMetaObject(const ibClassID& clsidIn, ibValueMetaObject* parent, bool runObject)
{
	// ⭐ A CONFIGURATION THAT MAY NOT BE EDITED IS NOT EDITED — asked at the DOOR, not at the
	// command that happens to have a person behind it (Max, 2026-09-01: *"if the metadata is in
	// read mode, it should raise if something tries to change it — or at least return false"*).
	// The designer's trees each carried their own check; a tool over MCP carried none.
	if (!IsEditable())
		return nullptr;

	// ⭐⭐ THE OWNER IS ASKED WHETHER IT HOSTS THIS KIND, AND ITS ANSWER IS TAKEN. ResolveChild
	// says both things at once: WHICH variant of the kind belongs here (a tabular section's RAM
	// MD_TBL and DB-backed MD_TBLR are the same kind, and the owner remaps a pasted child to ITS
	// variant), and, by answering 0, that it hosts no such thing at all.
	//
	// 🛑 THE ZERO WAS READ TWO WAYS, and that was the whole defect (2026-09-01). Here it used to
	// mean "the owner has no opinion — build it anyway"; in ibValueMetaObject::Init, four lines of
	// FilterChild away, the same zero means "refused". So a child its owner had already declined
	// was built, attached, refused on attachment, destroyed, and the refusal came back as
	// ibBackendCoreException *"Error initializing object 'Dimension'"* — an exception raised for an
	// ordinary answer, journalled at exception level, and surfaced to the person as a modal warning
	// carrying a truncated number. Pasting a register's contents onto a document produced six.
	//
	// One reading now: 0 is no, and nothing is built. The check in Init stays as the guard for the
	// other creation roads — this is the door, not the only one.
	ibClassID clsid = clsidIn;
	if (parent != nullptr) {
		const ibClassID resolved = parent->ResolveChild(clsidIn);
		if (resolved == 0)
			return nullptr;
		clsid = resolved;
	}

	wxASSERT(clsid != 0);

	ibValue* ppParams[] = { parent };
	ibValueMetaObject* newMetaObject = nullptr;

	try {
		// AddChild (inside Init via ppParams[0]=parent) takes the owning reference.
		newMetaObject = ibValue::CreateAndConvertObjectRef<ibValueMetaObject>(clsid, ppParams, 1);
	}
	catch (...) {
		return nullptr;
	}

	if (newMetaObject != nullptr) {

		newMetaObject->SetName(
			GetNewName(clsid, parent, newMetaObject->GetClassName())
		);

		//always create meta object
		bool success = newMetaObject->OnCreateMetaObject(this, runObject ? newObjectFlag : pasteObjectFlag);

		//first initialization
		if (!success || !newMetaObject->OnLoadMetaObject(this)) {
			if (parent != nullptr)
				parent->RemoveChild(newMetaObject); // owning vector releases → destroys
			else
				wxDELETE(newMetaObject); // never attached (no parent) — destroy directly
			return nullptr;
		}

		//and running initialization
		if (runObject && (!success || !newMetaObject->OnBeforeRunMetaObject(newObjectFlag))) {
			if (parent != nullptr)
				parent->RemoveChild(newMetaObject); // owning vector releases → destroys
			else
				wxDELETE(newMetaObject); // never attached (no parent) — destroy directly
			return nullptr;
		}

		Modify(true);

		if (runObject && (!success || !newMetaObject->OnAfterRunMetaObject(newObjectFlag))) {
			if (parent != nullptr)
				parent->RemoveChild(newMetaObject); // owning vector releases → destroys
			else
				wxDELETE(newMetaObject); // never attached (no parent) — destroy directly
			return nullptr;
		}

		// ⭐ AND EVERYONE WATCHING IS TOLD — last, after every phase that could still undo the
		// creation, because an object that did not survive its own initialisation was never
		// created. A watcher may act on this (the designer's tree asks which kind of form it is
		// and writes the answer in); none of them can refuse it.
		//
		// 🛑 …BUT ONLY WHEN THE OBJECT IS FINISHED, AND `runObject` IS WHAT SAYS SO (Max, 2026-09-01:
		// *"the cycle is broken — you have to hold the notifier until everything has run"*). `false`
		// means this object is a SHELL somebody is about to paste or load into: its name is the one
		// GetNewName just invented, its properties are the type's defaults, and every one of them is
		// about to be replaced.
		//
		// Announcing it there is what made a watcher draw a row saying `Dimension1` beside a panel
		// saying `Warehouse4`, and a default `Length 10` under a reference type — not stale drawing,
		// but an accurate picture of a half-built object. Whoever fills the shell announces it when
		// it is full: ibValueMetaObject::PasteObject does, at its end.
		if (runObject)
			MetaObjectStage(ibMetaDataNotifier::ibMetaStage::Created, newMetaObject);
	}

	return newMetaObject;
}

wxString ibMetaData::GetNewName(const ibClassID& clsid, ibValueMetaObject* parent, const wxString& strPrefix, bool forConstructor)
{
	const wxString currPrefix = strPrefix.Length() > 0 ?
		strPrefix : wxT("newItem");

	// Normalise to upper case, matching stringUtils::CompareString's
	// case-insensitive semantics, so set lookups are exact comparisons.
	const auto toKey = [](const wxString& name) {
		std::wstring key = name.ToStdWstring();
		std::transform(key.begin(), key.end(), key.begin(), ::towupper);
		return key;
	};

	// Collect the sibling names already taken by objects of this class
	// once, so each candidate is probed in O(1). The old loop rescanned
	// every child for each candidate suffix — O(children * candidates)
	// per call. FilterChild(clsid) is invariant here (every surviving
	// child has class == clsid), so it is hoisted out of the scan.
	std::unordered_set<std::wstring> takenNames;
	if (parent != nullptr && parent->FilterChild(clsid)) {

		for (unsigned int idx = 0; idx < parent->GetChildCount(); idx++) {

			auto child = parent->GetChild(idx);
			if (clsid != child->GetClassType())
				continue;
			if (child->IsDeleted())
				continue;

			takenNames.insert(toKey(child->GetName()));
		}
	}

	// forConstructor allows the bare prefix as the first candidate;
	// otherwise numbering starts at 1.
	unsigned int countRec = forConstructor ? 0 : 1;
	wxString newName = forConstructor ?
		currPrefix : wxString::Format(wxT("%s%d"), currPrefix, countRec);

	while (takenNames.count(toKey(newName)) > 0)
		newName = wxString::Format(wxT("%s%d"), currPrefix, ++countRec);

	return newName;
}

// ⭐⭐ COPY AND PASTE, AS DOORS OF THE METADATA — the pair that completes the set. Five things can
// happen to an object in a configuration and each is one entry point here, raising its own event:
// CreateMetaObject, RenameMetaObject, CopyMetaObject, PasteMetaObject, RemoveMetaObject (Max,
// 2026-09-01). Whoever wants to change something WITHOUT the event writes the property itself —
// that is the only way round, and it is deliberate rather than accidental.

// The payload's own shape. It travelled with the walkers and belongs beside them.
#define	headerBlock 0x002330
#define	dataBlock 0x002350
#define	childBlock 0x002370

bool ibMetaData::CopyMetaObject(const ibValueMetaObject* object, ibWriterMemory& writer) const
{
	if (object == nullptr)
		return false;

	// Nothing is announced: the configuration did not change.

#pragma region _copy_guard_h_

	// Every node of the copied subtree is stamped with a fresh copy-guid for the duration, so a
	// form inside it writes its source hops in COPY format and the paste can re-home them. Cleared
	// on the way out, whatever happens.
	class ibControlCopyGuard {

		static void Generate(const ibValueMetaObject* copyObject) {
			for (unsigned int idx = 0; idx < copyObject->GetChildCount(); idx++)
				Generate(copyObject->GetChild(idx));
			copyObject->m_metaCopyGuid = wxNewUniqueGuid;
		}

		static void Erase(const ibValueMetaObject* copyObject) {
			for (unsigned int idx = 0; idx < copyObject->GetChildCount(); idx++)
				Erase(copyObject->GetChild(idx));
			copyObject->m_metaCopyGuid = wxNullGuid;
		}

	public:

		ibControlCopyGuard(const ibValueMetaObject* copyObject) : m_copyObject(copyObject) { Generate(m_copyObject); }
		~ibControlCopyGuard() { Erase(m_copyObject); }

	protected:
		const ibValueMetaObject* m_copyObject = nullptr;
	};

	ibControlCopyGuard controlCopyGuard(object);

#pragma endregion

	wxASSERT(object->m_metaCopyGuid.isValid());

#pragma region _copy_fill_h_

	class ibControlMemoryWriter {
	public:

		static bool CopyObject(const ibValueMetaObject* copyObject, ibWriterMemory& writer)
		{
			ibWriterMemory writerHeaderMemory;
			writerHeaderMemory.w_s32(copyObject->m_metaData->GetVersion());
			writerHeaderMemory.w_stringZ(copyObject->m_metaCopyGuid);
			// NO CLASS ID IN THE HEADER, deliberately. A paste is a MERGE BY NAME (see
			// ibPropertyObject::PasteProperty): the TARGET's class is decided by where the paste
			// lands, and the payload only supplies values for the properties the two share —
			// pasting a Document onto a Constant is a legitimate request, not a mismatch to refuse.
			// A class id here would only tempt the next reader to compare and reject.
			writer.w_chunk(headerBlock, writerHeaderMemory.pointer(), writerHeaderMemory.size());

			ibWriterMemory writerChildMemory;

			for (ibValueMetaObject* object : copyObject->m_children) {

				if (!copyObject->FilterChild(object->GetClassType()))
					continue;
				if (object->IsDeleted())
					continue;
				ibWriterMemory writerMemory;
				if (!CopyObject(object, writerMemory))
					return false;

				writerChildMemory.w_chunk(object->GetClassType(), writerMemory.pointer(), writerMemory.size());
			}

			writer.w_chunk(childBlock, writerChildMemory.pointer(), writerChildMemory.size());

			ibWriterMemory writerDataMemory;

			if (!copyObject->CopyProperty(writerDataMemory))
				return false;

			if (!copyObject->SaveInterface(writerDataMemory))
				return false;

			if (!copyObject->SaveRole(writerDataMemory))
				return false;

			writer.w_chunk(dataBlock, writerDataMemory.pointer(), writerDataMemory.size());
			return true;
		}
	};

#pragma endregion

	return ibControlMemoryWriter::CopyObject(object, writer);
}

ibValueMetaObject* ibMetaData::PasteMetaObject(const ibClassID& clsid,
	ibValueMetaObject* parentMetaObj, ibReaderMemory& reader)
{
	if (!IsEditable())   // see CreateMetaObject — the rule lives in the door
		return nullptr;

#pragma region _paste_fill_h_

	class ibControlMemoryReader {

		static bool PasteObject(ibValueMetaObject* pasteObject, ibReaderMemory& reader)
		{
			ibMetaData* metaData = pasteObject->GetMetaData();

			std::shared_ptr <ibReaderMemory>readerHeaderMemory(reader.open_chunk(headerBlock));

			/*const ibVersionID& version =*/ readerHeaderMemory->r_s32();
			pasteObject->m_metaGuid = readerHeaderMemory->r_stringZ();

			// MARK the pasted object as pasted — its paste-guid equals its own guid (the source copy-guid it was
			// created under). IsPasteMode() then holds while the tree runs, so a form re-loaded here re-homes its
			// source hops onto this NEW object via GetIdByGuid; the OUTER guard clears the mark on exit. This mirrors
			// ibControlCopyGuard on the copy side. No new flag — the existing paste-guid IS the mark.
			pasteObject->m_metaPasteGuid = pasteObject->m_metaGuid;

			// Running initialization AS A PASTE (pasteObjectFlag, NOT onlyLoadFlag): a pasted object is a NEW object,
			// so its RUN event must register its queryable source — exactly like a fresh create (newObjectFlag) does.
			// onlyLoadFlag gates the queryable registration OFF (the load-only pass), which left a copied catalog /
			// register unregistered → its source descriptor was unresolvable ("the source cannot see it"). — Max.
			if (!pasteObject->OnBeforeRunMetaObject(pasteObjectFlag))
				return false;

			std::shared_ptr <ibReaderMemory>readerDataMemory(reader.open_chunk(dataBlock));

			if (!pasteObject->PasteProperty(*readerDataMemory))
				return false;

			pasteObject->BuildNewName();

			pasteObject->LoadInterface(*readerDataMemory);
			pasteObject->LoadRole(*readerDataMemory);

			if (!pasteObject->OnAfterRunMetaObject(pasteObjectFlag))
				return false;

			return PasteChildren(pasteObject, reader, metaData);
		}

		// The child block, read the same way by both entries below — it was written out twice, and
		// the two copies had begun to drift.
		static bool PasteChildren(ibValueMetaObject* pasteObject, ibReaderMemory& reader, ibMetaData* metaData)
		{
			std::shared_ptr <ibReaderMemory> readerChildMemory(reader.open_chunk(childBlock));
			if (readerChildMemory == nullptr)
				return true;

			ibReaderMemory* prevReaderMemory = nullptr;
			do {
				ibClassID clsid = 0;
				ibReaderMemory* readerMemory = readerChildMemory->open_chunk_iterator(clsid, &*prevReaderMemory);
				if (readerMemory == nullptr)
					break;
				if (clsid > 0) {
					// The shell for a CHILD — a part of a result, never one itself, which is what
					// `false` says. Nothing is announced for it.
					ibValueMetaObject* metaObject = metaData->CreateMetaObject(clsid, pasteObject, false);
					if (metaObject != nullptr) {
						if (!PasteObject(metaObject, *readerMemory)) {
							// THE PARENT ALREADY OWNS IT — AddChild took the owning reference inside
							// CreateMetaObject. wxDELETE frees the object behind the owner's back and
							// leaves the owning vector holding a dangling pointer it releases again
							// later. Same door CreateMetaObject's own failure path uses.
							pasteObject->RemoveChild(metaObject);
							return false;
						}
					}
					else {
						// The owner does not host this kind. Not a failure of the paste: a
						// cross-kind paste — a register's contents laid onto a document — keeps
						// what the two have in common and legitimately drops the rest, and the
						// owner is asked BEFORE anything is built (ibMetaData::CreateMetaObject).
						//
						// ⚠ SAID IN THE FILE, NOT TO THE PERSON. This was ibJournalWarning, and a
						// warning is echoed as wxLogWarning, which in a GUI application opens a
						// modal: six skipped children became a dialog listing six numbers, in
						// front of somebody who had only pressed Paste (Max, 2026-09-01: *"that's
						// a probe, isn't it?"*).
						//
						// ⚠ AND IT NAMED NOTHING. `(unsigned int)clsid` cuts a 64-bit kind-typed
						// id to 32 bits — throwing away the KIND byte, which is the half that
						// says WHAT it is — so the number identified nothing and matched nothing.
						// The metadata knows the name; ask it.
						ibJournalInfo(wxT("metadata"), wxT("PasteObject: '%s' is not hosted by '%s' - skipped"),
							metaData->GetNameObjectFromID(clsid).c_str(), pasteObject->GetClassName().c_str());
					}
				}
				prevReaderMemory = readerMemory;
			} while (true);

			return true;
		}

	public:

		// Recursively clear the paste marks once the whole tree is pasted, run and its forms re-homed — the mirror of
		// ibControlCopyGuard::Erase. RAII from PasteMetaObject.
		static void ErasePasteGuid(const ibValueMetaObject* pasteObject) {
			for (unsigned int idx = 0; idx < pasteObject->GetChildCount(); idx++)
				ErasePasteGuid(pasteObject->GetChild(idx));
			pasteObject->m_metaPasteGuid = wxNullGuid;
		}

		static bool PasteAndRunObject(ibValueMetaObject* pasteObject, ibReaderMemory& reader)
		{
			ibMetaData* metaData = pasteObject->GetMetaData();

			std::shared_ptr <ibReaderMemory>readerHeaderMemory(reader.open_chunk(headerBlock));

			/*const ibVersionID& version =*/ readerHeaderMemory->r_s32();
			/*pasteObject->m_metaGuid =*/ readerHeaderMemory->r_stringZ();

			// MARK the ROOT as pasted too — the SAME two-level rule the copy side already follows. On copy,
			// ibControlCopyGuard::Generate stamps the whole subtree ROOT + children, so IsCopyMode() holds and the
			// form writes its blob in COPY format (per-hop guid/kind tags). On paste, only the recursive CHILD
			// PasteObject stamps its objects; this ROOT entry did not — so a form copied AS PART OF a whole
			// metaobject (a child) re-homed fine, but a form copied DIRECTLY (a common form, or a form within a
			// group) — which arrives HERE as the root — had IsPasteMode() == false and read its COPY-format blob as
			// RAW: the reader mis-parses the hop layout and drops the source-hop paths (and the attribute section).
			// Keep the root's OWN m_metaGuid (it is the paste TARGET's identity — the source guid read above is
			// intentionally discarded, unlike a child which adopts it); the mark only has to be valid for the whole
			// run so LoadControl routes to PasteNode, and the OUTER guard clears it on exit.
			pasteObject->m_metaPasteGuid = pasteObject->m_metaGuid;

			// Running initialization AS A PASTE (pasteObjectFlag, NOT onlyLoadFlag): a pasted object is a NEW object,
			// so its RUN event must register its queryable source — exactly like a fresh create (newObjectFlag) does.
			// onlyLoadFlag gates the queryable registration OFF (the load-only pass), which left a copied catalog /
			// register unregistered → its source descriptor was unresolvable ("the source cannot see it"). — Max.
			if (!pasteObject->OnBeforeRunMetaObject(pasteObjectFlag))
				return false;

			std::shared_ptr <ibReaderMemory>readerDataMemory(reader.open_chunk(dataBlock));

			if (!pasteObject->PasteProperty(*readerDataMemory))
				return false;

			pasteObject->BuildNewName();

			pasteObject->LoadInterface(*readerDataMemory);
			pasteObject->LoadRole(*readerDataMemory);

			if (!pasteObject->OnAfterRunMetaObject(pasteObjectFlag))
				return false;

			if (!PasteChildren(pasteObject, reader, metaData))
				return false;

			return pasteObject->OnReloadMetaObject();
		}
	};

#pragma endregion

	// The shell — a PART, not a result. `false` is what says so, and nothing is announced for it:
	// its name is invented, its properties are the type's defaults and its children are not there.
	ibValueMetaObject* const pasted = CreateMetaObject(clsid, parentMetaObj, false);
	if (pasted == nullptr)
		return nullptr;

	bool filled = false;
	{
		// RAII: whatever the outcome, clear the paste marks once the whole tree is pasted, run and its forms
		// re-homed — the mirror of ibControlCopyGuard erasing the copy marks after a copy.
		struct ibControlPasteGuard {
			const ibValueMetaObject* m_pasteObject;
			~ibControlPasteGuard() { ibControlMemoryReader::ErasePasteGuid(m_pasteObject); }
		} pasteGuard{ pasted };

		filled = ibControlMemoryReader::PasteAndRunObject(pasted, reader);
	}

	if (!filled) {
		// A PASTE THAT FAILED LEAVES NOTHING BEHIND. The object was already in the configuration
		// before the payload was read, so a bad payload would otherwise leave it standing —
		// unshown, because nothing announced it, and saved with the configuration all the same.
		RemoveMetaObject(pasted, parentMetaObj);
		return nullptr;
	}

	// ⭐ AND NOW IT IS A RESULT: filled, its children grown, its name the one that was free, its
	// paste marks cleared. Said once, from here — the same stage an ordinary create ends in,
	// because the fact is the same.
	MetaObjectStage(ibMetaDataNotifier::ibMetaStage::Created, pasted);
	return pasted;
}

bool ibMetaData::RenameMetaObject(ibValueMetaObject* metaObject, const wxString& newName)
{
	if (!IsEditable())   // see CreateMetaObject — the rule lives in the door
		return false;

	bool foundedName = false;

	for (const auto object : GetAnyArrayObject(metaObject->GetClassType())) {
		if (object->GetParent() != metaObject->GetParent())
			continue;
		if (object != metaObject &&
			stringUtils::CompareString(newName, object->GetName())) {
			foundedName = true;
			break;
		}
	}

	if (foundedName) return false;

	// ⭐ AND IT RENAMES. It used to only ASK — answer whether the name was free, run the hook, and
	// leave the assignment to the caller — so every caller carried the second half, and the ones
	// that forgot reported a success the object did not have: an object asked for as "Товары"
	// stood in the tree as "Catalog3" and said it had been named.
	//
	// The hook runs BEFORE the name changes, deliberately: what it does is bring the rest of the
	// configuration into step with a name that is about to be taken (a common attribute rewrites
	// its copies, a common module is re-keyed in the module storage), and a refusal from it means
	// the rename does not happen at all.
	if (metaObject->OnRenameMetaObject(newName)) {
		metaObject->SetName(newName);
		Modify(true);

		// The name is ALREADY the new one when this goes out — a watcher relabelling its row reads
		// it off the object rather than being handed a string that may not have been taken.
		//
		// ⭐ …UNLESS THE OBJECT IS STILL BEING BUILT, and it is already known that it is: the paste
		// mark stands on every node of a pasted subtree for the whole run, and the outer guard in
		// PasteObject clears it. A name bumped by BuildNewName under that mark is not a rename
		// anybody has to hear about — nothing of the object has been shown yet, and the finished
		// object is announced ONCE, at the end. Without this one paste of a catalog sent a Renamed
		// per attribute, per tabular section and per form, on top of everything else that speaks
		// during a build (Max, 2026-09-01: *"a lot of needless events get generated — they must not
		// be sent"*, *"at create it already knows it is a paste"*).
		if (!metaObject->IsPasteMode())
			MetaObjectStage(ibMetaDataNotifier::ibMetaStage::Renamed, metaObject);
		return true;
	}
	return false;
}

bool ibMetaData::RemoveMetaObject(ibValueMetaObject* object, ibValueMetaObject* parent)
{
	if (!IsEditable())   // see CreateMetaObject — the rule lives in the door
		return false;

	// Both close phases — symmetric with CreateMetaObject's Before+After run phases.
	// The before-hook carries the real teardown (RemoveCompileModule /
	// RemoveCommonModule from the compile cache + module storage); skipping it left
	// stale module registrations that later asserted on an "empty registry".
	if (!object->OnBeforeCloseMetaObject())
		return false;
	if (object->OnAfterCloseMetaObject()) {
		if (object->OnDeleteMetaObject()) {
			object->MarkAsDeleted();
			for (unsigned int idx = 0; idx < object->GetChildCount(); idx++) {
				auto child = object->GetChild(idx);
				if (!object->FilterChild(child->GetClassType()))
					continue;
				RemoveMetaObject(child, object);
			}
		}
		object->OnReloadMetaObject();
		Modify(true);

		// ⭐⭐ SAID LAST, WHEN IT IS TRUE (Max, 2026-09-01: *"you send the delete command, it marks
		// and deletes the object, the cycle runs — and THEN the notifier broadcasts that the
		// designer must reflect the change"*). The same order the create now follows.
		//
		// 🛑 IT WAS SAID FIRST, AND SO IT WAS SAID OF THINGS THAT DID NOT HAPPEN. Every early
		// return above — a close phase that refuses, a delete hook that says no — leaves the object
		// standing in the configuration, and the watchers had already been told it was gone: the
		// row taken away, the editors over it closed, while the object is still there and still
		// gets saved. An announcement is a fact, not an intention.
		//
		// ⚠ THE OBJECT IS STILL FINDABLE HERE, which is what makes this possible: MarkAsDeleted
		// MARKS — it does not destroy — so a watcher can still look up what it was showing OF the
		// object and take it away.
		MetaObjectStage(ibMetaDataNotifier::ibMetaStage::Removed, object);
		return true;
	}

	return false;
}
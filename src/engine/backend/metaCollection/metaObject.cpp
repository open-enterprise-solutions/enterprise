////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : metaobject
////////////////////////////////////////////////////////////////////////////

#include "metaObject.h"
#include "backend/appData.h"
#include "backend/metadataConfiguration.h"   // ibMetaDataConfigurationBase::GetRestructureInfo (the static ledger accessor)

#include "backend/metaData.h"
#include "backend/databaseLayer/databaseErrorCodes.h"

#include <wx/log.h>

// Restructure-ledger facade — one call onto the active config's ledger (the static accessor).
void ibValueMetaObject::RestructureInfo   (const wxString& message) { ibMetaDataConfigurationBase::GetRestructureInfo().AppendInfo(message);    }
void ibValueMetaObject::RestructureWarning(const wxString& message) { ibMetaDataConfigurationBase::GetRestructureInfo().AppendWarning(message); }
void ibValueMetaObject::RestructureError  (const wxString& message) { ibMetaDataConfigurationBase::GetRestructureInfo().AppendError(message);   }

//*****************************************************************************************
//*                                  MetaObject                                           *
//*****************************************************************************************

void ibValueMetaObject::ResetGuid()
{
	m_metaGuid = wxNewUniqueGuid;
}

void ibValueMetaObject::ResetId()
{
	if (m_metaData != nullptr) {
		m_metaId = m_metaData->GenerateNewID();
	}
}

/////////////////////////////////////////////////////////////////////////////////////////

ibBackendMetadataTree* ibValueMetaObject::GetMetaDataTree() const
{
	return m_metaData ? m_metaData->GetMetaTree() : nullptr;
}

/////////////////////////////////////////////////////////////////////////////////////////

bool ibValueMetaObject::BuildNewName()
{
	const wxString& strName = GetName(); bool foundedName = false;
	std::vector<ibValueMetaObject*> array;
	if (m_parent != nullptr && m_parent->FillArrayObjectByFilter(array, { GetClassType() })) {
		for (const auto object : array) {
			if (object->GetParent() != GetParent())
				continue;
			if (object != this &&
				stringUtils::CompareString(strName, object->GetName())) {
				foundedName = true;
				break;
			}
		}
	}

	if (foundedName) {
		const wxString& metaPrevName = m_propertyName->GetValueAsString();
		size_t length = metaPrevName.length();
		while (length >= 0 && stringUtils::IsDigit(metaPrevName[--length]));
		const wxString& metaName = m_metaData->GetNewName(GetClassType(), GetParent(), metaPrevName.Left(length + 1));
		SetName(metaName);
		const wxString& metaPrevSynonym = m_propertySynonym->GetValueAsString();
		const wxString& metaSynonym = metaPrevSynonym.Length() > 0 ? stringUtils::GenerateSynonym(metaName) : wxString(wxEmptyString);
		SetSynonym(metaSynonym);
	}

	return !foundedName;
}

/////////////////////////////////////////////////////////////////////////////////////////

ibValueMetaObject::ibValueMetaObject(const wxString& strName, const wxString& synonym, const wxString& comment) : ibValueDynamicMembers(ibValueTypes::TYPE_VALUE, true),
m_metaFlags(metaDefaultFlag), m_metaId(0), m_metaData(nullptr)
{
	m_members.Bind(this, &ibValueMetaObject::FillMembers);
	m_propertyName->SetValue(strName);
	m_propertySynonym->SetValue(synonym);
	m_propertyComment->SetValue(comment);
}

ibValueMetaObject::~ibValueMetaObject()
{
	// Children are released by the ibPropertyObjectHelper base destructor (owning
	// handles cascade down the subtree). The delete event (OnDeleteMetaObject) is
	// a separate, preceding step.
}

// (CreateMetaTable / UpdateMetaTable / DeleteMetaTable + CreateAndUpdateTableDB removed: structure DDL
//  is now the config-save differ's job — a metaobject DECLARES its tables AND their seed rows via
//  ContributeTables; ibStructureBuilder snapshots + diffs both structure and data. query/schemaSnapshot.h.)

bool ibValueMetaObject::OnCreateMetaObject(ibMetaData* metaData, int flags)
{
	GenerateGuid();
	wxASSERT(metaData);
	m_metaId = metaData->GenerateNewID();
	m_metaData = metaData;

	// WHO got which number, and under whom. The owner is the half that identifies a slot: six
	// analytics slots and a dimension are indistinguishable by class alone, and it is precisely a
	// slot and a dimension that were seen holding the same id.
#ifdef DEBUG  
	ibJournalInfo(wxT("metadata"),wxT("* Create metaData object %s with id %i"),
		GetClassName(), GetMetaID()
	);
#endif
	return true;
}

bool ibValueMetaObject::OnLoadMetaObject(ibMetaData* metaData)
{
	m_metaData = metaData;
	return true;
}

bool ibValueMetaObject::OnDeleteMetaObject()
{
	return true;
}

bool ibValueMetaObject::OnAfterCloseMetaObject()
{
	ibBackendMetadataTree* const metaTree = m_metaData->GetMetaTree();
	if (metaTree != nullptr)
		metaTree->CloseMetaObject(this);
	return true;
}

#pragma region interface_h
void ibValueMetaObject::DoSetInterface(const ibMetaID& id, const bool& val)
{
	m_metaData->Modify(true);
}
#pragma endregion
#pragma region role_h 
void ibValueMetaObject::DoSetRight(const ibRole* role, const bool& val)
{
	m_metaData->Modify(true);
}
#pragma endregion

bool ibValueMetaObject::IsFullAccess() const
{
	if (appData->DesignerMode())
		return true;

	return m_metaData->IsFullAccess();
}

ibRoleUserInfo ibValueMetaObject::GetUserRoleInfo() const
{
	// Each role arrives carrying WHAT IT IS — the id and how to combine it, stamped once when the
	// session compiled its root. Nothing to look up here.
	ibRoleUserInfo roleInfo;
	for (const auto& role : appData->GetUserRoleArray())
		roleInfo.m_arrayRole.emplace_back(role.m_miRoleId, role.m_mode);
	return roleInfo;
}

#define	headerBlock 0x002330
#define	dataBlock 0x002350
#define	childBlock 0x002370

bool ibValueMetaObject::Init()
{
	// always false
	return false;
}

bool ibValueMetaObject::Init(ibValue** paParams, const long lSizeArray)
{
	if (lSizeArray < 1)
		return false;

	ibValueMetaObject* parent = nullptr;
	if (paParams[0]->ConvertToValue(parent)) {
		if (parent == nullptr)
			return true;
		// Check acceptance BEFORE attaching: with owning children a rejected node
		// would already sit in the parent's vector when CreateObjectRef wxDELETEs
		// it on Init failure → double free. Reject first, attach only if accepted.
		if (!parent->FilterChild(GetClassType()))
			return false;
		SetParent(parent);
		parent->AddChild(this);
		return true;
	}

	return false;
}

bool ibValueMetaObject::IsEditable() const
{
	if (!IsEnabled() || IsDeleted())
		return false;

	ibBackendMetadataTree* const metaTree = m_metaData->GetMetaTree();
	if (metaTree != nullptr)
		return metaTree->IsEditable();

	return m_parent != nullptr ?
		m_parent->IsEditable() : true;
}

bool ibValueMetaObject::CompareObject(const ibValueMetaObject* compareObject) const
{
#pragma region _compare_fill_h_
	class ibControlComparator {
	public:

		static bool CompareObject(
			const ibValueMetaObject* compareObject1,
			const ibValueMetaObject* compareObject2
		)
		{
			if (compareObject2 == nullptr)
				return false;

			if (compareObject1->GetClassType() != compareObject2->GetClassType())
				return false;

			if (compareObject1->GetMetaID() != compareObject2->GetMetaID())
				return false;

			for (unsigned int idx = 0; idx < compareObject1->GetPropertyCount(); idx++) {

				const ibProperty* propDst = compareObject1->GetProperty(idx);
				wxASSERT(propDst);

				const ibProperty* propSrc = compareObject2->GetProperty(propDst->GetName());

				if (propSrc == nullptr)
					return false;

				if (propDst->GetValue() != propSrc->GetValue())
					return false;
			}

			return (compareObject1->GetPropertyCount() == compareObject2->GetPropertyCount() && compareObject1->GetEventCount() == compareObject2->GetEventCount()) &&
				compareObject1->GetChildCount() == compareObject2->GetChildCount();
		}
	};

#pragma endregion 

	return ibControlComparator::CompareObject(this, compareObject);
}

bool ibValueMetaObject::CopyObject(ibWriterMemory& writer) const
{
#pragma region _copy_guard_h_

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

	ibControlCopyGuard controlCopyGuard(this);

#pragma endregion 

	wxASSERT(m_metaCopyGuid.isValid());

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

	return ibControlMemoryWriter::CopyObject(this, writer);
}

bool ibValueMetaObject::PasteObject(ibReaderMemory& reader)
{
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

			std::shared_ptr <ibReaderMemory> readerChildMemory(reader.open_chunk(childBlock));
			if (readerChildMemory != nullptr) {
				ibReaderMemory* prevReaderMemory = nullptr;
				do {
					ibClassID clsid = 0;
					ibReaderMemory* readerMemory = readerChildMemory->open_chunk_iterator(clsid, &*prevReaderMemory);
					if (readerMemory == nullptr)
						break;
					if (clsid > 0) {
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
							// Don't drop silently: a child the target owner cannot host is a real
							// mismatch worth a log, not a quietly-incomplete paste.
							ibJournalWarning(wxT("metadata"),wxT("PasteObject: child clsid %u not accepted by target owner - skipped"), (unsigned int)clsid);
						}
					}
					prevReaderMemory = readerMemory;
				} while (true);
			}
			
			return true;
		}

	public:

		// Recursively clear the paste marks once the whole tree is pasted, run and its forms re-homed — the mirror of
		// ibControlCopyGuard::Erase. RAII from the public PasteObject.
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
			// run so LoadControl routes to PasteNode, and the OUTER ibControlPasteGuard clears it on exit.
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

			std::shared_ptr <ibReaderMemory> readerChildMemory(reader.open_chunk(childBlock));
			if (readerChildMemory != nullptr) {
				ibReaderMemory* prevReaderMemory = nullptr;
				do {
					ibClassID clsid = 0;
					ibReaderMemory* readerMemory = readerChildMemory->open_chunk_iterator(clsid, &*prevReaderMemory);
					if (readerMemory == nullptr)
						break;
					if (clsid > 0) {
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
							// Don't drop silently: a child the target owner cannot host is a real
							// mismatch worth a log, not a quietly-incomplete paste.
							ibJournalWarning(wxT("metadata"),wxT("PasteObject: child clsid %u not accepted by target owner - skipped"), (unsigned int)clsid);
						}
					}
					prevReaderMemory = readerMemory;
				} while (true);
			}

			return pasteObject->OnReloadMetaObject();
		}
	};

#pragma endregion

	// RAII: whatever the outcome, clear the paste marks once the whole tree is pasted, run and its forms re-homed —
	// the mirror of ibControlCopyGuard erasing the copy marks after CopyObject.
	struct ibControlPasteGuard {
		const ibValueMetaObject* m_pasteObject;
		~ibControlPasteGuard() { ibControlMemoryReader::ErasePasteGuid(m_pasteObject); }
	} pasteGuard{ this };

	return ibControlMemoryReader::PasteAndRunObject(this, reader);
}

void ibValueMetaObject::SetName(const wxString& strName)
{
	m_propertyName->SetValue(strName);

	// EVERY ctor of this metaobject now computes a different name than the one it is filed under.
	// Say so and nothing more — the registry recomputes the whole view on the next lookup by name.
	// The object inspector's rename does not come through here (it writes the property and then
	// calls OnPropertyChanged, which says the same thing); this covers the PROGRAMMATIC renames —
	// BuildNewName on paste, GetNewName on create, the tree's Save — which previously left the
	// cache pointing a stale name at a live ctor.
	if (m_metaData != nullptr)
		m_metaData->InvalidateCtorNames();
}

bool ibValueMetaObject::ChangeChildPosition(ibValueMetaObject* object, unsigned int pos)
{
	m_metaData->Modify(true);
	return ibPropertyObjectHelper::ChangeChildPosition(object, pos);
}

wxString ibValueMetaObject::GetModuleName() const
{
	ibValueMetaObject* parent = GetParent();
	//wxASSERT(parent);
	if (parent != nullptr) {
		return parent->GetName() + wxT(": ") + GetName();
	}
	return GetName();
}

wxString ibValueMetaObject::GetFullName() const
{
	wxString strFullName;

	ibValueMetaObject* parent = GetParent();

	if (parent != nullptr && g_metaModuleCLSID != GetClassType() && g_metaManagerCLSID != GetClassType())
		strFullName = strFullName + GetClassName() + '.' + GetName();
	else
		strFullName = GetName();

	while (parent != nullptr) {
		if (g_metaCommonMetadataCLSID != parent->GetClassType()) {
			strFullName = parent->GetClassName() + '.' +
				parent->GetName() + '.' +
				strFullName;
		}
		parent = parent->GetParent();
	}

	return strFullName;
}

wxString ibValueMetaObject::GetFileName() const
{
	return m_metaData->GetFileName();
}

//****************************************************************************
//*                              Support methods                             *
//****************************************************************************

void ibValueMetaObject::FillMembers(ibMemberTable& helper) const
{
	for (unsigned idx = 0; idx < ibPropertyObject::GetPropertyCount(); idx++) {
		ibProperty* property = ibPropertyObject::GetProperty(idx);
		if (property == nullptr) continue;
		helper.AppendProp(property->GetName(), true, false, idx);
	}
}

bool ibValueMetaObject::SetPropVal(const long lPropNum, const ibValue& varPropVal)
{
	ibProperty* property = GetPropertyByIndex(lPropNum);
	if (property != nullptr) return property->SetDataValue(varPropVal);
	return false;
}

bool ibValueMetaObject::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	const ibProperty* property = GetPropertyByIndex(lPropNum);
	if (property != nullptr) return property->GetDataValue(pvarPropVal);
	return false;
}

#include "backend/backend_exception.h"
#include "backend/session/session.h"
#include "backend/session/sessionRegistry.h"

namespace {
	// True if the most recent RequireExclusiveForDDL on this thread
	// auto-acquired exclusive mode. ReleaseAutoExclusive uses it to know
	// whether to drop the flag (don't drop if the caller had it before).
	thread_local bool ts_acquiredByGate = false;
}

void ibRestructureInfo::RequireExclusiveForDDL()
{
	ts_acquiredByGate = false;

	// Already exclusive? Caller (or previous gate) holds it. Don't touch.
	if (appData->ExclusiveMode()) return;

	// Bootstrap path — no current session / no registry yet (e.g. first-open
	// auto-save triggered by m_configNew during LoadDatabase before the
	// session is attached). Nobody else can be connected at this stage, so
	// skip the gate entirely.
	auto* session  = ibSession::Current();
	auto* registry = ibApplicationData::GetSessionRegistry();
	if (session == nullptr || registry == nullptr) return;

	// Normal apply — try to acquire exclusive. Succeeds only if we are the
	// sole live session in the cluster; holds the flag for the rest of the
	// apply, blocking newcomers.
	const auto verdict = registry->SetExclusive(session, true);
	if (verdict == ibSession::ibExclusiveResult::Granted) {
		ts_acquiredByGate = true;
		return;
	}

	ibBackendCoreException::Error(
		_("Structure (DDL) changes require exclusive mode. Other sessions "
		  "are connected - disconnect them and try again. "
		  "Code-only changes (modules, forms) can be saved without it."));
}

void ibRestructureInfo::ReleaseAutoExclusive()
{
	if (!ts_acquiredByGate) return;
	ts_acquiredByGate = false;
	auto* session  = ibSession::Current();
	auto* registry = ibApplicationData::GetSessionRegistry();
	if (session != nullptr && registry != nullptr) {
		registry->SetExclusive(session, false);
	}
}

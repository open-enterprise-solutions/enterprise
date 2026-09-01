////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : metaobject
////////////////////////////////////////////////////////////////////////////

#include "metaObject.h"
#include "backend/appData.h"
#include "backend/metadataConfiguration.h"   // ibMetaDataConfigurationBase::GetRestructureInfo (the static ledger accessor)

#include "backend/metaData.h"
#include "backend/utils/debugTrace.h"   // ibDebugTraceEnabled — per-object id tracing is opt-in
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

		// ⭐ THROUGH THE RENAME DOOR, not through SetName (Max, 2026-09-01: *"the name change should
		// probably go through rename"*). SetName only writes the field; RenameMetaObject is the whole
		// act — it runs OnRenameMetaObject, which brings the rest of the configuration into step with
		// a name that is about to be taken (a common attribute rewrites its copies, a common module is
		// re-keyed in the module storage), and it ANNOUNCES the change.
		//
		// 🛑 THAT ANNOUNCEMENT IS WHAT WAS MISSING. A paste says `Created` from inside
		// CreateMetaObject — before the copied data is read and before this runs — so a watcher drew
		// the row under the name NewItem handed out, and the bump to `Warehouse1` arrived through
		// SetName, which tells nobody (Max: *"the value is set, but the tree shows the primary name"*).
		//
		// ⚠ The uniqueness test inside cannot refuse this one: GetNewName has just built a name that
		// nothing else carries.
		m_metaData->RenameMetaObject(this, metaName);

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
	// ⚠ OFF UNLESS ASKED FOR (`OES_TRACE_METAIDS=1`). This fires for EVERY object of every
	// configuration load — two hundred lines before the first window appears — and a journal whose
	// first half is a list of things that always happen is a journal whose second half nobody
	// reaches. The counter it exists for is still one variable away when it is needed.
	static const bool s_traceIds = ibDebugTraceEnabled("OES_TRACE_METAIDS");
	if (s_traceIds)
		ibJournalInfo(wxT("metadata"), wxT("id %i -> %s"), GetMetaID(), GetClassName());
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
	// 🛑 THE EDITORS ARE NOT CLOSED HERE ANY MORE, and they never needed to be. This hook runs on
	// two roads, and BOTH of them already say so: a deleted object broadcasts `Removed` one call
	// earlier (ibMetaData::RemoveMetaObject) and a watcher shuts what it was showing of it; a whole
	// container being let go broadcasts `Closed` before the teardown starts. Closing per NODE was a
	// third road to the same state — it ran once for every object in a configuration on the way out,
	// to do what one signal does once.
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

	// Asked of the metadata, which asks everyone watching — any one that can edit is enough, and
	// nobody watching means nothing restricts it. The walk up to the parent is gone with the same
	// change: it was the fallback for "no tree installed", and an empty list already answers that.
	return m_metaData != nullptr ? m_metaData->IsEditable() : true;
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

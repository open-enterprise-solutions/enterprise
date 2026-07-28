////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : ibValueRecordDataObjectRef — instance Read / Save /
//	              Delete on the runtime-data side. Routes through
//	              ses_query (session's holder) so writes join the outer
//	              document-save TX. Includes the GenerateNextIdentifier
//	              static utility used by catalogs / documents to allocate
//	              codes through sys_sequence.
////////////////////////////////////////////////////////////////////////////

#include "commonObject.h"

#include "backend/appData.h"
#include "backend/session/session.h"
#include "backend/databaseLayer/connectionPool.h"
#include "backend/databaseLayer/connectionScope.h"
#include "backend/databaseLayer/databaseErrorCodes.h"
#include "backend/backend_form.h"
#include "backend/logger/logger.h"

#include "backend/metaCollection/partial/tabularSection/tabularSection.h"
#include "backend/metaCollection/partial/reference/reference.h"
#include "backend/metaCollection/attribute/metaAttributeObject.h"
#include "backend/query/dataQueryBuilder.h"      // L3 door — record read / write / delete via the universal entry

#include "backend/system/systemManager.h"
#include "backend/backend_exception.h"
#include "backend/utils/dataVersion.hpp"
#include "backend/lock/lockManager.h"

bool ibValueRecordDataObjectRef::ReadData()
{
	return ReadData(m_objGuid);
}

bool ibValueRecordDataObjectRef::ReadData(const ibGuid& srcGuid)
{
	if (m_newObject && !srcGuid.isValid())
		return false;
	wxASSERT(m_metaObject);

	// (half-)L3 read: load the row by its OWN key through the universal door.
	// The dialect FIRST/LIMIT fork is closed by L2; the default door pulls the
	// session holder, so this still joins any open document-save TX (the old
	// ses_query path). No TableExists probe — the table is a hard precondition.
	ibDataQueryBuilder readQuery;
	readQuery.From(m_metaObject->GetQueryable()).WhereKey(srcGuid);
	ibReadPageRequest page;
	page.m_count = 1;

	bool succes = false;
	ibDataQueryResult selection = readQuery.Execute(page);
	if (selection.Next()) {
		succes = true;
		//load other attributes
		for (const auto object : m_metaObject->GetGenericAttributeArrayObject()) {
			if (!m_metaObject->IsDataReference(object->GetMetaID()))
				m_listObjectValue[object->GetMetaID()] = selection.GetValue(object);
		}
		for (const auto object : m_metaObject->GetGenericTableArrayObject()) {
			ibValueTabularSectionDataObjectRef* tabularSection = new ibValueTabularSectionDataObjectRef(this, object);
			if (!tabularSection->LoadData(srcGuid)) succes = false;
			m_listObjectValue.insert_or_assign(object->GetMetaID(), tabularSection);
		}
	}
	// selection's destructor closes the cursor + statement (RAII).

	// Capture DataVersion at Read time — the Write/Delete path's
	// LockAndCheckDataVersion compares this against the row's current
	// version to detect concurrent updates. New objects (no row yet)
	// leave m_loadedDataVersion empty so the check is skipped on first
	// Save. See docs/record-locks.md.
	if (succes && !m_newObject)
		CaptureLoadedDataVersion();
	return succes;
}

bool ibValueRecordDataObjectRef::TryAcquireFormLock(ibLockMode mode)
{
	if (m_newObject)            return true;  // no DB row to identify yet
	if (m_formLockHandle.IsValid()) return true;  // already held

	// Default options — wait at driver's normal lock-timeout. Throws
	// ibBackendLockException::LockConflict if another session holds an
	// incompatible lock; caller propagates as form-open failure.
	auto* lm = ibApplicationData::GetLockManager();
	if (lm == nullptr)
		ibBackendCoreException::Error(_("Lock manager not initialised"));
	m_formLockHandle = lm->Acquire({
		ibLockItem::ForRef(m_metaObject->GetDocPath(), m_objGuid, mode)
	});
	return true;
}

bool ibValueRecordDataObjectRef::LockAndCheckDataVersion(bool bump)
{
	const auto dvAttr = m_metaObject != nullptr ? m_metaObject->GetDataVersion() : nullptr;

	// No DataVersion attribute declared on this metaobject (shouldn't
	// happen for mutable-refs — DataVersion is hard-wired in
	// ibValueMetaObjectRecordDataMutableRef — but stay defensive so
	// future metaobject kinds that opt out don't trip an assert).
	if (dvAttr == nullptr)
		return true;

	const ibMetaID dvId = dvAttr->GetMetaID();

	// Existing rows only — new ones have no DB row to lock and no
	// prior version to compare. The bump still fires below so the
	// freshly-inserted row carries a valid initial stamp.
	if (!m_newObject && !m_loadedDataVersion.IsEmpty()) {
		// Row lock + current-version read in ONE L3 selection: From(record).Where(uuid) with the
		// page FOR-UPDATE flag (the dialect appends its row-lock clause via m_rowLockSuffix). Runs on
		// the session holder — the SAME connection as the open write-scope TX — so the lock is held
		// until commit. The version reads through GetValue(dvAttr), which assembles the attribute
		// from its physical fields. DataVersion is NOT a single column: its SQL field name is the
		// COMPOSITE "<fld>_TYPE,<fld>_S" (type tag + string data), so the former raw
		// GetResultString(that) failed "field not found" — the provider's attribute assembly is the
		// only correct read. (docs/record-locks.md)
		ibDataQueryBuilder q;
		q.WithAccessPolicy(nullptr)   // row LOCK + version read is a physical concurrency op, not a user read:
		 .From(m_metaObject->GetQueryable())   // lock the RAW row regardless of RLS visibility (RLS is enforced by
		 .Where(ibRawDBColumn::String(wxT("uuid")), ibValue(wxString(m_objGuid)));   // the guarded UPDATE below)
		ibReadPageRequest page;
		page.m_count         = 1;
		page.m_lockForUpdate = true;
		ibDataQueryResult sel = q.Execute(page);

		const bool rowFound = sel.Next();
		const wxString dbVer = rowFound ? sel.GetValue(dvAttr).GetString() : wxString();

		// Row disappeared between our load and write — somebody else
		// committed a DELETE. Treat as a version conflict for UX
		// consistency: same "reload and retry" recovery path.
		if (!rowFound) {
			ibBackendLockException::VersionChangedThrow(
				m_metaObject->GetSynonym(),
				m_loadedDataVersion,
				wxT("<deleted>"));
		}

		if (dbVer != m_loadedDataVersion) {
			ibBackendLockException::VersionChangedThrow(
				m_metaObject->GetSynonym(),
				m_loadedDataVersion,
				dbVer);
		}
	}

	// Bump for Write paths so SaveData's UPSERT stamps the row with a
	// fresh version. Skipped on Delete (the row is going away). The new
	// stamp goes into the ATTRIBUTE slot only — m_loadedDataVersion is
	// advanced later by CommitWriteScope, once the row is durable. If we
	// advanced the marker here and the Write then rolled back (RLS deny,
	// SaveData failure, BeforeWrite/OnWrite cancel), the DB row would keep
	// its old version while the marker held the new one, so the very next
	// Save would read a mismatch and wrongly demand "reopen the form".
	// The back-to-back script-Write case (obj.Write(); obj.Write();) stays
	// consistent because the first Write's commit syncs the marker before
	// the second Write's check runs.
	if (bump) {
		const wxString newStamp = ibDataVersion::NewStamp();
		SetValueByMetaID(dvId, ibValue(newStamp));
	}

	return true;
}

void ibValueRecordDataObjectRef::CaptureLoadedDataVersion()
{
	const auto dvAttr = m_metaObject != nullptr ? m_metaObject->GetDataVersion() : nullptr;
	if (dvAttr == nullptr)
		return;
	const auto it = m_listObjectValue.find(dvAttr->GetMetaID());
	if (it != m_listObjectValue.end())
		m_loadedDataVersion = it->second.GetString();
}

//----------------------------------------------------------------------
// Phase A scaffold helpers — pre/post bookkeeping extracted from the
// per-subclass WriteObject / DeleteObject bodies. Lives here next to
// the lock methods (TryAcquireFormLock / LockAndCheckDataVersion)
// they call into — the helpers themselves are lock-orchestration over
// the DB-query primitives, so this is their natural home.
// See commonObject.h ibValueRecordDataObjectRef Begin*/Commit* docs.
//----------------------------------------------------------------------

bool ibValueRecordDataObjectRef::BeginWriteScope(ibConnectionScope& scope)
{
	if (appData->DesignerMode())          return false;   // designer skip
	if (!scope || !scope->IsOpen())
		ibBackendCoreException::Error(_("Database is not open!"));
	if (ibBackendException::IsEvalMode()) return false;   // eval skip

	if (!m_metaObject->AccessRight_Write()) {
		ibBackendAccessException::Error();   // throws — return for warning silence
		return false;
	}

	scope.SafeBeginTransaction();
	TryAcquireFormLock();                                  // soft-lock re-attempt
	LockAndCheckDataVersion(/*bump=*/true);                // row lock + version + bump
	return true;
}

bool ibValueRecordDataObjectRef::BeginDeleteScope(ibConnectionScope& scope)
{
	if (appData->DesignerMode())          return false;
	if (!scope || !scope->IsOpen())
		ibBackendCoreException::Error(_("Database is not open!"));
	if (ibBackendException::IsEvalMode()) return false;

	if (!m_metaObject->AccessRight_Delete()) {
		ibBackendAccessException::Error();
		return false;
	}

	scope.SafeBeginTransaction();
	TryAcquireFormLock();
	LockAndCheckDataVersion(/*bump=*/false);               // no bump — row going away
	return true;
}

void ibValueRecordDataObjectRef::CommitWriteScope(ibConnectionScope& scope,
                                                   ibBackendValueForm* valueForm,
                                                   bool newObject)
{
	scope.SafeCommitTransaction();

	// The row is durable — NOW advance the in-memory version marker to the
	// stamp we just committed (LockAndCheckDataVersion wrote it into the
	// DataVersion attribute before SaveData). This is the ONLY place the
	// marker moves forward, so a failed/rolled-back write leaves it matching
	// the unchanged DB row and the next Save retries cleanly instead of
	// demanding a form reopen. See docs/record-locks.md.
	CaptureLoadedDataVersion();

	// Audit AFTER SafeCommitTransaction — the row is durable. If the
	// commit threw, RAII rollback fires and we never get here. Source
	// "record" is universal (Catalog / Document / ChartOf* all route
	// through this scaffold); ref_guid + ref_meta_id let the viewer
	// drill back to the actual object that was written.
	if (ibLog && ibLog->IsEnabled(ibLogLevel::Audit)) {
		const wxString refGuid = m_reference_impl
			? ibGuid(m_reference_impl->m_guid).str()
			: wxString();
		const int refMetaId = m_reference_impl
			? static_cast<int>(m_reference_impl->GetMetaID())
			: 0;
		ibLog->Audit(wxT("record"),
		             newObject ? wxT("created") : wxT("saved"),
		             GetSourceCaption(),
		             refGuid,
		             refMetaId);
	}

	if (valueForm != nullptr) {
		if (newObject) valueForm->NotifyCreate(GetReference());
		else           valueForm->NotifyChange(GetReference());
	}
	m_objModified = false;
}

void ibValueRecordDataObjectRef::CommitDeleteScope(ibConnectionScope& scope,
                                                    ibBackendValueForm* valueForm)
{
	scope.SafeCommitTransaction();

	if (ibLog && ibLog->IsEnabled(ibLogLevel::Audit)) {
		const wxString refGuid = m_reference_impl
			? ibGuid(m_reference_impl->m_guid).str()
			: wxString();
		const int refMetaId = m_reference_impl
			? static_cast<int>(m_reference_impl->GetMetaID())
			: 0;
		ibLog->Audit(wxT("record"), wxT("deleted"),
		             GetSourceCaption(), refGuid, refMetaId);
	}

	if (valueForm != nullptr)
		valueForm->NotifyDelete(GetReference());
}

bool ibValueRecordDataObjectRef::SaveData()
{
	//check fill attributes — find() so the probe doesn't auto-insert
	//an empty value into m_listObjectValue.
	bool fillCheck = true;
	wxASSERT(m_metaObject);
	for (const auto object : m_metaObject->GetGenericAttributeArrayObject()) {
		if (!object->FillCheck()) continue;
		const auto it = m_listObjectValue.find(object->GetMetaID());
		if (it == m_listObjectValue.end() || it->second.IsEmpty()) {
			wxString fillError =
				wxString::Format(_("""%s"" is a required field"), object->GetSynonym());
			ibValueSystemFunction::Message(fillError, ibStatusMessage::ibStatusMessage_Information);
			fillCheck = false;
		}
	}

	if (!fillCheck)
		return false;

	m_objGuid = m_reference_impl->m_guid;

	// WRITE the main row through the L3 door — BY COLUMN, no statement, no positions visible
	// here: From(meta) + SetValue(col, value)* + Insert()/Update(). uuid is the row-key — a RAW primary
	// string column (the guid bound straight, no translation); the attributes are metadata
	// columns. The provider decomposes each attribute into its physical columns and binds into
	// a hidden L2 statement; the per-DBMS UPSERT spelling is closed by the dialect template;
	// the match key is the queryable's row-key (uuid). The write goes through the session
	// holder, so it joins the outer document-save TX.
	ibDataQueryBuilder writer;
	writer.From(m_metaObject->GetQueryable())
	      .SetValue(ibRawDBColumn::String(wxT("uuid")), ibValue(m_objGuid));   // row-key value
	for (const auto object : m_metaObject->GetGenericAttributeArrayObject()) {
		ibValue value;
		if (m_metaObject->IsDataReference(object->GetMetaID())) {
			// The row's OWN reference, written in the SAME binary form (_RTRef +
			// _RRRef = ibReference[guid][metaID]) as any reference TO this row. That
			// byte-identity is what makes a dot-walk JOIN equate
			// source.fldX_RRRef = target.<selfref>_RRRef. CreateRaw skips PrepareRef
			// (we only need the reference identity bytes, not materialised members).
			value = ibValue(ibValueReferenceDataObject::CreateRaw(m_metaObject, m_objGuid));
		}
		else {
			const auto it = m_listObjectValue.find(object->GetMetaID());
			if (it != m_listObjectValue.end())
				value = it->second;
		}
		writer.SetValue(object, value);
	}
	// The object already knows the event: a NEW object is a CREATE — plain INSERT; an EXISTING object is a
	// REWRITE — a guarded UPDATE (WHERE key AND the folded RLS predicate), so a write Restrict actually bites
	// (a plain UPSERT has no WHERE and would ignore the folded predicate). new=true -> Insert, new=false ->
	// Update; creation and write stay separate events, and the rewrite enforces the row filter.
	bool hasError = m_newObject ? !writer.Insert() : !writer.Update();

	//table parts
	if (!hasError) {
		for (const auto object : m_metaObject->GetGenericTableArrayObject()) {
			ibValueTabularSectionDataObjectBase* tabularSection = nullptr;
			if (m_listObjectValue[object->GetMetaID()].ConvertToValue(tabularSection)) {
				if (!tabularSection->SaveData()) {
					hasError = true;
					break;
				}
			}
			else if (m_listObjectValue[object->GetMetaID()].GetType() != TYPE_NULL) {
				hasError = true;
				break;
			}
		}
	}

	if (!hasError) {
		m_newObject = false;
	}

	return !hasError;
}

bool ibValueRecordDataObjectRef::DeleteData()
{
	if (m_newObject)
		return true;
	//table parts
	for (const auto object : m_metaObject->GetTableArrayObject()) {
		ibValueTabularSectionDataObjectBase* tabularSection = nullptr;
		if (m_listObjectValue[object->GetMetaID()].ConvertToValue(tabularSection)) {
			if (!tabularSection->DeleteData())
				return false;
		}
		else if (m_listObjectValue[object->GetMetaID()].GetType() != TYPE_NULL)
			return false;

	}
	// Delete the main row through the L3 door — WHERE uuid = <guid>, no statement / L2 visible.
	// Best-effort like the prior path (it ignored the result).
	ibDataQueryBuilder()
		.From(m_metaObject->GetQueryable())
		.Where(ibRawDBColumn::String(wxT("uuid")), ibValue(m_objGuid))
		.Delete();
	return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool ibValueRecordDataObjectRef::IsSetUniqueIdentifier() const
{
	const auto object = m_metaObject->GetAttributeForCode();
	if (object != nullptr)
		return !m_listObjectValue.at(object->GetMetaID()).IsEmpty();
	return false;
}

bool ibValueRecordDataObjectRef::GenerateUniqueIdentifier(const wxString& strPrefix)
{
	const auto object = m_metaObject->GetAttributeForCode();
	if (object != nullptr && !IsSetUniqueIdentifier()) {
		m_listObjectValue.insert_or_assign(object->GetMetaID(),
			GenerateNextIdentifier(object, strPrefix));
		return true;
	}
	return false;
}

bool ibValueRecordDataObjectRef::ResetUniqueIdentifier()
{
	const auto object = m_metaObject->GetAttributeForCode();
	if (object != nullptr && IsSetUniqueIdentifier()) {
		m_listObjectValue.insert_or_assign(object->GetMetaID(), object->CreateValue());
		return true;
	}
	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool ibValueRecordDataObjectHierarchyRef::ReadData()
{
	if (ibValueRecordDataObjectRef::ReadData()) {
		const ibValueMetaObjectRecordDataHierarchyMutableRef* metaFolder = GetMetaObject();
		wxASSERT(metaFolder);
		ibValue isFolder; ibValueRecordDataObjectHierarchyRef::GetValueByMetaID(*metaFolder->GetDataIsFolder(), isFolder);
		if (isFolder.GetBoolean())
			m_objMode = ibObjectMode::OBJECT_FOLDER;
		else
			m_objMode = ibObjectMode::OBJECT_ITEM;
		return true;
	}
	return false;
}

bool ibValueRecordDataObjectHierarchyRef::ReadData(const ibGuid& srcGuid)
{
	if (ibValueRecordDataObjectRef::ReadData(srcGuid)) {
		const ibValueMetaObjectRecordDataHierarchyMutableRef* metaFolder = GetMetaObject();
		wxASSERT(metaFolder);
		ibValue isFolder; ibValueRecordDataObjectHierarchyRef::GetValueByMetaID(*metaFolder->GetDataIsFolder(), isFolder);
		if (isFolder.GetBoolean())
			m_objMode = ibObjectMode::OBJECT_FOLDER;
		else
			m_objMode = ibObjectMode::OBJECT_ITEM;
		return true;
	}
	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ibValueRecordDataObjectRef::SetDeletionMark(bool deletionMark)
{
	if (m_newObject)
		return;

	if (m_metaObject != nullptr) {
		ibValueMetaObjectAttributePredefined* attributeDeletionMark = m_metaObject->GetDataDeletionMark();
		wxASSERT(attributeDeletionMark);
		ibValueRecordDataObjectRef::SetValueByMetaID(*attributeDeletionMark, deletionMark);
	}

	SaveModify();
}


//**********************************************************************************************************
//*                                          Code generator												   *
//**********************************************************************************************************

ibValue ibValueRecordDataObjectRef::GenerateNextIdentifier(ibValueMetaObjectAttributeBase* attribute, const wxString& strPrefix)
{
	wxASSERT(attribute);

	// Connection comes from the active session (instance method = guaranteed
	// session context); the increment shares its TX with the outer document
	// save so rollback rolls back both — no orphan sequence values on
	// failure. ses_query throws if there's no current session or its
	// connection is closed.
	const auto db = ses_query;   // shared_ptr<ibDatabaseLayer>

	const int driver = db->GetDatabaseLayerType();
	if (driver != DATABASELAYER_FIREBIRD && driver != DATABASELAYER_POSTGRESQL)
		ibBackendCoreException::Error(_("GenerateNextIdentifier requires Firebird or PostgreSQL"
			" - atomic UPDATE...RETURNING is not portable to other backends."));

	// Period bucket — parking value for now; per-type periodicity (catalog
	// vs document) is a future feature.
	const int interval = 20000101;

	const ibTypeDescription& typeDesc = attribute->GetTypeDesc();

	// Bootstrap helper: when sys_sequence has no row yet for this key,
	// scan the actual data table for the highest existing counter value
	// matching strPrefix so the first-ever sequence INSERT doesn't
	// collide with imported / migrated records. Returns 0 on empty.
	// GetSQLFieldName() returns the composite "<fld>_TYPE,<fld>_<X>"
	// shape used by SaveData; we want the raw per-type sub-column.
	auto scanDataMax = [&]() -> ibNumber {
		const wxString tableName = m_metaObject->GetPhysicalTableName();
		const wxString fieldBase = attribute->GetPhysicalName();
		ibNumber maxFound = 0;
		if (attribute->ContainType(ibValueTypes::TYPE_NUMBER)) {
			const wxString numField = fieldBase + wxT("_N");
			ibDatabaseResultSet* rs = db->RunQueryWithResults(
				wxT("SELECT MAX(%s) FROM %s;"), numField, tableName);
			if (rs != nullptr) {
				if (rs->Next() && !rs->IsFieldNull(1))
					maxFound = rs->GetResultNumber(1);
				db->CloseResultSet(rs);
			}
		} else if (attribute->ContainType(ibValueTypes::TYPE_STRING)) {
			const wxString strField = fieldBase + wxT("_S");
			ibStatementGuard st(db, db->PrepareStatement(
				wxT("SELECT MAX(%s) FROM %s WHERE %s LIKE ?;"),
				strField, tableName, strField));
			if (st) {
				st->SetParamString(1, strPrefix + wxT("%"));
				ibDatabaseResultSet* rs = st->RunQueryWithResults();
				if (rs != nullptr) {
					if (rs->Next() && !rs->IsFieldNull(1)) {
						const wxString maxCode = rs->GetResultString(1);
						if (maxCode.length() > strPrefix.length()) {
							long parsed = 0;
							maxCode.Mid(strPrefix.length()).ToLong(&parsed);
							if (parsed > 0) maxFound = parsed;
						}
					}
					db->CloseResultSet(rs);
				}
			}
		}
		return maxFound;
	};

	// FB / PG: single-statement UPDATE...RETURNING is atomic. The row
	// is locked, incremented and returned in one shot — concurrent
	// sessions (incl. cross-machine) sequentialise through the DB row
	// lock with no race window between SELECT and UPSERT. Two attempts:
	//   1) UPDATE...RETURNING — row exists → done.
	//   2) Bootstrap-scan + INSERT. INSERT may PK-conflict if another
	//      session inserted first; the next loop iteration's UPDATE arm
	//      then succeeds with the racing session's value + 1.
	ibNumber resultCode = 1;
	bool gotCode = false;
	for (int attempt = 0; attempt < 2 && !gotCode; ++attempt) {
		ibStatementGuard upd(db, db->PrepareStatement(
			wxT("UPDATE %s SET number = number + 1 WHERE interval = ? AND meta_guid = ? AND prefix = ? RETURNING number;"),
			sequence_table));
		if (upd) {
			upd->SetParamInt(1, interval);
			upd->SetParamString(2, m_metaObject->GetDocPath());
			upd->SetParamString(3, strPrefix);
			ibDatabaseResultSet* rs = upd->RunQueryWithResults();
			if (rs != nullptr) {
				if (rs->Next() && !rs->IsFieldNull(1)) {
					resultCode = rs->GetResultNumber(1);
					gotCode = true;
				}
				db->CloseResultSet(rs);
			}
		}
		if (gotCode) break;

		// Row missing — bootstrap from data table and INSERT.
		const ibNumber candidate = scanDataMax() + 1;
		ibStatementGuard ins(db, db->PrepareStatement(
			wxT("INSERT INTO %s (interval, meta_guid, prefix, number) VALUES (?, ?, ?, ?);"),
			sequence_table));
		if (ins) {
			ins->SetParamInt(1, interval);
			ins->SetParamString(2, m_metaObject->GetDocPath());
			ins->SetParamString(3, strPrefix);
			ins->SetParamNumber(4, candidate);
			if (ins->RunQuery() != DATABASE_LAYER_QUERY_RESULT_ERROR) {
				resultCode = candidate;
				gotCode = true;
			}
			// else: PK conflict from a racing session's bootstrap
			// → loop, UPDATE arm now succeeds.
		}
	}

	if (!gotCode) return attribute->CreateValue();

	if (attribute->ContainType(ibValueTypes::TYPE_NUMBER))
		return resultCode;

	if (attribute->ContainType(ibValueTypes::TYPE_STRING)) {
		// "<prefix><zero-padded number>" — width derived from typeDesc.
		// Defensive else: if length was shrunk in the designer below
		// what the prefix already takes, drop the prefix and pad the
		// number to prefix-width so the output keeps a stable size.
		// Uses ibNumber::Format directly — no int64 cap, big counters OK.
		const size_t prefLen = strPrefix.length();
		const size_t totLen  = (size_t)typeDesc.GetLength();
		ibNumber::Format fmt;
		fmt.minIntDigits = (int)((prefLen < totLen) ? totLen - prefLen : prefLen);
		return (prefLen < totLen ? strPrefix : wxString()) + resultCode.ToString(fmt);
	}

	wxASSERT_MSG(false, "m_metaAttribute->GetClsidList() != ibValueTypes::TYPE_NUMBER"
		"|| m_metaAttribute->GetClsidList() != ibValueTypes::TYPE_STRING");

	return wxEmptyValue;
}

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
#include "backend/databaseLayer/databaseErrorCodes.h"

#include "backend/metaCollection/partial/tabularSection/tabularSection.h"
#include "backend/metaCollection/attribute/metaAttributeObject.h"

#include "backend/system/systemManager.h"

bool ibValueRecordDataObjectRef::ReadData()
{
	return ReadData(m_objGuid);
}

bool ibValueRecordDataObjectRef::ReadData(const ibGuid& srcGuid)
{
	const auto db = ses_query;   // session-bound conn; throws if no active session

	if (m_newObject && !srcGuid.isValid())
		return false;
	wxASSERT(m_metaObject);
	const wxString tableName = m_metaObject->GetTableNameDB();

	// No TableExists probe — table is created at metadata-apply time and
	// is a hard precondition for any descriptor data op. PrepareStatement
	// returns null on missing-table; we treat that as no data.
	const wxString sql = (db->GetDatabaseLayerType() == DATABASELAYER_FIREBIRD)
		? wxString("SELECT FIRST 1 * FROM " + tableName + " WHERE uuid = ?;")
		: wxString("SELECT * FROM " + tableName + " WHERE uuid = ? LIMIT 1;");
	ibStatementGuard st(db, db->PrepareStatement(sql));
	if (!st) return false;
	st->SetParamString(1, srcGuid.str());
	ibDatabaseResultSet* resultSet = st->RunQueryWithResults();
	if (resultSet == nullptr) return false;
	bool succes = false;
	if (resultSet->Next()) {
		succes = true;
		//load other attributes
		for (const auto object : m_metaObject->GetGenericAttributeArrayObject()) {
			if (!m_metaObject->IsDataReference(object->GetMetaID())) {
				ibValueMetaObjectAttributeBase::GetValueAttribute(object, m_listObjectValue[object->GetMetaID()], resultSet);
			}
		}
		for (const auto object : m_metaObject->GetGenericTableArrayObject()) {
			ibValueTabularSectionDataObjectRef* tabularSection = new ibValueTabularSectionDataObjectRef(this, object);
			if (!tabularSection->LoadData(srcGuid)) succes = false;
			m_listObjectValue.insert_or_assign(object->GetMetaID(), tabularSection);
		}
	}
	db->CloseResultSet(resultSet);
	return succes;
}

bool ibValueRecordDataObjectRef::SaveData()
{
	const auto db = ses_query;

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

	// UPSERT main row in one statement — replaces the previous DELETE +
	// INSERT pair (the outer DeleteData also cascade-DELETEd tabular
	// sections, but tabularSection->SaveData below already does its own
	// DELETE+INSERT, so the cascade was redundant work). Saves 1 main-row
	// RT plus N redundant tabular-DELETE RTs per save.
	const bool isFB = (db->GetDatabaseLayerType() == DATABASELAYER_FIREBIRD);
	const wxString& tableName = m_metaObject->GetTableNameDB();

	// Collect column names once — reused by UPSERT clauses below.
	wxString cols = "uuid";
	wxString placeholders = "?";
	for (const auto object : m_metaObject->GetGenericAttributeArrayObject()) {
		if (m_metaObject->IsDataReference(object->GetMetaID()))
			continue;
		cols += ", " + ibValueMetaObjectAttributeBase::GetSQLFieldName(object);
		const unsigned int fieldCount = ibValueMetaObjectAttributeBase::GetSQLFieldCount(object);
		for (unsigned int i = 0; i < fieldCount; i++)
			placeholders += ", ?";
	}

	wxString queryText;
	if (isFB) {
		// UPDATE OR INSERT — FB rewrites the row in place if PK matches.
		queryText = "UPDATE OR INSERT INTO " + tableName + " (" + cols
		          + ") VALUES (" + placeholders + ") MATCHING (uuid);";
	} else {
		// PG ON CONFLICT — assign every non-uuid column from EXCLUDED.
		// GetExcludeSQLFieldName(attr) emits "<col>=excluded.<col>" for
		// every sub-column of the attribute; reuse the same helper used
		// by SetConstValue.
		wxString assignments;
		for (const auto object : m_metaObject->GetGenericAttributeArrayObject()) {
			if (m_metaObject->IsDataReference(object->GetMetaID()))
				continue;
			if (!assignments.empty()) assignments += ", ";
			assignments += ibValueMetaObjectAttributeBase::GetExcludeSQLFieldName(object);
		}
		queryText = "INSERT INTO " + tableName + " (" + cols
		          + ") VALUES (" + placeholders
		          + ") ON CONFLICT (uuid) DO UPDATE SET " + assignments + ";";
	}

	ibStatementGuard statement(db, db->PrepareStatement(queryText));
	if (!statement)
		return false;

	m_objGuid = m_reference_impl->m_guid;
	statement->SetParamString(1, m_objGuid.str());

	int position = 2;

	for (const auto object : m_metaObject->GetGenericAttributeArrayObject()) {
		if (m_metaObject->IsDataReference(object->GetMetaID()))
			continue;
		ibValueMetaObjectAttributeBase::SetValueAttribute(
			object,
			m_listObjectValue.at(object->GetMetaID()),
			statement.get(),
			position
		);
	}

	bool hasError =
		statement->RunQuery() == DATABASE_LAYER_QUERY_RESULT_ERROR;

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
	const auto db = ses_query;

	if (m_newObject)
		return true;
	const wxString& tableName = m_metaObject->GetTableNameDB();
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
	ibStatementGuard del(db, db->PrepareStatement("DELETE FROM " + tableName + " WHERE uuid = ?;"));
	if (del) {
		del->SetParamString(1, m_objGuid.str());
		del->RunQuery();
	}
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
			" — atomic UPDATE...RETURNING is not portable to other backends."));

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
		const wxString tableName = m_metaObject->GetTableNameDB();
		const wxString fieldBase = attribute->GetFieldNameDB();
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
							if (parsed > 0) maxFound = static_cast<int64_t>(parsed);
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
	//   1) UPDATE...RETURNING — row exists в†’ done.
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
			// в†’ loop, UPDATE arm now succeeds.
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

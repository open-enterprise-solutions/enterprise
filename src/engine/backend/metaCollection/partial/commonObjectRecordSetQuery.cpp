////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : ibValueRecordSetObject — register / record-set ops.
//	              Exist (with optional MAX(line_number) probe), Read by
//	              key OR by current m_keyValues, Save with replace flag,
//	              Delete by key. FB uses UPDATE OR INSERT MATCHING for
//	              upsert; PG plain INSERT inside the replace-then-insert
//	              cycle. Session-bound conn via ses_query so register
//	              writes share the outer document-save TX.
////////////////////////////////////////////////////////////////////////////

#include "commonObject.h"

#include "backend/appData.h"
#include "backend/session/session.h"
#include "backend/databaseLayer/connectionPool.h"
#include "backend/databaseLayer/connectionScope.h"
#include "backend/databaseLayer/databaseErrorCodes.h"

#include "backend/metaCollection/attribute/metaAttributeObject.h"

#include "backend/system/systemManager.h"
#include "backend/backend_exception.h"

bool ibValueRecordSetObject::LockByKeys()
{
	if (m_metaObject == nullptr || m_keyValues.empty())
		return true;

	const auto db = ses_query;
	const wxString tableName = m_metaObject->GetTableNameDB();
	const wxString hint = db->RowLockHint();

	int position = 1;
	wxString queryText = wxT("SELECT 1 FROM ") + tableName;
	bool firstWhere = true;

	// Mirrors ExistData() — iterates the register's dimension key set
	// (FillArrayObjectByDimension returns {recorder} for AR/AcR and
	// IR-Subordinate, {period, dim1, dim2, ...} for non-recorder IR)
	// filtered by FindKeyValue so partially-populated record sets only
	// constrain on the keys actually bound.
	for (const auto object : m_metaObject->GetGenericDimensionArrayObject()) {
		if (!ibValueRecordSetObject::FindKeyValue(object->GetMetaID()))
			continue;
		queryText += (firstWhere ? wxT(" WHERE ") : wxT(" AND "))
		           + ibValueMetaObjectAttributeBase::GetCompositeSQLFieldName(object);
		firstWhere = false;
	}

	// No key fields populated — nothing meaningful to scope the lock to.
	// Skip silently; the caller's UPSERT path will catch any unique-key
	// conflict via the DB constraint instead.
	if (firstWhere)
		return true;

	if (!hint.IsEmpty())
		queryText += wxT(" ") + hint;
	queryText += wxT(";");

	ibStatementGuard statement(db, db->PrepareStatement(queryText));
	if (!statement)
		ibBackendCoreException::Error(_("Failed to prepare register-lock query"));

	for (const auto object : m_metaObject->GetGenericDimensionArrayObject()) {
		if (!ibValueRecordSetObject::FindKeyValue(object->GetMetaID()))
			continue;
		ibValueMetaObjectAttributeBase::SetValueAttribute(
			object,
			m_keyValues.at(object->GetMetaID()),
			statement.get(),
			position
		);
	}

	ibDatabaseResultSet* resultSet = statement->RunQueryWithResults();
	if (resultSet == nullptr)
		ibBackendCoreException::Error(_("Failed to acquire register lock"));
	// Drain — we want the lock side effect, not the row data. For
	// recorder-keyed registers this may iterate N existing lines
	// (the FOR UPDATE locks all of them); for empty key buckets the
	// loop exits immediately and we still hold the lock against
	// concurrent INSERTs (gap-lock semantics per driver — see
	// docs/record-locks.md).
	while (resultSet->Next()) {}
	db->CloseResultSet(resultSet);
	return true;
}

//----------------------------------------------------------------------
// Phase A scaffold helpers — register-side counterpart of
// ibValueRecordDataObjectRef's Begin*/Commit*. Lives next to the
// LockByKeys query method it calls into. See commonObject.h docs.
//----------------------------------------------------------------------

bool ibValueRecordSetObject::BeginRecordSetWriteScope(ibConnectionScope& scope)
{
	if (appData->DesignerMode())          return false;
	if (!scope || !scope->IsOpen())
		ibBackendCoreException::Error(_("Database is not open!"));
	if (ibBackendException::IsEvalMode()) return false;

	if (!m_metaObject->AccessRight_Write()) {
		ibBackendAccessException::Error();
		return false;
	}

	scope.SafeBeginTransaction();
	LockByKeys();
	return true;
}

bool ibValueRecordSetObject::BeginRecordSetDeleteScope(ibConnectionScope& scope)
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
	LockByKeys();
	return true;
}

void ibValueRecordSetObject::CommitRecordSetScope(ibConnectionScope& scope)
{
	scope.SafeCommitTransaction();
	m_objModified = false;
}

bool ibValueRecordSetObject::ExistData()
{
	const auto db = ses_query;
	const bool isFB = (db->GetDatabaseLayerType() == DATABASELAYER_FIREBIRD);

	const wxString tableName = m_metaObject->GetTableNameDB(); int position = 1;
	wxString queryText = isFB ? "SELECT FIRST 1 1 FROM " + tableName
	                          : "SELECT 1 FROM " + tableName;
	bool firstWhere = true;

	for (const auto object : m_metaObject->GetGenericDimensionArrayObject()) {
		if (!ibValueRecordSetObject::FindKeyValue(object->GetMetaID()))
			continue;
		queryText += (firstWhere ? " WHERE " : " AND ")
		           + ibValueMetaObjectAttributeBase::GetCompositeSQLFieldName(object);
		firstWhere = false;
	}
	if (!isFB)
		queryText += " LIMIT 1";
	queryText += ";";

	ibStatementGuard statement(db, db->PrepareStatement(queryText));
	if (!statement)
		return false;

	for (const auto object : m_metaObject->GetGenericDimensionArrayObject()) {
		if (!ibValueRecordSetObject::FindKeyValue(object->GetMetaID()))
			continue;
		ibValueMetaObjectAttributeBase::SetValueAttribute(
			object,
			m_keyValues.at(object->GetMetaID()),
			statement.get(),
			position
		);
	}

	ibDatabaseResultSet* resultSet = statement->RunQueryWithResults();
	if (resultSet == nullptr)
		return false;
	bool founded = resultSet->Next();
	db->CloseResultSet(resultSet);
	return founded;
}

bool ibValueRecordSetObject::ExistData(ibNumber& lastNum)
{
	const auto db = ses_query;

	const wxString tableName = m_metaObject->GetTableNameDB(); int position = 1;
	// MAX aggregation in SQL — DB uses any index on (recorder, line_number)
	// instead of streaming the whole rowset client-side.
	const wxString lineNumField = m_metaObject->GetRegisterLineNumber()->GetFieldNameDB() + wxT("_N");
	wxString queryText = "SELECT MAX(" + lineNumField + ") FROM " + tableName;
	bool firstWhere = true;

	for (const auto object : m_metaObject->GetGenericDimensionArrayObject()) {
		if (!ibValueRecordSetObject::FindKeyValue(object->GetMetaID()))
			continue;
		queryText += (firstWhere ? " WHERE " : " AND ")
		           + ibValueMetaObjectAttributeBase::GetCompositeSQLFieldName(object);
		firstWhere = false;
	}
	queryText += ";";

	ibStatementGuard statement(db, db->PrepareStatement(queryText));
	if (!statement)
		return false;

	for (const auto object : m_metaObject->GetGenericDimensionArrayObject()) {
		if (!ibValueRecordSetObject::FindKeyValue(object->GetMetaID()))
			continue;
		ibValueMetaObjectAttributeBase::SetValueAttribute(
			object,
			m_keyValues.at(object->GetMetaID()),
			statement.get(),
			position
		);
	}

	ibDatabaseResultSet* resultSet = statement->RunQueryWithResults();
	if (resultSet == nullptr)
		return false;
	bool founded = false; lastNum = 1;
	if (resultSet->Next() && !resultSet->IsFieldNull(1)) {
		lastNum = resultSet->GetResultNumber(1);
		founded = true;
	}
	db->CloseResultSet(resultSet);
	return founded;
}

bool ibValueRecordSetObject::ReadData(const ibUniqueKeyPair& key)
{
	const auto db = ses_query;

	ibValueModelRamTableBase::Clear(); int position = 1;

	wxString tableName = m_metaObject->GetTableNameDB();
	wxString queryText = "SELECT * FROM " + tableName; bool firstWhere = true;

	for (const auto object : m_metaObject->GetGenericDimensionArrayObject()) {
		if (!key.FindKey(object->GetMetaID()))
			continue;
		if (firstWhere) {
			queryText = queryText + " WHERE ";
		}
		queryText = queryText +
			(firstWhere ? " " : " AND ") + ibValueMetaObjectAttributeBase::GetCompositeSQLFieldName(object);
		if (firstWhere) {
			firstWhere = false;
		}
	}
	ibStatementGuard statement(db, db->PrepareStatement(queryText));
	if (!statement)
		return false;
	for (const auto object : m_metaObject->GetGenericDimensionArrayObject()) {
		if (!key.FindKey(object->GetMetaID()))
			continue;
		ibValueMetaObjectAttributeBase::SetValueAttribute(
			object,
			key.GetKey(object->GetMetaID()),
			statement.get(),
			position
		);
	}
	ibDatabaseResultSet* resultSet = statement->RunQueryWithResults();
	if (resultSet == nullptr)
		return false;
	while (resultSet->Next()) {
		ibValueTableRow* rowData = new ibValueTableRow();
		for (const auto object : m_metaObject->GetGenericDimensionArrayObject()) {
			ibValueMetaObjectAttributeBase::GetValueAttribute(object, rowData->AppendTableValue(object->GetMetaID()), resultSet);
		}
		for (const auto object : m_metaObject->GetGenericAttributeArrayObject()) {
			ibValueMetaObjectAttributeBase::GetValueAttribute(object, rowData->AppendTableValue(object->GetMetaID()), resultSet);
		}
		ibValueModelRamTableBase::Append(rowData, !ibBackendException::IsEvalMode());
		m_selected = true;
	}

	db->CloseResultSet(resultSet);

	return GetRowCount() > 0;
}

bool ibValueRecordSetObject::ReadData()
{
	const auto db = ses_query;

	ibValueModelRamTableBase::Clear(); int position = 1;

	wxString tableName = m_metaObject->GetTableNameDB();
	wxString queryText = "SELECT * FROM " + tableName; bool firstWhere = true;

	for (const auto object : m_metaObject->GetGenericDimensionArrayObject()) {
		if (!ibValueRecordSetObject::FindKeyValue(object->GetMetaID()))
			continue;
		if (firstWhere) {
			queryText = queryText + " WHERE ";
		}
		queryText = queryText +
			(firstWhere ? " " : " AND ") + ibValueMetaObjectAttributeBase::GetCompositeSQLFieldName(object);
		if (firstWhere) {
			firstWhere = false;
		}
	}
	ibStatementGuard statement(db, db->PrepareStatement(queryText));
	if (!statement)
		return false;
	for (const auto object : m_metaObject->GetGenericDimensionArrayObject()) {
		if (!ibValueRecordSetObject::FindKeyValue(object->GetMetaID()))
			continue;
		ibValueMetaObjectAttributeBase::SetValueAttribute(
			object,
			m_keyValues.at(object->GetMetaID()),
			statement.get(),
			position
		);
	}
	ibDatabaseResultSet* resultSet = statement->RunQueryWithResults();
	if (resultSet == nullptr)
		return false;
	while (resultSet->Next()) {
		ibValueTableRow* rowData = new ibValueTableRow();
		for (const auto object : m_metaObject->GetGenericDimensionArrayObject()) {
			ibValueMetaObjectAttributeBase::GetValueAttribute(object, rowData->AppendTableValue(object->GetMetaID()), resultSet);
		}
		for (const auto object : m_metaObject->GetGenericAttributeArrayObject()) {
			ibValueMetaObjectAttributeBase::GetValueAttribute(object, rowData->AppendTableValue(object->GetMetaID()), resultSet);
		}
		ibValueModelRamTableBase::Append(rowData, !ibBackendException::IsEvalMode());
		m_selected = true;
	}

	db->CloseResultSet(resultSet);

	return GetRowCount() > 0;
}

bool ibValueRecordSetObject::SaveData(bool replace, bool clearTable)
{
	const auto db = ses_query;

	//check fill attributes 
	bool fillCheck = true; long currLine = 1;
	for (long row = 0; row < GetRowCount(); row++) {
		for (const auto object : m_metaObject->GetGenericAttributeArrayObject()) {
			if (object->FillCheck()) {
				ibValueTableRow* node = GetViewData<ibValueTableRow>(GetItem(row));
				wxASSERT(node);
				if (node->IsEmptyValue(object->GetMetaID())) {
					wxString fillError =
						wxString::Format(_("The %s is required on line %i of the %s"), object->GetSynonym(), currLine, m_metaObject->GetSynonym());
					ibValueSystemFunction::Message(fillError, ibStatusMessage::ibStatusMessage_Information);
					fillCheck = false;
				}
			}
		}
		currLine++;
	}

	if (!fillCheck)
		return false;

	ibNumber numberLine = 1, oldNumberLine = 1;

	if (m_metaObject->HasRecorder() &&
		ibValueRecordSetObject::ExistData(oldNumberLine)) {
		if (replace && !ibValueRecordSetObject::DeleteData())
			return false;
		if (!replace) {
			numberLine = oldNumberLine;
		}
	}
	else if (ibValueRecordSetObject::ExistData()) {
		if (replace && !ibValueRecordSetObject::DeleteData())
			return false;
		if (!replace) {
			numberLine = oldNumberLine;
		}
	}

	wxString tableName = m_metaObject->GetTableNameDB(); wxString queryText; bool firstUpdate = true;
	if (db->GetDatabaseLayerType() != DATABASELAYER_FIREBIRD) {
		queryText = "INSERT INTO " + tableName + " (";
	}
	else {
		queryText = "UPDATE OR INSERT INTO " + tableName + " (";
	}
	for (const auto object : m_metaObject->GetGenericAttributeArrayObject()) {
		queryText += (firstUpdate ? "" : ",") + ibValueMetaObjectAttributeBase::GetSQLFieldName(object);
		if (firstUpdate) {
			firstUpdate = false;
		}
	}
	queryText += ") VALUES ("; bool firstInsert = true;
	for (const auto object : m_metaObject->GetGenericAttributeArrayObject()) {
		unsigned int fieldCount = ibValueMetaObjectAttributeBase::GetSQLFieldCount(object);
		for (unsigned int i = 0; i < fieldCount; i++) {
			queryText += (firstInsert ? "?" : ",?");
			if (firstInsert) {
				firstInsert = false;
			}
		}
	}

	if (db->GetDatabaseLayerType() != DATABASELAYER_FIREBIRD) {
		queryText += ")";
	}
	else {
		queryText += ") MATCHING (";
		if (m_metaObject->HasRecorder()) {
			ibValueMetaObjectAttributePredefined* attributeRecorder = m_metaObject->GetRegisterRecorder();
			wxASSERT(attributeRecorder);
			queryText += ibValueMetaObjectAttributeBase::GetSQLFieldName(attributeRecorder);
			ibValueMetaObjectAttributePredefined* attributeNumberLine = m_metaObject->GetRegisterLineNumber();
			wxASSERT(attributeNumberLine);
			queryText += "," + ibValueMetaObjectAttributeBase::GetSQLFieldName(attributeNumberLine);
		}
		else
		{
			bool firstMatching = true;
			for (const auto object : m_metaObject->GetGenericDimensionArrayObject()) {
				queryText += (firstMatching ? "" : ",") + ibValueMetaObjectAttributeBase::GetSQLFieldName(object);
				if (firstMatching) {
					firstMatching = false;
				}
			}
		}
		queryText += ");";
	}

	ibPreparedStatement* statement = db->PrepareStatement(queryText);
	if (statement == nullptr)
		return false;

	bool hasError = false;

	for (long row = 0; row < GetRowCount(); row++) {
		if (hasError)
			break;
		int position = 1;
		for (const auto object : m_metaObject->GetGenericAttributeArrayObject()) {
			auto foundedKey = m_keyValues.find(object->GetMetaID());
			if (foundedKey != m_keyValues.end()) {
				ibValueMetaObjectAttributeBase::SetValueAttribute(
					object,
					foundedKey->second,
					statement,
					position
				);
			}
			else if (m_metaObject->IsRegisterLineNumber(object->GetMetaID())) {
				ibValueMetaObjectAttributeBase::SetValueAttribute(
					object,
					numberLine++,
					statement,
					position
				);
			}
			else {
				ibValueTableRow* node = GetViewData< ibValueTableRow>(GetItem(row));
				wxASSERT(node);
				ibValueMetaObjectAttributeBase::SetValueAttribute(
					object,
					node->GetTableValue(object->GetMetaID()),
					statement,
					position
				);
			}
		}

		hasError = statement->RunQuery() == DATABASE_LAYER_QUERY_RESULT_ERROR;
	}

	db->CloseStatement(statement);

	if (!hasError && !SaveVirtualTable())
		return false;

	if (!hasError && clearTable)
		ibValueModelRamTableBase::Clear();
	else if (!clearTable)
		m_selected = true;

	return !hasError;
}

bool ibValueRecordSetObject::DeleteData()
{
	const auto db = ses_query;

	wxString tableName = m_metaObject->GetTableNameDB();
	wxString queryText = "DELETE FROM " + tableName; bool firstWhere = true;
	for (const auto object : m_metaObject->GetGenericDimensionArrayObject()) {
		if (!ibValueRecordSetObject::FindKeyValue(object->GetMetaID()))
			continue;
		if (firstWhere) {
			queryText = queryText + " WHERE ";
		}
		queryText = queryText +
			(firstWhere ? " " : " AND ") + ibValueMetaObjectAttributeBase::GetCompositeSQLFieldName(object);
		if (firstWhere) {
			firstWhere = false;
		}
	}

	ibPreparedStatement* statement = db->PrepareStatement(queryText); int position = 1;

	if (statement == nullptr)
		return false;

	for (const auto object : m_metaObject->GetGenericDimensionArrayObject()) {
		if (!ibValueRecordSetObject::FindKeyValue(object->GetMetaID()))
			continue;
		ibValueMetaObjectAttributeBase::SetValueAttribute(
			object,
			m_keyValues.at(object->GetMetaID()),
			statement,
			position
		);
	}

	statement->RunQuery();
	db->CloseStatement(statement);
	return DeleteVirtualTable();
}

//**********************************************************************************************************
//*                                          Code generator												   *
//**********************************************************************************************************

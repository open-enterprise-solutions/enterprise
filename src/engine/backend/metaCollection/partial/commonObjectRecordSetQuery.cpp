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
#include "backend/query/dataQueryBuilder.h"   // L3 write core (ibDataQueryBuilder::WriteRow)

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
	// Composite-key existence probe through the L3 door: only the BOUND dimensions
	// constrain (FindKeyValue filter), each decomposed inside L3 across its physical
	// fields. The FB FIRST / others LIMIT fork and the statement are gone.
	try {
		ibDataQueryBuilder q;
		q.From(m_metaObject->GetQueryable());
		for (const auto object : m_metaObject->GetGenericDimensionArrayObject()) {
			if (!ibValueRecordSetObject::FindKeyValue(object->GetMetaID()))
				continue;
			q.Where(object, ibComparisonType::ibComparisonType_Equal,
				m_keyValues.at(object->GetMetaID()));
		}
		ibReadPageRequest page;
		page.m_count = 1;
		ibDataQueryResult selection = q.Select(page);
		return selection.Next();
	}
	catch (...) {}
	return false;
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
	ibValueModelRamTableBase::Clear();

	// Composite-key read through the L3 door — only the bound dimensions (key.FindKey)
	// constrain, decomposed inside L3. Each row's dimensions AND resources come from
	// the L3 selection (GetValue) — no raw result set, no statement here.
	try {
		ibDataQueryBuilder q;
		q.From(m_metaObject->GetQueryable());
		for (const auto object : m_metaObject->GetGenericDimensionArrayObject()) {
			if (!key.FindKey(object->GetMetaID()))
				continue;
			q.Where(object, ibComparisonType::ibComparisonType_Equal,
				key.GetKey(object->GetMetaID()));
		}
		ibReadPageRequest page;
		page.m_count = 0;   // every matching line
		ibDataQueryResult selection = q.Select(page);
		while (selection.Next()) {
			ibValueTableRow* rowData = new ibValueTableRow();
			for (const auto object : m_metaObject->GetGenericDimensionArrayObject())
				rowData->AppendTableValue(object->GetMetaID()) = selection.GetValue(object);
			for (const auto object : m_metaObject->GetGenericAttributeArrayObject())
				rowData->AppendTableValue(object->GetMetaID()) = selection.GetValue(object);
			ibValueModelRamTableBase::Append(rowData, !ibBackendException::IsEvalMode());
			m_selected = true;
		}
	}
	catch (...) { return false; }

	return GetRowCount() > 0;
}

bool ibValueRecordSetObject::ReadData()
{
	ibValueModelRamTableBase::Clear();

	// As ReadData(key) but scoped by the current m_keyValues (FindKeyValue filter).
	try {
		ibDataQueryBuilder q;
		q.From(m_metaObject->GetQueryable());
		for (const auto object : m_metaObject->GetGenericDimensionArrayObject()) {
			if (!ibValueRecordSetObject::FindKeyValue(object->GetMetaID()))
				continue;
			q.Where(object, ibComparisonType::ibComparisonType_Equal,
				m_keyValues.at(object->GetMetaID()));
		}
		ibReadPageRequest page;
		page.m_count = 0;   // every matching line
		ibDataQueryResult selection = q.Select(page);
		while (selection.Next()) {
			ibValueTableRow* rowData = new ibValueTableRow();
			for (const auto object : m_metaObject->GetGenericDimensionArrayObject())
				rowData->AppendTableValue(object->GetMetaID()) = selection.GetValue(object);
			for (const auto object : m_metaObject->GetGenericAttributeArrayObject())
				rowData->AppendTableValue(object->GetMetaID()) = selection.GetValue(object);
			ibValueModelRamTableBase::Append(rowData, !ibBackendException::IsEvalMode());
			m_selected = true;
		}
	}
	catch (...) { return false; }

	return GetRowCount() > 0;
}

bool ibValueRecordSetObject::SaveData(bool replace, bool clearTable)
{
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

	const wxString tableName = m_metaObject->GetTableNameDB();

	// UPSERT each line through the L3 write core. Match keys are the register's
	// identity ATTRIBUTES — recorder+line (HasRecorder) or the dimensions; the
	// core expands them to physical fields. This UNIFIES the old fork, where FB
	// upserted but PG did a plain INSERT.
	std::vector<const ibValueMetaObjectAttributeBase*> matchKeyAttrs;
	if (m_metaObject->HasRecorder()) {
		matchKeyAttrs.push_back(m_metaObject->GetRegisterRecorder());
		matchKeyAttrs.push_back(m_metaObject->GetRegisterLineNumber());
	}
	else {
		for (const auto object : m_metaObject->GetGenericDimensionArrayObject())
			matchKeyAttrs.push_back(object);
	}

	bool hasError = false;

	for (long row = 0; row < GetRowCount() && !hasError; row++) {
		// Each line's assignments BY ATTRIBUTE: a key value, the auto line number,
		// or the row's value. No columns, no positions — the core owns those.
		std::vector<std::pair<const ibValueMetaObjectAttributeBase*, ibValue>> assignments;
		for (const auto object : m_metaObject->GetGenericAttributeArrayObject()) {
			auto foundedKey = m_keyValues.find(object->GetMetaID());
			if (foundedKey != m_keyValues.end())
				assignments.emplace_back(object, foundedKey->second);
			else if (m_metaObject->IsRegisterLineNumber(object->GetMetaID()))
				assignments.emplace_back(object, ibValue(numberLine++));
			else {
				ibValueTableRow* node = GetViewData<ibValueTableRow>(GetItem(row));
				wxASSERT(node);
				assignments.emplace_back(object, node->GetTableValue(object->GetMetaID()));
			}
		}
		hasError = !ibDataQueryBuilder::WriteRow(ibDataQueryBuilder::WriteKind::Upsert, tableName,
			wxEmptyString, ibValue(), assignments, matchKeyAttrs);
	}

	if (!hasError && !SaveVirtualTable())
		return false;

	if (!hasError) {
		// m_selected drives IsEmpty()/IsNewObject(); it must reflect the persisted
		// DB state, not whether the RAM table is currently populated. The old code
		// only set it on the !clearTable branch, so a normal write-with-clear left a
		// freshly-written set reporting empty/new. replace → exactly the rows just
		// written; append → those plus whatever already existed.
		const long savedRows = GetRowCount();
		if (clearTable)
			ibValueModelRamTableBase::Clear();
		m_selected = (savedRows > 0) || (!replace && m_selected);
	}

	return !hasError;
}

bool ibValueRecordSetObject::DeleteData()
{
	// DELETE the record set by its dimension key — composite WHERE built by the
	// L3 core from the dimension ATTRIBUTES present in m_keyValues (it expands
	// each to its physical fields and binds positionally). No fields, no
	// positions here.
	std::vector<std::pair<const ibValueMetaObjectAttributeBase*, ibValue>> assignments;
	for (const auto object : m_metaObject->GetGenericDimensionArrayObject()) {
		if (!ibValueRecordSetObject::FindKeyValue(object->GetMetaID()))
			continue;
		assignments.emplace_back(object, m_keyValues.at(object->GetMetaID()));
	}
	ibDataQueryBuilder::WriteRow(ibDataQueryBuilder::WriteKind::Delete, m_metaObject->GetTableNameDB(),
		wxEmptyString, ibValue(), assignments);

	m_selected = false; // the record set no longer exists in the DB
	return DeleteVirtualTable();
}

//**********************************************************************************************************
//*                                          Code generator												   *
//**********************************************************************************************************

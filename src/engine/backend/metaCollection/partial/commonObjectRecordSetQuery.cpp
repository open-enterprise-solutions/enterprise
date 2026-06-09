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
#include "backend/query/dataQueryBuilder.h"   // L3 write/read door (From/SetValue/Where/Upsert/Delete) + ibRawDBColumn

#include "backend/system/systemManager.h"
#include "backend/backend_exception.h"

bool ibValueRecordSetObject::LockByKeys()
{
	if (m_metaObject == nullptr || m_keyValues.empty())
		return true;

	// Lock the existing lines of this composite key for the open write TX. Mirrors ExistData()
	// — only the BOUND dimensions constrain (FindKeyValue filter; GetGenericDimensionArrayObject
	// = {recorder} for AR/AcR, {period, dim...} for non-recorder IR), each decomposed inside L3.
	// The pessimistic row lock rides as page.m_lockForUpdate: the dialect appends its row-lock
	// clause (FB "WITH LOCK", PG/MySQL "FOR UPDATE"; SQLite no-op — whole-DB TX lock). Draining
	// the selection holds the lock. No statement, no SetValueAttribute. (docs/record-locks.md)
	try {
		ibDataQueryBuilder q;
		q.From(m_metaObject->GetQueryable());
		bool anyKey = false;
		for (const auto object : m_metaObject->GetGenericDimensionArrayObject()) {
			if (!ibValueRecordSetObject::FindKeyValue(object->GetMetaID()))
				continue;
			q.Where(object, m_keyValues.at(object->GetMetaID()));
			anyKey = true;
		}
		// No key fields populated — nothing to scope the lock to; the UPSERT path catches any
		// unique-key conflict via the DB constraint instead.
		if (!anyKey)
			return true;

		ibReadPageRequest page;
		page.m_count = 0;              // every matching line
		page.m_lockForUpdate = true;   // pessimistic row lock (FOR UPDATE / WITH LOCK)
		ibDataQueryResult selection = q.Select(page);
		while (selection.Next()) {}
	}
	catch (...) {
		ibBackendCoreException::Error(_("Failed to acquire register lock"));
	}
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
	// MAX(line number) over the bound composite key, through the L3 door's aggregate terminal —
	// the DB uses any index on (recorder, line_number) instead of streaming the rowset. Only the
	// BOUND dimensions constrain (FindKeyValue filter), each decomposed inside L3. No statement.
	lastNum = 1;
	try {
		ibDataQueryBuilder q;
		q.From(m_metaObject->GetQueryable());
		for (const auto object : m_metaObject->GetGenericDimensionArrayObject()) {
			if (!ibValueRecordSetObject::FindKeyValue(object->GetMetaID()))
				continue;
			q.Where(object, m_keyValues.at(object->GetMetaID()));
		}
		q.Max(m_metaObject->GetRegisterLineNumber(), wxT("maxLine"));
		ibDataQueryResult selection = q.SelectAggregate();
		if (selection.Next()) {
			const ibValue maxLine = selection.GetColumn(wxT("maxLine"));
			if (!maxLine.IsEmpty()) {
				lastNum = maxLine.GetNumber();
				return true;
			}
		}
	}
	catch (...) {}
	return false;
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

	// UPSERT each line through the L3 write door. The match keys are the register's identity
	// columns, AUTO-detected by IsPrimaryKey — recorder + line number for a recorder-based
	// register, period + dimensions for an information register — so there is no explicit match
	// list and no FB/PG fork (the dialect closes the UPSERT spelling).
	bool hasError = false;

	for (long row = 0; row < GetRowCount() && !hasError; row++) {
		// Each line's assignments BY COLUMN: a key value, the auto line number, or the row's
		// value. No fields, no positions — the door / provider owns those.
		ibDataQueryBuilder q;
		q.From(m_metaObject->GetQueryable());
		for (const auto object : m_metaObject->GetGenericAttributeArrayObject()) {
			auto foundedKey = m_keyValues.find(object->GetMetaID());
			if (foundedKey != m_keyValues.end())
				q.SetValue(object, foundedKey->second);
			else if (m_metaObject->IsRegisterLineNumber(object->GetMetaID()))
				q.SetValue(object, ibValue(numberLine++));
			else {
				ibValueTableRow* node = GetViewData<ibValueTableRow>(GetItem(row));
				wxASSERT(node);
				q.SetValue(object, node->GetTableValue(object->GetMetaID()));
			}
		}
		hasError = !q.Upsert();
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
	// DELETE the record set by its key — WHERE the register's identity columns present in
	// m_keyValues (recorder / period + dimensions) = value, through the L3 write door. It
	// expands each column to its physical fields and binds. No fields, no positions here.
	ibDataQueryBuilder q;
	q.From(m_metaObject->GetQueryable());
	for (const auto object : m_metaObject->GetGenericDimensionArrayObject()) {
		if (!ibValueRecordSetObject::FindKeyValue(object->GetMetaID()))
			continue;
		q.Where(object, m_keyValues.at(object->GetMetaID()));
	}
	q.Delete();

	m_selected = false; // the record set no longer exists in the DB
	return DeleteVirtualTable();
}

//**********************************************************************************************************
//*                                          Code generator												   *
//**********************************************************************************************************

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
#include "backend/query/dataQueryBuilder.h"   // L3 write/read door (From/SetValue/Where/Upsert/Delete) + ibBackendColumnRawDB

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
	// clause (FB "WITH LOCK", PG "FOR UPDATE"; SQLite no-op — whole-DB TX lock). Draining
	// the selection holds the lock. No statement, no SetValueAttribute. (docs/record-locks.md)
	try {
		ibDataQueryBuilder q;
		q.WithAccessPolicy(nullptr);   // a row LOCK is a physical concurrency op, NOT a user read: it must see
		q.From(m_metaObject->GetQueryable());   // and lock the RAW rows regardless of RLS visibility (like ExistData).
		                                        // RLS is enforced at the write itself (guarded DELETE/UPDATE + Allowed).
		bool anyKey = false;
		for (const auto object : m_metaObject->GetGenericDimensionArrayObject()) {
			if (!ibValueRecordSetObject::FindKeyValue(object->GetMetaID()))
				continue;
			q.Where(object->GetQueryColumn(), m_keyValues.at(object->GetMetaID()));
			anyKey = true;
		}
		// No key fields populated — nothing to scope the lock to; the UPSERT path catches any
		// unique-key conflict via the DB constraint instead.
		if (!anyKey)
			return true;

		ibReadPageRequest page;
		page.m_count = 0;              // every matching line
		page.m_lockForUpdate = true;   // pessimistic row lock (FOR UPDATE / WITH LOCK)
		ibDataQueryResult selection = q.Execute(page);
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

	// ⭐ THE QUESTION IS NOT "is this an evaluation" BUT "may this evaluation write". A watch may
	// not — that is what eval mode is for. The debugger's sandbox may, and must: it exists to write,
	// measure and be undone, and it runs inside a transaction that is always rolled back
	// (backend_exception.h). Under the old, wider test it wrote nothing and said nothing.
	if (ibBackendException::IsEvalMode()
		&& !ibBackendException::IsEvalSandbox()) return false;

	if (!m_metaObject->AccessRight_Write()) {
		// Name the register AND the right: during a posting cascade several objects are gated in a
		// row, and "not enough access rights" alone does not say which one closed the door.
		ibBackendAccessException::Error(wxString::Format(_("writing to register '%s'"),
			m_metaObject->GetSynonym()));
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

	// …and the same for a delete: see the note on the write scope above.
	if (ibBackendException::IsEvalMode()
		&& !ibBackendException::IsEvalSandbox()) return false;

	if (!m_metaObject->AccessRight_Delete()) {
		ibBackendAccessException::Error(wxString::Format(_("clearing register '%s'"),
			m_metaObject->GetSynonym()));
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
	// constrain (FindKeyValue filter), each decomposed inside L3 across its physical fields.
	// UNGUARDED (WithAccessPolicy(nullptr)): this decides whether a replace must DELETE the old set, so it
	// must see the RAW physical rows, not the RLS-filtered view. Otherwise records the role cannot read are
	// invisible here -> DeleteData is skipped -> the insert DUPLICATES them (or hits a unique key). Seeing
	// them raw lets DeleteData run over the whole set. Note what the delete does NOT do any more: its row
	// count is not read as a verdict on rights. A set is addressed by its recorder, so an empty one is a
	// normal state — the permission question was answered before the statement, by this register's own
	// Write / Delete right and by the policy refusing outright. (docs: access-policy-rls, write-deny)
	try {
		ibDataQueryBuilder q;
		q.WithAccessPolicy(nullptr);
		q.From(m_metaObject->GetQueryable());
		for (const auto object : m_metaObject->GetGenericDimensionArrayObject()) {
			if (!ibValueRecordSetObject::FindKeyValue(object->GetMetaID()))
				continue;
			q.Where(object->GetQueryColumn(), ibQueryFilterOp::Equal,
				m_keyValues.at(object->GetMetaID()));
		}
		ibReadPageRequest page;
		page.m_count = 1;
		ibDataQueryResult selection = q.Execute(page);
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
	// UNGUARDED (raw physical rows, whatever RLS would hide), because the number it answers has to
	// clear EVERY stored line, not only the readable ones.
	//
	// It no longer decides whether the old set is deleted — a replace deletes unconditionally now
	// — so its only consumer is the numbering of an APPEND. That is why the swallow below is
	// narrowed: an engine failure answering "there is nothing stored" would restart the numbering
	// at 1 and collide with rows that are still there, which surfaces as a unique-key violation
	// with nothing pointing back here.
	lastNum = 1;
	try {
		ibDataQueryBuilder q;
		q.WithAccessPolicy(nullptr);
		q.From(m_metaObject->GetQueryable());
		for (const auto object : m_metaObject->GetGenericDimensionArrayObject()) {
			if (!ibValueRecordSetObject::FindKeyValue(object->GetMetaID()))
				continue;
			q.Where(object->GetQueryColumn(), m_keyValues.at(object->GetMetaID()));
		}
		q.Max(m_metaObject->GetRegisterLineNumber()->GetQueryColumn(), wxT("maxLine"));
		ibDataQueryResult selection = q.SelectAggregate();
		if (selection.Next()) {
			const ibValue maxLine = selection.GetColumn(wxT("maxLine"));
			if (!maxLine.IsEmpty()) {
				lastNum = maxLine.GetNumber();
				return true;
			}
		}
	}
	catch (const ibBackendException&) { throw; }   // the engine's own reason — it names the table and the column
	catch (...) {}
	return false;
}

bool ibValueRecordSetObject::ReadData(const ibUniqueKeyPair& key)
{
	ibValueModelStorage::Clear();

	// Composite-key read through the L3 door — only the bound dimensions (key.FindKey)
	// constrain, decomposed inside L3. Each row's dimensions AND resources come from
	// the L3 selection (GetValue) — no raw result set, no statement here.
	try {
		ibDataQueryBuilder q;
		q.From(m_metaObject->GetQueryable());
		for (const auto object : m_metaObject->GetGenericDimensionArrayObject()) {
			if (!key.FindKey(object->GetMetaID()))
				continue;
			q.Where(object->GetQueryColumn(), ibQueryFilterOp::Equal,
				key.GetKey(object->GetMetaID()));
		}
		ibReadPageRequest page;
		page.m_count = 0;   // every matching line
		ibDataQueryResult selection = q.Execute(page);
		while (selection.Next()) {
			ibComposerNode* rowData = new ibComposerNode();
			for (const auto object : m_metaObject->GetGenericDimensionArrayObject())
				rowData->AppendTableValue(object->GetMetaID()) = selection.GetValue(object->GetQueryColumn());
			for (const auto object : m_metaObject->GetGenericAttributeArrayObject())
				rowData->AppendTableValue(object->GetMetaID()) = selection.GetValue(object->GetQueryColumn());
			ibValueModelStorage::Append(rowData, !ibBackendException::IsEvalMode());
			m_selected = true;
		}
	}
	catch (...) { return false; }

	return GetRowCount() > 0;
}

bool ibValueRecordSetObject::ReadData()
{
	ibValueModelStorage::Clear();

	// As ReadData(key) but scoped by the current m_keyValues (FindKeyValue filter).
	try {
		ibDataQueryBuilder q;
		q.From(m_metaObject->GetQueryable());
		for (const auto object : m_metaObject->GetGenericDimensionArrayObject()) {
			if (!ibValueRecordSetObject::FindKeyValue(object->GetMetaID()))
				continue;
			q.Where(object->GetQueryColumn(), ibQueryFilterOp::Equal,
				m_keyValues.at(object->GetMetaID()));
		}
		ibReadPageRequest page;
		page.m_count = 0;   // every matching line
		ibDataQueryResult selection = q.Execute(page);
		while (selection.Next()) {
			ibComposerNode* rowData = new ibComposerNode();
			for (const auto object : m_metaObject->GetGenericDimensionArrayObject())
				rowData->AppendTableValue(object->GetMetaID()) = selection.GetValue(object->GetQueryColumn());
			for (const auto object : m_metaObject->GetGenericAttributeArrayObject())
				rowData->AppendTableValue(object->GetMetaID()) = selection.GetValue(object->GetQueryColumn());
			ibValueModelStorage::Append(rowData, !ibBackendException::IsEvalMode());
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
				ibComposerNode* node = GetViewData<ibComposerNode>(GetItem(row));
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

	// REPLACE DELETES; IT DOES NOT ASK FIRST.
	//
	// Both branches used to probe with ExistData() and only then delete — an extra round trip on
	// every posting, to answer a question the DELETE answers by itself: removing no rows is the
	// ordinary state of a set addressed by its key, not a failure. Worse, the probe decided
	// whether the old rows were cleared at all, and it reports "nothing there" for a failure as
	// well as for an empty set (it catches everything and returns false) — so a probe that fell
	// over skipped the delete and the new lines were written ON TOP of the old ones.
	//
	// The existence question survives only where it is genuinely needed: appending to a stored set
	// continues the stored numbering, and that needs MAX(line number), not existence.
	if (replace) {
		if (!ibValueRecordSetObject::DeleteData())
			return false;
	}
	else if (m_metaObject->HasRecorder()) {
		ibValueRecordSetObject::ExistData(oldNumberLine);
		numberLine = oldNumberLine;
	}

	// Keyed off the record set's EVENT, not off what is physically stored: a NEW set (not selected)
	// is a create -> INSERT; an EXISTING one (selected) is a rewrite -> UPSERT. m_selected picks the
	// event so create and write stay distinct, exactly as they do for the owning object, and under a
	// policy that distinction is which right gets asked (CheckCreate vs CheckUpdate).
	//
	// ⚠ Worth knowing when reading the batch below: under `replace` the DELETE above has already
	// emptied the set, so an UPSERT there can match nothing and is an INSERT in all but name — it
	// simply cannot batch, because the event says rewrite. Making the rewrite path batch means
	// either a MERGE in the L2 IR, or separating "which statement" from "which right is asked".
	// That is a decision about access, not about speed, so it is not taken here.
	bool hasError = false;

	// Each line's assignments BY COLUMN: a key value, the auto line number, or the row's
	// value. No fields, no positions — the door / provider owns those.
	auto stageRow = [&](ibDataQueryBuilder& q, long row) {
		for (const auto object : m_metaObject->GetGenericAttributeArrayObject()) {
			// ⭐ A KEY THAT IS IN THE FILTER IS USED — whatever it holds. The filter's `Use` IS its
			// presence here (setting Use = True inserts the key, False erases it), so an entry with an
			// empty value is a deliberate "records whose dimension is blank" and gets written as such.
			// The question of WHICH keys are in the filter belongs to whoever built the set, not here.
			auto foundedKey = m_keyValues.find(object->GetMetaID());
			if (foundedKey != m_keyValues.end())
				q.SetValue(object->GetQueryColumn(), foundedKey->second);
			else if (m_metaObject->IsRegisterLineNumber(object->GetMetaID()))
				q.SetValue(object->GetQueryColumn(), ibValue(numberLine++));
			else {
				ibComposerNode* node = GetViewData<ibComposerNode>(GetItem(row));
				wxASSERT(node);
				q.SetValue(object->GetQueryColumn(), node->GetTableValue(object->GetMetaID()));
			}
		}
	};

	if (m_selected) {
		// A SET THAT CAME FROM THE DATABASE REWRITES ROW BY ROW. Each line may already exist, so the
		// write is an UPSERT, and the match is the dialect's own per-statement form — Firebird's
		// UPDATE OR INSERT takes no SELECT source. Batching this needs a MERGE the L2 IR does not
		// carry yet; until it does, the rewrite path stays as it was rather than pretending.
		for (long row = 0; row < GetRowCount() && !hasError; row++) {
			ibDataQueryBuilder q;
			q.From(m_metaObject->GetQueryable());
			stageRow(q, row);
			hasError = !q.Upsert();
		}
	}
	else {
		// A FRESH SET IS ONE STATEMENT PER CHUNK, NOT ONE PER LINE. Nothing here can already exist —
		// a new set under `replace`, or lines whose numbering continues past what is stored — so the
		// write is a plain INSERT, and the door stages every line before the provider emits it.
		// A thousand lines cost a thousand statements and a thousand round trips before this.
		ibDataQueryBuilder q;
		q.From(m_metaObject->GetQueryable());
		for (long row = 0; row < GetRowCount(); row++) {
			if (row > 0) q.NextRow();
			stageRow(q, row);
		}
		// An empty set writes nothing — the door always carries one (empty) row, so this must be
		// asked rather than left to the INSERT, which would otherwise emit a row of nulls.
		hasError = GetRowCount() > 0 && !q.Insert();
	}

	// (No totals write here. Derived state is maintained by the DATABASE trigger the schema
	//  installs on this table, inside this same transaction — so it cannot be bypassed by any
	//  other writer and cannot drift. Updating it from here would restore exactly the
	//  managed-code pattern the trigger replaced. See docs/register-totals-strategy.md.)

	if (!hasError) {
		// m_selected drives IsEmpty()/IsNewObject(); it must reflect the persisted
		// DB state, not whether the RAM table is currently populated. The old code
		// only set it on the !clearTable branch, so a normal write-with-clear left a
		// freshly-written set reporting empty/new. replace → exactly the rows just
		// written; append → those plus whatever already existed.
		const long savedRows = GetRowCount();
		if (clearTable)
			ibValueModelStorage::Clear();
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

	bool keyed = false;
	for (const auto object : m_metaObject->GetGenericDimensionArrayObject()) {
		if (!ibValueRecordSetObject::FindKeyValue(object->GetMetaID()))
			continue;
		q.Where(object->GetQueryColumn(), m_keyValues.at(object->GetMetaID()));
		keyed = true;
	}

	// A SET WITH NO KEY ADDRESSES THE WHOLE REGISTER, AND THAT IS SAID HERE RATHER THAN LEFT TO
	// AN EMPTY LOOP. Writing a set whose key was never assigned is a legitimate way to clear a
	// register outright — but until now the difference between "clear these movements" and "clear
	// every movement there is" was that the loop above happened to add no condition, which is a
	// distinction nothing in the code could see and nobody reviewing it could notice. The
	// behaviour is unchanged; what changes is that the wide case now leaves a trace.
	if (!keyed)
		ibJournalWarning(wxT("register"),_("Register '%s': the record set carries no key, so writing it clears every record"),
			m_metaObject->GetSynonym());

	// A FAILED DELETE RAISES, AND IT SAYS THAT IT WAS THE DELETE.
	//
	// This used to discard what Delete() answers and report `true` unconditionally, so the caller's
	// `if (replace && !DeleteData())` was unreachable: a DELETE that failed was followed by the
	// INSERT of the new lines, ON TOP of rows that were still there — duplicated movements, or a
	// unique-key violation far from the cause.
	//
	// Returning `false` would not be enough either. The bool travels up to WriteRecordSet, which
	// has one sentence for every way SaveData can end — "failed to store the records" — so a
	// failure to CLEAR would be reported as a failure to WRITE, and the reader would look in the
	// wrong half. An engine error already arrives here as an exception (ExecuteWrite rethrows
	// ibBackendException and only degrades an alien one to -1); this names the remaining case
	// rather than flattening it.
	//
	// Deleting NOTHING stays success — Delete() answers `affected >= 0`, and a set addressed by its
	// key may legitimately have no stored rows.
	if (!q.Delete())
		ibBackendCoreException::Error(_("Register '%s': failed to clear the stored records"),
			m_metaObject->GetSynonym());

	m_selected = false; // the record set no longer exists in the DB
	return true;        // the delete trigger reversed the totals in this same transaction
}

//**********************************************************************************************************
//*                                          Code generator												   *
//**********************************************************************************************************

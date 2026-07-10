////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : tabular sections
////////////////////////////////////////////////////////////////////////////

#include "tabularSection.h"

#include "backend/metaCollection/partial/commonObject.h"
#include "backend/query/dataQueryBuilder.h"            // L3 — read + write door (From/SetValue/Where/Insert/Delete) + ibRawDBColumn

// --- vended queryable — thin adapter forwarding to the tabular meta's methods ---
// The tabular section is uuid-keyed (1 parent -> N lines), ordered by line number.
// The adapter is parent-agnostic — the parent uuid is a query filter (WhereKey at
// read time) — so the persistent meta vends it via the common GetQueryable() interface
// and a transient (data-processor / report) parent simply never queries it.
const ibBackendQueryColumn* ibTabularQueryable::ResolveColumnByName(const wxString& name) const { return m_meta->FindAnyAttributeObjectByFilter(name); }
wxString ibTabularQueryable::GetQueryTableName() const { return m_meta->GetPhysicalTableName(); }
ibGuid ibTabularQueryable::GetQueryTableGuid() const { return m_meta->GetGuid(); }
wxString ibTabularQueryable::GetQueryName()      const { return m_meta->GetName(); }
ibMetaID ibTabularQueryable::GetQueryTableId() const { return m_meta->GetMetaID(); }
const ibMetaData* ibTabularQueryable::GetMetaData() const { return m_meta->GetMetaData(); }
// Identity tail = line number (rows of one parent are ordered + uniquely keyed by it). The PARENT
// uuid is NOT the identity tail — it is a plain query filter (Where on the raw "uuid" column at
// read/delete time), so the tabular section has no GetPrimaryKeyColumns (it INSERTs, never upserts).
std::vector<ibQuerySortItem> ibTabularQueryable::GetIdentitySort() const { return { ibQuerySortItem{ m_meta->GetNumberLine(), true } }; }
// (value materialisation moved to the DB provider's static get-helper.)

bool ibValueTabularSectionDataObjectRef::LoadData(const ibGuid& srcGuid, bool createData)
{
	if (m_objectValue->IsNewObject() && !srcGuid.isValid()) {
		m_readAfter = true;
		return false;
	}

	ibValueModelStorage::Clear();

	// Read every line for the parent uuid through the L3 door. The persistent
	// data-object supplies the queryable view (uuid key + line-number order); the
	// values come from the L3 selection (GetValue) — no L2 builder, no raw result
	// set at this call site.
	try {
		ibDataQueryBuilder q;
		// WHERE uuid = srcGuid (the parent filter is a plain raw-column condition, NOT a row-key
		// sentinel — the tabular identity tail is the line number), ORDER BY line number.
		q.From(m_metaTable->GetQueryable()).Where(ibRawDBColumn::String(wxT("uuid")), ibValue(wxString(srcGuid)));
		ibReadPageRequest page;
		page.m_count = 0;   // all lines
		ibDataQueryResult selection = q.Execute(page);
		while (selection.Next()) {
			ibComposerNode* rowData = new ibComposerNode();
			for (const auto object : m_metaTable->GetGenericAttributeArrayObject()) {
				if (m_metaTable->IsNumberLine(object->GetMetaID()))
					continue;
				rowData->AppendTableValue(object->GetMetaID()) = selection.GetValue(object);
			}
			ibValueModelStorage::Append(rowData, !ibBackendException::IsEvalMode());
		}
	}
	catch (...) { return false; }

	m_readAfter = true;
	return true;
}

//////////////////////////////////////////////////////////////////////////////////////////

#include "backend/system/systemManager.h"

bool ibValueTabularSectionDataObjectRef::SaveData()
{
	if (m_readOnly)
		return true;

	bool hasError = false;
	//check fill attributes
	bool fillCheck = true; long currLine = 1;
	for (long row = 0; row < GetRowCount(); row++) {
		for (const auto object : m_metaTable->GetGenericAttributeArrayObject()) {
			if (object->FillCheck()) {
				ibComposerNode* node = GetViewData<ibComposerNode>(GetItem(row));
				wxASSERT(node);
				if (node->IsEmptyValue(object->GetMetaID())) {
					wxString fillError =
						wxString::Format(_("The %s is required on line %i of the %s list"), object->GetSynonym(), currLine, m_metaTable->GetSynonym());
					ibValueSystemFunction::Message(fillError, ibStatusMessage::ibStatusMessage_Information);
					fillCheck = false;
				}
			}
		}
		currLine++;
	}

	if (!fillCheck)
		return false;

	if (!ibValueTabularSectionDataObjectRef::DeleteData())
		return false;

	// Per line: ALWAYS a plain INSERT. DeleteData above wiped any old rows for an EXISTING object (and was a
	// no-op for a new one), so every line here is a fresh row — never an in-place update. A native UPSERT is
	// both wrong and impossible here: a tabular row has NO primary-key column, so MATCHING would be empty
	// (`MATCHING ()` -> FB -104), and post-wipe there is nothing to match anyway. The owner's write is the
	// gated event; these lines inherit it (the builder is de-policied). uuid is the parent's raw row-key.
	ibNumber numberLine = 1;
	for (long row = 0; row < GetRowCount() && !hasError; row++) {
		ibDataQueryBuilder q;
		q.WithAccessPolicy(nullptr)
			.From(m_metaTable->GetQueryable())
			.SetValue(ibRawDBColumn::String(wxT("uuid")), ibValue(m_objectValue->GetGuid()));
		for (const auto object : m_metaTable->GetGenericAttributeArrayObject()) {
			if (!m_metaTable->IsNumberLine(object->GetMetaID())) {
				ibComposerNode* node = GetViewData<ibComposerNode>(GetItem(row));
				wxASSERT(node);
				q.SetValue(object, node->GetTableValue(object->GetMetaID()));
			}
			else {
				q.SetValue(object, ibValue(numberLine++));
			}
		}
		hasError = !q.Insert();
	}

	return !hasError;
}

bool ibValueTabularSectionDataObjectRef::DeleteData()
{
	if (m_readOnly || m_objectValue->IsNewObject())
		return true;

	// DELETE ... WHERE uuid = <parent guid> through the L3 write door (uuid = raw row-key). The tabular
	// section inherits the owner's access (WithAccessPolicy(nullptr)): the owner's OnAccessWrite already
	// gated this save, and an EMPTY section legitimately deletes 0 rows — under a policy that 0 is misread
	// as an access denial (fail-closed), which would wrongly block saving a document that has no lines.
	ibDataQueryBuilder()
		.WithAccessPolicy(nullptr)
		.From(m_metaTable->GetQueryable())
		.Where(ibRawDBColumn::String(wxT("uuid")), ibValue(m_objectValue->GetGuid()))
		.Delete();
	return true;
}
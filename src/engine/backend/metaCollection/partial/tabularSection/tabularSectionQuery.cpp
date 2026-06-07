////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : tabular sections
////////////////////////////////////////////////////////////////////////////

#include "tabularSection.h"

#include "backend/metaCollection/partial/commonObject.h"
#include "backend/query/dataQueryBuilder.h"            // L3 — read door + WriteRow write core (no L2 at this call site)

// --- vended queryable — thin adapter forwarding to the tabular meta's methods ---
// The tabular section is uuid-keyed (1 parent -> N lines), ordered by line number.
// The adapter is parent-agnostic — the parent uuid is a query filter (WhereKey at
// read time) — so the persistent meta vends it via the common GetQueryable() interface
// and a transient (data-processor / report) parent simply never queries it.
const ibValueMetaObjectAttributeBase* ibTabularQueryable::ResolveAttribute(const wxString& name) const { return m_meta->FindAnyAttributeObjectByFilter(name); }
const ibValueMetaObjectAttributeBase* ibTabularQueryable::ResolveAttribute(const ibMetaID& id) const { return m_meta->FindAnyAttributeObjectByFilter(id); }
wxString ibTabularQueryable::GetQueryTableName() const { return m_meta->GetTableNameDB(); }
ibMetaID ibTabularQueryable::GetQueryMetaID() const { return m_meta->GetMetaID(); }
wxString ibTabularQueryable::GetRowKeyColumn() const { return wxT("uuid"); }
std::vector<ibQuerySortItem> ibTabularQueryable::GetIdentitySort() const { return { ibQuerySortItem{ m_meta->GetNumberLine(), true } }; }
bool ibTabularQueryable::IsReferenceAttribute(const ibMetaID& /*id*/) const { return false; }
wxString ibTabularQueryable::MaterializeRowKey(ibDatabaseResultSet* /*rs*/) const { return wxEmptyString; }
ibValue ibTabularQueryable::MaterializeAttribute(const ibValueMetaObjectAttributeBase* attr, ibDatabaseResultSet* rs) const {
	ibValue v;
	ibValueMetaObjectAttributeBase::GetValueAttribute(attr, v, rs);
	return v;
}

bool ibValueTabularSectionDataObjectRef::LoadData(const ibGuid& srcGuid, bool createData)
{
	if (m_objectValue->IsNewObject() && !srcGuid.isValid()) {
		m_readAfter = true;
		return false;
	}

	ibValueModelRamTableBase::Clear();

	// Read every line for the parent uuid through the L3 door. The persistent
	// data-object supplies the queryable view (uuid key + line-number order); the
	// values come from the L3 selection (GetValue) — no L2 builder, no raw result
	// set at this call site.
	try {
		ibDataQueryBuilder q;
		q.From(m_metaTable->GetQueryable()).WhereKey(srcGuid);   // WHERE uuid = srcGuid, ORDER BY line number
		ibReadPageRequest page;
		page.m_count = 0;   // all lines
		ibDataQueryResult selection = q.Select(page);
		while (selection.Next()) {
			ibValueTableRow* rowData = new ibValueTableRow();
			for (const auto object : m_metaTable->GetGenericAttributeArrayObject()) {
				if (m_metaTable->IsNumberLine(object->GetMetaID()))
					continue;
				rowData->AppendTableValue(object->GetMetaID()) = selection.GetValue(object);
			}
			ibValueModelRamTableBase::Append(rowData, !ibBackendException::IsEvalMode());
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
				ibValueTableRow* node = GetViewData<ibValueTableRow>(GetItem(row));
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

	const wxString tableName = m_metaTable->GetTableNameDB();

	// Tabular section = plain INSERT per line (DeleteData above cleared the old
	// rows) through the L3 write core. uuid is the 1->1 key column (the parent
	// object); the attributes are the 1->N data columns. No fields, no positions.
	ibNumber numberLine = 1;
	for (long row = 0; row < GetRowCount() && !hasError; row++) {
		std::vector<std::pair<const ibValueMetaObjectAttributeBase*, ibValue>> assignments;
		for (const auto object : m_metaTable->GetGenericAttributeArrayObject()) {
			if (!m_metaTable->IsNumberLine(object->GetMetaID())) {
				ibValueTableRow* node = GetViewData<ibValueTableRow>(GetItem(row));
				wxASSERT(node);
				assignments.emplace_back(object, node->GetTableValue(object->GetMetaID()));
			}
			else {
				assignments.emplace_back(object, ibValue(numberLine++));
			}
		}
		hasError = !ibDataQueryBuilder::WriteRow(ibDataQueryBuilder::WriteKind::Insert, tableName,
			wxT("uuid"), ibValue(m_objectValue->GetGuid()), assignments);
	}

	return !hasError;
}

bool ibValueTabularSectionDataObjectRef::DeleteData()
{
	if (m_readOnly || m_objectValue->IsNewObject())
		return true;

	// DELETE ... WHERE uuid = <parent guid> through the L3 core (uuid = 1->1 key).
	ibDataQueryBuilder::WriteRow(ibDataQueryBuilder::WriteKind::Delete, m_metaTable->GetTableNameDB(),
		wxT("uuid"), ibValue(m_objectValue->GetGuid()), {});
	return true;
}
////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : tabular sections
////////////////////////////////////////////////////////////////////////////

#include "tabularSection.h"
#include "backend/backend_exception.h"    // ibBackendCoreException — a failed clear says it was the clear
#include "backend/query/columnLayout.h"   // ibOwnerRefColumn — the owner reference, named in one place

#include "backend/metaCollection/partial/commonObject.h"
#include "backend/query/dataQueryBuilder.h"            // L3 — read + write door (From/SetValue/Where/Insert/Delete) + ibRawDBColumn

// --- vended queryable — thin adapter forwarding to the tabular meta's methods ---
// The tabular section is uuid-keyed (1 parent -> N lines), ordered by line number.
// The adapter is parent-agnostic — the parent uuid is a query filter (WhereKey at
// read time) — so the persistent meta vends it via the common GetQueryable() interface
// and a transient (data-processor / report) parent simply never queries it.
// ⭐⭐ THE OWNER, AS A FIELD OF THE SECTION.
//
// A section's rows are tied to their owner by the owner reference each line stores — the SAME sixteen bytes the
// owner's own reference is. So the link that was only ever a physical detail becomes a field a query
// can name: `Ref` selects the owner and joins to it, and neither the author nor the engine needs a
// second column to make that possible.
//
// The target type is CONSTANT — a section belongs to one owner — so it rides on the column and never
// on the row. Built once and kept, because a queryable hands out column POINTERS and a temporary
// would leave every caller holding a dangling one.
const ibBackendQueryColumn* ibTabularQueryable::OwnerRefColumn() const
{
	if (!m_ownerRef) {
		const ibValueMetaObject* owner = m_meta != nullptr ? m_meta->GetParent() : nullptr;

		// ⚠ IT NEEDS AN IDENTITY, NOT JUST A NAME. Column ownership across joined sources is decided
		// by the column's ID, precisely because two sources can expose fields of the SAME name — and
		// every section's owner field is called `Ref`. Left at zero (the scaffold id), two sections
		// joined in one query would each claim the other's, and the read would take a field off the
		// wrong leaf. Derived from the SECTION's own metaID, so it is unique by construction; the
		// high bit keeps it clear of the attribute ids in the same table.
		//
		// The id is safe to mint here because this column is VENDED, never DECLARED: the section's
		// table contributes `uuid` as a scaffold, so the schema differ never sees this one.
		const ibMetaID identity = m_meta != nullptr ? (m_meta->GetMetaID() | 0x20000000) : 0;

		m_ownerRef.reset(new ibRawDBColumn(ibRawDBColumn::Reference(ibOwnerRefField(),
			owner != nullptr ? reference_to_clsid(owner->GetMetaID()) : 0, wxT("Ref"), identity)));
	}
	return m_ownerRef.get();
}

const ibBackendQueryColumn* ibTabularQueryable::ResolveColumnByName(const wxString& name) const
{
	if (name.IsSameAs(wxT("Ref"), false))
		return OwnerRefColumn();
	return m_meta->FindAnyAttributeObjectByFilter(name);
}

std::vector<const ibBackendQueryColumn*> ibTabularQueryable::GetColumns() const
{
	std::vector<const ibBackendQueryColumn*> columns;
	columns.push_back(OwnerRefColumn());
	for (const auto attribute : m_meta->GetGenericAttributeArrayObject())
		columns.push_back(attribute);
	return columns;
}
// ⚠ ANSWERED HERE, and it is not the duplicate the others were. Everywhere else the guid and the
// metaID are read off GetSourceMetaObject (ibBackendQueryable::GetQueryTableGuid) — a tabular section
// cannot say that: its metaobject is a COMPOSITE, not the generic data metaobject that question is
// typed on. It has an identity and no way to publish the thing carrying it, so it states it directly.
ibGuid ibTabularQueryable::GetQueryTableGuid() const { return m_meta->GetGuid(); }
ibMetaID ibTabularQueryable::GetQueryTableId() const { return m_meta->GetMetaID(); }
wxString ibTabularQueryable::GetQueryTableName() const { return m_meta->GetPhysicalTableName(); }
wxString ibTabularQueryable::GetQueryName()      const { return m_meta->GetName(); }
const ibMetaData* ibTabularQueryable::GetMetaData() const { return m_meta->GetMetaData(); }
// No key of its own: a line is identified by its OWNER plus its number, and the owner arrives as a
// plain query filter (Where on the raw "uuid" column at read/delete time) - so the tabular section
// vends no primary key, and INSERTs rather than upserts. Its reading order is the line number, which
// the caller states itself (LoadData orders by it).
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
		// WHERE owner = srcGuid (the parent filter is a plain raw-column condition, NOT an identity
		// sentinel — the tabular identity tail is the line number), ORDER BY line number.
		q.From(m_metaTable->GetQueryable()).Where(ibOwnerRefColumn(), ibValue(wxString(srcGuid)));
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

	// ALWAYS a plain INSERT. DeleteData above wiped any old rows for an EXISTING object (and was a
	// no-op for a new one), so every line here is a fresh row — never an in-place update. A native UPSERT is
	// both wrong and impossible here: a tabular row has NO primary-key column, so MATCHING would be empty
	// (`MATCHING ()` -> FB -104), and post-wipe there is nothing to match anyway. The owner's write is the
	// gated event; these lines inherit it (the builder is de-policied). The owner reference is the parent's.
	//
	// ONE STATEMENT PER CHUNK, NOT ONE PER LINE. All of the above is exactly the shape the door
	// batches — same columns line to line, a fresh INSERT, no trigger on the table. A hundred-line
	// document used to cost a hundred statements and a hundred round trips inside the owner's save
	// transaction.
	ibNumber numberLine = 1;
	ibDataQueryBuilder q;
	q.WithAccessPolicy(nullptr)
		.From(m_metaTable->GetQueryable());

	for (long row = 0; row < GetRowCount(); row++) {
		if (row > 0) q.NextRow();
		// The parent's reference goes on EVERY row: the columns are the statement's, so each
		// staged row must name the same list in the same order.
		q.SetValue(ibOwnerRefColumn(), ibValue(m_objectValue->GetGuid()));
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
	}

	// An empty section writes nothing. The door always carries one (empty) row, so this is asked
	// here rather than left to the INSERT, which would otherwise emit a row of nulls.
	hasError = GetRowCount() > 0 && !q.Insert();

	return !hasError;
}

bool ibValueTabularSectionDataObjectRef::DeleteData()
{
	if (m_readOnly || m_objectValue->IsNewObject())
		return true;

	// DELETE ... WHERE owner = <parent guid> through the L3 write door (a raw guid field). The tabular
	// section inherits the owner's access (WithAccessPolicy(nullptr)): the owner's OnAccessWrite already
	// gated this save, and an EMPTY section legitimately deletes 0 rows — under a policy that 0 is misread
	// as an access denial (fail-closed), which would wrongly block saving a document that has no lines.
	//
	// A FAILED DELETE RAISES, AND IT SAYS THAT IT WAS THE DELETE.
	//
	// This used to discard what Delete() answers and report `true` unconditionally, so SaveData's
	// `if (!DeleteData()) return false;` was unreachable — a failed DELETE was followed by the
	// INSERT of the new lines, on top of rows still in the table. It is worse here than for a
	// register: a tabular row has NO primary key (see SaveData), so nothing downstream can notice
	// the collision and the document simply shows its lines twice.
	//
	// It raises rather than answering `false`, because the bool would reach the owner's save as
	// "failed to save the object data" — a sentence about writing, for a failure to clear.
	//
	// Deleting NOTHING stays success — Delete() answers `affected >= 0`, so the empty-section case
	// the paragraph above protects is unaffected; only a real engine failure is named here.
	if (!ibDataQueryBuilder()
		.WithAccessPolicy(nullptr)
		.From(m_metaTable->GetQueryable())
		.Where(ibOwnerRefColumn(), ibValue(m_objectValue->GetGuid()))
		.Delete())
		ibBackendCoreException::Error(_("Tabular section '%s': failed to clear the stored rows"),
			m_metaTable->GetSynonym());

	return true;
}
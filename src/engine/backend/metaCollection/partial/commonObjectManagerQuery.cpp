////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : the managers' own reads — FindByCode / FindByDescription
//	              of a manager with predefined items, and
//	              ibValueRecordManagerObject's manager-style operations
//	              over a record-set (Exist / Read / Save / Delete). Thin
//	              wrappers over m_recordSet that adapt the unique-key API
//	              to the manager surface.
//
//	              ⚠ NO CONNECTION IS TAKEN HERE ANY MORE. Three functions
//	              opened with `const auto db = ses_query;` and never read
//	              it — residue of the pre-L3 code. Not inert: ses_query
//	              acquires a pool connection and RAISES when there is no
//	              active session, so a dead local could fail a read that
//	              needs no database of its own. The record set reaches its
//	              own connection through the door.
////////////////////////////////////////////////////////////////////////////

#include "commonObject.h"

#include "backend/appData.h"
#include "backend/session/session.h"

#include "backend/query/dataQueryBuilder.h"   // L3 door — composite-key existence probe, FindBy*

#include "backend/metaCollection/attribute/metaAttributeObject.h"   // FindBy* — the attribute read
#include "backend/metaCollection/partial/reference/reference.h"     // …and the reference it answers with

#include "backend/system/systemManager.h"

// ⭐⭐ FIND BY CODE / BY DESCRIPTION — ONE BODY. Five managers (catalog, the charts of accounts,
// calculation types and characteristic types, the parameterized job) each carried a copy of this, and
// all five handed back FREED MEMORY: the row's reference was held by a local value, the Create that
// followed found that very instance in the session's register and returned it bare, and the local let
// go of it on the way out — the last hold. The caller's first method call on the result took the
// application down (2026-09-10, `Catalogs.Employees.FindByDescription(...).IsEmpty()`, heap corruption),
// and a report parameter worked out through it came back empty. One copy (the calculation types') also
// still read the identity out of the reference's TEXT — its presentation — and so found nothing ever.
//
// The answer leaves as a VALUE: the row's own reference, held by the value that carries it out. The
// query goes through the L3 door (the engine's FIRST / LIMIT fork is L2's), the pattern as a bound value.
static ibValue ibFindRefLike(const ibValueMetaObjectRecordDataHierarchyMutableRef* meta,
                             ibValueMetaObjectAttributePredefined* attribute, const ibValue& pattern)
{
	if (meta == nullptr)
		return ibValue();
	if (attribute == nullptr || pattern.IsEmpty() || appData->DesignerMode())
		return ibValueReferenceDataObject::Create(meta);
	try {
		ibDataQueryBuilder q;
		q.From(meta->GetQueryable()).WhereLike(attribute->GetQueryColumn(), attribute->AdjustValue(pattern));
		ibReadPageRequest page;
		page.m_count = 1;
		ibDataQueryResult sel = q.Execute(page);
		if (sel.Next()) {
			// The identity column by NAME, not the last item of the identity sort (an ordering of the
			// object's own puts something else there).
			const ibValue found = sel.GetValue(meta->GetDataReference()->GetQueryColumn());
			if (found.ConvertToType<ibValueReferenceDataObject>() != nullptr)
				return found;
		}
	}
	catch (...) { /* fall through to an empty reference */ }
	return ibValueReferenceDataObject::Create(meta);
}

ibValue ibValueManagerDataObjectPredefined::FindByCode(const ibValue& code) const
{
	const ibValueMetaObjectRecordDataHierarchyMutableRef* meta = GetMetaObject();
	return ibFindRefLike(meta, meta != nullptr ? meta->GetDataCode() : nullptr, code);
}

ibValue ibValueManagerDataObjectPredefined::FindByDescription(const ibValue& description) const
{
	const ibValueMetaObjectRecordDataHierarchyMutableRef* meta = GetMetaObject();
	return ibFindRefLike(meta, meta != nullptr ? meta->GetDataDescription() : nullptr, description);
}

bool ibValueRecordManagerObject::ExistData()
{
	bool success = false;

	if (m_recordLine != nullptr) {
		// Composite-key existence probe through the L3 door: each dimension is an
		// Eq condition, decomposed inside L3 across all its physical fields. The
		// manual scope / transaction / statement and the GetCompositeSQLFieldName
		// concat are gone — the door owns the borrow and the binding.
		try {
			ibDataQueryBuilder q;
			q.From(m_metaObject->GetQueryable());
			for (const auto object : m_metaObject->GetGenericDimensionArrayObject()) {
				ibValue retValue; m_recordLine->GetValueByMetaID(object->GetMetaID(), retValue);
				q.Where(object->GetQueryColumn(), ibQueryFilterOp::Equal, retValue);
			}
			ibReadPageRequest page;
			page.m_count = 1;
			ibDataQueryResult selection = q.Execute(page);
			success = selection.Next();
		}
		catch (...) {}
	}

	return success;
}

bool ibValueRecordManagerObject::ReadData(const ibUniqueKeyPair& key)
{
	if (m_recordSet->ReadData(key)) {
		if (m_recordLine == nullptr) {
			m_recordLine = m_recordSet->GetRowAt(
				m_recordSet->GetItem(0)
			);
		}
		return true;
	}

	return false;
}

bool ibValueRecordManagerObject::SaveData(bool replace)
{
	if (m_recordSet->Selected()
		&& !DeleteData())
		return false;

	if (ExistData()) {
		wxString fillError =
			wxString::Format(_("This entry already exists. It is not possible to write a new value!"));
		ibValueSystemFunction::Message(fillError, ibStatusMessage::ibStatusMessage_Information);
		return false;
	}

	m_recordSet->m_keyValues.clear();
	wxASSERT(m_recordLine);
	for (const auto object : m_metaObject->GetGenericDimensionArrayObject()) {
		ibValue retValue; m_recordLine->GetValueByMetaID(object->GetMetaID(), retValue);
		m_recordSet->m_keyValues.insert_or_assign(
			object->GetMetaID(), retValue
		);
	}
	if (m_recordSet->WriteRecordSet(replace, false)) {
		m_objGuid.SetKeyValues(m_recordSet->m_keyValues);
		return true;
	}
	return false;
}

bool ibValueRecordManagerObject::DeleteData()
{
	return m_recordSet->DeleteRecordSet();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////

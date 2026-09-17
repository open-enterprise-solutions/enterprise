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
#include "backend/metaCollection/partial/registerQueryLowering.h"   // ibRegWhereKeyValue — a key value as a condition

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
		// Composite-key existence probe through the L3 door: each dimension is the key's
		// condition (ibRegWhereKeyValue — a period as the whole of it), decomposed inside L3
		// across all its physical fields. The door owns the borrow and the binding.
		try {
			ibDataQueryBuilder q;
			q.From(m_metaObject->GetQueryable());
			for (const auto object : m_metaObject->GetGenericDimensionArrayObject()) {
				ibValue retValue; m_recordLine->GetValueByMetaID(object->GetMetaID(), retValue);
				ibRegWhereKeyValue(q, m_metaObject, object, retValue);
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

// ⭐⭐ A RECORD IS READ BY ITS KEY, AND WHAT IS READ IS WHAT THE MANAGER THEN HOLDS. The set is read afresh under
// the key — which clears the line the manager was holding — so the manager takes the record found, and the set
// remembers the key it was found under: a Write after it replaces THAT record, a Delete removes THAT record.
// Nothing under the key leaves the manager as the caller filled it, and not read.
//
// 🛑 It used to keep the old line pointer: after a script's Read the fields still showed what had been set (a
// price of 0), and the set, read with no key at all, was marked read — so the Delete that followed removed every
// record of the register (measured 2026-09-17: twenty deletes of one price each left a table of five rows).
bool ibValueRecordManagerObject::ReadData(const ibUniqueKeyPair& key)
{
	std::vector<std::pair<ibMetaID, ibValue>> filled;
	if (m_recordLine != nullptr)
		for (const auto object : m_metaObject->GetGenericAttributeArrayObject()) {
			ibValue value;
			m_recordLine->GetValueByMetaID(object->GetMetaID(), value);
			filled.emplace_back(object->GetMetaID(), value);
		}
	m_recordLine = nullptr;   // the set is read afresh; the line it held goes with it

	if (m_recordSet->ReadData(key)) {
		m_recordLine = m_recordSet->GetRowAt(m_recordSet->GetItem(0));
		m_recordSet->m_keyValues = key.GetKeyValues();
		m_objGuid.SetKeyValues(key.GetKeyValues());
		return true;
	}

	m_recordSet->m_selected = false;
	PrepareEmptyObject(nullptr);
	for (const auto& field : filled)
		m_recordLine->SetValueByMetaID(field.first, field.second);
	return false;
}

// The key the manager's own fields name — every dimension of the register, the period among them.
ibUniqueKeyPair ibRecordKeyOf(const ibValueMetaObjectRegisterData* meta, ibValueModel::ibValueModelReturnLine* line)
{
	ibUniqueKeyPair key = meta->CreateUniqueKeyPair();
	ibRowMetaValues values;
	if (line != nullptr)
		for (const auto object : meta->GetGenericDimensionArrayObject()) {
			ibValue value;
			line->GetValueByMetaID(object->GetMetaID(), value);
			values.insert_or_assign(object->GetMetaID(), value);
		}
	key.SetKeyValues(values);
	return key;
}

bool ibValueRecordManagerObject::SaveData(bool replace)
{
	if (m_recordSet->Selected()
		&& !DeleteData())
		return false;

	// ⭐⭐ A RECORD UNDER A KEY THAT IS TAKEN IS A REPLACEMENT WHEN REPLACEMENT WAS ASKED FOR. `Write(True)` —
	// the default — means "this is the record under this key now", whether one was there or not, and the
	// set's write below replaces by the key. The probe used to refuse it regardless: a price written again
	// for the same item, a rate for a month already rated, failed with "failed to store the record" and a
	// message box nobody reads in a script (measured 2026-09-17, both kinds of information register). Only
	// `Write(False)` asks to add and nothing else — and the door then says why it will not, in words.
	if (!replace && ExistData()) {
		ibBackendCoreException::Error(
			_("Register '%s': a record with these key values already exists. Write(True) replaces it."),
			m_metaObject != nullptr ? m_metaObject->GetSynonym() : wxString());
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

// A manager deletes ONE record: the one it read, or — never read — the one its fields name. Never the set with no
// key, which is the whole register (see ReadData).
bool ibValueRecordManagerObject::DeleteData()
{
	if (!m_recordSet->m_selected || m_recordSet->m_keyValues.empty())
		m_recordSet->m_keyValues = ibRecordKeyOf(m_metaObject, m_recordLine).GetKeyValues();
	return m_recordSet->DeleteRecordSet();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////

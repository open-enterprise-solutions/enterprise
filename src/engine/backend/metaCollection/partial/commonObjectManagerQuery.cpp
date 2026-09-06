////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : ibValueRecordManagerObject — manager-style operations
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

#include "backend/query/dataQueryBuilder.h"   // L3 door — composite-key existence probe

#include "backend/system/systemManager.h"

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

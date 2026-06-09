////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : reference - db
////////////////////////////////////////////////////////////////////////////

#include "reference.h"
#include "backend/appData.h"
#include "backend/session/session.h"
#include "backend/metaCollection/partial/commonObject.h"
#include "backend/metaCollection/partial/tabularSection/tabularSection.h"
#include "backend/query/dataQueryBuilder.h"   // L3 door — reference read by key / scan


bool ibValueReferenceDataObject::ReadData(bool createData)
{
	if (m_metaObject == nullptr || !m_objGuid.isValid())
		return false;

	// Load the row by its own key through the L3 door — the FB FIRST / others
	// LIMIT fork and the raw-concat '%s' guid (injection-shaped) are gone. Values
	// come from the L3 selection (GetValue), not the raw result set.
	try {
		ibDataQueryBuilder q;
		q.From(m_metaObject->GetQueryable()).WhereKey(m_objGuid);
		ibReadPageRequest page;
		page.m_count = 1;
		ibDataQueryResult selection = q.Execute(page);
		if (selection.Next()) {
			for (const auto object : m_metaObject->GetGenericAttributeArrayObject())
				if (!m_metaObject->IsDataReference(object->GetMetaID()))
					m_listObjectValue[object->GetMetaID()] = selection.GetValue(object);
			return true;
		}
	}
	catch (...) {}
	return false;
}

bool ibValueReferenceDataObject::FindValue(const wxString& findData, std::vector<ibValue>& listValue) const
{
	class ibValueDataObjectComparator : public ibValueDataObject {
		bool ReadValues() {
			if (m_metaObject == nullptr || m_newObject)
				return false;
			try {
				ibDataQueryBuilder q;
				q.From(m_metaObject->GetQueryable()).WhereKey(m_objGuid);
				ibReadPageRequest page;
				page.m_count = 1;
				ibDataQueryResult selection = q.Execute(page);
				if (selection.Next())
					for (const auto object : m_metaObject->GetGenericAttributeArrayObject())
						if (!m_metaObject->IsDataReference(object->GetMetaID()))
							m_listObjectValue[object->GetMetaID()] = selection.GetValue(object);
				return true;
			}
			catch (...) { return false; }
		}
		const ibValueMetaObjectRecordDataRef* m_metaObject;
	private:
		ibValueDataObjectComparator(const ibValueMetaObjectRecordDataRef* metaObject, const ibGuid& guid) : ibValueDataObject(guid, false), m_metaObject(metaObject) {}
	public:

		static bool CompareValue(const wxString& findData, const ibValueMetaObjectRecordDataRef* metaObject, const ibGuid& guid) {
			bool allow = false;
			ibValueDataObjectComparator* comparator = new ibValueDataObjectComparator(metaObject, guid);
			if (comparator->ReadValues()) {
				for (const auto object : metaObject->GetSearchedAttributeObjectArray()) {
					const wxString& fieldCompare = comparator->m_listObjectValue[object->GetMetaID()].GetString();
					if (fieldCompare.Contains(findData)) {
						allow = true; break;
					}
				}
			}
			if (!allow) {
				const wxString& fieldCompare = metaObject->GetDataPresentation(comparator);
				if (fieldCompare.Contains(findData)) {
					allow = true;
				}
			}
			wxDELETE(comparator);
			return allow;
		}

		//get metaData from object
		virtual const ibValueMetaObjectRecordData* GetMetaObject() const {
			return m_metaObject;
		}
	};
	// Full scan ordered by key through the door (count <= 0 = unbounded); each
	// row's text is compared by the nested comparator.
	try {
		ibDataQueryBuilder q;
		q.From(m_metaObject->GetQueryable());
		ibReadPageRequest page;
		page.m_count = 0;   // no limit — full scan
		ibDataQueryResult selection = q.Execute(page);
		const ibBackendQueryColumn* keyCol = m_metaObject->GetQueryable()->GetIdentitySort().back().m_col;   // uuid identity column
		while (selection.Next()) {
			const ibGuid currentGuid = selection.GetValue(keyCol).GetString();
			if (ibValueDataObjectComparator::CompareValue(findData, m_metaObject, currentGuid))
				listValue.push_back(ibValueReferenceDataObject::Create(m_metaObject, currentGuid));
		}
		std::sort(listValue.begin(), listValue.end(),
			[](const ibValue& a, const ibValue& b) { return a.GetString() < b.GetString(); });
		return listValue.size() > 0;
	}
	catch (...) { return false; }
}
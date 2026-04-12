////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : reference - db
////////////////////////////////////////////////////////////////////////////

#include "reference.h"
#include "backend/appData.h"
#include "backend/metaCollection/partial/commonObject.h"
#include "backend/metaCollection/partial/tabularSection/tabularSection.h"
#include "backend/databaseLayer/databaseLayer.h"


bool ibValueReferenceDataObject::ReadData(bool createData)
{
	if (m_metaObject == nullptr || !m_objGuid.isValid())
		return false;

	const wxString& tableName = m_metaObject->GetTableNameDB();

	if (db_query->TableExists(tableName)) {
		ibDatabaseResultSet* resultSet = nullptr;	
		if (db_query->GetDatabaseLayerType() != DATABASELAYER_FIREBIRD)
			resultSet = db_query->RunQueryWithResults("SELECT * FROM %s WHERE uuid = '%s' LIMIT 1;", tableName, m_objGuid.str());
		else
			resultSet = db_query->RunQueryWithResults("SELECT FIRST 1 * FROM %s WHERE uuid = '%s';", tableName, m_objGuid.str());
		if (resultSet == nullptr)
			return false;
		bool readRef = false;
		if (resultSet->Next()) {			
			//load attributes 
			for (const auto object : m_metaObject->GetGenericAttributeArrayObject()) {
				if (!m_metaObject->IsDataReference(object->GetMetaID())) {
					ibValueMetaObjectAttributeBase::GetValueAttribute(
						object,
						m_listObjectValue[object->GetMetaID()],
						resultSet,
						createData
					);
				}
			}
			// table is collection values 
			//for (const auto object : m_metaObject->GetTableArrayObject()) {
			//	m_listObjectValue.insert_or_assign(object->GetMetaID(), 
			//		ibValue::CreateAndPrepareValueRef<ibValueTabularSectionDataObjectRef>(this, object, true));
			//}

			readRef = true;
		}	
		db_query->CloseResultSet(resultSet);
		return readRef;
	}
	return false;
}

bool ibValueReferenceDataObject::FindValue(const wxString& findData, std::vector<ibValue>& listValue) const
{
	class ibValueDataObjectComparator : public ibValueDataObject {
		bool ReadValues() {
			if (m_metaObject == nullptr || m_newObject)
				return false;
			const wxString& tableName = m_metaObject->GetTableNameDB();
			if (db_query->TableExists(tableName)) {

				ibDatabaseResultSet* resultSet = nullptr;
				if (db_query->GetDatabaseLayerType() != DATABASELAYER_FIREBIRD)
					resultSet = db_query->RunQueryWithResults("SELECT * FROM " + tableName + " WHERE uuid = '" + m_objGuid.str() + "' LIMIT 1;");
				else
					resultSet = db_query->RunQueryWithResults("SELECT FIRST 1 * FROM " + tableName + " WHERE uuid = '" + m_objGuid.str() + "';");

				if (!resultSet)
					return false;
				if (resultSet->Next()) {
					//load other attributes 
					for (const auto object : m_metaObject->GetGenericAttributeArrayObject()) {
						if (m_metaObject->IsDataReference(object->GetMetaID()))
							continue;
						ibValueMetaObjectAttributeBase::GetValueAttribute(object, m_listObjectValue[object->GetMetaID()], resultSet);
					}
				}
				db_query->CloseResultSet(resultSet);
				return true;
			}
			return false;
		}
		ibValueMetaObjectRecordDataRef* m_metaObject;
	private:
		ibValueDataObjectComparator(ibValueMetaObjectRecordDataRef* metaObject, const ibGuid& guid) : ibValueDataObject(guid, false), m_metaObject(metaObject) {}
	public:

		static bool CompareValue(const wxString& findData, ibValueMetaObjectRecordDataRef* metaObject, const ibGuid& guid) {
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
		virtual ibValueMetaObjectRecordData* GetMetaObject() const {
			return m_metaObject;
		}
	};
	const wxString& tableName = m_metaObject->GetTableNameDB();
	if (db_query->TableExists(tableName)) {
		ibDatabaseResultSet* resultSet = db_query->RunQueryWithResults("SELECT * FROM %s ORDER BY uuid; ", tableName);
		if (resultSet == nullptr)
			return false;
		while (resultSet->Next()) {
			const ibGuid& currentGuid = resultSet->GetResultString(guidName);
			if (ibValueDataObjectComparator::CompareValue(findData, m_metaObject, currentGuid)) {
				listValue.push_back(
					ibValueReferenceDataObject::Create(m_metaObject, currentGuid)
				);
			}
		}
		std::sort(listValue.begin(), listValue.end(), [](const ibValue& a, const ibValue& b) { return a.GetString() < b.GetString(); });
		db_query->CloseResultSet(resultSet);
		return listValue.size() > 0;
	}

	return false;
}
////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : reference - db
////////////////////////////////////////////////////////////////////////////

#include "reference.h"
#include "appData.h"
#include "core/metadata/metaObjects/objects/object.h"
#include "core/metadata/metaObjects/objects/tabularSection/tabularSection.h"
#include <3rdparty/databaseLayer/databaseLayer.h>
#include "utils/stringUtils.h"

bool CReferenceDataObject::ReadData(bool createData)
{
	if (m_metaObject == NULL || !m_objGuid.isValid())
		return false;

	const wxString& tableName = m_metaObject->GetTableNameDB();
	if (databaseLayer->TableExists(tableName)) {

		DatabaseResultSet* resultSet = NULL;
		if (databaseLayer->GetDatabaseLayerType() == DATABASELAYER_POSTGRESQL)
			resultSet = databaseLayer->RunQueryWithResults("SELECT * FROM %s WHERE UUID = '%s' LIMIT 1;", tableName, m_objGuid.str());
		else
			resultSet = databaseLayer->RunQueryWithResults("SELECT FIRST 1 * FROM %s WHERE UUID = '%s';", tableName, m_objGuid.str());

		if (resultSet == NULL)
			return false;

		bool readRef = false;
		
		if (resultSet->Next()) {		
			readRef = true;		
			//load attributes 
			for (auto attribute : m_metaObject->GetGenericAttributes()) {		
				if (!m_metaObject->IsDataReference(attribute->GetMetaID())) {
					IMetaAttributeObject::GetValueAttribute(
						attribute, 
						m_objectValues[attribute->GetMetaID()], 
						resultSet,
						createData
					);
				}
			}

			// table is collection values 
			for (auto table : m_metaObject->GetObjectTables()) {
				m_objectValues.insert_or_assign(table->GetMetaID(), new CTabularSectionDataObjectRef(this, table, true));
			}
		}	
		resultSet->Close();
		return readRef;
	}

	return false;
}

bool CReferenceDataObject::FindValue(const wxString& findData, std::vector<CValue>& foundedObjects) const
{
	class CObjectComparatorValue : public IObjectValueInfo {
		bool ReadValues() {
			if (m_metaObject == NULL || m_newObject)
				return false;
			const wxString& tableName = m_metaObject->GetTableNameDB();
			if (databaseLayer->TableExists(tableName)) {

				DatabaseResultSet* resultSet = NULL;
				if (databaseLayer->GetDatabaseLayerType() == DATABASELAYER_POSTGRESQL)
					resultSet = databaseLayer->RunQueryWithResults("SELECT * FROM " + tableName + " WHERE UUID = '" + m_objGuid.str() + "' LIMIT 1;");
				else
					resultSet = databaseLayer->RunQueryWithResults("SELECT FIRST 1 * FROM " + tableName + " WHERE UUID = '" + m_objGuid.str() + "';");

				if (!resultSet)
					return false;
				if (resultSet->Next()) {
					//load other attributes 
					for (auto attribute : m_metaObject->GetGenericAttributes()) {
						if (m_metaObject->IsDataReference(attribute->GetMetaID()))
							continue;
						IMetaAttributeObject::GetValueAttribute(attribute, m_objectValues[attribute->GetMetaID()], resultSet);
					}
				}
				resultSet->Close();
				return true;
			}
			return false;
		}
		IMetaObjectRecordDataRef* m_metaObject;
	private:
		CObjectComparatorValue(IMetaObjectRecordDataRef* metaObject, const Guid& guid) : IObjectValueInfo(guid, false), m_metaObject(metaObject) {}
	public:

		static bool CompareValue(const wxString& findData, IMetaObjectRecordDataRef* metaObject, const Guid& guid) {
			bool allow = false;
			CObjectComparatorValue* comparator = new CObjectComparatorValue(metaObject, guid);
			if (comparator->ReadValues()) {
				for (auto attribute : metaObject->GetSearchedAttributes()) {
					const wxString& fieldCompare = comparator->m_objectValues[attribute->GetMetaID()].GetString();
					if (fieldCompare.Contains(findData)) {
						allow = true; break;
					}
				}
			}
			if (!allow) {
				const wxString& fieldCompare = metaObject->GetDescription(comparator);
				if (fieldCompare.Contains(findData)) {
					allow = true;
				}
			}
			wxDELETE(comparator);
			return allow;
		}

		//get metadata from object 
		virtual IMetaObjectRecordData* GetMetaObject() const {
			return m_metaObject;
		}
	};
	const wxString& tableName = m_metaObject->GetTableNameDB();
	if (databaseLayer->TableExists(tableName)) {
		DatabaseResultSet* resultSet = databaseLayer->RunQueryWithResults("SELECT * FROM %s ORDER BY CAST(UUIDREF AS VARCHAR(36)); ", tableName);
		if (resultSet == NULL)
			return false;
		while (resultSet->Next()) {
			const Guid& currentGuid = resultSet->GetResultString(guidName);
			if (CObjectComparatorValue::CompareValue(findData, m_metaObject, currentGuid)) {
				foundedObjects.push_back(
					CReferenceDataObject::Create(m_metaObject, currentGuid)
				);
			}
		}
		std::sort(foundedObjects.begin(), foundedObjects.end(), [](const CValue& a, const CValue& b) { return a > b; });
		resultSet->Close();
		return foundedObjects.size() > 0;
	}

	return false;
}
////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : reference - db
////////////////////////////////////////////////////////////////////////////

#include "reference.h"
#include "appData.h"
#include "metadata/metaObjects/objects/object.h"
#include "metadata/metaObjects/objects/tabularSection/tabularSection.h"
#include "databaseLayer/databaseLayer.h"
#include "utils/stringUtils.h"

bool CReferenceDataObject::ReadData(bool createData)
{
	if (!m_metaObject || !m_objGuid.isValid())
		return false;

	wxString tableName = m_metaObject->GetTableNameDB();
	if (databaseLayer->TableExists(tableName)) {
		bool isLoaded = false;			
		DatabaseResultSet* resultSet = databaseLayer->RunQueryWithResults("SELECT FIRST 1 * FROM %s WHERE UUID = '%s';", tableName, m_objGuid.str());
		if (resultSet == NULL)
			return false;
		for (auto currTable : m_metaObject->GetObjectTables()) {
			CTabularSectionDataObjectRef* tabularSection =
				new CTabularSectionDataObjectRef(this, currTable);
			m_aObjectValues[currTable->GetMetaID()] = tabularSection;
			m_aObjectTables.push_back(tabularSection);
		}

		if (resultSet->Next()) {
			//load attributes 
			for (auto attribute : m_metaObject->GetGenericAttributes()) {
				if (!m_metaObject->IsDataReference(attribute->GetMetaID())) {
					m_aObjectValues.insert_or_assign(attribute->GetMetaID(),
						IMetaAttributeObject::GetValueAttribute(attribute, resultSet, createData)
					);
				}
				else {
					m_aObjectValues.insert_or_assign(attribute->GetMetaID(),
						CValue()
					);
				}
			}
			isLoaded = true;
			for (auto tabularSection : m_aObjectTables) {
				if (!tabularSection->LoadData(createData)) {
					isLoaded = false;
				}
			}
		}
		resultSet->Close();
		return isLoaded;
	}
	return false;
}

bool CReferenceDataObject::FindValue(const wxString& findData, std::vector<CValue>& foundedObjects)
{
	class CObjectComparatorValue : public IObjectValueInfo {
		bool ReadValues()
		{
			if (m_metaObject == NULL || m_newObject)
				return false;
			wxString tableName = m_metaObject->GetTableNameDB();
			if (databaseLayer->TableExists(tableName)) {
				wxString queryText = "SELECT FIRST 1 * FROM " + tableName + " WHERE UUID = '" + m_objGuid.str() + "';";
				DatabaseResultSet* resultSet = databaseLayer->RunQueryWithResults(queryText);
				if (!resultSet) {
					return false;
				}
				if (resultSet->Next()) {
					//load other attributes 
					for (auto attribute : m_metaObject->GetGenericAttributes()) {
						if (m_metaObject->IsDataReference(attribute->GetMetaID()))
							continue;
						m_aObjectValues.insert_or_assign(
							attribute->GetMetaID(),
							IMetaAttributeObject::GetValueAttribute(
								attribute, resultSet)
						);
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
					wxString fieldCompare = comparator->m_aObjectValues[attribute->GetMetaID()].GetString();
					if (fieldCompare.Contains(findData)) {
						allow = true; break;
					}
				}
			}
			if (!allow) {
				wxString fieldCompare = metaObject->GetDescription(comparator);
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

	wxString tableName = m_metaObject->GetTableNameDB();
	
	if (databaseLayer->TableExists(tableName)) {
		
		DatabaseResultSet* resultSet = databaseLayer->RunQueryWithResults("SELECT * FROM %s ORDER BY CAST(UUID AS VARCHAR(36)); ", tableName);
		
		if (resultSet == NULL)
			return false;

		while (resultSet->Next()) {
			auto currentGuid = resultSet->GetResultString(guidName);
			if (CObjectComparatorValue::CompareValue(findData, m_metaObject, currentGuid)) {
				foundedObjects.push_back(
					CReferenceDataObject::Create(m_metaObject, currentGuid)
				);
			}
		}
		resultSet->Close();
		return foundedObjects.size() > 0;
	}

	return false;
}
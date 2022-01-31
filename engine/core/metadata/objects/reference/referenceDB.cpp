////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : reference - db
////////////////////////////////////////////////////////////////////////////

#include "reference.h"
#include "appData.h"
#include "metadata/objects/baseObject.h"
#include "metadata/objects/tabularSection/tabularSection.h"
#include "databaseLayer/databaseLayer.h"
#include "utils/stringUtils.h"

bool CValueReference::ReadReferenceInDB()
{
	if (!m_metaObject || !m_objGuid.isValid())
		return false;
	wxString tableName = m_metaObject->GetTableNameDB();
	if (databaseLayer->TableExists(tableName)) {
		bool isLoaded = false;
		wxString sql = "SELECT FIRST 1 * FROM " + tableName + " WHERE UUID = '" + m_objGuid.str() + "';";
		DatabaseResultSet *resultSet = databaseLayer->RunQueryWithResults(sql);

		if (!resultSet)
			return false;

		for (auto currTable : m_metaObject->GetObjectTables()) {
			CValueTabularRefSection *tabularSection =
				new CValueTabularRefSection(this, currTable);
			m_aObjectValues[currTable->GetMetaID()] = tabularSection;
			m_aObjectTables.push_back(tabularSection);
		}

		if (resultSet->Next()) {
			//load attributes 
			for (auto attribute : m_metaObject->GetObjectAttributes()) {
				wxString nameAttribute = attribute->GetFieldNameDB();
				switch (attribute->GetTypeObject())
				{
				case eValueTypes::TYPE_BOOLEAN:
					m_aObjectValues[attribute->GetMetaID()] = resultSet->GetResultBool(nameAttribute); break;
				case eValueTypes::TYPE_NUMBER:
					m_aObjectValues[attribute->GetMetaID()] = resultSet->GetResultNumber(nameAttribute); break;
				case eValueTypes::TYPE_DATE:
					m_aObjectValues[attribute->GetMetaID()] = resultSet->GetResultDate(nameAttribute); break;
				case eValueTypes::TYPE_STRING:
					m_aObjectValues[attribute->GetMetaID()] = resultSet->GetResultString(nameAttribute); break;
				default:
				{
					wxMemoryBuffer bufferData;
					resultSet->GetResultBlob(nameAttribute, bufferData);
					if (!bufferData.IsEmpty()) {
						m_aObjectValues[attribute->GetMetaID()] = CValueReference::CreateFromPtr(
							m_metaObject->GetMetadata(), bufferData.GetData()
						);
					}
					else {
						m_aObjectValues[attribute->GetMetaID()] = new CValueReference(
							m_metaObject->GetMetadata(), attribute->GetTypeObject()
						);
					}
					break;
				}
				}
			}
			isLoaded = true;
			for (auto tabularSection : m_aObjectTables) {
				if (!tabularSection->LoadDataFromDB()) {
					isLoaded = false;
				}
			}
		}
		resultSet->Close();
		return isLoaded;
	}
	return false;
}

bool CValueReference::FindValue(const wxString &findData, std::vector<CValue>& foundedObjects)
{
	class CObjectComparatorValue : public IObjectValueInfo {
		bool ReadValues()
		{
			if (!m_metaObject || m_bNewObject)
				return false;
			wxString tableName = m_metaObject->GetTableNameDB();
			if (databaseLayer->TableExists(tableName)) {
				wxString queryText = "SELECT FIRST 1 * FROM " + tableName + " WHERE UUID = '" + m_objGuid.str() + "';";
				DatabaseResultSet *resultSet = databaseLayer->RunQueryWithResults(queryText);
				if (!resultSet) {
					return false;
				}
				if (resultSet->Next()) {
					//load other attributes 
					for (auto attribute : m_metaObject->GetObjectAttributes()) {
						wxString nameAttribute = attribute->GetFieldNameDB();
						switch (attribute->GetTypeObject())
						{
						case eValueTypes::TYPE_BOOLEAN:
							m_aObjectValues[attribute->GetMetaID()] = resultSet->GetResultBool(nameAttribute); break;
						case eValueTypes::TYPE_NUMBER:
							m_aObjectValues[attribute->GetMetaID()] = resultSet->GetResultNumber(nameAttribute); break;
						case eValueTypes::TYPE_DATE:
							m_aObjectValues[attribute->GetMetaID()] = resultSet->GetResultDate(nameAttribute); break;
						case eValueTypes::TYPE_STRING:
							m_aObjectValues[attribute->GetMetaID()] = resultSet->GetResultString(nameAttribute); break;
						default:
						{
							wxMemoryBuffer bufferData;
							resultSet->GetResultBlob(nameAttribute, bufferData);
							if (!bufferData.IsEmpty()) {
								m_aObjectValues[attribute->GetMetaID()] = CValueReference::CreateFromPtr(
									m_metaObject->GetMetadata(), bufferData.GetData()
								);
							}
							else {
								m_aObjectValues[attribute->GetMetaID()] = new CValueReference(
									m_metaObject->GetMetadata(), attribute->GetTypeObject()
								);
							}
							break;
						}
						}
					}
				}
				resultSet->Close();
				return true;
			}
			return false;
		}

		IMetaObjectRefValue *m_metaObject;

	private:
		CObjectComparatorValue(IMetaObjectRefValue *metaObject, const Guid &guid) : IObjectValueInfo(guid, false), m_metaObject(metaObject) {}
	public:

		static bool CompareValue(const wxString &findData, IMetaObjectRefValue *metaObject, const Guid &guid) {
			bool allow = false;
			CObjectComparatorValue *comparator = new CObjectComparatorValue(metaObject, guid);
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
		virtual IMetaObjectValue *GetMetaObject() const { return m_metaObject; };
	};

	wxString tableName = m_metaObject->GetTableNameDB();
	if (databaseLayer->TableExists(tableName)) {
		DatabaseResultSet *resultSet = databaseLayer->RunQueryWithResults("SELECT * FROM %s ORDER BY CAST(UUID AS VARCHAR(36)); ", tableName);
		if (!resultSet) {
			return false;
		}
		while (resultSet->Next()) {
			Guid currentGuid = resultSet->GetResultString(guidName); 
			if (CObjectComparatorValue::CompareValue(findData, m_metaObject, currentGuid)) {
				foundedObjects.push_back(
					new CValueReference(m_metaObject, currentGuid)
				);
			}
		}
		resultSet->Close();
		return foundedObjects.size() > 0;
	}

	return false;
}
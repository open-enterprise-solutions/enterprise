#include "objectSelector.h"

#include "backend/metaCollection/partial/tabularSection/tabularSection.h"

#include "backend/databaseLayer/databaseLayer.h"
#include "backend/appData.h"

/////////////////////////////////////////////////////////////////////////

void CSelectorDataObject::Reset()
{
	m_objGuid.reset(); m_newObject = false;
	if (!appData->DesignerMode()) {
		m_currentValues.clear();
		IPreparedStatement* statement = db_query->PrepareStatement("SELECT _uuid FROM %s ORDER BY CAST(_uuid AS VARCHAR(36)); ", m_metaObject->GetTableNameDB());
		IDatabaseResultSet* resultSet = statement->RunQueryWithResults();
		while (resultSet->Next()) {
			m_currentValues.push_back(
				resultSet->GetResultString(guidName)
			);
		};
		db_query->CloseResultSet(resultSet);
		db_query->CloseStatement(statement);
	}
	for (auto& obj : m_metaObject->GetObjectAttributes()) {
		if (!appData->DesignerMode()) {
			m_objectValues.insert_or_assign(obj->GetMetaID(), eValueTypes::TYPE_NULL);
		}
		else {
			m_objectValues.insert_or_assign(obj->GetMetaID(), obj->CreateValue());
		}
	}
	for (auto& obj : m_metaObject->GetObjectTables()) {
		if (!appData->DesignerMode()) {
			m_objectValues.insert_or_assign(obj->GetMetaID(), eValueTypes::TYPE_NULL);
		}
		else {
			m_objectValues.insert_or_assign(obj->GetMetaID(), new CTabularSectionDataObjectRef(this, obj));
		}
	}
}

bool CSelectorDataObject::Read()
{
	if (!m_objGuid.isValid())
		return false;

	m_objectValues.clear();

	IPreparedStatement* statement = nullptr;
	if (db_query->GetDatabaseLayerType() == DATABASELAYER_POSTGRESQL)
		statement = db_query->PrepareStatement("SELECT * FROM %s WHERE _uuid = '%s' LIMIT; ", m_metaObject->GetTableNameDB(), m_objGuid.str());
	else
		statement = db_query->PrepareStatement("SELECT FIRST 1 * FROM %s WHERE _uuid = '%s'; ", m_metaObject->GetTableNameDB(), m_objGuid.str());

	if (statement == nullptr)
		return false;
	bool isLoaded = false;
	IDatabaseResultSet* resultSet = statement->RunQueryWithResults();
	if (resultSet->Next()) {

		m_objectValues.insert_or_assign(m_metaObject->GetMetaID(), CReferenceDataObject::CreateFromResultSet(resultSet, m_metaObject, m_objGuid));

		//load attributes 
		for (auto& obj : m_metaObject->GetObjectAttributes()) {
			if (m_metaObject->IsDataReference(obj->GetMetaID()))
				continue;
			IMetaObjectAttribute::GetValueAttribute(
				obj, m_objectValues[obj->GetMetaID()], resultSet);
		}
		for (auto& obj : m_metaObject->GetObjectTables()) {
			CTabularSectionDataObjectRef* tabularSection = CValue::CreateAndConvertObjectValueRef<CTabularSectionDataObjectRef>(this, obj);
			if (!tabularSection->LoadData(m_objGuid))
				isLoaded = false;
			m_objectValues.insert_or_assign(obj->GetMetaID(), tabularSection);
		}

		isLoaded = true;
	}
	db_query->CloseResultSet(resultSet);
	db_query->CloseStatement(statement);
	return isLoaded;
}

/////////////////////////////////////////////////////////////////////////

void CSelectorRegisterObject::Reset()
{
	m_keyValues.clear();
	if (!appData->DesignerMode()) {
		m_currentValues.clear();
		IPreparedStatement* statement = db_query->PrepareStatement("SELECT * FROM %s; ", m_metaObject->GetTableNameDB());
		IDatabaseResultSet* resultSet = statement->RunQueryWithResults();
		while (resultSet->Next()) {
			valueArray_t keyRow;
			if (m_metaObject->HasRecorder()) {
				CMetaObjectAttributeDefault* attributeRecorder = m_metaObject->GetRegisterRecorder();
				wxASSERT(attributeRecorder);
				IMetaObjectAttribute::GetValueAttribute(attributeRecorder, keyRow[attributeRecorder->GetMetaID()], resultSet);
				CMetaObjectAttributeDefault* attributeNumberLine = m_metaObject->GetRegisterLineNumber();
				wxASSERT(attributeNumberLine);
				IMetaObjectAttribute::GetValueAttribute(attributeNumberLine, keyRow[attributeNumberLine->GetMetaID()], resultSet);
			}
			else {
				for (auto& obj : m_metaObject->GetGenericDimensions()) {
					IMetaObjectAttribute::GetValueAttribute(obj, keyRow[obj->GetMetaID()], resultSet);
				}
			}
			m_currentValues.push_back(keyRow);
		};
		db_query->CloseResultSet(resultSet);
		db_query->CloseStatement(statement);
	}
}

bool CSelectorRegisterObject::Read()
{
	if (m_keyValues.empty())
		return false;

	m_objectValues.clear(); 
	
	int position = 1;
	
	wxString queryText = ""; bool isLoaded = false;

	if (db_query->GetDatabaseLayerType() == DATABASELAYER_POSTGRESQL) {
		queryText = "SELECT FIRST 1 * FROM " + m_metaObject->GetTableNameDB(); bool firstWhere = true;
		for (auto& obj : m_metaObject->GetGenericDimensions()) {
			if (firstWhere) {
				queryText = queryText + " WHERE ";
			}
			queryText = queryText +
				(firstWhere ? " " : " AND ") + IMetaObjectAttribute::GetCompositeSQLFieldName(obj);
			if (firstWhere) {
				firstWhere = false;
			}
		}
	}
	else {
		queryText = "SELECT * FROM " + m_metaObject->GetTableNameDB(); bool firstWhere = true;
		for (auto& obj : m_metaObject->GetGenericDimensions()) {
			if (firstWhere) {
				queryText = queryText + " WHERE ";
			}
			queryText = queryText +
				(firstWhere ? " " : " AND ") + IMetaObjectAttribute::GetCompositeSQLFieldName(obj);
			if (firstWhere) {
				firstWhere = false;
			}
		}
		queryText += " LIMIT 1 ";
	}

	IPreparedStatement* statement = db_query->PrepareStatement(queryText);

	if (statement == nullptr)
		return false;

	for (auto& obj : m_metaObject->GetGenericDimensions()) {
		IMetaObjectAttribute::SetValueAttribute(
			obj,
			m_keyValues.at(obj->GetMetaID()),
			statement,
			position
		);
	}

	IDatabaseResultSet* resultSet = statement->RunQueryWithResults();

	if (resultSet->Next()) {
		isLoaded = true;
		//load attributes 
		valueArray_t keyTable, rowTable;
		for (auto& obj : m_metaObject->GetGenericDimensions())
			IMetaObjectAttribute::GetValueAttribute(obj, keyTable[obj->GetMetaID()], resultSet);
		for (auto& obj : m_metaObject->GetGenericAttributes())
			IMetaObjectAttribute::GetValueAttribute(obj, rowTable[obj->GetMetaID()], resultSet);
		m_objectValues.insert_or_assign(keyTable, rowTable);
	}
	db_query->CloseResultSet(resultSet);
	db_query->CloseStatement(statement);
	return isLoaded;
}

#include "objectSelector.h"
#include "core/metadata/metaObjects/objects/tabularSection/tabularSection.h"
#include <3rdparty/databaseLayer/databaseLayer.h>
#include "appData.h"

ISelectorObject::ISelectorObject() : CValue(eValueTypes::TYPE_VALUE, true),
m_methodHelper(new CMethodHelper())
{
}

ISelectorObject::~ISelectorObject()
{
	wxDELETE(m_methodHelper);
}

#include "core/metadata/singleClass.h"

CLASS_ID ISelectorObject::GetTypeClass() const
{
	IMetaTypeObjectValueSingle* clsFactory =
		GetMetaObject()->GetTypeObject(eMetaObjectType::enSelection);
	wxASSERT(clsFactory);
	return clsFactory->GetTypeClass();
}

wxString ISelectorObject::GetTypeString() const
{
	IMetaTypeObjectValueSingle* clsFactory =
		GetMetaObject()->GetTypeObject(eMetaObjectType::enSelection);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

wxString ISelectorObject::GetString() const
{
	IMetaTypeObjectValueSingle* clsFactory =
		GetMetaObject()->GetTypeObject(eMetaObjectType::enSelection);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

/////////////////////////////////////////////////////////////////////////

void CSelectorDataObject::Reset()
{
	m_objGuid.reset(); m_newObject = false;
	if (!appData->DesignerMode()) {
		m_currentValues.clear();
		PreparedStatement* statement = databaseLayer->PrepareStatement("SELECT UUID FROM %s ORDER BY CAST(UUID AS VARCHAR(36)); ", m_metaObject->GetTableNameDB());
		DatabaseResultSet* resultSet = statement->RunQueryWithResults();
		while (resultSet->Next()) {
			m_currentValues.push_back(
				resultSet->GetResultString(guidName)
			);
		};
		databaseLayer->CloseResultSet(resultSet);
		databaseLayer->CloseStatement(statement);
	}
	for (auto attribute : m_metaObject->GetObjectAttributes()) {
		if (!appData->DesignerMode()) {
			m_objectValues.insert_or_assign(attribute->GetMetaID(), eValueTypes::TYPE_NULL);
		}
		else {
			m_objectValues.insert_or_assign(attribute->GetMetaID(), attribute->CreateValue());
		}
	}
	for (auto table : m_metaObject->GetObjectTables()) {
		if (!appData->DesignerMode()) {
			m_objectValues.insert_or_assign(table->GetMetaID(), eValueTypes::TYPE_NULL);
		}
		else {
			m_objectValues.insert_or_assign(table->GetMetaID(), new CTabularSectionDataObjectRef(this, table));
		}
	}
}

bool CSelectorDataObject::Read()
{
	if (!m_objGuid.isValid())
		return false;
	m_objectValues.clear();

	PreparedStatement* statement = NULL;
	if (databaseLayer->GetDatabaseLayerType() == DATABASELAYER_POSTGRESQL)
		statement = databaseLayer->PrepareStatement("SELECT * FROM %s WHERE UUID = '%s' LIMIT; ", m_metaObject->GetTableNameDB(), m_objGuid.str());
	else
		statement = databaseLayer->PrepareStatement("SELECT FIRST 1 * FROM %s WHERE UUID = '%s'; ", m_metaObject->GetTableNameDB(), m_objGuid.str());

	if (statement == NULL)
		return false;
	bool isLoaded = false;
	DatabaseResultSet* resultSet = statement->RunQueryWithResults();
	if (resultSet->Next()) {
		isLoaded = true;
		wxMemoryBuffer bufferData;
		resultSet->GetResultBlob(guidRef, bufferData);
		if (!bufferData.IsEmpty()) {
			m_objectValues.insert_or_assign(m_metaObject->GetMetaID(), CReferenceDataObject::CreateFromPtr(m_metaObject->GetMetadata(), bufferData.GetData()));
		}
		//load attributes 
		for (auto attribute : m_metaObject->GetObjectAttributes()) {
			if (m_metaObject->IsDataReference(attribute->GetMetaID()))
				continue;
			IMetaAttributeObject::GetValueAttribute(
				attribute, m_objectValues[attribute->GetMetaID()], resultSet);
		}
		for (auto table : m_metaObject->GetObjectTables()) {
			CTabularSectionDataObjectRef* tabularSection = new CTabularSectionDataObjectRef(this, table);
			if (!tabularSection->LoadData(m_objGuid))
				isLoaded = false;
			m_objectValues.insert_or_assign(table->GetMetaID(), tabularSection);
		}
	}
	databaseLayer->CloseResultSet(resultSet);
	databaseLayer->CloseStatement(statement);
	return isLoaded;
}

CSelectorDataObject::CSelectorDataObject(IMetaObjectRecordDataMutableRef* metaObject) :
	ISelectorObject(),
	IObjectValueInfo(Guid(), false),
	m_metaObject(metaObject)
{
	Reset();
}

bool CSelectorDataObject::Next()
{
	if (appData->DesignerMode()) {
		return false;
	}

	if (!m_objGuid.isValid()) {
		if (m_currentValues.size() > 0) {
			auto itStart = m_currentValues.begin();
			m_objGuid = *itStart;
			return Read();
		}
	}
	else {
		auto itFounded = std::find(m_currentValues.begin(), m_currentValues.end(), m_objGuid);
		ptrdiff_t pos =
			std::distance(m_currentValues.begin(), itFounded);
		if (pos == m_currentValues.size() - 1) {
			return false;
		}
		std::advance(itFounded, 1);
		m_objGuid = *itFounded;
		return Read();
	}

	return false;
}

IRecordDataObjectRef* CSelectorDataObject::GetObject(const Guid& guid) const
{
	if (appData->DesignerMode()) {
		return m_metaObject->CreateObjectValue();
	}

	if (!guid.isValid()) {
		return NULL;
	}

	return m_metaObject->CreateObjectValue(guid);
}

//////////////////////////////////////////////////////////////////////////

void CSelectorRegisterObject::Reset()
{
	m_keyValues.clear();
	if (!appData->DesignerMode()) {
		m_currentValues.clear();
		PreparedStatement* statement = databaseLayer->PrepareStatement("SELECT * FROM %s; ", m_metaObject->GetTableNameDB());
		DatabaseResultSet* resultSet = statement->RunQueryWithResults();
		while (resultSet->Next()) {
			valueArray_t keyRow;
			if (m_metaObject->HasRecorder()) {
				CMetaDefaultAttributeObject* attributeRecorder = m_metaObject->GetRegisterRecorder();
				wxASSERT(attributeRecorder);
				IMetaAttributeObject::GetValueAttribute(attributeRecorder, keyRow[attributeRecorder->GetMetaID()], resultSet);
				CMetaDefaultAttributeObject* attributeNumberLine = m_metaObject->GetRegisterLineNumber();
				wxASSERT(attributeNumberLine);
				IMetaAttributeObject::GetValueAttribute(attributeNumberLine, keyRow[attributeNumberLine->GetMetaID()], resultSet);
			}
			else {
				for (auto dimension : m_metaObject->GetGenericDimensions()) {
					IMetaAttributeObject::GetValueAttribute(dimension, keyRow[dimension->GetMetaID()], resultSet);
				}
			}
			m_currentValues.push_back(keyRow);
		};
		databaseLayer->CloseResultSet(resultSet);
		databaseLayer->CloseStatement(statement);
	}
}

bool CSelectorRegisterObject::Read()
{
	if (m_keyValues.empty())
		return false;

	m_objectValues.clear(); int position = 1;

	bool isLoaded = false;

	wxString queryText = "";

	if (databaseLayer->GetDatabaseLayerType() == DATABASELAYER_POSTGRESQL) {
		queryText = "SELECT FIRST 1 * FROM " + m_metaObject->GetTableNameDB(); bool firstWhere = true;
		for (auto attribute : m_metaObject->GetGenericDimensions()) {
			if (firstWhere) {
				queryText = queryText + " WHERE ";
			}
			queryText = queryText +
				(firstWhere ? " " : " AND ") + IMetaAttributeObject::GetCompositeSQLFieldName(attribute);
			if (firstWhere) {
				firstWhere = false;
			}
		}
	}
	else {
		queryText = "SELECT * FROM " + m_metaObject->GetTableNameDB(); bool firstWhere = true;
		for (auto attribute : m_metaObject->GetGenericDimensions()) {
			if (firstWhere) {
				queryText = queryText + " WHERE ";
			}
			queryText = queryText +
				(firstWhere ? " " : " AND ") + IMetaAttributeObject::GetCompositeSQLFieldName(attribute);
			if (firstWhere) {
				firstWhere = false;
			}
		}
		queryText += " LIMIT 1 ";
	}

	PreparedStatement* statement = databaseLayer->PrepareStatement(queryText);

	if (statement == NULL)
		return false;

	for (auto attribute : m_metaObject->GetGenericDimensions()) {
		IMetaAttributeObject::SetValueAttribute(
			attribute,
			m_keyValues.at(attribute->GetMetaID()),
			statement,
			position
		);
	}

	DatabaseResultSet* resultSet = statement->RunQueryWithResults();

	if (resultSet->Next()) {
		isLoaded = true;
		//load attributes 
		valueArray_t keyTable, rowTable;
		for (auto attribute : m_metaObject->GetGenericDimensions())
			IMetaAttributeObject::GetValueAttribute(attribute, keyTable[attribute->GetMetaID()], resultSet);
		for (auto attribute : m_metaObject->GetGenericAttributes())
			IMetaAttributeObject::GetValueAttribute(attribute, rowTable[attribute->GetMetaID()], resultSet);
		m_objectValues.insert_or_assign(keyTable, rowTable);
	}
	databaseLayer->CloseResultSet(resultSet);
	databaseLayer->CloseStatement(statement);
	return isLoaded;
}

CSelectorRegisterObject::CSelectorRegisterObject(IMetaObjectRegisterData* metaObject) :
	ISelectorObject(),
	m_metaObject(metaObject)
{
	Reset();
}

bool CSelectorRegisterObject::Next()
{
	if (appData->DesignerMode()) {
		return false;
	}

	if (m_keyValues.empty()) {
		if (m_currentValues.size() > 0) {
			auto itStart = m_currentValues.begin();
			m_keyValues = *itStart;
			return Read();
		}
	}
	else {
		auto itFounded = std::find(m_currentValues.begin(), m_currentValues.end(), m_keyValues);
		ptrdiff_t pos =
			std::distance(m_currentValues.begin(), itFounded);
		if (pos == m_currentValues.size() - 1) {
			return false;
		}
		std::advance(itFounded, 1);
		m_keyValues = *itFounded;
		return Read();
	}

	return false;
}

IRecordManagerObject* CSelectorRegisterObject::GetRecordManager(const valueArray_t& keyValues) const
{
	if (appData->DesignerMode()) {
		return m_metaObject->CreateRecordManagerObjectValue();
	}

	if (keyValues.empty()) {
		return NULL;
	}

	return m_metaObject->CreateRecordManagerObjectValue(
		CUniquePairKey(m_metaObject, keyValues)
	);
}

enum Func {
	enNext,
	enReset,
	enGetObjectRecord
};

void CSelectorDataObject::PrepareNames() const
{
	m_methodHelper->ClearHelper();

	m_methodHelper->AppendFunc("next", "next()");
	m_methodHelper->AppendFunc("reset", "reset()");
	m_methodHelper->AppendFunc("getObject", "getObject()");

	for (auto attributes : m_metaObject->GetObjectAttributes()) {
		m_methodHelper->AppendProp(attributes->GetName(), true, false, attributes->GetMetaID());
	}

	for (auto tables : m_metaObject->GetObjectTables()) {
		m_methodHelper->AppendProp(tables->GetName(), true, false, tables->GetMetaID());
	}

	m_methodHelper->AppendProp(wxT("reference"), m_metaObject->GetMetaID());
}

bool CSelectorDataObject::CallAsFunc(const long lMethodNum, CValue& pvarRetValue, CValue** paParams, const long lSizeArray)
{
	switch (lMethodNum)
	{
	case enNext:
		pvarRetValue = Next();
		return true;
	case enReset:
		Reset();
		return true;
	case enGetObjectRecord:
		pvarRetValue = GetObject(m_objGuid);
		return true;
	}

	return false;
}

bool CSelectorDataObject::SetPropVal(const long lPropNum, const CValue& varPropVal)
{
	return false;
}

bool CSelectorDataObject::GetPropVal(const long lPropNum, CValue& pvarPropVal)
{
	const meta_identifier_t& id = m_methodHelper->GetPropData(lPropNum);
	if (!m_objGuid.isValid()) {
		if (!appData->DesignerMode()) {
			pvarPropVal = CValue(eValueTypes::TYPE_NULL);
			return true;
		}
	}
	if (!m_metaObject->IsDataReference(id)) {
		pvarPropVal = m_objectValues.at(id);
		return true;
	}
	pvarPropVal = CReferenceDataObject::Create(m_metaObject, m_objGuid);
	return true;
}

void CSelectorRegisterObject::PrepareNames() const
{
	m_methodHelper->AppendFunc("next", "next()");
	m_methodHelper->AppendFunc("reset", "reset()");

	if (m_metaObject->HasRecordManager()) {
		m_methodHelper->AppendFunc("getRecordManager", "getRecordManager()");
	}

	for (auto attributes : m_metaObject->GetGenericAttributes()) {
		m_methodHelper->AppendProp(
			attributes->GetName(),
			true,
			false,
			attributes->GetMetaID()
		);
	}
}

bool CSelectorRegisterObject::CallAsFunc(const long lMethodNum, CValue& pvarRetValue, CValue** paParams, const long lSizeArray)
{
	switch (lMethodNum)
	{
	case enNext:
		pvarRetValue = Next();
		return true;
	case enReset:
		Reset();
		return true;
	case enGetObjectRecord:
		pvarRetValue = GetRecordManager(m_keyValues);
		return true;
	}

	return false;
}

bool CSelectorRegisterObject::SetPropVal(const long lPropNum, const CValue& varPropVal)
{
	return false;
}

bool CSelectorRegisterObject::GetPropVal(const long lPropNum, CValue& pvarPropVal)
{
	const meta_identifier_t& id = m_methodHelper->GetPropData(lPropNum);
	if (m_keyValues.empty()) {
		if (!appData->DesignerMode()) {
			pvarPropVal = CValue(eValueTypes::TYPE_NULL);
			return true;
		}
	}

	pvarPropVal = m_objectValues[m_keyValues][id];
	return true;
}

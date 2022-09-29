#include "objectSelector.h"
#include "compiler/methods.h"
#include "metadata/metaObjects/objects/tabularSection/tabularSection.h"
#include "databaseLayer/databaseLayer.h"
#include "appData.h"

ISelectorObject::ISelectorObject() : CValue(eValueTypes::TYPE_VALUE, true),
m_methods(new CMethods())
{
}

ISelectorObject::~ISelectorObject()
{
	wxDELETE(m_methods);
}

#include "metadata/singleMetaTypes.h"

CLASS_ID ISelectorObject::GetClassType() const
{
	IMetaTypeObjectValueSingle* clsFactory =
		GetMetaObject()->GetTypeObject(eMetaObjectType::enSelection);
	wxASSERT(clsFactory);
	return clsFactory->GetClassType();
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

void ISelectorDataObject::Reset()
{
	m_objGuid.reset(); m_newObject = false;
	if (!appData->DesignerMode()) {
		m_aCurrentValues.clear();
		PreparedStatement* statement = databaseLayer->PrepareStatement("SELECT UUID FROM %s ORDER BY CAST(UUID AS VARCHAR(36)); ", m_metaObject->GetTableNameDB());
		DatabaseResultSet* resultSet = statement->RunQueryWithResults();
		while (resultSet->Next()) {
			m_aCurrentValues.push_back(
				resultSet->GetResultString(guidName)
			);
		};
		databaseLayer->CloseResultSet(resultSet);
		databaseLayer->CloseStatement(statement);
	}
	for (auto currAttribute : m_metaObject->GetObjectAttributes()) {
		if (!appData->DesignerMode()) {
			m_aObjectValues[currAttribute->GetMetaID()] = CValue(eValueTypes::TYPE_NULL);
		}
		else {
			m_aObjectValues[currAttribute->GetMetaID()] = currAttribute->CreateValue();
		}
	}
	for (auto currTable : m_metaObject->GetObjectTables()) {
		if (!appData->DesignerMode()) {
			m_aObjectValues[currTable->GetMetaID()] = CValue(eValueTypes::TYPE_NULL);
		}
		else {
			m_aObjectValues[currTable->GetMetaID()] =
				new CTabularSectionDataObjectRef(this, currTable);
		}
	}
}

bool ISelectorDataObject::Read()
{
	if (!m_objGuid.isValid())
		return false;

	m_aObjectValues.clear(); m_aObjectTables.clear();

	for (auto currTable : m_metaObject->GetObjectTables()) {
		CTabularSectionDataObjectRef* tabularSection =
			new CTabularSectionDataObjectRef(this, currTable);
		m_aObjectValues[currTable->GetMetaID()] = tabularSection;
		m_aObjectTables.push_back(tabularSection);
	}

	bool isLoaded = false;

	PreparedStatement* statement = databaseLayer->PrepareStatement("SELECT FIRST 1 * FROM %s WHERE UUID = '%s'; ", m_metaObject->GetTableNameDB(), m_objGuid.str());

	if (statement == NULL)
		return false;

	DatabaseResultSet* resultSet = statement->RunQueryWithResults();

	if (resultSet->Next()) {
		isLoaded = true;
		wxMemoryBuffer bufferData;
		resultSet->GetResultBlob(guidRef, bufferData);
		if (!bufferData.IsEmpty()) {
			m_aObjectValues[m_metaObject->GetMetaID()] = CReferenceDataObject::CreateFromPtr(
				m_metaObject->GetMetadata(), bufferData.GetData()
			);
		}
		//load attributes 
		for (auto attribute : m_metaObject->GetObjectAttributes()) {
			if (m_metaObject->IsDataReference(attribute->GetMetaID()))
				continue;
			m_aObjectValues.insert_or_assign(
				attribute->GetMetaID(),
				IMetaAttributeObject::GetValueAttribute(
					attribute, resultSet)
			);
		}
		for (auto tabularSection : m_aObjectTables) {
			if (!tabularSection->LoadData()) {
				isLoaded = false;
			}
		}
	}
	databaseLayer->CloseResultSet(resultSet);
	databaseLayer->CloseStatement(statement);
	return isLoaded;
}

ISelectorDataObject::ISelectorDataObject(IMetaObjectRecordDataRef* metaObject) :
	ISelectorObject(),
	IObjectValueInfo(Guid(), false),
	m_metaObject(metaObject)
{
	Reset();
}

bool ISelectorDataObject::Next()
{
	if (appData->DesignerMode()) {
		return false;
	}

	if (!m_objGuid.isValid()) {
		if (m_aCurrentValues.size() > 0) {
			auto itStart = m_aCurrentValues.begin();
			m_objGuid = *itStart;
			return Read();
		}
	}
	else {
		auto itFounded = std::find(m_aCurrentValues.begin(), m_aCurrentValues.end(), m_objGuid);
		ptrdiff_t pos =
			std::distance(m_aCurrentValues.begin(), itFounded);
		if (pos == m_aCurrentValues.size() - 1) {
			return false;
		}
		std::advance(itFounded, 1);
		m_objGuid = *itFounded;
		return Read();
	}

	return false;
}

IRecordDataObjectRef* ISelectorDataObject::GetObject(const Guid& guid) const
{
	if (appData->DesignerMode()) {
		return m_metaObject->CreateObjectValue();
	}

	if (!guid.isValid()) {
		return NULL;
	}

	return m_metaObject->CreateObjectValue(guid);
}

enum {
	enNext,
	enReset,
	enGetObjectRecord
};

void ISelectorDataObject::PrepareNames() const
{
	m_methods->AppendMethod("next", "next()");
	m_methods->AppendMethod("reset", "reset()");
	m_methods->AppendMethod("getObject", "getObject()");

	for (auto attributes : m_metaObject->GetObjectAttributes()) {
		m_methods->AppendAttribute(attributes->GetName(), wxT("selector"), attributes->GetMetaID());
	}
	for (auto tables : m_metaObject->GetObjectTables()) {
		m_methods->AppendAttribute(tables->GetName(), wxT("selector"), tables->GetMetaID());
	}

	m_methods->AppendAttribute(wxT("reference"), wxT("selector"), m_metaObject->GetMetaID());
}

CValue ISelectorDataObject::Method(methodArg_t& aParams)
{
	switch (aParams.GetIndex())
	{
	case enNext:
		return Next();
	case enReset:
		Reset();
		break;
	case enGetObjectRecord:
		return GetObject(m_objGuid);
	}

	return CValue();
}

void ISelectorDataObject::SetAttribute(attributeArg_t& aParams, CValue& cVal)
{
	const meta_identifier_t& id = m_methods->GetAttributePosition(aParams.GetIndex());
}

CValue ISelectorDataObject::GetAttribute(attributeArg_t& aParams)
{
	const meta_identifier_t& id = m_methods->GetAttributePosition(aParams.GetIndex());

	if (!m_objGuid.isValid()) {
		if (!appData->DesignerMode()) {
			return CValue(eValueTypes::TYPE_NULL);
		}
	}

	if (!m_metaObject->IsDataReference(id))
		return m_aObjectValues[id];

	return CReferenceDataObject::Create(m_metaObject, m_objGuid);
}

//////////////////////////////////////////////////////////////////////////

void ISelectorRegisterObject::Reset()
{
	m_aKeyValues.clear(); 
	if (!appData->DesignerMode()) {
		m_aCurrentValues.clear();
		PreparedStatement* statement = databaseLayer->PrepareStatement("SELECT * FROM %s; ", m_metaObject->GetTableNameDB());
		DatabaseResultSet* resultSet = statement->RunQueryWithResults();
		while (resultSet->Next()) {
			std::map<meta_identifier_t, CValue> keyRow;
			if (m_metaObject->HasRecorder()) {
				CMetaDefaultAttributeObject* attributeRecorder = m_metaObject->GetRegisterRecorder();
				wxASSERT(attributeRecorder);
				keyRow.insert_or_assign(attributeRecorder->GetMetaID(),
					IMetaAttributeObject::GetValueAttribute(attributeRecorder, resultSet));
				CMetaDefaultAttributeObject* attributeNumberLine = m_metaObject->GetRegisterLineNumber();
				wxASSERT(attributeNumberLine);
				keyRow.insert_or_assign(attributeNumberLine->GetMetaID(),
					IMetaAttributeObject::GetValueAttribute(attributeNumberLine, resultSet));
			}
			else {
				for (auto dimension : m_metaObject->GetGenericDimensions()) {
					keyRow.insert_or_assign(dimension->GetMetaID(),
						IMetaAttributeObject::GetValueAttribute(dimension, resultSet));
				}
			}
			m_aCurrentValues.push_back(keyRow);
		};
		databaseLayer->CloseResultSet(resultSet);
		databaseLayer->CloseStatement(statement);
	}
}

bool ISelectorRegisterObject::Read()
{
	if (m_aKeyValues.empty())
		return false;

	m_aObjectValues.clear(); int position = 1;

	bool isLoaded = false;
	wxString queryText = "SELECT FIRST 1 * FROM " + m_metaObject->GetTableNameDB(); bool firstWhere = true;

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

	PreparedStatement* statement = databaseLayer->PrepareStatement(queryText);

	if (statement == NULL)
		return false;

	for (auto attribute : m_metaObject->GetGenericDimensions()) {
		IMetaAttributeObject::SetValueAttribute(
			attribute,
			m_aKeyValues.at(attribute->GetMetaID()),
			statement,
			position
		);
	}

	DatabaseResultSet* resultSet = statement->RunQueryWithResults();

	if (resultSet->Next()) {
		isLoaded = true;
		//load attributes 
		std::map<meta_identifier_t, CValue> aKeyTable, aRowTable;
		for (auto attribute : m_metaObject->GetGenericDimensions()) {
			aKeyTable.insert_or_assign(
				attribute->GetMetaID(),
				IMetaAttributeObject::GetValueAttribute(attribute, resultSet)
			);
		}
		for (auto attribute : m_metaObject->GetGenericAttributes()) {
			aRowTable.insert_or_assign(
				attribute->GetMetaID(),
				IMetaAttributeObject::GetValueAttribute(attribute, resultSet)
			);
		}
		m_aObjectValues.insert_or_assign(aKeyTable, aRowTable);
	}
	databaseLayer->CloseResultSet(resultSet);
	databaseLayer->CloseStatement(statement);
	return isLoaded;
}

ISelectorRegisterObject::ISelectorRegisterObject(IMetaObjectRegisterData* metaObject) :
	ISelectorObject(),
	m_metaObject(metaObject)
{
	Reset();
}

bool ISelectorRegisterObject::Next()
{
	if (appData->DesignerMode()) {
		return false;
	}

	if (m_aKeyValues.empty()) {
		if (m_aCurrentValues.size() > 0) {
			auto itStart = m_aCurrentValues.begin();
			m_aKeyValues = *itStart;
			return Read();
		}
	}
	else {
		auto itFounded = std::find(m_aCurrentValues.begin(), m_aCurrentValues.end(), m_aKeyValues);
		ptrdiff_t pos =
			std::distance(m_aCurrentValues.begin(), itFounded);
		if (pos == m_aCurrentValues.size() - 1) {
			return false;
		}
		std::advance(itFounded, 1);
		m_aKeyValues = *itFounded;
		return Read();
	}

	return false;
}

IRecordManagerObject* ISelectorRegisterObject::GetRecordManager(const std::map<meta_identifier_t, CValue>& keyValues) const
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

void ISelectorRegisterObject::PrepareNames() const
{
	m_methods->AppendMethod("next", "next()");
	m_methods->AppendMethod("reset", "reset()");
	
	if (m_metaObject->HasRecordManager()) {
		m_methods->AppendMethod("getRecordManager", "getRecordManager()");
	}

	for (auto attributes : m_metaObject->GetGenericAttributes()) {
		m_methods->AppendAttribute(attributes->GetName(), wxT("selector"), attributes->GetMetaID());
	}
}

CValue ISelectorRegisterObject::Method(methodArg_t& aParams)
{
	switch (aParams.GetIndex())
	{
	case enNext:
		return Next();
	case enReset:
		Reset();
		break;
	case enGetObjectRecord: 
		return GetRecordManager(m_aKeyValues);
	}

	return CValue();
}

void ISelectorRegisterObject::SetAttribute(attributeArg_t& aParams, CValue& cVal)
{
	const meta_identifier_t& id = m_methods->GetAttributePosition(aParams.GetIndex());
}

CValue ISelectorRegisterObject::GetAttribute(attributeArg_t& aParams)
{
	const meta_identifier_t& id = m_methods->GetAttributePosition(aParams.GetIndex());
	if (m_aKeyValues.empty()) {
		if (!appData->DesignerMode()) {
			return CValue(eValueTypes::TYPE_NULL);
		}
	}
	return m_aObjectValues[m_aKeyValues][id];
}

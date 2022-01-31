#include "objectSelector.h"
#include "compiler/methods.h"
#include "metadata/objects/tabularSection/tabularSection.h"
#include "databaseLayer/databaseLayer.h"
#include "appData.h"

void IDataSelectorValue::Reset()
{
	m_objGuid.reset(); m_bNewObject = false;
	if (!appData->DesignerMode()) {
		m_aObjectData.clear();
		PreparedStatement *statement = databaseLayer->PrepareStatement("SELECT UUID FROM %s ORDER BY CAST(UUID AS VARCHAR(36)); ", m_metaObject->GetTableNameDB());
		DatabaseResultSet *resultSet = statement->RunQueryWithResults();
		while (resultSet->Next()) {
			m_aObjectData.push_back(resultSet->GetResultString(guidName));
		};
		resultSet->Close();
	}
	for (auto currAttribute : m_metaObject->GetObjectAttributes()) {
		if (!appData->DesignerMode()) {
			m_aObjectValues[currAttribute->GetMetaID()] = CValue(eValueTypes::TYPE_NULL);
		}
		else {
			CValueTypeDescription *td = currAttribute->GetValueTypeDescription();
			m_aObjectValues[currAttribute->GetMetaID()] = td->AdjustValue();
			wxDELETE(td);
		}
	}
	for (auto currTable : m_metaObject->GetObjectTables()) {
		if (!appData->DesignerMode()) {
			m_aObjectValues[currTable->GetMetaID()] = CValue(eValueTypes::TYPE_NULL);
		}
		else {
			m_aObjectValues[currTable->GetMetaID()] =
				new CValueTabularRefSection(this, currTable);
		}
	}
}

bool IDataSelectorValue::Read()
{
	if (!m_objGuid.isValid())
		return false;

	m_aObjectValues.clear(); m_aObjectTables.clear();

	for (auto currTable : m_metaObject->GetObjectTables()) {
		CValueTabularRefSection *tabularSection =
			new CValueTabularRefSection(this, currTable);
		m_aObjectValues[currTable->GetMetaID()] = tabularSection;
		m_aObjectTables.push_back(tabularSection);
	}

	bool isLoaded = false;
	PreparedStatement *statement = databaseLayer->PrepareStatement("SELECT FIRST 1 * FROM %s WHERE UUID = '%s'; ", m_metaObject->GetTableNameDB(), m_objGuid.str());
	if (statement == NULL)
		return false;
	DatabaseResultSet *resultSet = statement->RunQueryWithResults();

	if (resultSet->Next()) {
		isLoaded = true;
		wxMemoryBuffer bufferData;
		resultSet->GetResultBlob(guidRef, bufferData);
		if (!bufferData.IsEmpty()) {
			m_aObjectValues[m_metaObject->GetMetaID()] = CValueReference::CreateFromPtr(
				m_metaObject->GetMetadata(), bufferData.GetData()
			);
		}
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
		for (auto tabularSection : m_aObjectTables) {
			if (!tabularSection->LoadDataFromDB()) {
				isLoaded = false;
			}
		}
	}
	resultSet->Close();
	return isLoaded;
}

IDataSelectorValue::IDataSelectorValue(IMetaObjectRefValue *metaObject) : CValue(eValueTypes::TYPE_VALUE, true), IObjectValueInfo(Guid(), false), 
m_metaObject(metaObject), m_methods(new CMethods())
{
	Reset();
}

IDataSelectorValue::~IDataSelectorValue()
{
	wxDELETE(m_methods);
}

bool IDataSelectorValue::Next()
{
	if (appData->DesignerMode()) {
		return false;
	}

	if (!m_objGuid.isValid()) {
		if (m_aObjectData.size() > 0) {
			auto itStart = m_aObjectData.begin();
			m_objGuid = *itStart;
			return Read();
		}
	}
	else {
		auto itFounded = std::find(m_aObjectData.begin(), m_aObjectData.end(), m_objGuid);
		ptrdiff_t pos =
			std::distance(m_aObjectData.begin(), itFounded);
		if (pos == m_aObjectData.size() - 1) {
			return false;
		}
		std::advance(itFounded, 1);
		m_objGuid = *itFounded;
		return Read();
	}

	return false;
}

IDataObjectRefValue *IDataSelectorValue::GetObject(const Guid &guid) const
{
	if (appData->DesignerMode()) {
		return m_metaObject->CreateObjectRefValue();
	}

	if (!guid.isValid()) {
		return NULL;
	}
	return m_metaObject->CreateObjectRefValue(guid);
}

#include "metadata/singleMetaTypes.h"

CLASS_ID IDataSelectorValue::GetTypeID() const
{
	IMetaTypeObjectValueSingle *clsFactory =
		m_metaObject->GetTypeObject(eMetaObjectType::enSelection);
	wxASSERT(clsFactory);
	return clsFactory->GetTypeID();
}

wxString IDataSelectorValue::GetTypeString() const
{
	IMetaTypeObjectValueSingle *clsFactory =
		m_metaObject->GetTypeObject(eMetaObjectType::enSelection);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

wxString IDataSelectorValue::GetString() const
{
	IMetaTypeObjectValueSingle *clsFactory =
		m_metaObject->GetTypeObject(eMetaObjectType::enSelection);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

enum {
	enNext,
	enReset,
	enGetObject
};

void IDataSelectorValue::PrepareNames() const
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

CValue IDataSelectorValue::Method(methodArg_t &aParams)
{
	switch (aParams.GetIndex())
	{
	case enNext: return Next();
	case enReset: Reset(); break;
	case enGetObject: return GetObject(m_objGuid);
	}

	return CValue();
}

void IDataSelectorValue::SetAttribute(attributeArg_t &aParams, CValue &cVal)
{
	meta_identifier_t id = m_methods->GetAttributePosition(aParams.GetIndex());
}

CValue IDataSelectorValue::GetAttribute(attributeArg_t &aParams)
{
	meta_identifier_t id = m_methods->GetAttributePosition(aParams.GetIndex());
	if (!m_objGuid.isValid()) {
		if (!appData->DesignerMode()) {
			return CValue(eValueTypes::TYPE_NULL);
		}
	}
	return m_aObjectValues[id];
}

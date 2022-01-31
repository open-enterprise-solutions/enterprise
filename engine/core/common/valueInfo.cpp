#include "valueInfo.h"
#include "metadata/objects/reference/reference.h"
#include "metadata/objects/baseObject.h"
#include "metadata/objects/tabularSection/tabularSection.h"
#include "databaseLayer/databaseLayer.h"
#include "appData.h" 

void IObjectValueInfo::SetValueByMetaID(meta_identifier_t id, const CValue &cVal)
{
}

CValue IObjectValueInfo::GetValueByMetaID(meta_identifier_t id) const
{
	return CValue();
}

IValueTabularSection *IObjectValueInfo::GetTableByMetaID(meta_identifier_t id) const
{
	auto foundedIt = std::find_if(m_aObjectTables.begin(), m_aObjectTables.end(), [id](IValueTabularSection *table) {
		return id == table->GetMetaID();
	});

	if (foundedIt != m_aObjectTables.end()) {
		return *foundedIt;
	}

	return NULL;
}

void IObjectValueInfo::PrepareValues()
{
	IMetaObjectValue *metaObject = GetMetaObject();
	wxASSERT(metaObject);
	//attrbutes can refValue 
	for (auto attribute : metaObject->GetObjectAttributes()) {
		switch (attribute->GetTypeObject())
		{
		case eValueTypes::TYPE_BOOLEAN: m_aObjectValues[attribute->GetMetaID()] = eValueTypes::TYPE_BOOLEAN; break;
		case eValueTypes::TYPE_NUMBER: m_aObjectValues[attribute->GetMetaID()] = eValueTypes::TYPE_NUMBER; break;
		case eValueTypes::TYPE_DATE: m_aObjectValues[attribute->GetMetaID()] = eValueTypes::TYPE_DATE; break;
		case eValueTypes::TYPE_STRING: m_aObjectValues[attribute->GetMetaID()] = eValueTypes::TYPE_STRING; break;
		default: m_aObjectValues[attribute->GetMetaID()] = new CValueReference(
			metaObject->GetMetadata(), attribute->GetTypeObject()
		); break;
		}
	}

	// table is collection values 
	for (auto table : metaObject->GetObjectTables()) {
		CValueTabularSection *tableSection = new CValueTabularSection(this, table);
		m_aObjectValues[table->GetMetaID()] = tableSection;
		m_aObjectTables.push_back(tableSection);
	}
}
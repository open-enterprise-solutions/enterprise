#include "valueInfo.h"
#include "metadata/metaObjects/objects/reference/reference.h"
#include "metadata/metaObjects/objects/object.h"
#include "metadata/metaObjects/objects/tabularSection/tabularSection.h"
#include "databaseLayer/databaseLayer.h"
#include "appData.h" 

void IObjectValueInfo::SetValueByMetaID(const meta_identifier_t &id, const CValue &cVal)
{
}

CValue IObjectValueInfo::GetValueByMetaID(const meta_identifier_t &id) const
{
	return CValue();
}

ITabularSectionDataObject *IObjectValueInfo::GetTableByMetaID(const meta_identifier_t &id) const
{
	auto foundedIt = std::find_if(m_aObjectTables.begin(), m_aObjectTables.end(), [id](ITabularSectionDataObject *table) {
		return id == table->GetMetaID();
	});

	if (foundedIt != m_aObjectTables.end()) {
		return *foundedIt;
	}

	return NULL;
}

void IObjectValueInfo::PrepareValues()
{
	IMetaObjectRecordData *metaObject = GetMetaObject();
	wxASSERT(metaObject);
	
	//attrbutes can refValue 
	for (auto attribute : metaObject->GetObjectAttributes()) {
		m_aObjectValues[attribute->GetMetaID()] = attribute->CreateValue(); 
	}

	// table is collection values 
	for (auto table : metaObject->GetObjectTables()) {
		CTabularSectionDataObject *tableSection = new CTabularSectionDataObject(this, table);
		m_aObjectValues[table->GetMetaID()] = tableSection;
		m_aObjectTables.push_back(tableSection);
	}
}
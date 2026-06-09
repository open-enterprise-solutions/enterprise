#include "objectSelector.h"

#include "backend/metaCollection/partial/tabularSection/tabularSection.h"

#include "backend/appData.h"
#include "backend/session/session.h"
#include "backend/query/dataQueryBuilder.h"   // L3 door — selector reads / scans

/////////////////////////////////////////////////////////////////////////

void ibValueSelectorRecordDataObject::Reset()
{
	m_objGuid.reset(); m_newObject = false;
	if (!appData->DesignerMode()) {
		m_currentValues.clear();
		// Full key scan through the L3 door — row keys from the L3 selection.
		ibDataQueryBuilder q;
		q.From(m_metaObject->GetQueryable());
		ibReadPageRequest page;
		page.m_count = 0;   // unbounded
		ibDataQueryResult selection = q.Execute(page);
		const ibBackendQueryColumn* keyCol = m_metaObject->GetQueryable()->GetIdentitySort().back().m_col;   // uuid identity column
		while (selection.Next())
			m_currentValues.push_back(selection.GetValue(keyCol).GetString());
	}
	for (const auto object : m_metaObject->GetAttributeArrayObject()) {
		if (!appData->DesignerMode()) {
			m_listObjectValue.insert_or_assign(object->GetMetaID(), ibValueTypes::TYPE_NULL);
		}
		else {
			m_listObjectValue.insert_or_assign(object->GetMetaID(), object->CreateValue());
		}
	}
	for (const auto object : m_metaObject->GetTableArrayObject()) {
		if (!appData->DesignerMode()) {
			m_listObjectValue.insert_or_assign(object->GetMetaID(), ibValueTypes::TYPE_NULL);
		}
		else {
			m_listObjectValue.insert_or_assign(object->GetMetaID(), new ibValueTabularSectionDataObjectRef(this, object));
		}
	}
}

bool ibValueSelectorRecordDataObject::Read()
{
	if (!m_objGuid.isValid())
		return false;

	m_listObjectValue.clear();

	// Read the row by key through the L3 door; every value (incl. the self
	// reference) comes from the L3 selection as a ready ibValue — no raw result.
	try {
		ibDataQueryBuilder q;
		q.From(m_metaObject->GetQueryable()).WhereKey(m_objGuid);
		ibReadPageRequest page;
		page.m_count = 1;
		ibDataQueryResult selection = q.Execute(page);
		if (selection.Next()) {
			m_listObjectValue.insert_or_assign(m_metaObject->GetMetaID(),
				selection.GetValue(m_metaObject->GetDataReference()));
			for (const auto object : m_metaObject->GetAttributeArrayObject())
				if (!m_metaObject->IsDataReference(object->GetMetaID()))
					m_listObjectValue[object->GetMetaID()] = selection.GetValue(object);
			for (const auto object : m_metaObject->GetTableArrayObject()) {
				ibValueTabularSectionDataObjectRef* tabularSection = new ibValueTabularSectionDataObjectRef(this, object);
				tabularSection->LoadData(m_objGuid);
				m_listObjectValue.insert_or_assign(object->GetMetaID(), tabularSection);
			}
			return true;
		}
	}
	catch (...) {}
	return false;
}

/////////////////////////////////////////////////////////////////////////

void ibValueSelectorRegisterDataObject::Reset()
{
	m_keyValues.clear();
	if (!appData->DesignerMode()) {
		m_currentValues.clear();
		// Full scan through the L3 door; key columns come from the L3 selection.
		ibDataQueryBuilder q;
		q.From(m_metaObject->GetQueryable());
		ibReadPageRequest page;
		page.m_count = 0;   // unbounded
		ibDataQueryResult selection = q.Execute(page);
		while (selection.Next()) {
			ibRowMetaValues keyRow;
			if (m_metaObject->HasRecorder()) {
				ibValueMetaObjectAttributePredefined* attributeRecorder = m_metaObject->GetRegisterRecorder();
				wxASSERT(attributeRecorder);
				keyRow[attributeRecorder->GetMetaID()] = selection.GetValue(attributeRecorder);
				ibValueMetaObjectAttributePredefined* attributeNumberLine = m_metaObject->GetRegisterLineNumber();
				wxASSERT(attributeNumberLine);
				keyRow[attributeNumberLine->GetMetaID()] = selection.GetValue(attributeNumberLine);
			}
			else {
				for (const auto object : m_metaObject->GetGenericDimensionArrayObject())
					keyRow[object->GetMetaID()] = selection.GetValue(object);
			}
			m_currentValues.push_back(keyRow);
		}
	}
}

bool ibValueSelectorRegisterDataObject::Read()
{
	if (m_keyValues.empty())
		return false;

	m_listObjectValue.clear();

	// Composite-key read through the L3 door: each dimension is an Eq condition,
	// decomposed inside L3 across ALL its physical fields (the FB FIRST / others
	// LIMIT fork, the GetCompositeSQLFieldName concat and the positional binding
	// all move into the door). Values come from the L3 selection (GetValue) — no
	// raw result set, no prepared statement at this call site.
	try {
		ibDataQueryBuilder q;
		q.From(m_metaObject->GetQueryable());
		for (const auto object : m_metaObject->GetGenericDimensionArrayObject())
			q.Where(object, ibComparisonType::ibComparisonType_Equal,
				m_keyValues.at(object->GetMetaID()));
		ibReadPageRequest page;
		page.m_count = 1;
		ibDataQueryResult selection = q.Execute(page);
		if (selection.Next()) {
			ibRowMetaValues keyTable, rowTable;
			for (const auto object : m_metaObject->GetGenericDimensionArrayObject())
				keyTable[object->GetMetaID()] = selection.GetValue(object);
			for (const auto object : m_metaObject->GetGenericAttributeArrayObject())
				rowTable[object->GetMetaID()] = selection.GetValue(object);
			m_listObjectValue.insert_or_assign(keyTable, rowTable);
			return true;
		}
	}
	catch (...) {}
	return false;
}

////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : tabular sections
////////////////////////////////////////////////////////////////////////////

#include "tabularSection.h"
#include "appData.h"
#include "databaseLayer/databaseLayer.h"
#include "databaseLayer/databaseErrorCodes.h"
#include "metadata/metaObjects/objects/object.h"
#include "utils/stringUtils.h"

wxIMPLEMENT_ABSTRACT_CLASS(ITabularSectionDataObject, IValueTable);
wxIMPLEMENT_DYNAMIC_CLASS(ITabularSectionDataObject::CTabularSectionDataObjectReturnLine, IValueTable::IValueTableReturnLine);
wxIMPLEMENT_DYNAMIC_CLASS(CTabularSectionDataObject, ITabularSectionDataObject);
wxIMPLEMENT_DYNAMIC_CLASS(CTabularSectionDataObjectRef, ITabularSectionDataObject);

//////////////////////////////////////////////////////////////////////
//               ITabularSectionDataObject                          //
//////////////////////////////////////////////////////////////////////

#include "metadata/metadata.h"
#include "metadata/singleMetaTypes.h"

CValue ITabularSectionDataObject::GetAt(const CValue& cKey)
{
	unsigned int index = cKey.ToUInt();

	if (index >= m_aObjectValues.size() && !appData->DesignerMode())
		CTranslateError::Error(_("Index outside array bounds"));

	return new CTabularSectionDataObjectReturnLine(this, index);
}

CLASS_ID ITabularSectionDataObject::GetClassType() const
{
	IMetadata* metaData = m_metaTable->GetMetadata();
	wxASSERT(metaData);
	IMetaTypeObjectValueSingle* clsFactory =
		metaData->GetTypeObject(m_metaTable, eMetaObjectType::enTabularSection);
	wxASSERT(clsFactory);
	return clsFactory->GetClassType();
}

wxString ITabularSectionDataObject::GetTypeString() const
{
	IMetadata* metaData = m_metaTable->GetMetadata();
	wxASSERT(metaData);
	IMetaTypeObjectValueSingle* clsFactory =
		metaData->GetTypeObject(m_metaTable, eMetaObjectType::enTabularSection);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

wxString ITabularSectionDataObject::GetString() const
{
	IMetadata* metaData = m_metaTable->GetMetadata();
	wxASSERT(metaData);
	IMetaTypeObjectValueSingle* clsFactory =
		metaData->GetTypeObject(m_metaTable, eMetaObjectType::enTabularSection);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

#include "frontend/visualView/controls/form.h"

void ITabularSectionDataObject::SetValueByMetaID(long line, const meta_identifier_t& id, const CValue& cVal)
{
	if (m_metaTable->IsNumberLine(id)) {
		return;
	}

	if (!appData->DesignerMode()) {
		IMetaAttributeObject* metaAttribute = m_metaTable->FindAttribute(id);
		wxASSERT(metaAttribute);
		m_aObjectValues[line][id] = metaAttribute->AdjustValue(cVal);
		ITabularSectionDataObject::Cleared();
	}
}

CValue ITabularSectionDataObject::GetValueByMetaID(long line, const meta_identifier_t& id) const
{
	IMetaAttributeObject* metaAttribute = m_metaTable->FindAttribute(id);
	wxASSERT(metaAttribute);

	if (m_metaTable->IsNumberLine(id)) {
		return line + 1;
	}

	if (appData->DesignerMode()) {
		return metaAttribute->CreateValue();
	}

	return m_aObjectValues[line].at(id);
}

//////////////////////////////////////////////////////////////////////
//               CTabularSectionDataObject                          //
//////////////////////////////////////////////////////////////////////

CTabularSectionDataObject::CTabularSectionDataObject() {}

CTabularSectionDataObject::CTabularSectionDataObject(IObjectValueInfo* dataObject, CMetaTableObject* tableObject) :
	ITabularSectionDataObject(dataObject, tableObject)
{
}

//////////////////////////////////////////////////////////////////////
//               CTabularSectionDataObjectRef                       //
//////////////////////////////////////////////////////////////////////

CTabularSectionDataObjectRef::CTabularSectionDataObjectRef() {}

CTabularSectionDataObjectRef::CTabularSectionDataObjectRef(IObjectValueInfo* dataObject, CMetaTableObject* tableObject) :
	ITabularSectionDataObject(dataObject, tableObject)
{
}

bool CTabularSectionDataObjectRef::LoadData(bool createData)
{
	if (m_dataObject->IsNewObject())
		return false;

	m_aObjectValues.clear();
	IMetaObjectRecordData* metaObject = m_dataObject->GetMetaObject();
	wxASSERT(metaObject);

	wxString tableName = m_metaTable->GetTableNameDB();
	wxString sql = "SELECT * FROM " + tableName + " WHERE UUID = '" + m_dataObject->GetGuid() + "'";

	DatabaseResultSet* resultSet = databaseLayer->RunQueryWithResults(sql);
	if (!resultSet) {
		return false;
	}
	while (resultSet->Next()) {
		std::map<meta_identifier_t, CValue> aRowTable;
		for (auto attribute : m_metaTable->GetObjectAttributes()) {
			wxString nameAttribute = attribute->GetFieldNameDB();
			if (m_metaTable->IsNumberLine(attribute->GetMetaID())) {
				aRowTable[attribute->GetMetaID()] = CValue(); //numberline is special field
				continue;
			}
			aRowTable.insert_or_assign(attribute->GetMetaID(),
				IMetaAttributeObject::GetValueAttribute(attribute, resultSet, createData));
		}
		m_aObjectValues.push_back(aRowTable);
	}
	resultSet->Close();
	if (!CTranslateError::IsSimpleMode()) {
		IValueTable::Reset(m_aObjectValues.size());
	}
	return true;
}

#include "compiler/systemObjects.h"

bool CTabularSectionDataObjectRef::SaveData()
{
	bool hasError = false;

	//check fill attributes 
	bool fillCheck = true; int nLine = 1;
	for (auto objectValue : m_aObjectValues) {
		for (auto attribute : m_metaTable->GetObjectAttributes()) {
			if (attribute->FillCheck()) {
				if (objectValue[attribute->GetMetaID()].IsEmpty()) {
					wxString fillError =
						wxString::Format(_("The %s is required on line %i of the %s list"), attribute->GetSynonym(), nLine, m_metaTable->GetSynonym());
					CSystemObjects::Message(fillError, eStatusMessage::eStatusMessage_Information);
					fillCheck = false;
				}
			}
		}
		nLine++;
	}

	if (!fillCheck) {
		return false;
	}

	if (!CTabularSectionDataObjectRef::DeleteData()) {
		return false;
	}

	CMetaDefaultAttributeObject* numLine = m_metaTable->GetNumberLine();
	wxASSERT(numLine);
	IMetaObjectRecordData* metaObject = m_dataObject->GetMetaObject();
	wxASSERT(metaObject);
	reference_t* reference_impl = new reference_t(metaObject->GetMetaID(), m_dataObject->GetGuid());

	wxString tableName = m_metaTable->GetTableNameDB();
	wxString queryText = "UPDATE OR INSERT INTO " + tableName + " (";
	queryText += "UUID, UUIDREF";
	for (auto attribute : m_metaTable->GetObjectAttributes()) {
		queryText = queryText + ", " + IMetaAttributeObject::GetSQLFieldName(attribute);
	}
	queryText += ") VALUES (?, ?";
	for (auto attribute : m_metaTable->GetObjectAttributes()) {
		unsigned int fieldCount = IMetaAttributeObject::GetSQLFieldCount(attribute);
		for (unsigned int i = 0; i < fieldCount; i++) {
			queryText += ", ?";
		}
	}
	queryText += ") MATCHING (UUID, " + IMetaAttributeObject::GetSQLFieldName(numLine) + ");";
	PreparedStatement* statement = databaseLayer->PrepareStatement(queryText);

	if (statement == NULL)
		return false;

	number_t numberLine = 1;
	for (auto objectValue : m_aObjectValues) {

		if (hasError)
			break;

		int position = 3;

		statement->SetParamString(1, m_dataObject->GetGuid());
		statement->SetParamBlob(2, reference_impl, sizeof(reference_t));
		for (auto attribute : m_metaTable->GetObjectAttributes()) {
			if (!m_metaTable->IsNumberLine(attribute->GetMetaID())) {
				IMetaAttributeObject::SetValueAttribute(
					attribute,
					objectValue.at(attribute->GetMetaID()),
					statement,
					position
				);
			}
			else {
				IMetaAttributeObject::SetValueAttribute(
					attribute,
					numberLine++,
					statement,
					position
				);
			}
		}

		hasError = statement->RunQuery() == DATABASE_LAYER_QUERY_RESULT_ERROR;
	}

	databaseLayer->CloseStatement(statement); 
	delete reference_impl;

	return !hasError;
}

bool CTabularSectionDataObjectRef::DeleteData()
{
	if (m_dataObject->IsNewObject())
		return true;

	IMetaObjectRecordData* metaObject = m_dataObject->GetMetaObject();
	wxASSERT(metaObject);
	wxString tableName = m_metaTable->GetTableNameDB();
	databaseLayer->RunQuery("DELETE FROM " + tableName + " WHERE UUID = '" + m_dataObject->GetGuid() + "';");
	return true;
}

#include "compiler/valueTable.h"

bool ITabularSectionDataObject::LoadDataFromTable(IValueTable* srcTable)
{
	IValueTableColumnCollection* colData = srcTable ? 
		srcTable->GetColumns() : NULL;
	
	if (colData == NULL)
		return false;

	wxArrayString columnName;
	for (unsigned int idx = 0; idx < colData->GetColumnCount() - 1; idx++) {
		IValueTableColumnCollection::IValueTableColumnInfo* colInfo = colData->GetColumnInfo(idx);
		wxASSERT(colInfo);
		if (m_dataColumnCollection->GetColumnByName(colInfo->GetColumnName()) != NULL) {
			columnName.push_back(colInfo->GetColumnName());
		}
	}

	unsigned int rowsCount = srcTable->GetCount();
	for (unsigned int row = 0; row < rowsCount; row++) {
		IValueTableReturnLine* retLine = srcTable->GetRowAt(row);
		IValueTableReturnLine* newRetLine = new CTabularSectionDataObjectReturnLine(this, AppenRow());
		newRetLine->PrepareNames();
		for (auto colName : columnName) {
			newRetLine->SetAttribute(colName, retLine->GetAttribute(colName));
		}
		wxDELETE(newRetLine);
	}

	return true;
}

IValueTable* ITabularSectionDataObject::SaveDataToTable() const
{
	CValueTable* valueTable = new CValueTable;
	IValueTableColumnCollection* colData = valueTable->GetColumns();

	for (unsigned int idx = 0; idx < m_dataColumnCollection->GetColumnCount() - 1; idx++) {
		IValueTableColumnCollection::IValueTableColumnInfo* colInfo = m_dataColumnCollection->GetColumnInfo(idx);
		wxASSERT(colInfo);
		IValueTableColumnCollection::IValueTableColumnInfo* newColInfo = colData->AddColumn(
			colInfo->GetColumnName(), colInfo->GetColumnTypes(), colInfo->GetColumnCaption(), colInfo->GetColumnWidth()
		);
		newColInfo->SetColumnID(colInfo->GetColumnID());
	}

	valueTable->PrepareNames();

	for (auto row : m_aObjectValues) {
		CValueTable::CValueTableReturnLine* retLine = valueTable->AddRow();
		wxASSERT(retLine);
		for (auto cols : row) {
			if (m_metaTable->IsNumberLine(cols.first))
				continue;
			retLine->SetValueByMetaID(cols.first, cols.second);
		}
	}

	return valueTable;
}

void CTabularSectionDataObjectRef::SetValueByMetaID(long line, const meta_identifier_t& id, const CValue& cVal)
{
	CValueForm* foundedForm = CValueForm::FindFormByGuid(
		m_dataObject->GetGuid()
	);

	ITabularSectionDataObject::SetValueByMetaID(line, id, cVal);

	if (foundedForm != NULL) {
		foundedForm->Modify(true);
	}
}

CValue CTabularSectionDataObjectRef::GetValueByMetaID(long line, const meta_identifier_t& id) const
{
	return ITabularSectionDataObject::GetValueByMetaID(line, id);
}

//////////////////////////////////////////////////////////////////////
//               CTabularSectionDataObjectColumnCollection          //
//////////////////////////////////////////////////////////////////////

#include "compiler/methods.h"

wxIMPLEMENT_DYNAMIC_CLASS(ITabularSectionDataObject::CTabularSectionDataObjectColumnCollection, IValueTable::IValueTableColumnCollection);

ITabularSectionDataObject::CTabularSectionDataObjectColumnCollection::CTabularSectionDataObjectColumnCollection() : IValueTableColumnCollection(), m_methods(NULL), m_ownerTable(NULL)
{
}

ITabularSectionDataObject::CTabularSectionDataObjectColumnCollection::CTabularSectionDataObjectColumnCollection(ITabularSectionDataObject* ownerTable) : IValueTableColumnCollection(), m_methods(new CMethods()), m_ownerTable(ownerTable)
{
	CMetaTableObject* metaTable = m_ownerTable->GetMetaObject();
	wxASSERT(metaTable);

	for (auto attributes : metaTable->GetObjectAttributes()) {
		if (metaTable->IsNumberLine(attributes->GetMetaID()))
			continue;
		CValueTabularSectionColumnInfo* columnInfo = new CValueTabularSectionColumnInfo(attributes);
		m_aColumnInfo.insert_or_assign(attributes->GetMetaID(), columnInfo);
		columnInfo->IncrRef();
	}
}

ITabularSectionDataObject::CTabularSectionDataObjectColumnCollection::~CTabularSectionDataObjectColumnCollection()
{
	for (auto& colInfo : m_aColumnInfo) {
		CValueTabularSectionColumnInfo* columnInfo = colInfo.second;
		wxASSERT(columnInfo);
		columnInfo->DecrRef();
	}

	wxDELETE(m_methods);
}

void ITabularSectionDataObject::CTabularSectionDataObjectColumnCollection::SetAt(const CValue& cKey, CValue& cVal)//индекс массива должен начинаться с 0
{
}

CValue ITabularSectionDataObject::CTabularSectionDataObjectColumnCollection::GetAt(const CValue& cKey) //индекс массива должен начинаться с 0
{
	unsigned int index = cKey.ToUInt();

	if ((index < 0 || index >= m_aColumnInfo.size() && !appData->DesignerMode()))
		CTranslateError::Error(_("Index goes beyond array"));

	auto itFounded = m_aColumnInfo.begin();
	std::advance(itFounded, index);
	return itFounded->second;
}

//////////////////////////////////////////////////////////////////////
//               CValueTabularSectionColumnInfo                     //
//////////////////////////////////////////////////////////////////////

wxIMPLEMENT_DYNAMIC_CLASS(ITabularSectionDataObject::CTabularSectionDataObjectColumnCollection::CValueTabularSectionColumnInfo, IValueTable::IValueTableColumnCollection::IValueTableColumnInfo);

ITabularSectionDataObject::CTabularSectionDataObjectColumnCollection::CValueTabularSectionColumnInfo::CValueTabularSectionColumnInfo() :
	IValueTableColumnInfo(), m_metaAttribute(NULL)
{
}

ITabularSectionDataObject::CTabularSectionDataObjectColumnCollection::CValueTabularSectionColumnInfo::CValueTabularSectionColumnInfo(IMetaAttributeObject* metaAttribute) :
	IValueTableColumnInfo(), m_metaAttribute(metaAttribute)
{
}

ITabularSectionDataObject::CTabularSectionDataObjectColumnCollection::CValueTabularSectionColumnInfo::~CValueTabularSectionColumnInfo()
{
}

//////////////////////////////////////////////////////////////////////
//               CTabularSectionDataObjectReturnLine                //
//////////////////////////////////////////////////////////////////////

ITabularSectionDataObject::CTabularSectionDataObjectReturnLine::CTabularSectionDataObjectReturnLine()
	: IValueTableReturnLine(), m_methods(new CMethods())
{
}

ITabularSectionDataObject::CTabularSectionDataObjectReturnLine::CTabularSectionDataObjectReturnLine(ITabularSectionDataObject* ownerTable, int line)
	: IValueTableReturnLine(), m_ownerTable(ownerTable), m_lineTable(line), m_methods(new CMethods())
{
}

ITabularSectionDataObject::CTabularSectionDataObjectReturnLine::~CTabularSectionDataObjectReturnLine()
{
	wxDELETE(m_methods);
}

void ITabularSectionDataObject::CTabularSectionDataObjectReturnLine::PrepareNames() const
{
	std::vector<SEng> aAttributes;

	for (auto attribute : m_ownerTable->m_metaTable->GetObjectAttributes())
	{
		SEng aAttribute;
		aAttribute.sName = attribute->GetName();
		aAttribute.iName = attribute->GetMetaID();
		aAttribute.sSynonym = wxT("default");
		aAttributes.push_back(aAttribute);
	}

	m_methods->PrepareAttributes(aAttributes.data(), aAttributes.size());
}


void ITabularSectionDataObject::CTabularSectionDataObjectReturnLine::SetValueByMetaID(const meta_identifier_t& id, const CValue& cVal)
{
	m_ownerTable->SetValueByMetaID(m_lineTable, id, cVal);
}

CValue ITabularSectionDataObject::CTabularSectionDataObjectReturnLine::GetValueByMetaID(const meta_identifier_t& id) const
{
	return m_ownerTable->GetValueByMetaID(m_lineTable, id);
}

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

SO_VALUE_REGISTER(ITabularSectionDataObject::CTabularSectionDataObjectColumnCollection, "tabularSectionColumn", CTabularSectionDataObjectColumnCollection, TEXT2CLSID("VL_TSCL"));
SO_VALUE_REGISTER(ITabularSectionDataObject::CTabularSectionDataObjectColumnCollection::CValueTabularSectionColumnInfo, "tabularSectionColumnInfo", CValueTabularSectionColumnInfo, TEXT2CLSID("VL_CI"));
SO_VALUE_REGISTER(ITabularSectionDataObject::CTabularSectionDataObjectReturnLine, "tabularSectionRow", CTabularSectionDataObjectReturnLine, TEXT2CLSID("VL_TBCR"));
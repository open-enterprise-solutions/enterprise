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
wxIMPLEMENT_DYNAMIC_CLASS(ITabularSectionDataObject::CTabularSectionDataObjectReturnLine, IValueTable::IValueModelReturnLine);
wxIMPLEMENT_DYNAMIC_CLASS(CTabularSectionDataObject, ITabularSectionDataObject);
wxIMPLEMENT_DYNAMIC_CLASS(CTabularSectionDataObjectRef, ITabularSectionDataObject);

//////////////////////////////////////////////////////////////////////
//               ITabularSectionDataObject                          //
//////////////////////////////////////////////////////////////////////

#include "metadata/metadata.h"
#include "metadata/singleMetaTypes.h"

wxDataViewItem ITabularSectionDataObject::FindRowValue(const CValue& cVal, const wxString& colName) const
{
	IValueModelColumnCollection::IValueModelColumnInfo* colInfo = m_dataColumnCollection->GetColumnByName(colName);
	if (colInfo != NULL) {
		for (long row = 0; row < GetRowCount(); row++) {
			const wxDataViewItem& item = GetItem(row);
			wxValueTableRow* node = GetViewData<wxValueTableRow>(item);
			if (node != NULL &&
				cVal == node->GetValue((meta_identifier_t)colInfo->GetColumnID())) {
				return item;
			}
		}
	}
	return wxDataViewItem(NULL);
}

wxDataViewItem ITabularSectionDataObject::FindRowValue(IValueModelReturnLine* retLine) const
{
	return wxDataViewItem(NULL);
}

CValue ITabularSectionDataObject::GetAt(const CValue& cKey)
{
	long index = cKey.ToUInt();
	if (index >= GetRowCount() && !appData->DesignerMode())
		CTranslateError::Error(_("Index outside array bounds"));
	return new CTabularSectionDataObjectReturnLine(this, GetItem(index));
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
	if (m_metaTable->IsNumberLine(id)) 
		return;
	if (!appData->DesignerMode()) {
		wxValueTableRow* node = GetViewData(GetItem(line));
		if (node != NULL) {
			IMetaAttributeObject* metaAttribute = m_metaTable->FindAttribute(id);
			wxASSERT(metaAttribute);
			node->SetValue(
				metaAttribute->AdjustValue(cVal), id, true
			);
		}
	}
}

CValue ITabularSectionDataObject::GetValueByMetaID(long line, const meta_identifier_t& id) const
{
	if (m_metaTable->IsNumberLine(id))
		return line + 1;
	IMetaAttributeObject* metaAttribute = m_metaTable->FindAttribute(id);
	wxASSERT(metaAttribute);
	if (appData->DesignerMode())
		return metaAttribute->CreateValue();
	wxValueTableRow* node = GetViewData(GetItem(line));
	if (node != NULL)
		return node->GetValue(id);
	return CValue();
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
	IValueTable::Clear();
	IMetaObjectRecordData* metaObject = m_dataObject->GetMetaObject();
	wxASSERT(metaObject);

	wxString tableName = m_metaTable->GetTableNameDB();
	wxString sql = "SELECT * FROM " + tableName + " WHERE UUID = '" + m_dataObject->GetGuid() + "'";

	DatabaseResultSet* resultSet = databaseLayer->RunQueryWithResults(sql);
	if (resultSet == NULL)
		return false;
	while (resultSet->Next()) {
		modelArray_t modelValues;
		for (auto attribute : m_metaTable->GetObjectAttributes()) {
			wxString nameAttribute = attribute->GetFieldNameDB();
			if (m_metaTable->IsNumberLine(attribute->GetMetaID())) {
				modelValues[attribute->GetMetaID()] = CValue(); //numberline is special field
				continue;
			}
			modelValues.insert_or_assign(attribute->GetMetaID(),
				IMetaAttributeObject::GetValueAttribute(attribute, resultSet, createData));
		}
		IValueTable::Append(new wxValueTableRow(modelValues),
			!CTranslateError::IsSimpleMode()
		);

	}
	resultSet->Close();
	return true;
}

#include "compiler/systemObjects.h"

bool CTabularSectionDataObjectRef::SaveData()
{
	bool hasError = false;
	//check fill attributes 
	bool fillCheck = true; long currLine = 1;
	for (long row = 0; row < GetRowCount(); row++) {
		for (auto attribute : m_metaTable->GetObjectAttributes()) {
			if (attribute->FillCheck()) {
				wxValueTableRow* node = GetViewData(GetItem(row));
				wxASSERT(node);
				if (node->IsEmptyValue(attribute->GetMetaID())) {
					wxString fillError =
						wxString::Format(_("The %s is required on line %i of the %s list"), attribute->GetSynonym(), currLine, m_metaTable->GetSynonym());
					CSystemObjects::Message(fillError, eStatusMessage::eStatusMessage_Information);
					fillCheck = false;
				}
			}
		}
		currLine++;
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
	for (long row = 0; row < GetRowCount(); row++) {
		if (hasError)
			break;
		int position = 3;
		statement->SetParamString(1, m_dataObject->GetGuid());
		statement->SetParamBlob(2, reference_impl, sizeof(reference_t));
		for (auto attribute : m_metaTable->GetObjectAttributes()) {
			if (!m_metaTable->IsNumberLine(attribute->GetMetaID())) {
				wxValueTableRow* node = GetViewData(GetItem(row));
				wxASSERT(node);
				IMetaAttributeObject::SetValueAttribute(
					attribute,
					node->GetValue(attribute->GetMetaID()),
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
	IValueModelColumnCollection* colData = srcTable ?
		srcTable->GetColumnCollection() : NULL;

	if (colData == NULL)
		return false;

	wxArrayString columnName;
	for (unsigned int idx = 0; idx < colData->GetColumnCount() - 1; idx++) {
		IValueModelColumnCollection::IValueModelColumnInfo* colInfo = colData->GetColumnInfo(idx);
		wxASSERT(colInfo);
		if (m_dataColumnCollection->GetColumnByName(colInfo->GetColumnName()) != NULL) {
			columnName.push_back(colInfo->GetColumnName());
		}
	}

	unsigned int rowCount = srcTable->GetRowCount();
	for (unsigned int row = 0; row < rowCount; row++) {
		IValueModelReturnLine* retLine = srcTable->GetRowAt(srcTable->GetItem(row));
		IValueModelReturnLine* newRetLine = new CTabularSectionDataObjectReturnLine(this, GetItem(AppendRow()));
		newRetLine->PrepareNames();
		for (auto colName : columnName) {
			newRetLine->SetAttribute(colName, retLine->GetAttribute(colName));
		}
		delete newRetLine;
		delete retLine;
	}

	return true;
}

IValueTable* ITabularSectionDataObject::SaveDataToTable() const
{
	CValueTable* valueTable = new CValueTable;
	IValueModelColumnCollection* colData = valueTable->GetColumnCollection();
	for (unsigned int idx = 0; idx < m_dataColumnCollection->GetColumnCount() - 1; idx++) {
		IValueModelColumnCollection::IValueModelColumnInfo* colInfo = m_dataColumnCollection->GetColumnInfo(idx);
		wxASSERT(colInfo);
		IValueModelColumnCollection::IValueModelColumnInfo* newColInfo = colData->AddColumn(
			colInfo->GetColumnName(), colInfo->GetColumnTypes(), colInfo->GetColumnCaption(), colInfo->GetColumnWidth()
		);
		newColInfo->SetColumnID(colInfo->GetColumnID());
	}
	valueTable->PrepareNames();
	for (long row = 0; row < GetRowCount(); row++) {
		CValueTable::CValueTableReturnLine* retLine = valueTable->GetRowAt(valueTable->AppendRow());
		wxASSERT(retLine);
		wxValueTableRow* node = GetViewData(GetItem(row));
		wxASSERT(node);
		for (unsigned int col = 0; col < colData->GetColumnCount(); col++) {
			IValueModelColumnCollection::IValueModelColumnInfo* colInfo = colData->GetColumnInfo(col);
			wxASSERT(colInfo);
			if (m_metaTable->IsNumberLine(colInfo->GetColumnID()))
				continue;
			if (node != NULL)
				retLine->SetValueByMetaID(colInfo->GetColumnID(), node->GetValue((meta_identifier_t)colInfo->GetColumnID()));
		}
		delete retLine;
	}
	return valueTable;
}

void CTabularSectionDataObjectRef::SetValueByMetaID(long line, const meta_identifier_t& id, const CValue& cVal)
{
	CValueForm* foundedForm = CValueForm::FindFormByGuid(
		m_dataObject->GetGuid()
	);
	ITabularSectionDataObject::SetValueByMetaID(line, id, cVal);
	if (foundedForm != NULL) 
		foundedForm->Modify(true);
}

CValue CTabularSectionDataObjectRef::GetValueByMetaID(long line, const meta_identifier_t& id) const
{
	return ITabularSectionDataObject::GetValueByMetaID(line, id);
}

//////////////////////////////////////////////////////////////////////
//               CTabularSectionDataObjectColumnCollection          //
//////////////////////////////////////////////////////////////////////

#include "compiler/methods.h"

wxIMPLEMENT_DYNAMIC_CLASS(ITabularSectionDataObject::CTabularSectionDataObjectColumnCollection, IValueTable::IValueModelColumnCollection);

ITabularSectionDataObject::CTabularSectionDataObjectColumnCollection::CTabularSectionDataObjectColumnCollection() : IValueModelColumnCollection(), m_methods(NULL), m_ownerTable(NULL)
{
}

ITabularSectionDataObject::CTabularSectionDataObjectColumnCollection::CTabularSectionDataObjectColumnCollection(ITabularSectionDataObject* ownerTable) : IValueModelColumnCollection(), m_methods(new CMethods()), m_ownerTable(ownerTable)
{
	CMetaTableObject* metaTable = m_ownerTable->GetMetaObject();
	wxASSERT(metaTable);

	for (auto attributes : metaTable->GetObjectAttributes()) {
		if (metaTable->IsNumberLine(attributes->GetMetaID()))
			continue;
		CValueTabularSectionColumnInfo* columnInfo = new CValueTabularSectionColumnInfo(attributes);
		m_columnInfo.insert_or_assign(attributes->GetMetaID(), columnInfo);
		columnInfo->IncrRef();
	}
}

ITabularSectionDataObject::CTabularSectionDataObjectColumnCollection::~CTabularSectionDataObjectColumnCollection()
{
	for (auto& colInfo : m_columnInfo) {
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

	if ((index < 0 || index >= m_columnInfo.size() && !appData->DesignerMode()))
		CTranslateError::Error(_("Index goes beyond array"));

	auto itFounded = m_columnInfo.begin();
	std::advance(itFounded, index);
	return itFounded->second;
}

//////////////////////////////////////////////////////////////////////
//               CValueTabularSectionColumnInfo                     //
//////////////////////////////////////////////////////////////////////

wxIMPLEMENT_DYNAMIC_CLASS(ITabularSectionDataObject::CTabularSectionDataObjectColumnCollection::CValueTabularSectionColumnInfo, IValueTable::IValueModelColumnCollection::IValueModelColumnInfo);

ITabularSectionDataObject::CTabularSectionDataObjectColumnCollection::CValueTabularSectionColumnInfo::CValueTabularSectionColumnInfo() :
	IValueModelColumnInfo(), m_metaAttribute(NULL)
{
}

ITabularSectionDataObject::CTabularSectionDataObjectColumnCollection::CValueTabularSectionColumnInfo::CValueTabularSectionColumnInfo(IMetaAttributeObject* metaAttribute) :
	IValueModelColumnInfo(), m_metaAttribute(metaAttribute)
{
}

ITabularSectionDataObject::CTabularSectionDataObjectColumnCollection::CValueTabularSectionColumnInfo::~CValueTabularSectionColumnInfo()
{
}

//////////////////////////////////////////////////////////////////////
//               CTabularSectionDataObjectReturnLine                //
//////////////////////////////////////////////////////////////////////

ITabularSectionDataObject::CTabularSectionDataObjectReturnLine::CTabularSectionDataObjectReturnLine(ITabularSectionDataObject* ownerTable, const wxDataViewItem& line)
	: IValueModelReturnLine(line), m_ownerTable(ownerTable), m_methods(new CMethods()) {
}

ITabularSectionDataObject::CTabularSectionDataObjectReturnLine::~CTabularSectionDataObjectReturnLine() {
	wxDELETE(m_methods);
}

void ITabularSectionDataObject::CTabularSectionDataObjectReturnLine::PrepareNames() const
{
	std::vector<SEng> aAttributes;
	for (auto attribute : m_ownerTable->m_metaTable->GetObjectAttributes()) {
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
	if (m_ownerTable->ValidateReturnLine(this)) {
		m_ownerTable->SetValueByMetaID(
			m_ownerTable->GetRow(m_lineItem), id, cVal);
	}
}

CValue ITabularSectionDataObject::CTabularSectionDataObjectReturnLine::GetValueByMetaID(const meta_identifier_t& id) const
{
	CTabularSectionDataObjectReturnLine *ret = const_cast<CTabularSectionDataObjectReturnLine*>(this);
	wxASSERT(ret);
	if (m_ownerTable->ValidateReturnLine(ret)) {
		return m_ownerTable->GetValueByMetaID(
			m_ownerTable->GetRow(m_lineItem), id);
	}
	return _("Error while reading data");
}

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

SO_VALUE_REGISTER(ITabularSectionDataObject::CTabularSectionDataObjectColumnCollection, "tabularSectionColumn", CTabularSectionDataObjectColumnCollection, TEXT2CLSID("VL_TSCL"));
SO_VALUE_REGISTER(ITabularSectionDataObject::CTabularSectionDataObjectColumnCollection::CValueTabularSectionColumnInfo, "tabularSectionColumnInfo", CValueTabularSectionColumnInfo, TEXT2CLSID("VL_CI"));
SO_VALUE_REGISTER(ITabularSectionDataObject::CTabularSectionDataObjectReturnLine, "tabularSectionRow", CTabularSectionDataObjectReturnLine, TEXT2CLSID("VL_TBCR"));
////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : tabular sections
////////////////////////////////////////////////////////////////////////////

#include "tabularSection.h"
#include "appData.h"
#include <3rdparty/databaseLayer/databaseLayer.h>
#include <3rdparty/databaseLayer/databaseErrorCodes.h>
#include "core/metadata/metaObjects/objects/object.h"
#include "utils/stringUtils.h"

wxIMPLEMENT_ABSTRACT_CLASS(ITabularSectionDataObject, IValueTable);
wxIMPLEMENT_DYNAMIC_CLASS(ITabularSectionDataObject::CTabularSectionDataObjectReturnLine, IValueTable::IValueModelReturnLine);
wxIMPLEMENT_DYNAMIC_CLASS(CTabularSectionDataObject, ITabularSectionDataObject);
wxIMPLEMENT_DYNAMIC_CLASS(CTabularSectionDataObjectRef, ITabularSectionDataObject);

//////////////////////////////////////////////////////////////////////
//               ITabularSectionDataObject                          //
//////////////////////////////////////////////////////////////////////

#include "core/metadata/metadata.h"
#include "core/metadata/singleClass.h"

wxDataViewItem ITabularSectionDataObject::FindRowValue(const CValue& varValue, const wxString& colName) const
{
	IValueModelColumnCollection::IValueModelColumnInfo* colInfo = m_dataColumnCollection->GetColumnByName(colName);
	if (colInfo != NULL) {
		for (long row = 0; row < GetRowCount(); row++) {
			const wxDataViewItem& item = GetItem(row);
			wxValueTableRow* node = GetViewData<wxValueTableRow>(item);
			if (node != NULL &&
				varValue == node->GetValue((meta_identifier_t)colInfo->GetColumnID())) {
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

bool ITabularSectionDataObject::GetAt(const CValue& varKeyValue, CValue& pvarValue)
{
	long index = varKeyValue.GetUInteger();
	if (index >= GetRowCount() && !appData->DesignerMode()) {
		CTranslateError::Error("Array index out of bounds");
		return false;
	}
	pvarValue = new CTabularSectionDataObjectReturnLine(this, GetItem(index));
	return true;
}

CLASS_ID ITabularSectionDataObject::GetTypeClass() const
{
	IMetadata* metaData = m_metaTable->GetMetadata();
	wxASSERT(metaData);
	IMetaTypeObjectValueSingle* clsFactory =
		metaData->GetTypeObject(m_metaTable, eMetaObjectType::enTabularSection);
	wxASSERT(clsFactory);
	return clsFactory->GetTypeClass();
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

bool ITabularSectionDataObject::SetValueByMetaID(const wxDataViewItem& item, const meta_identifier_t& id, const CValue& varMetaVal)
{
	if (m_readOnly)
		return false;

	if (m_metaTable->IsNumberLine(id))
		return false;

	if (!appData->DesignerMode()) {
		wxValueTableRow* node = GetViewData<wxValueTableRow>(item);
		if (node != NULL) {
			IMetaAttributeObject* metaAttribute = m_metaTable->FindProp(id);
			wxASSERT(metaAttribute);
			return node->SetValue(
				id, metaAttribute->AdjustValue(varMetaVal), true
			);
		}
	}

	return false;
}

bool ITabularSectionDataObject::GetValueByMetaID(const wxDataViewItem& item, const meta_identifier_t& id, CValue& pvarMetaVal) const
{
	if (m_metaTable->IsNumberLine(id)) {
		pvarMetaVal = GetRow(item) + 1;
		return true;
	}

	IMetaAttributeObject* metaAttribute = m_metaTable->FindProp(id);
	wxASSERT(metaAttribute);
	if (appData->DesignerMode()) {
		pvarMetaVal = metaAttribute->CreateValue();
		return true;
	}
	wxValueTableRow* node = GetViewData<wxValueTableRow>(item);
	if (node != NULL)
		return node->GetValue(id, pvarMetaVal);
	return false;
}

//////////////////////////////////////////////////////////////////////
//               CTabularSectionDataObject                          //
//////////////////////////////////////////////////////////////////////

CTabularSectionDataObject::CTabularSectionDataObject() {}

CTabularSectionDataObject::CTabularSectionDataObject(IRecordDataObject* recordObject, CMetaTableObject* tableObject) :
	ITabularSectionDataObject(recordObject, tableObject)
{
}

//////////////////////////////////////////////////////////////////////
//               CTabularSectionDataObjectRef                       //
//////////////////////////////////////////////////////////////////////

CTabularSectionDataObjectRef::CTabularSectionDataObjectRef() : m_readAfter(false) {}

CTabularSectionDataObjectRef::CTabularSectionDataObjectRef(CReferenceDataObject* reference, CMetaTableObject* tableObject, bool readAfter) :
	ITabularSectionDataObject(reference, tableObject, true), m_readAfter(readAfter)
{
}

CTabularSectionDataObjectRef::CTabularSectionDataObjectRef(IRecordDataObjectRef* recordObject, CMetaTableObject* tableObject) :
	ITabularSectionDataObject(recordObject, tableObject), m_readAfter(false)
{
}

CTabularSectionDataObjectRef::CTabularSectionDataObjectRef(CSelectorDataObject* selectorObject, CMetaTableObject* tableObject) :
	ITabularSectionDataObject((IObjectValueInfo*)selectorObject, tableObject), m_readAfter(false)
{
}

bool CTabularSectionDataObjectRef::LoadData(const Guid& srcGuid, bool createData)
{
	if (m_objectValue->IsNewObject() && !srcGuid.isValid()) {
		m_readAfter = true;
		return false;
	}

	IValueTable::Clear();
	IMetaObjectRecordData* metaObject = m_objectValue->GetMetaObject();
	wxASSERT(metaObject);
	const wxString& tableName = m_metaTable->GetTableNameDB();
	const wxString& sqlQuery = "SELECT * FROM " + tableName + " WHERE UUID = '" + srcGuid.str() + "'";
	DatabaseResultSet* resultSet = databaseLayer->RunQueryWithResults(sqlQuery);
	if (resultSet == NULL)
		return false;
	while (resultSet->Next()) {
		valueArray_t modelValues;
		for (auto attribute : m_metaTable->GetObjectAttributes()) {
			if (m_metaTable->IsNumberLine(attribute->GetMetaID()))
				continue;
			IMetaAttributeObject::GetValueAttribute(attribute, modelValues[attribute->GetMetaID()], resultSet, createData);
		}
		IValueTable::Append(new wxValueTableRow(modelValues),
			!CTranslateError::IsSimpleMode()
		);

	}
	resultSet->Close();
	m_readAfter = true;
	return true;
}

#include "core/compiler/systemObjects.h"

bool CTabularSectionDataObjectRef::SaveData()
{
	if (m_readOnly)
		return true;

	bool hasError = false;
	//check fill attributes 
	bool fillCheck = true; long currLine = 1;
	for (long row = 0; row < GetRowCount(); row++) {
		for (auto attribute : m_metaTable->GetObjectAttributes()) {
			if (attribute->FillCheck()) {
				wxValueTableRow* node = GetViewData<wxValueTableRow>(GetItem(row));
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

	if (!fillCheck)
		return false;

	if (!CTabularSectionDataObjectRef::DeleteData())
		return false;

	CMetaDefaultAttributeObject* numLine = m_metaTable->GetNumberLine();
	wxASSERT(numLine);
	IMetaObjectRecordData* metaObject = m_objectValue->GetMetaObject();
	wxASSERT(metaObject);
	reference_t* reference_impl = new reference_t(metaObject->GetMetaID(), m_objectValue->GetGuid());

	const wxString& tableName = m_metaTable->GetTableNameDB();
	wxString queryText = "INSERT INTO " + tableName + " (";
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
	queryText += ");";

	PreparedStatement* statement = databaseLayer->PrepareStatement(queryText);
	if (statement == NULL)
		return false;

	number_t numberLine = 1;
	for (long row = 0; row < GetRowCount(); row++) {
		if (hasError)
			break;
		int position = 3;
		statement->SetParamString(1, m_objectValue->GetGuid());
		statement->SetParamBlob(2, reference_impl, sizeof(reference_t));
		for (auto attribute : m_metaTable->GetObjectAttributes()) {
			if (!m_metaTable->IsNumberLine(attribute->GetMetaID())) {
				wxValueTableRow* node = GetViewData<wxValueTableRow>(GetItem(row));
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
	if (m_readOnly)
		return true;

	if (m_objectValue->IsNewObject())
		return true;

	IMetaObjectRecordData* metaObject = m_objectValue->GetMetaObject();
	wxASSERT(metaObject);
	const wxString& tableName = m_metaTable->GetTableNameDB();
	databaseLayer->RunQuery("DELETE FROM " + tableName + " WHERE UUID = '" + m_objectValue->GetGuid() + "';");
	return true;
}

#include "core/compiler/valueTable.h"

bool ITabularSectionDataObject::LoadDataFromTable(IValueTable* srcTable)
{
	if (m_readOnly)
		return false;

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
		const wxDataViewItem& srcItem = srcTable->GetItem(row);
		const wxDataViewItem& dstItem = GetItem(AppendRow());
		for (auto colName : columnName) {
			CValue cRetValue;
			if (srcTable->GetValueByMetaID(srcItem, srcTable->GetColumnIDByName(colName), cRetValue)) {
				const meta_identifier_t& id = GetColumnIDByName(colName);
				if (id != wxNOT_FOUND) SetValueByMetaID(dstItem, id, cRetValue);
			}
		}
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
			colInfo->GetColumnName(), colInfo->GetColumnType(), colInfo->GetColumnCaption(), colInfo->GetColumnWidth()
		);
		newColInfo->SetColumnID(colInfo->GetColumnID());
	}
	valueTable->PrepareNames();
	for (long row = 0; row < GetRowCount(); row++) {
		const wxDataViewItem& srcItem = GetItem(row);
		const wxDataViewItem& dstItem = valueTable->GetItem(valueTable->AppendRow());
		for (unsigned int col = 0; col < colData->GetColumnCount(); col++) {
			IValueModelColumnCollection::IValueModelColumnInfo* colInfo = colData->GetColumnInfo(col);
			wxASSERT(colInfo);
			if (m_metaTable->IsNumberLine(colInfo->GetColumnID()))
				continue;
			CValue cRetValue;
			if (GetValueByMetaID(srcItem, colInfo->GetColumnID(), cRetValue)) {
				const meta_identifier_t& id = GetColumnIDByName(colInfo->GetColumnName());
				if (id != wxNOT_FOUND) valueTable->SetValueByMetaID(dstItem, id, cRetValue);
			}
		}
	}

	return valueTable;
}

bool CTabularSectionDataObjectRef::SetValueByMetaID(const wxDataViewItem& item, const meta_identifier_t& id, const CValue& varMetaVal)
{
	if (varMetaVal != ITabularSectionDataObject::GetValueByMetaID(item, id)) {
		CValueForm* const foundedForm = CValueForm::FindFormByGuid(
			m_objectValue->GetGuid()
		);
		bool result = ITabularSectionDataObject::SetValueByMetaID(item, id, varMetaVal);
		if (result && foundedForm != NULL)
			foundedForm->Modify(true);
		return result;
	}

	return false;
}

bool CTabularSectionDataObjectRef::GetValueByMetaID(const wxDataViewItem& item, const meta_identifier_t& id, CValue& pvarMetaVal) const
{
	return ITabularSectionDataObject::GetValueByMetaID(item, id, pvarMetaVal);
}

//////////////////////////////////////////////////////////////////////
//               CTabularSectionDataObjectColumnCollection          //
//////////////////////////////////////////////////////////////////////

wxIMPLEMENT_DYNAMIC_CLASS(ITabularSectionDataObject::CTabularSectionDataObjectColumnCollection, IValueTable::IValueModelColumnCollection);

ITabularSectionDataObject::CTabularSectionDataObjectColumnCollection::CTabularSectionDataObjectColumnCollection() : IValueModelColumnCollection(), m_methodHelper(NULL), m_ownerTable(NULL)
{
}

ITabularSectionDataObject::CTabularSectionDataObjectColumnCollection::CTabularSectionDataObjectColumnCollection(ITabularSectionDataObject* ownerTable) : IValueModelColumnCollection(), m_methodHelper(new CMethodHelper()), m_ownerTable(ownerTable)
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

	wxDELETE(m_methodHelper);
}

bool ITabularSectionDataObject::CTabularSectionDataObjectColumnCollection::SetAt(const CValue& varKeyValue, const CValue& varValue)//индекс массива должен начинаться с 0
{
	return false;
}

bool ITabularSectionDataObject::CTabularSectionDataObjectColumnCollection::GetAt(const CValue& varKeyValue, CValue& pvarValue) //индекс массива должен начинаться с 0
{
	unsigned int index = varKeyValue.GetUInteger();
	if ((index < 0 || index >= m_columnInfo.size() && !appData->DesignerMode())) {
		CTranslateError::Error("Index goes beyond array");
		return false;
	}
	auto itFounded = m_columnInfo.begin();
	std::advance(itFounded, index);
	pvarValue = itFounded->second;
	return true;
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
	: IValueModelReturnLine(line), m_ownerTable(ownerTable), m_methodHelper(new CMethodHelper()) {
}

ITabularSectionDataObject::CTabularSectionDataObjectReturnLine::~CTabularSectionDataObjectReturnLine() {
	wxDELETE(m_methodHelper);
}

void ITabularSectionDataObject::CTabularSectionDataObjectReturnLine::PrepareNames() const
{
	m_methodHelper->ClearHelper();
	for (auto attribute : m_ownerTable->m_metaTable->GetObjectAttributes()) {
		m_methodHelper->AppendProp(
			attribute->GetName(),
			true,
			!m_ownerTable->m_metaTable->IsNumberLine(attribute->GetMetaID()),
			attribute->GetMetaID()
		);
	}
}

long ITabularSectionDataObject::AppendRow(unsigned int before)
{
	valueArray_t valueRow;
	for (auto attribute : m_metaTable->GetObjectAttributes()) {
		if (!m_metaTable->IsNumberLine(attribute->GetMetaID()))
			valueRow.insert_or_assign(attribute->GetMetaID(), attribute->CreateValue());
	}

	if (before > 0)
		return IValueTable::Insert(
			new wxValueTableRow(valueRow), before, !CTranslateError::IsSimpleMode());

	return IValueTable::Append(
		new wxValueTableRow(valueRow), !CTranslateError::IsSimpleMode()
	);
}

long CTabularSectionDataObjectRef::AppendRow(unsigned int before)
{
	if (!CTranslateError::IsSimpleMode())
		m_objectValue->Modify(true);
	return ITabularSectionDataObject::AppendRow(before);
}

//****************************************************************************
//*                              Support methods                             *
//****************************************************************************

void ITabularSectionDataObject::PrepareNames() const
{
	m_methodHelper->ClearHelper();

	if (m_readOnly) {
		m_methodHelper->AppendFunc("count", "count()", enCount, eTabularSection);
		m_methodHelper->AppendFunc("find", 2, "find(value, col)", enFind, eTabularSection);
		m_methodHelper->AppendFunc("unload", "unload()", enUnload, eTabularSection);
		m_methodHelper->AppendFunc("getMetadata", "getMetadata()", enGetMetadata, eTabularSection);
	}
	else {
		m_methodHelper->AppendFunc("add", "add()", enAddValue, eTabularSection);
		m_methodHelper->AppendFunc("count", "count()", enCount, eTabularSection);
		m_methodHelper->AppendFunc("find", 2, "find(value, col)", enFind, eTabularSection);
		m_methodHelper->AppendFunc("delete", 1, "delete(row)", enDelete, eTabularSection);
		m_methodHelper->AppendFunc("clear", "clear()", enClear, eTabularSection);
		m_methodHelper->AppendFunc("load", 1, "load(table)", enLoad, eTabularSection);
		m_methodHelper->AppendFunc("unload", "unload()", enUnload, eTabularSection);
		m_methodHelper->AppendFunc("getMetadata", "getMetadata()", enGetMetadata, eTabularSection);
	}
}

//****************************************************************************
//*                              Override attribute                          *
//****************************************************************************

bool ITabularSectionDataObject::CTabularSectionDataObjectReturnLine::SetPropVal(const long lPropNum, const CValue& varPropVal)
{
	return SetValueByMetaID(m_methodHelper->GetPropData(lPropNum), varPropVal);
}

bool ITabularSectionDataObject::CTabularSectionDataObjectReturnLine::GetPropVal(const long lPropNum, CValue& pvarPropVal)
{
	return GetValueByMetaID(m_methodHelper->GetPropData(lPropNum), pvarPropVal);
}

#include "core/metadata/metadata.h"

bool ITabularSectionDataObject::CallAsFunc(const long lMethodNum, CValue& pvarRetValue, CValue** paParams, const long lSizeArray)
{
	long lMethodAlias = m_methodHelper->GetMethodAlias(lMethodNum);
	if (lMethodAlias != eTabularSection)
		return false;
	long lMethodData = m_methodHelper->GetMethodData(lMethodNum);
	switch (lMethodData)
	{
	case enAddValue:
		pvarRetValue = new CTabularSectionDataObjectReturnLine(this, GetItem(AppendRow()));
		return true;
	case enFind: {
		const wxDataViewItem& item = FindRowValue(*paParams[0], paParams[1]->GetString());
		if (item.IsOk())
			pvarRetValue = GetRowAt(item);
		return true;
	}
	case enCount:
		pvarRetValue = (unsigned int)GetRowCount();
	case enDelete: {
		CTabularSectionDataObjectReturnLine* retLine = NULL;
		if (paParams[0]->ConvertToValue(retLine)) {
			wxValueTableRow* node = GetViewData<wxValueTableRow>(retLine->GetLineItem());
			if (node != NULL)
				IValueTable::Remove(node);
		}
		else {
			const number_t& number = paParams[0]->GetNumber();
			wxValueTableRow* node = GetViewData<wxValueTableRow>(GetItem(number.ToInt()));
			if (node != NULL)
				IValueTable::Remove(node);
		}
		return true;
	}
	case enClear:
		Clear();
		return true;
	case enLoad:
		ITabularSectionDataObject::LoadDataFromTable(paParams[0]->ConvertToType<IValueTable>());
		return true;
	case enUnload:
		pvarRetValue = SaveDataToTable();
		return true;
	case enGetMetadata:
		pvarRetValue = m_metaTable;
		return true;
	}

	return false;
}

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

SO_VALUE_REGISTER(ITabularSectionDataObject::CTabularSectionDataObjectColumnCollection, "tabularSectionColumn", TEXT2CLSID("VL_TSCL"));
SO_VALUE_REGISTER(ITabularSectionDataObject::CTabularSectionDataObjectColumnCollection::CValueTabularSectionColumnInfo, "tabularSectionColumnInfo", TEXT2CLSID("VL_CI"));
SO_VALUE_REGISTER(ITabularSectionDataObject::CTabularSectionDataObjectReturnLine, "tabularSectionRow", TEXT2CLSID("VL_TBCR"));
////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : tabular sections
////////////////////////////////////////////////////////////////////////////

#include "tabularSection.h"

#include "backend/metaCollection/partial/object.h"
#include "backend/metaCollection/partial/reference/reference.h"

#include "backend/appData.h"

wxIMPLEMENT_ABSTRACT_CLASS(ITabularSectionDataObject, IValueTable);
wxIMPLEMENT_DYNAMIC_CLASS(ITabularSectionDataObject::CTabularSectionDataObjectReturnLine, IValueTable::IValueModelReturnLine);
wxIMPLEMENT_DYNAMIC_CLASS(CTabularSectionDataObject, ITabularSectionDataObject);
wxIMPLEMENT_DYNAMIC_CLASS(CTabularSectionDataObjectRef, ITabularSectionDataObject);

//////////////////////////////////////////////////////////////////////
//               ITabularSectionDataObject                          //
//////////////////////////////////////////////////////////////////////

#include "backend/metaData.h"
#include "backend/objCtor.h"

wxDataViewItem ITabularSectionDataObject::FindRowValue(const CValue& varValue, const wxString& colName) const
{
	IValueModelColumnCollection::IValueModelColumnInfo* colInfo = m_dataColumnCollection->GetColumnByName(colName);
	if (colInfo != nullptr) {
		for (long row = 0; row < GetRowCount(); row++) {
			const wxDataViewItem& item = GetItem(row);
			wxValueTableRow* node = GetViewData<wxValueTableRow>(item);
			if (node != nullptr &&
				varValue == node->GetTableValue(colInfo->GetColumnID())) {
				return item;
			}
		}
	}
	return wxDataViewItem(nullptr);
}

wxDataViewItem ITabularSectionDataObject::FindRowValue(IValueModelReturnLine* retLine) const
{
	return wxDataViewItem(nullptr);
}

bool ITabularSectionDataObject::GetAt(const CValue& varKeyValue, CValue& pvarValue)
{
	long index = varKeyValue.GetUInteger();
	if (index >= GetRowCount() && !appData->DesignerMode()) {
		CBackendException::Error("Array index out of bounds");
		return false;
	}
	pvarValue = new CTabularSectionDataObjectReturnLine(this, GetItem(index));
	return true;
}

class_identifier_t ITabularSectionDataObject::GetClassType() const
{
	IMetaData* metaData = m_metaTable->GetMetaData();
	wxASSERT(metaData);
	IMetaValueTypeCtor* clsFactory =
		metaData->GetTypeCtor(m_metaTable, eCtorMetaType::eCtorMetaType_TabularSection);
	wxASSERT(clsFactory);
	return clsFactory->GetClassType();
}

wxString ITabularSectionDataObject::GetClassName() const
{
	IMetaData* metaData = m_metaTable->GetMetaData();
	wxASSERT(metaData);
	IMetaValueTypeCtor* clsFactory =
		metaData->GetTypeCtor(m_metaTable, eCtorMetaType::eCtorMetaType_TabularSection);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

wxString ITabularSectionDataObject::GetString() const
{
	IMetaData* metaData = m_metaTable->GetMetaData();
	wxASSERT(metaData);
	IMetaValueTypeCtor* clsFactory =
		metaData->GetTypeCtor(m_metaTable, eCtorMetaType::eCtorMetaType_TabularSection);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

bool ITabularSectionDataObject::SetValueByMetaID(const wxDataViewItem& item, const meta_identifier_t& id, const CValue& varMetaVal)
{
	if (m_readOnly)
		return false;

	if (m_metaTable->IsNumberLine(id))
		return false;

	if (!appData->DesignerMode()) {
		wxValueTableRow* node = GetViewData<wxValueTableRow>(item);
		if (node != nullptr) {
			IMetaObjectAttribute* metaAttribute = m_metaTable->FindProp(id);
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

	IMetaObjectAttribute* metaAttribute = m_metaTable->FindProp(id);
	wxASSERT(metaAttribute);
	if (appData->DesignerMode()) {
		pvarMetaVal = metaAttribute->CreateValue();
		return true;
	}
	wxValueTableRow* node = GetViewData<wxValueTableRow>(item);
	if (node != nullptr)
		return node->GetValue(id, pvarMetaVal);
	return false;
}

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
		CTabularSectionDataObjectReturnLine* retLine = nullptr;
		if (paParams[0]->ConvertToValue(retLine)) {
			wxValueTableRow* node = GetViewData<wxValueTableRow>(retLine->GetLineItem());
			if (node != nullptr)
				IValueTable::Remove(node);
		}
		else {
			const number_t& number = paParams[0]->GetNumber();
			wxValueTableRow* node = GetViewData<wxValueTableRow>(GetItem(number.ToInt()));
			if (node != nullptr)
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

//////////////////////////////////////////////////////////////////////
//               CTabularSectionDataObject                          //
//////////////////////////////////////////////////////////////////////

CTabularSectionDataObject::CTabularSectionDataObject() {}

CTabularSectionDataObject::CTabularSectionDataObject(IRecordDataObject* recordObject, CMetaObjectTable* tableObject) :
	ITabularSectionDataObject(recordObject, tableObject)
{
}

//////////////////////////////////////////////////////////////////////
//               CTabularSectionDataObjectRef                       //
//////////////////////////////////////////////////////////////////////

CTabularSectionDataObjectRef::CTabularSectionDataObjectRef() : m_readAfter(false) {}

CTabularSectionDataObjectRef::CTabularSectionDataObjectRef(CReferenceDataObject* reference, CMetaObjectTable* tableObject, bool readAfter) :
	ITabularSectionDataObject(reference, tableObject, true), m_readAfter(readAfter)
{
}

CTabularSectionDataObjectRef::CTabularSectionDataObjectRef(IRecordDataObjectRef* recordObject, CMetaObjectTable* tableObject) :
	ITabularSectionDataObject(recordObject, tableObject), m_readAfter(false)
{
}

CTabularSectionDataObjectRef::CTabularSectionDataObjectRef(CSelectorDataObject* selectorObject, CMetaObjectTable* tableObject) :
	ITabularSectionDataObject((IObjectValueInfo*)selectorObject, tableObject), m_readAfter(false)
{
}

#include "backend/compiler/value/valueTable.h"

bool ITabularSectionDataObject::LoadDataFromTable(IValueTable* srcTable)
{
	if (m_readOnly)
		return false;

	IValueModelColumnCollection* colData = srcTable ?
		srcTable->GetColumnCollection() : nullptr;

	if (colData == nullptr)
		return false;

	wxArrayString columnName;
	for (unsigned int idx = 0; idx < colData->GetColumnCount() - 1; idx++) {
		IValueModelColumnCollection::IValueModelColumnInfo* colInfo = colData->GetColumnInfo(idx);
		wxASSERT(colInfo);
		if (m_dataColumnCollection->GetColumnByName(colInfo->GetColumnName()) != nullptr) {
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
		IBackendValueForm* const foundedForm = IBackendValueForm::FindFormByUniqueKey(
			m_objectValue->GetGuid()
		);
		bool result = ITabularSectionDataObject::SetValueByMetaID(item, id, varMetaVal);
		if (result && foundedForm != nullptr)
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
	for (auto& obj : m_ownerTable->m_metaTable->GetObjectAttributes()) {
		m_methodHelper->AppendProp(
			obj->GetName(),
			true,
			!m_ownerTable->m_metaTable->IsNumberLine(obj->GetMetaID()),
			obj->GetMetaID()
		);
	}
}

bool ITabularSectionDataObject::CTabularSectionDataObjectReturnLine::SetPropVal(const long lPropNum, const CValue& varPropVal)
{
	return SetValueByMetaID(m_methodHelper->GetPropData(lPropNum), varPropVal);
}

bool ITabularSectionDataObject::CTabularSectionDataObjectReturnLine::GetPropVal(const long lPropNum, CValue& pvarPropVal)
{
	return GetValueByMetaID(m_methodHelper->GetPropData(lPropNum), pvarPropVal);
}

class_identifier_t ITabularSectionDataObject::CTabularSectionDataObjectReturnLine::GetClassType() const
{
	const IMetaObject* metaTable = m_ownerTable->GetMetaObject();
	IMetaData* metaData = metaTable->GetMetaData();
	wxASSERT(metaData);
	IMetaValueTypeCtor* clsFactory =
		metaData->GetTypeCtor(metaTable, eCtorMetaType::eCtorMetaType_TabularSection_String);
	wxASSERT(clsFactory);
	return clsFactory->GetClassType();
}

wxString ITabularSectionDataObject::CTabularSectionDataObjectReturnLine::GetClassName() const
{
	const IMetaObject* metaTable = m_ownerTable->GetMetaObject();
	IMetaData* metaData = metaTable->GetMetaData();
	wxASSERT(metaData);
	IMetaValueTypeCtor* clsFactory =
		metaData->GetTypeCtor(metaTable, eCtorMetaType::eCtorMetaType_TabularSection_String);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

wxString ITabularSectionDataObject::CTabularSectionDataObjectReturnLine::GetString() const
{
	const IMetaObject* metaTable = m_ownerTable->GetMetaObject();
	IMetaData* metaData = metaTable->GetMetaData();
	wxASSERT(metaData);
	IMetaValueTypeCtor* clsFactory =
		metaData->GetTypeCtor(metaTable, eCtorMetaType::eCtorMetaType_TabularSection_String);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

//////////////////////////////////////////////////////////////////////
//               CTabularSectionDataObjectColumnCollection          //
//////////////////////////////////////////////////////////////////////

wxIMPLEMENT_DYNAMIC_CLASS(ITabularSectionDataObject::CTabularSectionDataObjectColumnCollection, IValueTable::IValueModelColumnCollection);

ITabularSectionDataObject::CTabularSectionDataObjectColumnCollection::CTabularSectionDataObjectColumnCollection() : IValueModelColumnCollection(), m_methodHelper(nullptr), m_ownerTable(nullptr)
{
}

ITabularSectionDataObject::CTabularSectionDataObjectColumnCollection::CTabularSectionDataObjectColumnCollection(ITabularSectionDataObject* ownerTable) : IValueModelColumnCollection(), m_methodHelper(new CMethodHelper()), m_ownerTable(ownerTable)
{
	CMetaObjectTable* metaTable = m_ownerTable->GetMetaObject();
	wxASSERT(metaTable);
	for (auto& obj : metaTable->GetObjectAttributes()) {
		if (metaTable->IsNumberLine(obj->GetMetaID()))
			continue;
		CValueTabularSectionColumnInfo* columnInfo = new CValueTabularSectionColumnInfo(obj);
		m_columnInfo.insert_or_assign(obj->GetMetaID(), columnInfo);
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
		CBackendException::Error("Index goes beyond array");
		return false;
	}
	auto it = m_columnInfo.begin();
	std::advance(it, index);
	pvarValue = it->second;
	return true;
}

//////////////////////////////////////////////////////////////////////
//               CValueTabularSectionColumnInfo                     //
//////////////////////////////////////////////////////////////////////

wxIMPLEMENT_DYNAMIC_CLASS(ITabularSectionDataObject::CTabularSectionDataObjectColumnCollection::CValueTabularSectionColumnInfo, IValueTable::IValueModelColumnCollection::IValueModelColumnInfo);

ITabularSectionDataObject::CTabularSectionDataObjectColumnCollection::CValueTabularSectionColumnInfo::CValueTabularSectionColumnInfo() :
	IValueModelColumnInfo(), m_metaAttribute(nullptr)
{
}

ITabularSectionDataObject::CTabularSectionDataObjectColumnCollection::CValueTabularSectionColumnInfo::CValueTabularSectionColumnInfo(IMetaObjectAttribute* metaAttribute) :
	IValueModelColumnInfo(), m_metaAttribute(metaAttribute)
{
}

ITabularSectionDataObject::CTabularSectionDataObjectColumnCollection::CValueTabularSectionColumnInfo::~CValueTabularSectionColumnInfo()
{
}

long ITabularSectionDataObject::AppendRow(unsigned int before)
{
	wxValueTableRow* rowData = new wxValueTableRow();
	for (auto& obj : m_metaTable->GetObjectAttributes()) {
		if (!m_metaTable->IsNumberLine(obj->GetMetaID()))
			rowData->AppendTableValue(obj->GetMetaID(), obj->CreateValue());
	}

	if (before > 0)
		return IValueTable::Insert(rowData, before, !CBackendException::IsEvalMode());

	return IValueTable::Append(rowData, !CBackendException::IsEvalMode());
}

long CTabularSectionDataObjectRef::AppendRow(unsigned int before)
{
	if (!CBackendException::IsEvalMode())
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

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

SYSTEM_TYPE_REGISTER(ITabularSectionDataObject::CTabularSectionDataObjectColumnCollection, "tabularSectionColumn", string_to_clsid("VL_TSCL"));
SYSTEM_TYPE_REGISTER(ITabularSectionDataObject::CTabularSectionDataObjectColumnCollection::CValueTabularSectionColumnInfo, "tabularSectionColumnInfo", string_to_clsid("VL_CI"));
SYSTEM_TYPE_REGISTER(ITabularSectionDataObject::CTabularSectionDataObjectReturnLine, "tabularSectionRow", string_to_clsid("VL_TBCR"));
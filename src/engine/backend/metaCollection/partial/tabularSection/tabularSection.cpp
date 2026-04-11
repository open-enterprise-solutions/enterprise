////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : tabular sections
////////////////////////////////////////////////////////////////////////////

#include "tabularSection.h"

#include "backend/metaCollection/partial/commonObject.h"
#include "backend/metaCollection/partial/reference/reference.h"

#include "backend/appData.h"

wxIMPLEMENT_ABSTRACT_CLASS(ibValueTabularSectionDataObjectBase, ibValueModelTableBase);
wxIMPLEMENT_DYNAMIC_CLASS(ibValueTabularSectionDataObjectBase::ibValueTabularSectionDataObjectReturnLine, ibValueModelTableBase::ibValueModelReturnLine);
wxIMPLEMENT_DYNAMIC_CLASS(ibValueTabularSectionDataObject, ibValueTabularSectionDataObjectBase);
wxIMPLEMENT_DYNAMIC_CLASS(ibValueTabularSectionDataObjectRef, ibValueTabularSectionDataObjectBase);

//////////////////////////////////////////////////////////////////////
//               ibValueTabularSectionDataObjectBase                          //
//////////////////////////////////////////////////////////////////////

#include "backend/metaData.h"
#include "backend/objCtor.h"

ibDataViewItem ibValueTabularSectionDataObjectBase::FindRowValue(const ibValue& varValue, const wxString& colName) const
{
	ibValueModelColumnCollection::ibValueModelColumnInfo* colInfo = m_recordColumnCollection->GetColumnByName(colName);
	if (colInfo != nullptr) {
		for (long row = 0; row < GetRowCount(); row++) {
			const ibDataViewItem& item = GetItem(row);
			ibValueTableRow* node = GetViewData<ibValueTableRow>(item);
			if (node != nullptr &&
				varValue == node->GetTableValue(colInfo->GetColumnID())) {
				return item;
			}
		}
	}
	return ibDataViewItem(nullptr);
}

ibDataViewItem ibValueTabularSectionDataObjectBase::FindRowValue(ibValueModelReturnLine* retLine) const
{
	return ibDataViewItem(nullptr);
}

bool ibValueTabularSectionDataObjectBase::GetAt(const ibValue& varKeyValue, ibValue& pvarValue)
{
	long index = varKeyValue.GetUInteger();
	if (index >= GetRowCount() && !appData->DesignerMode()) {
		ibBackendCoreException::Error(_("Array index out of bounds"));
		return false;
	}

	pvarValue = ibValue::CreateAndPrepareValueRef<ibValueTabularSectionDataObjectReturnLine>(this, GetItem(index));
	return true;
}

ibClassID ibValueTabularSectionDataObjectBase::GetClassType() const
{
	const ibMetaData* metaData = m_metaTable->GetMetaData();
	wxASSERT(metaData);
	if (m_metaTable->IsAllowed()) {
		const ibCtorMetaValueType* clsFactory =
			metaData->GetTypeCtor(m_metaTable, ibCtorObjectMetaType::ibCtorObjectMetaType_TabularSection);
		wxASSERT(clsFactory);
		return clsFactory->GetClassType();
	}
	return 0;
}

wxString ibValueTabularSectionDataObjectBase::GetClassName() const
{
	const ibMetaData* metaData = m_metaTable->GetMetaData();
	wxASSERT(metaData);
	if (m_metaTable->IsAllowed()) {
		const ibCtorMetaValueType* clsFactory =
			metaData->GetTypeCtor(m_metaTable, ibCtorObjectMetaType::ibCtorObjectMetaType_TabularSection);
		wxASSERT(clsFactory);
		return clsFactory->GetClassName();
	}
	return _("<deleted metaobject>");
}

wxString ibValueTabularSectionDataObjectBase::GetString() const
{
	if (m_metaTable->IsAllowed()) {
		const ibMetaData* metaData = m_metaTable->GetMetaData();
		wxASSERT(metaData);
		const ibCtorMetaValueType* clsFactory =
			metaData->GetTypeCtor(m_metaTable, ibCtorObjectMetaType::ibCtorObjectMetaType_TabularSection);
		wxASSERT(clsFactory);
		return clsFactory->GetClassName();
	}
	return _("<deleted metaobject>");
}

bool ibValueTabularSectionDataObjectBase::SetValueByMetaID(const ibDataViewItem& item, const ibMetaID& id, const ibValue& varMetaVal)
{
	if (m_readOnly || m_metaTable->IsNumberLine(id))
		return false;

	if (!appData->DesignerMode()) {
		ibValueTableRow* node = GetViewData<ibValueTableRow>(item);
		if (node != nullptr) {
			const ibValueMetaObjectAttributeBase* attribute = m_metaTable->FindAnyAttributeObjectByFilter(id);
			wxASSERT(attribute);
			if (attribute == nullptr) return false;
			return node->SetValue(
				id, attribute->AdjustValue(varMetaVal), true
			);
		}
	}

	return false;
}

bool ibValueTabularSectionDataObjectBase::GetValueByMetaID(const ibDataViewItem& item, const ibMetaID& id, ibValue& pvarMetaVal) const
{
	if (m_metaTable->IsNumberLine(id)) {
		pvarMetaVal = ibValue(ibNumber(static_cast<int>(GetRow(item) + 1)));
		return true;
	}

	if (appData->DesignerMode()) {
		const ibValueMetaObjectAttributeBase* attribute = m_metaTable->FindAnyAttributeObjectByFilter(id);
		wxASSERT(attribute);
		pvarMetaVal = attribute->CreateValue();
		return true;
	}

	ibValueTableRow* node = GetViewData<ibValueTableRow>(item);
	if (node != nullptr)
		return node->GetValue(id, pvarMetaVal);

	return false;
}

bool ibValueTabularSectionDataObjectBase::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	const long lMethodAlias = m_methodHelper->GetMethodAlias(lMethodNum);
	if (lMethodAlias != eTabularSection)
		return false;

	const long lMethodData = m_methodHelper->GetMethodData(lMethodNum);
	switch (lMethodData)
	{
	case enAddValue:
		pvarRetValue = ibValue::CreateAndPrepareValueRef<ibValueTabularSectionDataObjectReturnLine>(this, GetItem(AppendRow()));
		return true;
	case enFind: {
		const ibDataViewItem& item = FindRowValue(*paParams[0], paParams[1]->GetString());
		if (item.IsOk())
			pvarRetValue = GetRowAt(item);
		return true;
	}
	case enCount:
		pvarRetValue = (unsigned int)GetRowCount();
		return true;
	case enDelete: {
		ibValueTabularSectionDataObjectReturnLine* retLine = nullptr;
		if (paParams[0]->ConvertToValue(retLine)) {
			ibValueTableRow* node = GetViewData<ibValueTableRow>(retLine->GetLineItem());
			if (node != nullptr)
				ibValueModelTableBase::Remove(node);
		}
		else {
			const ibNumber& number = paParams[0]->GetNumber();
			ibValueTableRow* node = GetViewData<ibValueTableRow>(GetItem(number.ToInt()));
			if (node != nullptr)
				ibValueModelTableBase::Remove(node);
		}
		return true;
	}
	case enClear:
		Clear();
		return true;
	case enLoad:
		ibValueTabularSectionDataObjectBase::LoadDataFromTable(paParams[0]->ConvertToType<ibValueModelTableBase>());
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

void ibValueTabularSectionDataObjectBase::RefreshTabularSection() {

	if (!ibBackendException::IsEvalMode()) {

		ibValue filterValue;
		for (long row = 0; row < ibValueModelTableBase::GetRowCount(); row++) {

			const ibDataViewItem& item = ibValueModelTableBase::GetItem(row); bool success_compare = true;
			for (auto filter : m_filterRow.m_filters) {

				if (filter.m_filterUse && GetValueByMetaID(item, filter.m_filterModel, filterValue)) {

					if (filter.m_filterComparison == ibComparisonType_Equal)
						success_compare = success_compare && filter.m_filterValue == filterValue;
					else if (filter.m_filterComparison == ibComparisonType_NotEqual)
						success_compare = success_compare && filter.m_filterValue != filterValue;

					if (!success_compare) break;
				}
			}

			ibValueModelTableBase::Show(item, success_compare);
		}
	}
}

//////////////////////////////////////////////////////////////////////
//               ibValueTabularSectionDataObject                          //
//////////////////////////////////////////////////////////////////////

ibValueTabularSectionDataObject::ibValueTabularSectionDataObject() {}

ibValueTabularSectionDataObject::ibValueTabularSectionDataObject(ibValueRecordDataObject* recordObject, ibValueMetaObjectTableData* tableObject) :
	ibValueTabularSectionDataObjectBase(recordObject, tableObject)
{
}

//////////////////////////////////////////////////////////////////////
//               ibValueTabularSectionDataObjectRef                       //
//////////////////////////////////////////////////////////////////////

ibValueTabularSectionDataObjectRef::ibValueTabularSectionDataObjectRef() : m_readAfter(false) {}

ibValueTabularSectionDataObjectRef::ibValueTabularSectionDataObjectRef(ibValueReferenceDataObject* reference, ibValueMetaObjectTableData* tableObject, bool readAfter) :
	ibValueTabularSectionDataObjectBase(reference, tableObject, true), m_readAfter(readAfter)
{
}

ibValueTabularSectionDataObjectRef::ibValueTabularSectionDataObjectRef(ibValueRecordDataObjectRef* recordObject, ibValueMetaObjectTableData* tableObject) :
	ibValueTabularSectionDataObjectBase(recordObject, tableObject), m_readAfter(false)
{
}

ibValueTabularSectionDataObjectRef::ibValueTabularSectionDataObjectRef(ibValueSelectorRecordDataObject* selectorObject, ibValueMetaObjectTableData* tableObject) :
	ibValueTabularSectionDataObjectBase((ibValueDataObject*)selectorObject, tableObject), m_readAfter(false)
{
}

#include "backend/system/value/valueTable.h"

bool ibValueTabularSectionDataObjectBase::LoadDataFromTable(ibValueModelTableBase* srcTable)
{
	if (m_readOnly)
		return false;

	ibValueModelColumnCollection* colData = srcTable ?
		srcTable->GetColumnCollection() : nullptr;

	if (colData == nullptr)
		return false;

	wxArrayString columnName;
	for (unsigned int idx = 0; idx < colData->GetColumnCount(); idx++) {
		ibValueModelColumnCollection::ibValueModelColumnInfo* colInfo = colData->GetColumnInfo(idx);
		wxASSERT(colInfo);
		if (m_recordColumnCollection->GetColumnByName(colInfo->GetColumnName()) != nullptr) {
			columnName.push_back(colInfo->GetColumnName());
		}
	}

	unsigned int rowCount = srcTable->GetRowCount();
	for (unsigned int row = 0; row < rowCount; row++) {
		const ibDataViewItem& srcItem = srcTable->GetItem(row);
		const ibDataViewItem& dstItem = GetItem(AppendRow());
		for (auto colName : columnName) {
			ibValue cRetValue;
			if (srcTable->GetValueByMetaID(srcItem, srcTable->GetColumnIDByName(colName), cRetValue)) {
				const ibMetaID& id = GetColumnIDByName(colName);
				if (id != wxNOT_FOUND) SetValueByMetaID(dstItem, id, cRetValue);
			}
		}
	}

	return true;
}

ibValueModelTableBase* ibValueTabularSectionDataObjectBase::SaveDataToTable() const
{
	ibValueModelTable* valueTable = ibValue::CreateAndPrepareValueRef<ibValueModelTable>();
	ibValueModelColumnCollection* colData = valueTable->GetColumnCollection();
	for (unsigned int idx = 0; idx < m_recordColumnCollection->GetColumnCount() - 1; idx++) {
		ibValueModelColumnCollection::ibValueModelColumnInfo* colInfo = m_recordColumnCollection->GetColumnInfo(idx);
		wxASSERT(colInfo);
		ibValueModelColumnCollection::ibValueModelColumnInfo* newColInfo = colData->AddColumn(
			colInfo->GetColumnName(), colInfo->GetColumnType(), colInfo->GetColumnCaption(), colInfo->GetColumnWidth()
		);
		newColInfo->SetColumnID(colInfo->GetColumnID());
	}
	valueTable->PrepareNames();
	for (long row = 0; row < GetRowCount(); row++) {
		const ibDataViewItem& srcItem = GetItem(row);
		const ibDataViewItem& dstItem = valueTable->GetItem(valueTable->AppendRow());
		for (unsigned int col = 0; col < colData->GetColumnCount(); col++) {
			ibValueModelColumnCollection::ibValueModelColumnInfo* colInfo = colData->GetColumnInfo(col);
			wxASSERT(colInfo);
			if (m_metaTable->IsNumberLine(colInfo->GetColumnID()))
				continue;
			ibValue cRetValue;
			if (GetValueByMetaID(srcItem, colInfo->GetColumnID(), cRetValue)) {
				const ibMetaID& id = GetColumnIDByName(colInfo->GetColumnName());
				if (id != wxNOT_FOUND) valueTable->SetValueByMetaID(dstItem, id, cRetValue);
			}
		}
	}

	return valueTable;
}

bool ibValueTabularSectionDataObjectRef::SetValueByMetaID(const ibDataViewItem& item, const ibMetaID& id, const ibValue& varMetaVal)
{
	if (varMetaVal != ibValueTabularSectionDataObjectBase::GetValueByMetaID(item, id)) {
		ibBackendValueForm* const foundedForm = ibBackendValueForm::FindFormByUniqueKey(
			m_objectValue->GetGuid()
		);
		bool result = ibValueTabularSectionDataObjectBase::SetValueByMetaID(item, id, varMetaVal);
		if (result && foundedForm != nullptr)
			foundedForm->Modify(true);
		return result;
	}

	return false;
}

bool ibValueTabularSectionDataObjectRef::GetValueByMetaID(const ibDataViewItem& item, const ibMetaID& id, ibValue& pvarMetaVal) const
{
	return ibValueTabularSectionDataObjectBase::GetValueByMetaID(item, id, pvarMetaVal);
}

//////////////////////////////////////////////////////////////////////
//               ibValueTabularSectionDataObjectReturnLine                //
//////////////////////////////////////////////////////////////////////

ibValueTabularSectionDataObjectBase::ibValueTabularSectionDataObjectReturnLine::ibValueTabularSectionDataObjectReturnLine(ibValueTabularSectionDataObjectBase* ownerTable, const ibDataViewItem& line)
	: ibValueModelReturnLine(line), m_ownerTable(ownerTable), m_methodHelper(new ibValueMethodHelper()) {
}

ibValueTabularSectionDataObjectBase::ibValueTabularSectionDataObjectReturnLine::~ibValueTabularSectionDataObjectReturnLine() {
	wxDELETE(m_methodHelper);
}

void ibValueTabularSectionDataObjectBase::ibValueTabularSectionDataObjectReturnLine::PrepareNames() const
{
	m_methodHelper->ClearHelper();

	//set object name 
	wxString objectName;

	for (const auto object : m_ownerTable->m_metaTable->GetGenericAttributeArrayObject()) {
		if (object->IsDeleted())
			continue;
		if (!object->GetObjectNameAsString(objectName))
			continue;
		m_methodHelper->AppendProp(
			objectName,
			true,
			!m_ownerTable->m_metaTable->IsNumberLine(object->GetMetaID()),
			object->GetMetaID()
		);
	}
}

bool ibValueTabularSectionDataObjectBase::ibValueTabularSectionDataObjectReturnLine::SetPropVal(const long lPropNum, const ibValue& varPropVal)
{
	return SetValueByMetaID(m_methodHelper->GetPropData(lPropNum), varPropVal);
}

bool ibValueTabularSectionDataObjectBase::ibValueTabularSectionDataObjectReturnLine::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	return GetValueByMetaID(m_methodHelper->GetPropData(lPropNum), pvarPropVal);
}

ibClassID ibValueTabularSectionDataObjectBase::ibValueTabularSectionDataObjectReturnLine::GetClassType() const
{
	const ibValueMetaObject* metaTable = m_ownerTable->GetMetaObject();
	const ibMetaData* metaData = metaTable->GetMetaData();
	wxASSERT(metaData);
	const ibCtorMetaValueType* clsFactory =
		metaData->GetTypeCtor(metaTable, ibCtorObjectMetaType::ibCtorObjectMetaType_TabularSection_String);
	wxASSERT(clsFactory);
	return clsFactory->GetClassType();
}

wxString ibValueTabularSectionDataObjectBase::ibValueTabularSectionDataObjectReturnLine::GetClassName() const
{
	const ibValueMetaObject* metaTable = m_ownerTable->GetMetaObject();
	const ibMetaData* metaData = metaTable->GetMetaData();
	wxASSERT(metaData);
	const ibCtorMetaValueType* clsFactory =
		metaData->GetTypeCtor(metaTable, ibCtorObjectMetaType::ibCtorObjectMetaType_TabularSection_String);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

wxString ibValueTabularSectionDataObjectBase::ibValueTabularSectionDataObjectReturnLine::GetString() const
{
	const ibValueMetaObject* metaTable = m_ownerTable->GetMetaObject();
	const ibMetaData* metaData = metaTable->GetMetaData();
	wxASSERT(metaData);
	const ibCtorMetaValueType* clsFactory =
		metaData->GetTypeCtor(metaTable, ibCtorObjectMetaType::ibCtorObjectMetaType_TabularSection_String);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

//////////////////////////////////////////////////////////////////////
//               ibValueTabularSectionDataObjectColumnCollection          //
//////////////////////////////////////////////////////////////////////

wxIMPLEMENT_DYNAMIC_CLASS(ibValueTabularSectionDataObjectBase::ibValueTabularSectionDataObjectColumnCollection, ibValueModelTableBase::ibValueModelColumnCollection);

ibValueTabularSectionDataObjectBase::ibValueTabularSectionDataObjectColumnCollection::ibValueTabularSectionDataObjectColumnCollection() :
	ibValueModelColumnCollection(),
	m_ownerTable(nullptr),
	m_methodHelper(nullptr)
{
}

ibValueTabularSectionDataObjectBase::ibValueTabularSectionDataObjectColumnCollection::ibValueTabularSectionDataObjectColumnCollection(ibValueTabularSectionDataObjectBase* ownerTable) :
	ibValueModelColumnCollection(),
	m_ownerTable(ownerTable),
	m_methodHelper(new ibValueMethodHelper())
{
	ibValueMetaObjectTableData* metaTable = m_ownerTable->GetMetaObject();
	wxASSERT(metaTable);
	for (const auto object : metaTable->GetGenericAttributeArrayObject()) {
		if (metaTable->IsNumberLine(object->GetMetaID()))
			continue;
		m_listColumnInfo.insert_or_assign(object->GetMetaID(),
			ibValue::CreateAndPrepareValueRef<ibValueTabularSectionColumnInfo>(object)
		);
	}
}

ibValueTabularSectionDataObjectBase::ibValueTabularSectionDataObjectColumnCollection::~ibValueTabularSectionDataObjectColumnCollection()
{
	wxDELETE(m_methodHelper);
}

bool ibValueTabularSectionDataObjectBase::ibValueTabularSectionDataObjectColumnCollection::SetAt(const ibValue& varKeyValue, const ibValue& varValue)//������ ������� ������ ���������� � 0
{
	return false;
}

bool ibValueTabularSectionDataObjectBase::ibValueTabularSectionDataObjectColumnCollection::GetAt(const ibValue& varKeyValue, ibValue& pvarValue) //������ ������� ������ ���������� � 0
{
	unsigned int index = varKeyValue.GetUInteger();
	if ((index < 0 || index >= m_listColumnInfo.size() && !appData->DesignerMode())) {
		ibBackendCoreException::Error(_("Index goes beyond array"));
		return false;
	}
	auto it = m_listColumnInfo.begin();
	std::advance(it, index);
	pvarValue = it->second;
	return true;
}

//////////////////////////////////////////////////////////////////////
//               ibValueTabularSectionColumnInfo                     //
//////////////////////////////////////////////////////////////////////

wxIMPLEMENT_DYNAMIC_CLASS(ibValueTabularSectionDataObjectBase::ibValueTabularSectionDataObjectColumnCollection::ibValueTabularSectionColumnInfo, ibValueModelTableBase::ibValueModelColumnCollection::ibValueModelColumnInfo);

ibValueTabularSectionDataObjectBase::ibValueTabularSectionDataObjectColumnCollection::ibValueTabularSectionColumnInfo::ibValueTabularSectionColumnInfo() :
	ibValueModelColumnInfo(), m_metaAttribute(nullptr)
{
}

ibValueTabularSectionDataObjectBase::ibValueTabularSectionDataObjectColumnCollection::ibValueTabularSectionColumnInfo::ibValueTabularSectionColumnInfo(ibValueMetaObjectAttributeBase* attribute) :
	ibValueModelColumnInfo(), m_metaAttribute(attribute)
{
}

ibValueTabularSectionDataObjectBase::ibValueTabularSectionDataObjectColumnCollection::ibValueTabularSectionColumnInfo::~ibValueTabularSectionColumnInfo()
{
}

long ibValueTabularSectionDataObjectBase::AppendRow(unsigned int before)
{
	ibValueTableRow* rowData = new ibValueTableRow();
	for (const auto object : m_metaTable->GetGenericAttributeArrayObject()) {
		if (!m_metaTable->IsNumberLine(object->GetMetaID()))
			rowData->AppendTableValue(object->GetMetaID(), object->CreateValue());
	}

	if (before > 0)
		return ibValueModelTableBase::Insert(rowData, before, !ibBackendException::IsEvalMode());

	return ibValueModelTableBase::Append(rowData, !ibBackendException::IsEvalMode());
}

long ibValueTabularSectionDataObjectRef::AppendRow(unsigned int before)
{
	if (!ibBackendException::IsEvalMode())
		m_objectValue->Modify(true);
	return ibValueTabularSectionDataObjectBase::AppendRow(before);
}

//****************************************************************************
//*                              Support methods                             *
//****************************************************************************

void ibValueTabularSectionDataObjectBase::PrepareNames() const
{
	m_methodHelper->ClearHelper();

	if (m_readOnly) {
		m_methodHelper->AppendFunc(wxT("Count"), wxT("Count()"), enCount, eTabularSection);
		m_methodHelper->AppendFunc(wxT("Find"), 2, wxT("Find(value : any, columnName : string)"), enFind, eTabularSection);
		m_methodHelper->AppendFunc(wxT("Unload"), wxT("Unload()"), enUnload, eTabularSection);
		m_methodHelper->AppendFunc(wxT("GetMetadata"), wxT("GetMetadata()"), enGetMetadata, eTabularSection);
	}
	else {
		m_methodHelper->AppendFunc(wxT("Add"), wxT("Add()"), enAddValue, eTabularSection);
		m_methodHelper->AppendFunc(wxT("Count"), wxT("Count()"), enCount, eTabularSection);
		m_methodHelper->AppendFunc(wxT("Find"), 2, wxT("Find(value : any, columnName : string)"), enFind, eTabularSection);
		m_methodHelper->AppendFunc(wxT("Delete"), 1, wxT("delete(row : tabularSectionRow)"), enDelete, eTabularSection);
		m_methodHelper->AppendFunc(wxT("Clear"), wxT("Clear()"), enClear, eTabularSection);
		m_methodHelper->AppendFunc(wxT("Load"), 1, wxT("Load(table : any table)"), enLoad, eTabularSection);
		m_methodHelper->AppendFunc(wxT("Unload"), wxT("Unload()"), enUnload, eTabularSection);
		m_methodHelper->AppendFunc(wxT("GetMetadata"), wxT("GetMetadata()"), enGetMetadata, eTabularSection);
	}
}

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

SYSTEM_TYPE_REGISTER(ibValueTabularSectionDataObjectBase::ibValueTabularSectionDataObjectColumnCollection, "TabularSectionColumn", string_to_clsid("VL_TSCL"));
SYSTEM_TYPE_REGISTER(ibValueTabularSectionDataObjectBase::ibValueTabularSectionDataObjectColumnCollection::ibValueTabularSectionColumnInfo, "TabularSectionColumnInfo", string_to_clsid("VL_CI"));
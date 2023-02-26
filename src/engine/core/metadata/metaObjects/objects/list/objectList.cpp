////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : list data 
////////////////////////////////////////////////////////////////////////////

#include "objectList.h"
#include "frontend/visualView/controls/form.h"
#include "core/common/srcExplorer.h"
#include "appData.h"

wxIMPLEMENT_ABSTRACT_CLASS(IListDataObject, IValueTable);

wxIMPLEMENT_DYNAMIC_CLASS(CListDataObjectEnumRef, IListDataObject);
wxIMPLEMENT_DYNAMIC_CLASS(CListDataObjectRef, IListDataObject);
wxIMPLEMENT_DYNAMIC_CLASS(CListRegisterObject, IListDataObject);

wxIMPLEMENT_ABSTRACT_CLASS(ITreeDataObject, IValueTree);
wxIMPLEMENT_DYNAMIC_CLASS(CTreeDataObjectFolderRef, ITreeDataObject);

IListDataObject::IListDataObject(IMetaObjectWrapperData* metaTable, const form_identifier_t& formType) :
	ISourceDataObject(), m_objGuid(Guid::newGuid()), m_methodHelper(new CMethodHelper())
{
	m_dataColumnCollection = new CDataObjectListColumnCollection(this, metaTable);
	m_dataColumnCollection->IncrRef();

	for (auto attribute : metaTable->GetGenericAttributes()) {
		m_filterRow.AppendFilter(
			attribute->GetMetaID(),
			attribute->GetName(),
			attribute->GetSynonym(),
			eComparisonType_Equal,
			attribute->GetTypeDescription(),
			attribute->CreateValue(),
			false
		);
	}
}

IListDataObject::~IListDataObject()
{
	if (m_dataColumnCollection != NULL) {
		m_dataColumnCollection->DecrRef();
	}

	wxDELETE(m_methodHelper);
}

///////////////////////////////////////////////////////////////////////////////

ITreeDataObject::ITreeDataObject(IMetaObjectWrapperData* metaTable, const form_identifier_t& formType) :
	ISourceDataObject(), m_objGuid(Guid::newGuid()), m_methodHelper(new CMethodHelper())
{
	m_dataColumnCollection = new CDataObjectTreeColumnCollection(this, metaTable);
	m_dataColumnCollection->IncrRef();

	for (auto attribute : metaTable->GetGenericAttributes()) {
		m_filterRow.AppendFilter(
			attribute->GetMetaID(),
			attribute->GetName(),
			attribute->GetSynonym(),
			attribute->GetTypeDescription(),
			attribute->CreateValue()
		);
	}
}

ITreeDataObject::~ITreeDataObject()
{
	if (m_dataColumnCollection != NULL) {
		m_dataColumnCollection->DecrRef();
	}

	wxDELETE(m_methodHelper);
}

//////////////////////////////////////////////////////////////////////
//					  CDataObjectListColumnCollection               //
//////////////////////////////////////////////////////////////////////

wxIMPLEMENT_DYNAMIC_CLASS(IListDataObject::CDataObjectListColumnCollection, IValueTable::IValueModelColumnCollection);

IListDataObject::CDataObjectListColumnCollection::CDataObjectListColumnCollection() :
	IValueModelColumnCollection(), m_methodHelper(NULL), m_ownerTable(NULL)
{
}

IListDataObject::CDataObjectListColumnCollection::CDataObjectListColumnCollection(IListDataObject* ownerTable, IMetaObjectWrapperData* metaTable) :
	IValueModelColumnCollection(), m_methodHelper(new CMethodHelper()), m_ownerTable(ownerTable)
{
	wxASSERT(metaTable);

	for (auto attributes : metaTable->GetGenericAttributes()) {
		CDataObjectListColumnInfo* columnInfo = new CDataObjectListColumnInfo(attributes);
		m_columnInfo.insert_or_assign(attributes->GetMetaID(), columnInfo);
		columnInfo->IncrRef();
	}
}

IListDataObject::CDataObjectListColumnCollection::~CDataObjectListColumnCollection()
{
	for (auto& colInfo : m_columnInfo) {
		CDataObjectListColumnInfo* columnInfo = colInfo.second;
		wxASSERT(columnInfo);
		columnInfo->DecrRef();
	}

	wxDELETE(m_methodHelper);
}

bool IListDataObject::CDataObjectListColumnCollection::SetAt(const CValue& varKeyValue, const CValue& varValue)//индекс массива должен начинаться с 0
{
	return false;
}

bool IListDataObject::CDataObjectListColumnCollection::GetAt(const CValue& varKeyValue, CValue& pvarValue) //индекс массива должен начинаться с 0
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

wxIMPLEMENT_DYNAMIC_CLASS(ITreeDataObject::CDataObjectTreeColumnCollection, IValueTree::IValueModelColumnCollection);

ITreeDataObject::CDataObjectTreeColumnCollection::CDataObjectTreeColumnCollection() :
	IValueModelColumnCollection(), m_methodHelper(NULL), m_ownerTable(NULL)
{
}

ITreeDataObject::CDataObjectTreeColumnCollection::CDataObjectTreeColumnCollection(ITreeDataObject* ownerTable, IMetaObjectWrapperData* metaTable) :
	IValueModelColumnCollection(), m_methodHelper(new CMethodHelper()), m_ownerTable(ownerTable)
{
	wxASSERT(metaTable);

	for (auto attributes : metaTable->GetGenericAttributes()) {
		CDataObjectTreeColumnInfo* columnInfo = new CDataObjectTreeColumnInfo(attributes);
		m_columnInfo.insert_or_assign(attributes->GetMetaID(), columnInfo);
		columnInfo->IncrRef();
	}
}

ITreeDataObject::CDataObjectTreeColumnCollection::~CDataObjectTreeColumnCollection()
{
	for (auto& colInfo : m_columnInfo) {
		CDataObjectTreeColumnInfo* columnInfo = colInfo.second;
		wxASSERT(columnInfo);
		columnInfo->DecrRef();
	}

	wxDELETE(m_methodHelper);
}

bool ITreeDataObject::CDataObjectTreeColumnCollection::SetAt(const CValue& varKeyValue, const CValue& varValue)//индекс массива должен начинаться с 0
{
	return false;
}

bool ITreeDataObject::CDataObjectTreeColumnCollection::GetAt(const CValue& varKeyValue, CValue& pvarValue) //индекс массива должен начинаться с 0
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
//							 CDataObjectListColumnInfo              //
//////////////////////////////////////////////////////////////////////

wxIMPLEMENT_DYNAMIC_CLASS(IListDataObject::CDataObjectListColumnCollection::CDataObjectListColumnInfo, IValueTable::IValueModelColumnCollection::IValueModelColumnInfo);

IListDataObject::CDataObjectListColumnCollection::CDataObjectListColumnInfo::CDataObjectListColumnInfo() :
	IValueModelColumnInfo(), m_metaAttribute(NULL)
{
}

IListDataObject::CDataObjectListColumnCollection::CDataObjectListColumnInfo::CDataObjectListColumnInfo(IMetaAttributeObject* metaAttribute) :
	IValueModelColumnInfo(), m_metaAttribute(metaAttribute)
{
}

IListDataObject::CDataObjectListColumnCollection::CDataObjectListColumnInfo::~CDataObjectListColumnInfo()
{
}

wxIMPLEMENT_DYNAMIC_CLASS(ITreeDataObject::CDataObjectTreeColumnCollection::CDataObjectTreeColumnInfo, IValueTree::IValueModelColumnCollection::IValueModelColumnInfo);

ITreeDataObject::CDataObjectTreeColumnCollection::CDataObjectTreeColumnInfo::CDataObjectTreeColumnInfo() :
	IValueModelColumnInfo(), m_metaAttribute(NULL)
{
}

ITreeDataObject::CDataObjectTreeColumnCollection::CDataObjectTreeColumnInfo::CDataObjectTreeColumnInfo(IMetaAttributeObject* metaAttribute) :
	IValueModelColumnInfo(), m_metaAttribute(metaAttribute)
{
}

ITreeDataObject::CDataObjectTreeColumnCollection::CDataObjectTreeColumnInfo::~CDataObjectTreeColumnInfo()
{
}

//////////////////////////////////////////////////////////////////////
//					  CDataObjectListReturnLine                     //
//////////////////////////////////////////////////////////////////////

wxIMPLEMENT_DYNAMIC_CLASS(IListDataObject::CDataObjectListReturnLine, IValueTable::IValueModelReturnLine);

IListDataObject::CDataObjectListReturnLine::CDataObjectListReturnLine(IListDataObject* ownerTable, const wxDataViewItem& line) :
	IValueModelReturnLine(line), m_methodHelper(new CMethodHelper()), m_ownerTable(ownerTable) {
}

IListDataObject::CDataObjectListReturnLine::~CDataObjectListReturnLine() {
	if (m_methodHelper)
		delete m_methodHelper;
}

void IListDataObject::CDataObjectListReturnLine::PrepareNames() const
{
	m_methodHelper->ClearHelper();

	IMetaObjectWrapperData* metaObject = m_ownerTable->GetMetaObject();
	for (auto attribute : metaObject->GetGenericAttributes()) {
		m_methodHelper->AppendProp(
			attribute->GetName(),
			attribute->GetMetaID()
		);
	}
}

bool IListDataObject::CDataObjectListReturnLine::SetPropVal(const long lPropNum, const CValue& varPropVal)
{
	return false;
}

bool IListDataObject::CDataObjectListReturnLine::GetPropVal(const long lPropNum, CValue& pvarPropVal)
{
	if (appData->DesignerMode())
		return false;
	const meta_identifier_t& id = m_methodHelper->GetPropData(lPropNum);
	wxValueTableRow* node = m_ownerTable->GetViewData<wxValueTableRow>(m_lineItem);
	if (node == NULL)
		return false;
	return node->GetValue(id, pvarPropVal);
}

wxIMPLEMENT_DYNAMIC_CLASS(ITreeDataObject::CDataObjectTreeReturnLine, IValueTree::IValueModelReturnLine);

ITreeDataObject::CDataObjectTreeReturnLine::CDataObjectTreeReturnLine(ITreeDataObject* ownerTable, const wxDataViewItem& line) :
	IValueModelReturnLine(line),
	m_methodHelper(new CMethodHelper()), m_ownerTable(ownerTable) {
}

ITreeDataObject::CDataObjectTreeReturnLine::~CDataObjectTreeReturnLine() {
	if (m_methodHelper)
		delete m_methodHelper;
}

void ITreeDataObject::CDataObjectTreeReturnLine::PrepareNames() const
{
	m_methodHelper->ClearHelper();

	IMetaObjectWrapperData* metaObject = m_ownerTable->GetMetaObject();
	for (auto attribute : metaObject->GetGenericAttributes()) {
		m_methodHelper->AppendProp(
			attribute->GetName(),
			attribute->GetMetaID()
		);
	}
}

bool ITreeDataObject::CDataObjectTreeReturnLine::SetPropVal(const long lPropNum, const CValue& varPropVal)
{
	return false;
}

bool ITreeDataObject::CDataObjectTreeReturnLine::GetPropVal(const long lPropNum, CValue& pvarPropVal)
{
	if (appData->DesignerMode())
		return false;
	const meta_identifier_t& id = m_methodHelper->GetPropData(lPropNum);
	wxValueTreeNode* node = m_ownerTable->GetViewData<wxValueTreeNode>(m_lineItem);
	if (node != NULL) {
		return node->GetValue(id, pvarPropVal);
	}
	return false;
}

/////////////////////////////////////////////////////////////////////////////////////////////

wxDataViewItem CListDataObjectEnumRef::FindRowValue(const CValue& varValue, const wxString& colName) const
{
	CReferenceDataObject* pRefData = NULL;
	if (varValue.ConvertToValue(pRefData)) {
		for (long row = 0; row < GetRowCount(); row++) {
			wxDataViewItem item = GetItem(row);
			if (item.IsOk()) {
				wxValueTableEnumRow* node = GetViewData<wxValueTableEnumRow>(item);
				if (node != NULL && pRefData->GetGuid() == node->GetGuid())
					return item;
			}
		}
	}
	return wxDataViewItem(NULL);
}

wxDataViewItem CListDataObjectEnumRef::FindRowValue(IValueModelReturnLine* retLine) const
{
	CValue retValue;
	if (!retLine->GetValueByMetaID(*m_metaObject->GetDataReference(), retValue))
		return wxDataViewItem(NULL);
	return FindRowValue(retValue);
}

CListDataObjectEnumRef::CListDataObjectEnumRef(IMetaObjectRecordDataEnumRef* metaObject, const form_identifier_t& formType, bool choiceMode) : IListDataObject(metaObject, formType),
m_metaObject(metaObject), m_choiceMode(choiceMode)
{
	IListDataObject::AppendSort(m_metaObject->GetDataOrder(), true, true, true);
	IListDataObject::AppendSort(m_metaObject->GetDataReference(), true, true, true);
}

CSourceExplorer CListDataObjectEnumRef::GetSourceExplorer() const
{
	CSourceExplorer srcHelper(
		m_metaObject, GetTypeClass(),
		true, true
	);
	srcHelper.AppendSource(m_metaObject->GetDataReference(), true, true);
	return srcHelper;
}

bool CListDataObjectEnumRef::GetModel(IValueModel*& tableValue, const meta_identifier_t& id)
{
	if (id == m_metaObject->GetMetaID()) {
		tableValue = this;
		return true;
	}
	return false;
}

//events 
void CListDataObjectEnumRef::ChooseValue(CValueForm* srcForm)
{
	wxValueTableEnumRow* node = GetViewData<wxValueTableEnumRow>(GetSelection());
	if (node == NULL)
		return;
	wxASSERT(srcForm);
	CReferenceDataObject* refData = m_metaObject->FindObjectValue(node->GetGuid());
	if (refData != NULL)
		srcForm->NotifyChoice(refData->GetValue());
}

#include "core/metadata/singleClass.h"

CLASS_ID CListDataObjectEnumRef::GetTypeClass() const
{
	IMetaTypeObjectValueSingle* clsFactory =
		m_metaObject->GetTypeObject(eMetaObjectType::enList);
	wxASSERT(clsFactory);
	return clsFactory->GetTypeClass();
}

wxString CListDataObjectEnumRef::GetTypeString() const
{
	IMetaTypeObjectValueSingle* clsFactory =
		m_metaObject->GetTypeObject(eMetaObjectType::enList);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

wxString CListDataObjectEnumRef::GetString() const
{
	IMetaTypeObjectValueSingle* clsFactory =
		m_metaObject->GetTypeObject(eMetaObjectType::enList);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

/////////////////////////////////////////////////////////////////////////////////////////////

wxDataViewItem CListDataObjectRef::FindRowValue(const CValue& varValue, const wxString& colName) const
{
	CReferenceDataObject* pRefData = NULL;
	if (varValue.ConvertToValue(pRefData)) {
		for (long row = 0; row < GetRowCount(); row++) {
			wxDataViewItem item = GetItem(row);
			if (item.IsOk()) {
				wxValueTableListRow* node = GetViewData<wxValueTableListRow>(item);
				if (node != NULL && pRefData->GetGuid() == node->GetGuid())
					return item;
			}
		}
	}
	return wxDataViewItem(NULL);
}

wxDataViewItem CListDataObjectRef::FindRowValue(IValueModelReturnLine* retLine) const
{
	CValue retValue;
	if (!retLine->GetValueByMetaID(*m_metaObject->GetDataReference(), retValue))
		return wxDataViewItem(NULL);
	return FindRowValue(retValue);
}

CListDataObjectRef::CListDataObjectRef(IMetaObjectRecordDataRef* metaObject, const form_identifier_t& formType, bool choiceMode) : IListDataObject(metaObject, formType),
m_metaObject(metaObject), m_choiceMode(choiceMode)
{
}

CSourceExplorer CListDataObjectRef::GetSourceExplorer() const
{
	CSourceExplorer srcHelper(
		m_metaObject, GetTypeClass(),
		true, true
	);

	for (auto attribute : m_metaObject->GetGenericAttributes()) {
		if (!m_metaObject->IsDataReference(attribute->GetMetaID()))
			srcHelper.AppendSource(attribute, true, true);
		else
			srcHelper.AppendSource(attribute, true, false);
	}

	return srcHelper;
}

bool CListDataObjectRef::GetModel(IValueModel*& tableValue, const meta_identifier_t& id)
{
	if (id == m_metaObject->GetMetaID()) {
		tableValue = this;
		return true;
	}
	return false;
}

//events 
void CListDataObjectRef::AddValue(unsigned int before)
{
	IMetaObjectRecordDataMutableRef* metaObject = NULL;
	if (m_metaObject->ConvertToValue(metaObject)) {
		IRecordDataObject* dataValue = metaObject->CreateObjectValue();
		if (dataValue != NULL) {
			dataValue->ShowValue();
		}
	}
}

void CListDataObjectRef::CopyValue()
{
	IMetaObjectRecordDataMutableRef* metaObject = NULL;
	if (m_metaObject->ConvertToValue(metaObject)) {
		wxValueTableListRow* node = GetViewData<wxValueTableListRow>(GetSelection());
		if (node == NULL)
			return;
		IRecordDataObject* dataValue = metaObject->CopyObjectValue(node->GetGuid());
		if (dataValue != NULL)
			dataValue->ShowValue();
	}
}

void CListDataObjectRef::EditValue()
{
	IMetaObjectRecordDataMutableRef* metaObject = NULL;
	if (m_metaObject->ConvertToValue(metaObject)) {
		wxValueTableListRow* node = GetViewData<wxValueTableListRow>(GetSelection());
		if (node == NULL)
			return;
		IRecordDataObject* dataValue = metaObject->CreateObjectValue(node->GetGuid());
		if (dataValue != NULL)
			dataValue->ShowValue();
	}
}

void CListDataObjectRef::DeleteValue()
{
	IMetaObjectRecordDataMutableRef* metaObject = NULL;
	if (m_metaObject->ConvertToValue(metaObject)) {
		wxValueTableListRow* node = GetViewData<wxValueTableListRow>(GetSelection());
		if (node == NULL)
			return;
		IRecordDataObject* objData =
			metaObject->CreateObjectValue(node->GetGuid());
		if (objData != NULL) {
			//objData->DeleteObject();
		}
		CListDataObjectRef::RefreshModel();
	}
}

void CListDataObjectRef::ChooseValue(CValueForm* srcForm)
{
	wxValueTableListRow* node = GetViewData<wxValueTableListRow>(GetSelection());
	if (node == NULL)
		return;
	wxASSERT(srcForm);
	CReferenceDataObject* refData = m_metaObject->FindObjectValue(node->GetGuid());
	if (refData != NULL)
		srcForm->NotifyChoice(refData->GetValue());
}

#include "core/metadata/singleClass.h"

CLASS_ID CListDataObjectRef::GetTypeClass() const
{
	IMetaTypeObjectValueSingle* clsFactory =
		m_metaObject->GetTypeObject(eMetaObjectType::enList);
	wxASSERT(clsFactory);
	return clsFactory->GetTypeClass();
}

wxString CListDataObjectRef::GetTypeString() const
{
	IMetaTypeObjectValueSingle* clsFactory =
		m_metaObject->GetTypeObject(eMetaObjectType::enList);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

wxString CListDataObjectRef::GetString() const
{
	IMetaTypeObjectValueSingle* clsFactory =
		m_metaObject->GetTypeObject(eMetaObjectType::enList);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

/////////////////////////////////////////////////////////////////////////////////////////////

wxDataViewItem CTreeDataObjectFolderRef::FindRowValue(const CValue& varValue, const wxString& colName) const
{
	CReferenceDataObject* pRefData = NULL;
	if (varValue.ConvertToValue(pRefData)) {
		std::function<void(wxValueTreeListNode*, wxValueTreeListNode*&, const Guid&)> findGuid = [&findGuid](wxValueTreeListNode* parent, wxValueTreeListNode*& foundedNode, const Guid& guid) {
			if (guid == parent->GetGuid()) {
				foundedNode = parent; return;
			}
			else if (foundedNode != NULL) {
				return;
			}
			for (unsigned int n = 0; n < parent->GetChildCount(); n++) {
				wxValueTreeListNode* node = dynamic_cast<wxValueTreeListNode*>(parent->GetChild(n));
				if (node != NULL)
					findGuid(node, foundedNode, guid);
				if (foundedNode != NULL)
					break;
			}
		};
		wxValueTreeListNode* foundedNode = NULL;
		for (unsigned int child = 0; child < GetRoot()->GetChildCount(); child++) {
			wxValueTreeListNode* node = dynamic_cast<wxValueTreeListNode*>(GetRoot()->GetChild(child));
			if (node != NULL)
				findGuid(node, foundedNode, pRefData->GetGuid());
			if (foundedNode != NULL)
				break;
		}
		if (foundedNode != NULL)
			return wxDataViewItem(foundedNode);
	}
	return wxDataViewItem(NULL);
}

wxDataViewItem CTreeDataObjectFolderRef::FindRowValue(IValueModelReturnLine* retLine) const
{
	CValue retValue;
	if (!retLine->GetValueByMetaID(*m_metaObject->GetDataReference(), retValue))
		return wxDataViewItem(NULL);
	return FindRowValue(retValue);
}

CTreeDataObjectFolderRef::CTreeDataObjectFolderRef(IMetaObjectRecordDataFolderMutableRef* metaObject, const form_identifier_t& formType,
	int listMode, bool choiceMode) : ITreeDataObject(metaObject, formType),
	m_metaObject(metaObject), m_listMode(listMode), m_choiceMode(choiceMode)
{
	ITreeDataObject::AppendSort(m_metaObject->GetDataIsFolder(), false, true, true);
	ITreeDataObject::AppendSort(m_metaObject->GetDataCode(), true, false);
	ITreeDataObject::AppendSort(m_metaObject->GetDataDescription(), true);
	ITreeDataObject::AppendSort(m_metaObject->GetDataReference());
}

CSourceExplorer CTreeDataObjectFolderRef::GetSourceExplorer() const
{
	CSourceExplorer srcHelper(
		m_metaObject, GetTypeClass(),
		true, true
	);

	for (auto attribute : m_metaObject->GetGenericAttributes()) {
	
		if (m_metaObject->IsDataFolder(attribute->GetMetaID())
			|| m_metaObject->IsDataReference(attribute->GetMetaID())) {
			continue;
		}
		
		if (m_metaObject->IsDataParent(attribute->GetMetaID())) {
			srcHelper.AppendSource(attribute, true, false);
			continue;
		}
		
		srcHelper.AppendSource(attribute, true, true);
	}

	return srcHelper;
}

bool CTreeDataObjectFolderRef::GetModel(IValueModel*& tableValue, const meta_identifier_t& id)
{
	if (id == m_metaObject->GetMetaID()) {
		tableValue = this;
		return true;
	}
	return false;
}

//events 
void CTreeDataObjectFolderRef::AddValue(unsigned int before)
{
	CValue isFolder = true; CValue cParent;
	wxValueTreeListNode* node = GetViewData<wxValueTreeListNode>(GetSelection());
	if (node != NULL) {
		node->GetValue(*m_metaObject->GetDataIsFolder(), isFolder);
		if (!isFolder.GetBoolean())
			node->GetValue(*m_metaObject->GetDataParent(), cParent);
		else
			node->GetValue(*m_metaObject->GetDataReference(), cParent);
	}
	IRecordDataObject* dataValue = m_metaObject->CreateObjectValue(eObjectMode::OBJECT_ITEM);
	if (dataValue != NULL) {
		dataValue->SetValueByMetaID(*m_metaObject->GetDataParent(), cParent);
		dataValue->ShowValue();
	}
}

void CTreeDataObjectFolderRef::AddFolderValue(unsigned int before)
{
	CValue isFolder = true; CValue cParent;
	wxValueTreeListNode* node = GetViewData<wxValueTreeListNode>(GetSelection());
	if (node != NULL) {
		node->GetValue(*m_metaObject->GetDataIsFolder(), isFolder);
		if (!isFolder.GetBoolean())
			node->GetValue(*m_metaObject->GetDataParent(), cParent);
		else
			node->GetValue(*m_metaObject->GetDataReference(), cParent);
	}
	IRecordDataObject* dataValue = m_metaObject->CreateObjectValue(eObjectMode::OBJECT_FOLDER);
	if (dataValue != NULL) {
		dataValue->SetValueByMetaID(*m_metaObject->GetDataParent(), cParent);
		dataValue->ShowValue();
	}
}

void CTreeDataObjectFolderRef::CopyValue()
{
	wxValueTreeListNode* node = GetViewData<wxValueTreeListNode>(GetSelection());
	if (node == NULL)
		return;
	CValue isFolder = false;
	node->GetValue(*m_metaObject->GetDataIsFolder(), isFolder);
	IRecordDataObject* dataValue = m_metaObject->CopyObjectValue(
		isFolder.GetBoolean() ? eObjectMode::OBJECT_FOLDER : eObjectMode::OBJECT_ITEM, node->GetGuid()
	);
	wxASSERT(dataValue);
	if (dataValue != NULL)
		dataValue->ShowValue();
}

void CTreeDataObjectFolderRef::EditValue()
{
	wxValueTreeListNode* node = GetViewData<wxValueTreeListNode>(GetSelection());
	if (node == NULL)
		return;
	CValue isFolder = false;
	node->GetValue(*m_metaObject->GetDataIsFolder(), isFolder);
	IRecordDataObject* dataValue =
		m_metaObject->CreateObjectValue(isFolder.GetBoolean() ? eObjectMode::OBJECT_FOLDER : eObjectMode::OBJECT_ITEM, node->GetGuid());
	wxASSERT(dataValue);
	if (dataValue != NULL)
		dataValue->ShowValue();
}

void CTreeDataObjectFolderRef::DeleteValue()
{
	wxValueTreeListNode* node = GetViewData<wxValueTreeListNode>(GetSelection());
	if (node == NULL)
		return;
	CValue isFolder = false;
	node->GetValue(*m_metaObject->GetDataIsFolder(), isFolder);
	IRecordDataObject* objData =
		m_metaObject->CreateObjectValue(isFolder.GetBoolean() ? eObjectMode::OBJECT_FOLDER : eObjectMode::OBJECT_ITEM, node->GetGuid());

	if (objData != NULL) {
		//objData->DeleteObject();
	}

	CTreeDataObjectFolderRef::RefreshModel();
}

void CTreeDataObjectFolderRef::ChooseValue(CValueForm* srcForm)
{
	wxValueTreeListNode* node = GetViewData<wxValueTreeListNode>(GetSelection());
	if (node == NULL)
		return;
	wxASSERT(srcForm);
	CValue cIsFolder = false;
	node->GetValue(*m_metaObject->GetDataIsFolder(), cIsFolder);

	CReferenceDataObject* refData = m_metaObject->FindObjectValue(node->GetGuid());
	if (m_listMode == LIST_FOLDER && cIsFolder.GetBoolean())
		srcForm->NotifyChoice(refData->GetValue());
	else if (m_listMode == LIST_ITEM && !cIsFolder.GetBoolean())
		srcForm->NotifyChoice(refData->GetValue());
	else if (m_listMode == LIST_ITEM_FOLDER)
		srcForm->NotifyChoice(refData->GetValue());
}

#include "core/metadata/singleClass.h"

CLASS_ID CTreeDataObjectFolderRef::GetTypeClass() const
{
	IMetaTypeObjectValueSingle* clsFactory =
		m_metaObject->GetTypeObject(eMetaObjectType::enList);
	wxASSERT(clsFactory);
	return clsFactory->GetTypeClass();
}

wxString CTreeDataObjectFolderRef::GetTypeString() const
{
	IMetaTypeObjectValueSingle* clsFactory =
		m_metaObject->GetTypeObject(eMetaObjectType::enList);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

wxString CTreeDataObjectFolderRef::GetString() const
{
	IMetaTypeObjectValueSingle* clsFactory =
		m_metaObject->GetTypeObject(eMetaObjectType::enList);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

/////////////////////////////////////////////////////////////////////////////////////////////

wxDataViewItem CListRegisterObject::FindRowValue(const CValue& varValue, const wxString& colName) const
{
	return wxDataViewItem(NULL);
}

wxDataViewItem CListRegisterObject::FindRowValue(IValueModelReturnLine* retLine) const
{
	for (long row = 0; row < GetRowCount(); row++) {
		wxDataViewItem item = GetItem(row);
		if (item.IsOk()) {
			wxValueTableKeyRow* node = GetViewData<wxValueTableKeyRow>(item);
			if (node != NULL) {
				bool founded = true;
				for (auto dimension : m_metaObject->GetGenericDimensions()) {
					CValue retValue; retLine->GetValueByMetaID(dimension->GetMetaID(), retValue);
					if (node->GetValue(dimension->GetMetaID()) != retValue)
						founded = false;
				}
				if (founded)
					return item;
			}
		}
	};

	return wxDataViewItem(NULL);
}

CListRegisterObject::CListRegisterObject(IMetaObjectRegisterData* metaObject, const form_identifier_t& formType) :
	IListDataObject(metaObject, formType), m_metaObject(metaObject)
{
	if (m_metaObject->HasRecorder()) {
		if (m_metaObject->HasPeriod())
			IListDataObject::AppendSort(metaObject->GetRegisterPeriod());
		else
			IListDataObject::AppendSort(metaObject->GetRegisterRecorder());
	}
	else if (m_metaObject->HasPeriod()) {
		IListDataObject::AppendSort(metaObject->GetRegisterPeriod());
	}
}

CSourceExplorer CListRegisterObject::GetSourceExplorer() const
{
	CSourceExplorer srcHelper(
		m_metaObject, GetTypeClass(),
		true, true
	);

	for (auto attribute : m_metaObject->GetGenericAttributes()) {
		srcHelper.AppendSource(attribute);
	}

	return srcHelper;
}

bool CListRegisterObject::GetModel(IValueModel*& tableValue, const meta_identifier_t& id)
{
	if (id == m_metaObject->GetMetaID()) {
		tableValue = this;
		return true;
	}
	return false;
}

//events 
void CListRegisterObject::AddValue(unsigned int before)
{
	if (m_metaObject != NULL &&
		m_metaObject->HasRecordManager()) {
		IRecordManagerObject* recordManager =
			m_metaObject->CreateRecordManagerObjectValue();
		wxASSERT(recordManager);
		recordManager->ShowFormValue();
	}
}

void CListRegisterObject::CopyValue()
{
	wxValueTableKeyRow* node = GetViewData<wxValueTableKeyRow>(GetSelection());
	if (node == NULL)
		return;
	if (m_metaObject != NULL &&
		m_metaObject->HasRecordManager()) {
		IRecordManagerObject* recordManager = m_metaObject->CopyRecordManagerObjectValue(
			node->GetUniquePairKey(m_metaObject)
		);
		wxASSERT(recordManager);
		recordManager->ShowValue();
	}
}

void CListRegisterObject::EditValue()
{
	wxValueTableKeyRow* node = GetViewData<wxValueTableKeyRow>(GetSelection());
	if (node == NULL)
		return;
	if (m_metaObject != NULL &&
		m_metaObject->HasRecordManager()) {
		IRecordManagerObject* recordManager = m_metaObject->CreateRecordManagerObjectValue(
			node->GetUniquePairKey(m_metaObject)
		);
		wxASSERT(recordManager);
		recordManager->ShowFormValue();
	}
	else if (m_metaObject != NULL) {
		CMetaDefaultAttributeObject* recorder =
			m_metaObject->GetRegisterRecorder();
		if (recorder != NULL) {
			CValue reference = node->GetValue(recorder->GetMetaID());
			reference.ShowValue();
		}
	}
}

void CListRegisterObject::DeleteValue()
{
	wxValueTableKeyRow* node = GetViewData<wxValueTableKeyRow>(GetSelection());
	if (node == NULL)
		return;
	if (m_metaObject != NULL &&
		m_metaObject->HasRecordManager()) {
		IRecordManagerObject* recordManager = m_metaObject->CreateRecordManagerObjectValue(
			node->GetUniquePairKey(m_metaObject)
		);
		wxASSERT(recordManager);
		recordManager->DeleteRegister();
	}

	CListRegisterObject::RefreshModel();
}

#include "core/metadata/singleClass.h"

CLASS_ID CListRegisterObject::GetTypeClass() const
{
	IMetaTypeObjectValueSingle* clsFactory =
		m_metaObject->GetTypeObject(eMetaObjectType::enList);
	wxASSERT(clsFactory);
	return clsFactory->GetTypeClass();
}

wxString CListRegisterObject::GetTypeString() const
{
	IMetaTypeObjectValueSingle* clsFactory =
		m_metaObject->GetTypeObject(eMetaObjectType::enList);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

wxString CListRegisterObject::GetString() const
{
	IMetaTypeObjectValueSingle* clsFactory =
		m_metaObject->GetTypeObject(eMetaObjectType::enList);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

//****************************************************************************
//*                              Support methods                             *
//****************************************************************************

void CListDataObjectEnumRef::PrepareNames() const
{
	m_methodHelper->ClearHelper();
	m_methodHelper->AppendProp(wxT("choiceMode"));
	m_methodHelper->AppendProc(wxT("refresh"), "refresh()");
}

bool CListDataObjectEnumRef::CallAsProc(const long lMethodNum, CValue** paParams, const long lSizeArray)
{
	switch (lMethodNum)
	{
	case enRefresh:
		RefreshModel();
		return true;
	}
	return false;
}

////////////////////////////////////////////////////////////////////////////////

void CListDataObjectRef::PrepareNames() const
{
	m_methodHelper->ClearHelper();
	m_methodHelper->AppendProp(wxT("choiceMode"));
	m_methodHelper->AppendProc(wxT("refresh"), "refresh()");
}

bool CListDataObjectRef::CallAsProc(const long lMethodNum, CValue** paParams, const long lSizeArray)
{
	switch (lMethodNum)
	{
	case enRefresh:
		RefreshModel();
		return true;
	}
	return false;
}

////////////////////////////////////////////////////////////////////////////////

void CTreeDataObjectFolderRef::PrepareNames() const
{
	m_methodHelper->ClearHelper();
	m_methodHelper->AppendProp(wxT("choiceMode"));
	m_methodHelper->AppendProc(wxT("refresh"), "refresh()");
}

bool CTreeDataObjectFolderRef::CallAsProc(const long lMethodNum, CValue** paParams, const long lSizeArray)
{
	switch (lMethodNum)
	{
	case enRefresh:
		RefreshModel();
		return true;
	}
	return false;
}

////////////////////////////////////////////////////////////////////////////////

void CListRegisterObject::PrepareNames() const
{
	m_methodHelper->ClearHelper();
	m_methodHelper->AppendProc(wxT("refresh"), "refresh()");
}

bool CListRegisterObject::CallAsProc(const long lMethodNum, CValue** paParams, const long lSizeArray)
{
	switch (lMethodNum)
	{
	case enRefresh:
		RefreshModel();
		return true;
	}
	return false;
}

//****************************************************************************
//*                              Override attribute                          *
//****************************************************************************

enum {
	enChoiceMode
};

bool CListDataObjectEnumRef::SetPropVal(const long lPropNum, const CValue& value) //установка атрибута
{
	return false;
}

bool CListDataObjectEnumRef::GetPropVal(const long lPropNum, CValue& pvarPropVal) //значение атрибута
{
	if (lPropNum == enChoiceMode) {
		pvarPropVal = m_choiceMode;
		return true;
	}
	return false;
}

bool CListDataObjectRef::SetPropVal(const long lPropNum, const CValue& value) //установка атрибута
{
	return false;
}

bool CListDataObjectRef::GetPropVal(const long lPropNum, CValue& pvarPropVal) //значение атрибута
{
	if (lPropNum == enChoiceMode) {
		pvarPropVal = m_choiceMode;
		return true;
	}
	return false;
}

bool CTreeDataObjectFolderRef::SetPropVal(const long lPropNum, const CValue& value) //установка атрибута
{
	return false;
}

bool CTreeDataObjectFolderRef::GetPropVal(const long lPropNum, CValue& pvarPropVal) //значение атрибута
{
	if (lPropNum == enChoiceMode) {
		pvarPropVal = m_choiceMode;
		return true;
	}
	return false;
}

bool CListRegisterObject::SetPropVal(const long lPropNum, const CValue& value) //установка атрибута
{
	return false;
}

bool CListRegisterObject::GetPropVal(const long lPropNum, CValue& pvarPropVal) //значение атрибута
{
	return false;
}

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

SO_VALUE_REGISTER(IListDataObject::CDataObjectListColumnCollection, "listColumn", TEXT2CLSID("VL_LVC"));
SO_VALUE_REGISTER(IListDataObject::CDataObjectListColumnCollection::CDataObjectListColumnInfo, "listColumnInfo", TEXT2CLSID("VL_LCI"));
SO_VALUE_REGISTER(IListDataObject::CDataObjectListReturnLine, "listValueRow", TEXT2CLSID("VL_LVR"));

SO_VALUE_REGISTER(ITreeDataObject::CDataObjectTreeColumnCollection, "treeColumn", TEXT2CLSID("VL_TVC"));
SO_VALUE_REGISTER(ITreeDataObject::CDataObjectTreeColumnCollection::CDataObjectTreeColumnInfo, "treeColumnInfo", TEXT2CLSID("VL_TCI"));
SO_VALUE_REGISTER(ITreeDataObject::CDataObjectTreeReturnLine, "treeValueRow", TEXT2CLSID("VL_TVR"));
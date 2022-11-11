////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : list data 
////////////////////////////////////////////////////////////////////////////

#include "objectList.h"
#include "frontend/visualView/controls/form.h"
#include "compiler/methods.h"
#include "common/srcExplorer.h"
#include "appData.h"

wxIMPLEMENT_ABSTRACT_CLASS(IListDataObject, IValueTable);

wxIMPLEMENT_DYNAMIC_CLASS(CListDataObjectRef, IListDataObject);
wxIMPLEMENT_DYNAMIC_CLASS(CListRegisterObject, IListDataObject);

wxIMPLEMENT_ABSTRACT_CLASS(ITreeDataObject, IValueTree);
wxIMPLEMENT_DYNAMIC_CLASS(CTreeDataObjectFolderRef, ITreeDataObject);

IListDataObject::IListDataObject(IMetaObjectWrapperData* metaTable, const form_identifier_t& formType) :
	ISourceDataObject(), m_objGuid(Guid::newGuid()), m_methods(new CMethods())
{
	m_dataColumnCollection = new CDataObjectListColumnCollection(this, metaTable);
	m_dataColumnCollection->IncrRef();
}

IListDataObject::~IListDataObject()
{
	if (m_dataColumnCollection != NULL) {
		m_dataColumnCollection->DecrRef();
	}

	wxDELETE(m_methods);
}

///////////////////////////////////////////////////////////////////////////////

ITreeDataObject::ITreeDataObject(IMetaObjectWrapperData* metaTable, const form_identifier_t& formType) :
	ISourceDataObject(), m_objGuid(Guid::newGuid()), m_methods(new CMethods())
{
	m_dataColumnCollection = new CDataObjectTreeColumnCollection(this, metaTable);
	m_dataColumnCollection->IncrRef();
}

ITreeDataObject::~ITreeDataObject()
{
	if (m_dataColumnCollection != NULL) {
		m_dataColumnCollection->DecrRef();
	}

	wxDELETE(m_methods);
}

//////////////////////////////////////////////////////////////////////
//					  CDataObjectListColumnCollection               //
//////////////////////////////////////////////////////////////////////

#include "compiler/methods.h"

wxIMPLEMENT_DYNAMIC_CLASS(IListDataObject::CDataObjectListColumnCollection, IValueTable::IValueModelColumnCollection);

IListDataObject::CDataObjectListColumnCollection::CDataObjectListColumnCollection() :
	IValueModelColumnCollection(), m_methods(NULL), m_ownerTable(NULL)
{
}

IListDataObject::CDataObjectListColumnCollection::CDataObjectListColumnCollection(IListDataObject* ownerTable, IMetaObjectWrapperData* metaTable) :
	IValueModelColumnCollection(), m_methods(new CMethods()), m_ownerTable(ownerTable)
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

	wxDELETE(m_methods);
}

void IListDataObject::CDataObjectListColumnCollection::SetAt(const CValue& cKey, CValue& cVal)//индекс массива должен начинаться с 0
{
}

CValue IListDataObject::CDataObjectListColumnCollection::GetAt(const CValue& cKey) //индекс массива должен начинаться с 0
{
	unsigned int index = cKey.ToUInt();

	if ((index < 0 || index >= m_columnInfo.size() && !appData->DesignerMode()))
		CTranslateError::Error(_("Index goes beyond array"));

	auto itFounded = m_columnInfo.begin();
	std::advance(itFounded, index);
	return itFounded->second;
}

wxIMPLEMENT_DYNAMIC_CLASS(ITreeDataObject::CDataObjectTreeColumnCollection, IValueTree::IValueModelColumnCollection);

ITreeDataObject::CDataObjectTreeColumnCollection::CDataObjectTreeColumnCollection() :
	IValueModelColumnCollection(), m_methods(NULL), m_ownerTable(NULL)
{
}

ITreeDataObject::CDataObjectTreeColumnCollection::CDataObjectTreeColumnCollection(ITreeDataObject* ownerTable, IMetaObjectWrapperData* metaTable) :
	IValueModelColumnCollection(), m_methods(new CMethods()), m_ownerTable(ownerTable)
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

	wxDELETE(m_methods);
}

void ITreeDataObject::CDataObjectTreeColumnCollection::SetAt(const CValue& cKey, CValue& cVal)//индекс массива должен начинаться с 0
{
}

CValue ITreeDataObject::CDataObjectTreeColumnCollection::GetAt(const CValue& cKey) //индекс массива должен начинаться с 0
{
	unsigned int index = cKey.ToUInt();

	if ((index < 0 || index >= m_columnInfo.size() && !appData->DesignerMode()))
		CTranslateError::Error(_("Index goes beyond array"));

	auto itFounded = m_columnInfo.begin();
	std::advance(itFounded, index);
	return itFounded->second;
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
	IValueModelReturnLine(line), m_methods(new CMethods()), m_ownerTable(ownerTable) {
}

IListDataObject::CDataObjectListReturnLine::~CDataObjectListReturnLine() {
	if (m_methods)
		delete m_methods;
}

void IListDataObject::CDataObjectListReturnLine::SetValueByMetaID(const meta_identifier_t& id, const CValue& cVal)
{
}

CValue IListDataObject::CDataObjectListReturnLine::GetValueByMetaID(const meta_identifier_t& id) const
{
	wxValueTableRow* node = m_ownerTable->GetViewData(m_lineItem);
	if (node != NULL)
		return node->GetValue(id);
	return CValue();
}

void IListDataObject::CDataObjectListReturnLine::PrepareNames() const
{
	std::vector<SEng> aAttributes;
	IMetaObjectWrapperData* metaObject = m_ownerTable->GetMetaObject();
	for (auto attribute : metaObject->GetGenericAttributes()) {
		SEng aAttribute;
		aAttribute.sName = attribute->GetName();
		aAttribute.sSynonym = wxT("default");
		aAttribute.iName = attribute->GetMetaID();
		aAttributes.push_back(aAttribute);
	}
	/*SEng aAttribute;
	aAttribute.sName = wxT("reference");
	aAttribute.sSynonym = wxT("reference");
	aAttribute.iName = metaObject->GetMetaID();
	aAttributes.push_back(aAttribute);*/
	m_methods->PrepareAttributes(aAttributes.data(), aAttributes.size());
}

void IListDataObject::CDataObjectListReturnLine::SetAttribute(attributeArg_t& aParams, CValue& cVal)
{
	const meta_identifier_t& id = m_methods->GetAttributePosition(aParams.GetIndex());
}

CValue IListDataObject::CDataObjectListReturnLine::GetAttribute(attributeArg_t& aParams)
{
	if (appData->DesignerMode())
		return CValue();
	const meta_identifier_t& id = m_methods->GetAttributePosition(aParams.GetIndex());
	wxValueTableRow* node = m_ownerTable->GetViewData(m_lineItem);
	if (node == NULL)
		return CValue();
	return node->GetValue(id);
}

wxIMPLEMENT_DYNAMIC_CLASS(ITreeDataObject::CDataObjectTreeReturnLine, IValueTree::IValueModelReturnLine);

ITreeDataObject::CDataObjectTreeReturnLine::CDataObjectTreeReturnLine(ITreeDataObject* ownerTable, const wxDataViewItem& line) :
	IValueModelReturnLine(line),
	m_methods(new CMethods()), m_ownerTable(ownerTable) {
}

ITreeDataObject::CDataObjectTreeReturnLine::~CDataObjectTreeReturnLine() {
	if (m_methods)
		delete m_methods;
}

void ITreeDataObject::CDataObjectTreeReturnLine::SetValueByMetaID(const meta_identifier_t& id, const CValue& cVal)
{
}

CValue ITreeDataObject::CDataObjectTreeReturnLine::GetValueByMetaID(const meta_identifier_t& id) const
{
	wxValueTreeNode* node = m_ownerTable->GetViewData(m_lineItem);
	if (node != NULL)
		return node->GetValue(id);
	return CValue();
}

void ITreeDataObject::CDataObjectTreeReturnLine::PrepareNames() const
{
	std::vector<SEng> aAttributes;
	IMetaObjectWrapperData* metaObject = m_ownerTable->GetMetaObject();
	for (auto attribute : metaObject->GetGenericAttributes()) {
		SEng aAttribute;
		aAttribute.sName = attribute->GetName();
		aAttribute.sSynonym = wxT("default");
		aAttribute.iName = attribute->GetMetaID();
		aAttributes.push_back(aAttribute);
	}

	/*SEng aAttribute;
	aAttribute.sName = wxT("reference");
	aAttribute.sSynonym = wxT("reference");
	aAttribute.iName = metaObject->GetMetaID();
	aAttributes.push_back(aAttribute);*/

	m_methods->PrepareAttributes(aAttributes.data(), aAttributes.size());
}

void ITreeDataObject::CDataObjectTreeReturnLine::SetAttribute(attributeArg_t& aParams, CValue& cVal)
{
	const meta_identifier_t& id = m_methods->GetAttributePosition(aParams.GetIndex());
}

CValue ITreeDataObject::CDataObjectTreeReturnLine::GetAttribute(attributeArg_t& aParams)
{
	if (appData->DesignerMode())
		return CValue();
	const meta_identifier_t& id = m_methods->GetAttributePosition(aParams.GetIndex());
	wxValueTreeNode* node = m_ownerTable->GetViewData(m_lineItem);
	if (node != NULL) {
		CValue cValue; node->GetValue(cValue, id);
		return cValue;
	}
	return CValue();
}

/////////////////////////////////////////////////////////////////////////////////////////////

wxDataViewItem CListDataObjectRef::FindRowValue(const CValue& cVal, const wxString &colName) const
{
	CReferenceDataObject* pRefData = NULL;
	if (cVal.ConvertToValue(pRefData)) {
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
	return FindRowValue(
		retLine->GetValueByMetaID(*m_metaObject->GetDataReference())
	);
}

CListDataObjectRef::CListDataObjectRef(IMetaObjectRecordDataRef* metaObject, const form_identifier_t& formType, bool choiceMode) : IListDataObject(metaObject, formType),
m_metaObject(metaObject), m_choiceMode(choiceMode)
{
}

CSourceExplorer CListDataObjectRef::GetSourceExplorer() const
{
	CSourceExplorer srcHelper(
		m_metaObject, GetClassType(),
		true, true
	);

	for (auto attribute : m_metaObject->GetGenericAttributes()) {
		if (!m_metaObject->IsDataReference(attribute->GetMetaID())) {
			if (m_metaObject->GetClsid() != g_metaEnumerationCLSID) {
				srcHelper.AppendSource(attribute, true, true);
			}
		}
		else if (m_metaObject->GetClsid() == g_metaEnumerationCLSID) {
			srcHelper.AppendSource(attribute, true, m_metaObject->GetClsid() == g_metaEnumerationCLSID);
		}
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
		IRecordDataObject* dataValue =
			metaObject->CreateObjectValue();
		if (dataValue != NULL) {
			dataValue->ShowValue();
		}
	}
}

void CListDataObjectRef::CopyValue()
{
	IMetaObjectRecordDataMutableRef* metaObject = NULL;
	if (m_metaObject->ConvertToValue(metaObject)) {
		wxDataViewItem currentData = GetSelection();
		if (!currentData.IsOk())
			return;
		wxValueTableListRow* node = GetViewData<wxValueTableListRow>(currentData);
		if (node == NULL)
			return;
		IRecordDataObject* dataValue =
			metaObject->CreateObjectValue(node->GetGuid());
		if (dataValue != NULL) {
			CValue reference = dataValue->CopyObjectValue();
			reference.ShowValue();
		}
	}
}

void CListDataObjectRef::EditValue()
{
	IMetaObjectRecordDataMutableRef* metaObject = NULL;
	if (m_metaObject->ConvertToValue(metaObject)) {
		wxDataViewItem currentData = GetSelection();
		if (!currentData.IsOk())
			return;
		wxValueTableListRow* node = GetViewData<wxValueTableListRow>(currentData);
		if (node == NULL)
			return;
		CValue reference =
			metaObject->CreateObjectValue(node->GetGuid());
		reference.ShowValue();
	}
}

void CListDataObjectRef::DeleteValue()
{
	IMetaObjectRecordDataMutableRef* metaObject = NULL;
	if (m_metaObject->ConvertToValue(metaObject)) {
		wxDataViewItem currentData = GetSelection();
		if (!currentData.IsOk())
			return;
		wxValueTableListRow* node = GetViewData<wxValueTableListRow>(currentData);
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
	wxDataViewItem currentData = GetSelection();
	if (!currentData.IsOk())
		return;
	wxASSERT(srcForm);
	wxValueTableListRow* node = GetViewData<wxValueTableListRow>(currentData);
	if (node == NULL)
		return;
	CValue reference =
		m_metaObject->FindObjectValue(node->GetGuid());

	srcForm->NotifyChoice(reference);
}

#include "metadata/singleMetaTypes.h"

CLASS_ID CListDataObjectRef::GetClassType() const
{
	IMetaTypeObjectValueSingle* clsFactory =
		m_metaObject->GetTypeObject(eMetaObjectType::enList);
	wxASSERT(clsFactory);
	return clsFactory->GetClassType();
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

wxDataViewItem CTreeDataObjectFolderRef::FindRowValue(const CValue& cVal, const wxString &colName) const
{
	CReferenceDataObject* pRefData = NULL;
	if (cVal.ConvertToValue(pRefData)) {
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
	return FindRowValue(
		retLine->GetValueByMetaID(*m_metaObject->GetDataReference())
	);
}

CTreeDataObjectFolderRef::CTreeDataObjectFolderRef(IMetaObjectRecordDataFolderMutableRef* metaObject, const form_identifier_t& formType,
	int listMode, bool choiceMode) : ITreeDataObject(metaObject, formType),
	m_metaObject(metaObject), m_listMode(listMode), m_choiceMode(choiceMode)
{
}

CSourceExplorer CTreeDataObjectFolderRef::GetSourceExplorer() const
{
	CSourceExplorer srcHelper(
		m_metaObject, GetClassType(),
		true, true
	);

	for (auto attribute : m_metaObject->GetGenericAttributes()) {
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
		node->GetValue(isFolder, *m_metaObject->GetDataIsFolder());
		if (!isFolder.GetBoolean()) {
			node->GetValue(cParent, *m_metaObject->GetDataParent());
		}
		else {
			node->GetValue(cParent, *m_metaObject->GetDataReference());
		}
	}
	IRecordDataObject* dataValue =
		m_metaObject->CreateObjectValue(eObjectMode::OBJECT_ITEM);
	if (dataValue == NULL)
		return;
	dataValue->SetValueByMetaID(*m_metaObject->GetDataParent(), cParent);
	dataValue->ShowValue();
}

void CTreeDataObjectFolderRef::AddFolderValue(unsigned int before)
{
	CValue isFolder = true; CValue cParent;
	wxValueTreeListNode* node = GetViewData<wxValueTreeListNode>(GetSelection());
	if (node != NULL) {
		node->GetValue(isFolder, *m_metaObject->GetDataIsFolder());
		if (!isFolder.GetBoolean()) {
			node->GetValue(cParent, *m_metaObject->GetDataParent());
		}
		else {
			node->GetValue(cParent, *m_metaObject->GetDataReference());
		}
	}

	IRecordDataObject* dataValue =
		m_metaObject->CreateObjectValue(eObjectMode::OBJECT_FOLDER);
	if (dataValue == NULL)
		return;
	dataValue->SetValueByMetaID(*m_metaObject->GetDataParent(), cParent);
	dataValue->ShowValue();
}

void CTreeDataObjectFolderRef::CopyValue()
{
	const wxDataViewItem& item = GetSelection();
	wxValueTreeListNode* node = static_cast<wxValueTreeListNode*>(item.GetID());
	if (node == NULL)
		return;
	CValue isFolder = false;
	node->GetValue(isFolder, *m_metaObject->GetDataIsFolder());
	IRecordDataObject* dataValue =
		m_metaObject->CreateObjectValue(isFolder.GetBoolean() ? eObjectMode::OBJECT_FOLDER : eObjectMode::OBJECT_ITEM, node->GetGuid());
	if (dataValue != NULL) {
		CValue reference = dataValue->CopyObjectValue();
		reference.ShowValue();
	}
}

void CTreeDataObjectFolderRef::EditValue()
{
	const wxDataViewItem& item = GetSelection();
	wxValueTreeListNode* node = static_cast<wxValueTreeListNode*>(item.GetID());
	if (node == NULL)
		return;
	CValue isFolder = false;
	node->GetValue(isFolder, *m_metaObject->GetDataIsFolder());
	CValue reference =
		m_metaObject->CreateObjectValue(isFolder.GetBoolean() ? eObjectMode::OBJECT_FOLDER : eObjectMode::OBJECT_ITEM, node->GetGuid());
	reference.ShowValue();
}

void CTreeDataObjectFolderRef::DeleteValue()
{
	const wxDataViewItem& item = GetSelection();
	wxValueTreeListNode* node = static_cast<wxValueTreeListNode*>(item.GetID());
	if (node == NULL)
		return;

	CValue isFolder = false;
	node->GetValue(isFolder, *m_metaObject->GetDataIsFolder());
	IRecordDataObject* objData =
		m_metaObject->CreateObjectValue(isFolder.GetBoolean() ? eObjectMode::OBJECT_FOLDER : eObjectMode::OBJECT_ITEM, node->GetGuid());

	if (objData != NULL) {
		//objData->DeleteObject();
	}

	CTreeDataObjectFolderRef::RefreshModel();
}

void CTreeDataObjectFolderRef::ChooseValue(CValueForm* srcForm)
{
	const wxDataViewItem& item = GetSelection();
	wxValueTreeListNode* node = static_cast<wxValueTreeListNode*>(item.GetID());
	if (node == NULL)
		return;
	wxASSERT(srcForm);
	CValue cIsFolder = false;
	node->GetValue(cIsFolder, *m_metaObject->GetDataIsFolder());

	CValue reference =
		m_metaObject->FindObjectValue(node->GetGuid());

	if (m_listMode == LIST_FOLDER && cIsFolder.GetBoolean())
		srcForm->NotifyChoice(reference);
	else if (m_listMode == LIST_ITEM && !cIsFolder.GetBoolean())
		srcForm->NotifyChoice(reference);
	else if (m_listMode == LIST_ITEM_FOLDER)
		srcForm->NotifyChoice(reference);
}

#include "metadata/singleMetaTypes.h"

CLASS_ID CTreeDataObjectFolderRef::GetClassType() const
{
	IMetaTypeObjectValueSingle* clsFactory =
		m_metaObject->GetTypeObject(eMetaObjectType::enList);
	wxASSERT(clsFactory);
	return clsFactory->GetClassType();
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

wxDataViewItem CListRegisterObject::FindRowValue(const CValue& cVal, const wxString &colName) const
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
					if (node->GetValue(dimension->GetMetaID()) != retLine->GetValueByMetaID(dimension->GetMetaID()))
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
}

CSourceExplorer CListRegisterObject::GetSourceExplorer() const
{
	CSourceExplorer srcHelper(
		m_metaObject, GetClassType(),
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
	wxDataViewItem currentData = GetSelection();
	if (!currentData.IsOk())
		return;
	wxValueTableKeyRow* node = GetViewData<wxValueTableKeyRow>(currentData);
	if (node == NULL)
		return;
	if (m_metaObject != NULL &&
		m_metaObject->HasRecordManager()) {
		IRecordManagerObject* recordManager = m_metaObject->CreateRecordManagerObjectValue(
			node->GetUniquePairKey(m_metaObject)
		);
		wxASSERT(recordManager);
		CValue recManager =
			recordManager->CopyRegisterValue();
		recManager.ShowValue();
	}
}

void CListRegisterObject::EditValue()
{
	wxDataViewItem currentData = GetSelection();
	if (!currentData.IsOk())
		return;
	wxValueTableKeyRow* node = GetViewData<wxValueTableKeyRow>(currentData);
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
	wxDataViewItem currentData = GetSelection();
	if (!currentData.IsOk())
		return;
	wxValueTableKeyRow* node = GetViewData<wxValueTableKeyRow>(currentData);
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

#include "metadata/singleMetaTypes.h"

CLASS_ID CListRegisterObject::GetClassType() const
{
	IMetaTypeObjectValueSingle* clsFactory =
		m_metaObject->GetTypeObject(eMetaObjectType::enList);
	wxASSERT(clsFactory);
	return clsFactory->GetClassType();
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

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

SO_VALUE_REGISTER(IListDataObject::CDataObjectListColumnCollection, "listColumn", CDataObjectListColumnCollection, TEXT2CLSID("VL_LVCC"));
SO_VALUE_REGISTER(IListDataObject::CDataObjectListColumnCollection::CDataObjectListColumnInfo, "listColumnInfo", CDataObjectListColumnInfo, TEXT2CLSID("VL_LVCI"));
SO_VALUE_REGISTER(IListDataObject::CDataObjectListReturnLine, "listValueRow", CDataObjectListReturnLine, TEXT2CLSID("VL_LVCR"));
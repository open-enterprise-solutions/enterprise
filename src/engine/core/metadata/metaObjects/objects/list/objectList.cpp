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
wxIMPLEMENT_DYNAMIC_CLASS(CListDataObjectGroupRef, IListDataObject);

wxIMPLEMENT_DYNAMIC_CLASS(CListRegisterObject, IListDataObject);

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

//////////////////////////////////////////////////////////////////////
//					  CDataObjectListColumnCollection               //
//////////////////////////////////////////////////////////////////////

#include "compiler/methods.h"

wxIMPLEMENT_DYNAMIC_CLASS(IListDataObject::CDataObjectListColumnCollection, IValueTable::IValueTableColumnCollection);

IListDataObject::CDataObjectListColumnCollection::CDataObjectListColumnCollection() :
	IValueTableColumnCollection(), m_methods(NULL), m_ownerTable(NULL)
{
}

IListDataObject::CDataObjectListColumnCollection::CDataObjectListColumnCollection(IListDataObject* ownerTable, IMetaObjectWrapperData* metaTable) :
	IValueTableColumnCollection(), m_methods(new CMethods()), m_ownerTable(ownerTable)
{
	wxASSERT(metaTable);

	for (auto attributes : metaTable->GetGenericAttributes()) {
		CDataObjectListColumnInfo* columnInfo = new CDataObjectListColumnInfo(attributes);
		m_aColumnInfo.insert_or_assign(attributes->GetMetaID(), columnInfo);
		columnInfo->IncrRef();
	}
}

IListDataObject::CDataObjectListColumnCollection::~CDataObjectListColumnCollection()
{
	for (auto& colInfo : m_aColumnInfo) {
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

	if ((index < 0 || index >= m_aColumnInfo.size() && !appData->DesignerMode()))
		CTranslateError::Error(_("Index goes beyond array"));

	auto itFounded = m_aColumnInfo.begin();
	std::advance(itFounded, index);
	return itFounded->second;
}

//////////////////////////////////////////////////////////////////////
//							 CDataObjectListColumnInfo                    //
//////////////////////////////////////////////////////////////////////

wxIMPLEMENT_DYNAMIC_CLASS(IListDataObject::CDataObjectListColumnCollection::CDataObjectListColumnInfo, IValueTable::IValueTableColumnCollection::IValueTableColumnInfo);

IListDataObject::CDataObjectListColumnCollection::CDataObjectListColumnInfo::CDataObjectListColumnInfo() :
	IValueTableColumnInfo(), m_metaAttribute(NULL)
{
}

IListDataObject::CDataObjectListColumnCollection::CDataObjectListColumnInfo::CDataObjectListColumnInfo(IMetaAttributeObject* metaAttribute) :
	IValueTableColumnInfo(), m_metaAttribute(metaAttribute)
{
}

IListDataObject::CDataObjectListColumnCollection::CDataObjectListColumnInfo::~CDataObjectListColumnInfo()
{
}

//////////////////////////////////////////////////////////////////////
//					  CDataObjectListReturnLine                     //
//////////////////////////////////////////////////////////////////////

wxIMPLEMENT_ABSTRACT_CLASS(IListDataObject::CDataObjectListReturnLine, IValueTable::IValueTableReturnLine);

IListDataObject::CDataObjectListReturnLine::CDataObjectListReturnLine(IListDataObject* ownerTable, int line) : IValueTableReturnLine(), m_methods(new CMethods()), m_ownerTable(ownerTable), m_lineTable(line) {}
IListDataObject::CDataObjectListReturnLine::~CDataObjectListReturnLine() { if (m_methods) delete m_methods; }

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

	SEng aAttribute;
	aAttribute.sName = wxT("reference");
	aAttribute.sSynonym = wxT("reference");
	aAttribute.iName = metaObject->GetMetaID();
	aAttributes.push_back(aAttribute);

	m_methods->PrepareAttributes(aAttributes.data(), aAttributes.size());
}

void IListDataObject::CDataObjectListReturnLine::SetAttribute(attributeArg_t& aParams, CValue& cVal)
{
	const meta_identifier_t& id = m_methods->GetAttributePosition(aParams.GetIndex());
	auto& rowValues = m_ownerTable->GetRowData(m_lineTable);
}

CValue IListDataObject::CDataObjectListReturnLine::GetAttribute(attributeArg_t& aParams)
{
	if (appData->DesignerMode()) {
		return CValue();
	}

	const meta_identifier_t& id = m_methods->GetAttributePosition(aParams.GetIndex());

	auto& rowValues = m_ownerTable->GetRowData(m_lineTable);
	auto itFoundedByIndex = rowValues.find(id);
	if (itFoundedByIndex != rowValues.end()) {
		return itFoundedByIndex->second;
	}

	return CValue();
}

/////////////////////////////////////////////////////////////////////////////////////////////

wxDataViewItem CListDataObjectRef::GetLineByGuid(const Guid& guid) const
{
	auto foundedIt = m_aObjectValues.find(guid);
	if (foundedIt == m_aObjectValues.end()) {
		return wxDataViewItem(NULL);
	}

	return GetItem(
		std::distance(m_aObjectValues.begin(), foundedIt)
	);
}

std::map<meta_identifier_t, CValue>& CListDataObjectRef::GetRowData(unsigned int lineTable)
{
	auto itFoundedByLine = m_aObjectValues.begin();
	std::advance(itFoundedByLine, lineTable);
	return itFoundedByLine->second;
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
	IRecordDataObject* dataValue =
		m_metaObject->CreateObjectValue();

	if (dataValue != NULL) {
		dataValue->ShowValue();
	}
}

void CListDataObjectRef::CopyValue()
{
	int currentLine = GetSelectionLine();
	if (currentLine == wxNOT_FOUND)
		return;

	auto itFounded = m_aObjectValues.begin();
	std::advance(itFounded, currentLine);

	IRecordDataObject* dataValue =
		m_metaObject->CreateObjectValue(itFounded->first);
	if (dataValue != NULL) {
		CValue reference = dataValue->CopyObjectValue();
		reference.ShowValue();
	}
}

void CListDataObjectRef::EditValue()
{
	int currentLine = GetSelectionLine();
	if (currentLine == wxNOT_FOUND)
		return;

	auto itFounded = m_aObjectValues.begin();
	std::advance(itFounded, currentLine);

	CValue reference =
		m_metaObject->CreateObjectValue(itFounded->first);

	reference.ShowValue();
}

void CListDataObjectRef::DeleteValue()
{
	int currentLine = GetSelectionLine();
	if (currentLine == wxNOT_FOUND)
		return;

	auto itFounded = m_aObjectValues.begin();
	std::advance(itFounded, currentLine);

	IRecordDataObject* objData =
		m_metaObject->CreateObjectValue(itFounded->first);

	if (objData != NULL) {
		//objData->DeleteObject();
	}

	CListDataObjectRef::RefreshModel();
}

void CListDataObjectRef::ChooseValue(CValueForm* srcForm)
{
	int currentLine = GetSelectionLine();
	if (currentLine == wxNOT_FOUND)
		return;
	wxASSERT(srcForm);
	auto itFounded = m_aObjectValues.begin();
	std::advance(itFounded, currentLine);

	CValue reference =
		m_metaObject->FindObjectValue(itFounded->first);

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

wxDataViewItem CListDataObjectGroupRef::GetLineByGuid(const Guid& guid) const
{
	auto foundedIt = m_aObjectValues.find(guid);
	if (foundedIt == m_aObjectValues.end()) {
		return wxDataViewItem(NULL);
	}

	return GetItem(
		std::distance(m_aObjectValues.begin(), foundedIt)
	);
}

std::map<meta_identifier_t, CValue>& CListDataObjectGroupRef::GetRowData(unsigned int lineTable)
{
	auto itFoundedByLine = m_aObjectValues.begin();
	std::advance(itFoundedByLine, lineTable);
	return itFoundedByLine->second;
}

CListDataObjectGroupRef::CListDataObjectGroupRef(IMetaObjectRecordDataGroupMutableRef* metaObject, const form_identifier_t& formType,
	int listMode, bool choiceMode) : IListDataObject(metaObject, formType),
	m_metaObject(metaObject), m_choiceMode(choiceMode)
{
}

CSourceExplorer CListDataObjectGroupRef::GetSourceExplorer() const
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

bool CListDataObjectGroupRef::GetModel(IValueModel*& tableValue, const meta_identifier_t& id)
{
	if (id == m_metaObject->GetMetaID()) {
		tableValue = this;
		return true;
	}
	return false;
}

//events 
void CListDataObjectGroupRef::AddValue(unsigned int before)
{
	IRecordDataObject* dataValue =
		m_metaObject->CreateObjectValue();

	if (dataValue != NULL) {
		dataValue->ShowValue();
	}
}

void CListDataObjectGroupRef::CopyValue()
{
	int currentLine = GetSelectionLine();
	if (currentLine == wxNOT_FOUND)
		return;

	auto itFounded = m_aObjectValues.begin();
	std::advance(itFounded, currentLine);

	IRecordDataObject* dataValue =
		m_metaObject->CreateObjectValue(itFounded->first);
	if (dataValue != NULL) {
		CValue reference = dataValue->CopyObjectValue();
		reference.ShowValue();
	}
}

void CListDataObjectGroupRef::EditValue()
{
	int currentLine = GetSelectionLine();
	if (currentLine == wxNOT_FOUND)
		return;

	auto itFounded = m_aObjectValues.begin();
	std::advance(itFounded, currentLine);

	CValue reference =
		m_metaObject->CreateObjectValue(itFounded->first);

	reference.ShowValue();
}

void CListDataObjectGroupRef::DeleteValue()
{
	int currentLine = GetSelectionLine();
	if (currentLine == wxNOT_FOUND)
		return;

	auto itFounded = m_aObjectValues.begin();
	std::advance(itFounded, currentLine);

	IRecordDataObject* objData =
		m_metaObject->CreateObjectValue(itFounded->first);

	if (objData != NULL) {
		//objData->DeleteObject();
	}

	CListDataObjectGroupRef::RefreshModel();
}

void CListDataObjectGroupRef::ChooseValue(CValueForm* srcForm)
{
	int currentLine = GetSelectionLine();
	if (currentLine == wxNOT_FOUND)
		return;
	wxASSERT(srcForm);
	auto itFounded = m_aObjectValues.begin();
	std::advance(itFounded, currentLine);

	CValue reference =
		m_metaObject->FindObjectValue(itFounded->first);

	srcForm->NotifyChoice(reference);
}

#include "metadata/singleMetaTypes.h"

CLASS_ID CListDataObjectGroupRef::GetClassType() const
{
	IMetaTypeObjectValueSingle* clsFactory =
		m_metaObject->GetTypeObject(eMetaObjectType::enList);
	wxASSERT(clsFactory);
	return clsFactory->GetClassType();
}

wxString CListDataObjectGroupRef::GetTypeString() const
{
	IMetaTypeObjectValueSingle* clsFactory =
		m_metaObject->GetTypeObject(eMetaObjectType::enList);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

wxString CListDataObjectGroupRef::GetString() const
{
	IMetaTypeObjectValueSingle* clsFactory =
		m_metaObject->GetTypeObject(eMetaObjectType::enList);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

/////////////////////////////////////////////////////////////////////////////////////////////

std::map<meta_identifier_t, CValue>& CListRegisterObject::GetRowData(unsigned int lineTable)
{
	auto itFoundedByLine = m_aObjectValues.begin();
	std::advance(itFoundedByLine, lineTable);
	return itFoundedByLine->second;
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
	int currentLine = GetSelectionLine();
	if (currentLine == wxNOT_FOUND)
		return;

	auto itFounded = m_aObjectValues.begin();
	std::advance(itFounded, currentLine);

	if (m_metaObject != NULL &&
		m_metaObject->HasRecordManager()) {
		IRecordManagerObject* recordManager = m_metaObject->CreateRecordManagerObjectValue(
			CUniquePairKey{ m_metaObject, itFounded->second }
		);
		wxASSERT(recordManager);
		CValue recManager =
			recordManager->CopyRegisterValue();
		recManager.ShowValue();
	}
}

void CListRegisterObject::EditValue()
{
	int currentLine = GetSelectionLine();
	if (currentLine == wxNOT_FOUND)
		return;

	auto itFounded = m_aObjectValues.begin();
	std::advance(itFounded, currentLine);

	if (m_metaObject != NULL &&
		m_metaObject->HasRecordManager()) {
		IRecordManagerObject* recordManager = m_metaObject->CreateRecordManagerObjectValue(
			CUniquePairKey{ m_metaObject, itFounded->second }
		);
		wxASSERT(recordManager);
		recordManager->ShowFormValue();
	}
	else if (m_metaObject != NULL) {
		CMetaDefaultAttributeObject* recorder =
			m_metaObject->GetRegisterRecorder();
		if (recorder != NULL) {
			auto itRef = itFounded->second;
			CValue reference =
				itRef.at(recorder->GetMetaID());
			reference.ShowValue();
		}
	}
}

void CListRegisterObject::DeleteValue()
{
	int currentLine = GetSelectionLine();
	if (currentLine == wxNOT_FOUND)
		return;

	auto itFounded = m_aObjectValues.begin();
	std::advance(itFounded, currentLine);

	if (m_metaObject != NULL &&
		m_metaObject->HasRecordManager()) {
		IRecordManagerObject* recordManager = m_metaObject->CreateRecordManagerObjectValue(
			CUniquePairKey{ m_metaObject, itFounded->second }
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
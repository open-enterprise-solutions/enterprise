////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : list data 
////////////////////////////////////////////////////////////////////////////

#include "objectList.h"
#include "backend/srcDataObject.h"
#include "backend/system/systemManager.h"

#include "backend/appData.h"




ibValueListDataObject::ibValueListDataObject(const ibValueMetaObjectGenericData* metaObject, const ibFormID& formType, bool choiceMode) :
	ibSourceDataObject(),
	m_recordColumnCollection(new ibValueDataObjectListColumnCollection(this, metaObject)),
	m_objGuid(choiceMode ? ibGuid::newGuid() : metaObject->GetGuid()) {
	for (const auto object : metaObject->GetGenericAttributeArrayObject()) {
		m_filterRow.AppendFilter(
			object->GetMetaID(),
			object->GetName(),
			object->GetSynonym(),
			ibComparisonType_Equal,
			object->GetTypeDesc(),
			object->CreateValue(),
			false
		);
	}
	// L5 — wire the composer's source once: the list IS metaobject-bound, and the
	// metaobject is its own language identity (Kind.Name). Settings come per fetch.
	m_composer.FromSource(ibValue::GetNameObjectFromID(metaObject->GetClassType()), metaObject->GetName());
}

ibValueListDataObject::~ibValueListDataObject()
{
}

///////////////////////////////////////////////////////////////////////////////

ibValueModelTreeDataObject::ibValueModelTreeDataObject(const ibValueMetaObjectGenericData* metaObject, const ibFormID& formType, bool choiceMode) :
	ibSourceDataObject(),
	m_recordColumnCollection(new ibValueDataObjectTreeColumnCollection(this, metaObject)),
	m_objGuid(choiceMode ? ibGuid::newGuid() : metaObject->GetGuid()) {
	for (const auto object : metaObject->GetGenericAttributeArrayObject()) {
		m_filterRow.AppendFilter(
			object->GetMetaID(),
			object->GetName(),
			object->GetSynonym(),
			object->GetTypeDesc(),
			object->CreateValue()
		);
	}
	// L5 — wire the composer's source once (see the list base ctor above).
	m_composer.FromSource(ibValue::GetNameObjectFromID(metaObject->GetClassType()), metaObject->GetName());
}

ibValueModelTreeDataObject::~ibValueModelTreeDataObject()
{
}

//////////////////////////////////////////////////////////////////////
//					  ibValueDataObjectListColumnCollection               //
//////////////////////////////////////////////////////////////////////


ibValueListDataObject::ibValueDataObjectListColumnCollection::ibValueDataObjectListColumnCollection() :
	ibValueModelColumnCollection(), m_ownerTable(nullptr)
{
}

ibValueListDataObject::ibValueDataObjectListColumnCollection::ibValueDataObjectListColumnCollection(ibValueListDataObject* ownerTable, const ibValueMetaObjectGenericData* metaObject) :
	ibValueModelColumnCollection(), m_ownerTable(ownerTable)
{
	wxASSERT(metaObject);

	for (const auto object : metaObject->GetGenericAttributeArrayObject()) {
		m_listColumnInfo.insert_or_assign(object->GetMetaID(), new ibValueDataObjectListColumnInfo(object));
	}
}

ibValueListDataObject::ibValueDataObjectListColumnCollection::~ibValueDataObjectListColumnCollection()
{
}

bool ibValueListDataObject::ibValueDataObjectListColumnCollection::SetAt(const ibValue& varKeyValue, const ibValue& varValue)//пїЅпїЅпїЅпїЅпїЅпїЅ пїЅпїЅпїЅпїЅпїЅпїЅпїЅ пїЅпїЅпїЅпїЅпїЅпїЅ пїЅпїЅпїЅпїЅпїЅпїЅпїЅпїЅпїЅпїЅ пїЅ 0
{
	return false;
}

bool ibValueListDataObject::ibValueDataObjectListColumnCollection::GetAt(const ibValue& varKeyValue, ibValue& pvarValue) //пїЅпїЅпїЅпїЅпїЅпїЅ пїЅпїЅпїЅпїЅпїЅпїЅпїЅ пїЅпїЅпїЅпїЅпїЅпїЅ пїЅпїЅпїЅпїЅпїЅпїЅпїЅпїЅпїЅпїЅ пїЅ 0
{
	unsigned int index = varKeyValue.GetUInteger();

	if ((index < 0 || index >= m_listColumnInfo.size() && !appData->DesignerMode())) {
		ibBackendCoreException::Error("Index goes beyond array");
		return false;
	}

	auto it = m_listColumnInfo.begin();
	std::advance(it, index);
	pvarValue = it->second;

	return true;
}


ibValueModelTreeDataObject::ibValueDataObjectTreeColumnCollection::ibValueDataObjectTreeColumnCollection() :
	ibValueModelColumnCollection(), m_ownerTable(nullptr)
{
}

ibValueModelTreeDataObject::ibValueDataObjectTreeColumnCollection::ibValueDataObjectTreeColumnCollection(ibValueModelTreeDataObject* ownerTable, const ibValueMetaObjectGenericData* metaObject) :
	ibValueModelColumnCollection(), m_ownerTable(ownerTable)
{
	wxASSERT(metaObject);

	for (const auto object : metaObject->GetGenericAttributeArrayObject()) {
		m_listColumnInfo.insert_or_assign(object->GetMetaID(),
			new ibValueDataObjectTreeColumnInfo(object));
	}
}

ibValueModelTreeDataObject::ibValueDataObjectTreeColumnCollection::~ibValueDataObjectTreeColumnCollection()
{
}

bool ibValueModelTreeDataObject::ibValueDataObjectTreeColumnCollection::SetAt(const ibValue& varKeyValue, const ibValue& varValue)//пїЅпїЅпїЅпїЅпїЅпїЅ пїЅпїЅпїЅпїЅпїЅпїЅпїЅ пїЅпїЅпїЅпїЅпїЅпїЅ пїЅпїЅпїЅпїЅпїЅпїЅпїЅпїЅпїЅпїЅ пїЅ 0
{
	return false;
}

bool ibValueModelTreeDataObject::ibValueDataObjectTreeColumnCollection::GetAt(const ibValue& varKeyValue, ibValue& pvarValue) //пїЅпїЅпїЅпїЅпїЅпїЅ пїЅпїЅпїЅпїЅпїЅпїЅпїЅ пїЅпїЅпїЅпїЅпїЅпїЅ пїЅпїЅпїЅпїЅпїЅпїЅпїЅпїЅпїЅпїЅ пїЅ 0
{
	unsigned int index = varKeyValue.GetUInteger();
	if ((index < 0 || index >= m_listColumnInfo.size() && !appData->DesignerMode())) {
		ibBackendCoreException::Error("Index goes beyond array");
		return false;
	}
	auto it = m_listColumnInfo.begin();
	std::advance(it, index);
	pvarValue = it->second;
	return true;
}


//////////////////////////////////////////////////////////////////////
//							 ibValueDataObjectListColumnInfo              //
//////////////////////////////////////////////////////////////////////


ibValueListDataObject::ibValueDataObjectListColumnCollection::ibValueDataObjectListColumnInfo::ibValueDataObjectListColumnInfo() :
	ibValueModelColumnInfo(), m_metaAttribute(nullptr)
{
}

ibValueListDataObject::ibValueDataObjectListColumnCollection::ibValueDataObjectListColumnInfo::ibValueDataObjectListColumnInfo(ibValueMetaObjectAttributeBase* attribute) :
	ibValueModelColumnInfo(), m_metaAttribute(attribute)
{
}

ibValueListDataObject::ibValueDataObjectListColumnCollection::ibValueDataObjectListColumnInfo::~ibValueDataObjectListColumnInfo()
{
}


ibValueModelTreeDataObject::ibValueDataObjectTreeColumnCollection::ibValueDataObjectTreeColumnInfo::ibValueDataObjectTreeColumnInfo() :
	ibValueModelColumnInfo(), m_metaAttribute(nullptr)
{
}

ibValueModelTreeDataObject::ibValueDataObjectTreeColumnCollection::ibValueDataObjectTreeColumnInfo::ibValueDataObjectTreeColumnInfo(ibValueMetaObjectAttributeBase* attribute) :
	ibValueModelColumnInfo(), m_metaAttribute(attribute)
{
}

ibValueModelTreeDataObject::ibValueDataObjectTreeColumnCollection::ibValueDataObjectTreeColumnInfo::~ibValueDataObjectTreeColumnInfo()
{
}

//////////////////////////////////////////////////////////////////////
//					  ibValueDataObjectListReturnLine                     //
//////////////////////////////////////////////////////////////////////


ibValueListDataObject::ibValueDataObjectListReturnLine::ibValueDataObjectListReturnLine(ibValueListDataObject* ownerTable, const ibDataViewItem& line) :
	ibValueModelReturnLine(line), m_ownerTable(ownerTable)
{
	m_members.Bind(this, &ibValueDataObjectListReturnLine::FillMembers);
}

ibValueListDataObject::ibValueDataObjectListReturnLine::~ibValueDataObjectListReturnLine()
{
}

void ibValueListDataObject::ibValueDataObjectListReturnLine::FillMembers(ibMemberTable& helper) const
{
	const ibValueMetaObjectGenericData* metaObject = m_ownerTable->GetMetaObject();
	for (const auto object : metaObject->GetGenericAttributeArrayObject()) {
		helper.AppendProp(
			object->GetName(),
			object->GetMetaID()
		);
	}
}

bool ibValueListDataObject::ibValueDataObjectListReturnLine::SetPropVal(const long lPropNum, const ibValue& varPropVal)
{
	return false;
}

bool ibValueListDataObject::ibValueDataObjectListReturnLine::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	if (appData->DesignerMode())
		return false;
	const ibMetaID& id = m_members.GetPropData(lPropNum);
	ibValueTableRow* node = m_ownerTable->GetViewData<ibValueTableRow>(m_lineItem);
	if (node == nullptr)
		return false;
	return node->GetValue(id, pvarPropVal);
}


ibValueModelTreeDataObject::ibValueDataObjectTreeReturnLine::ibValueDataObjectTreeReturnLine(ibValueModelTreeDataObject* ownerTable, const ibDataViewItem& line) :
	ibValueModelReturnLine(line),
	m_ownerTable(ownerTable)
{
	m_members.Bind(this, &ibValueDataObjectTreeReturnLine::FillMembers);
}

ibValueModelTreeDataObject::ibValueDataObjectTreeReturnLine::~ibValueDataObjectTreeReturnLine()
{
}

void ibValueModelTreeDataObject::ibValueDataObjectTreeReturnLine::FillMembers(ibMemberTable& helper) const
{
	const ibValueMetaObjectGenericData* metaObject = m_ownerTable->GetMetaObject();
	for (const auto object : metaObject->GetGenericAttributeArrayObject()) {
		helper.AppendProp(
			object->GetName(),
			object->GetMetaID()
		);
	}
}

bool ibValueModelTreeDataObject::ibValueDataObjectTreeReturnLine::SetPropVal(const long lPropNum, const ibValue& varPropVal)
{
	return false;
}

bool ibValueModelTreeDataObject::ibValueDataObjectTreeReturnLine::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	if (appData->DesignerMode())
		return false;
	const ibMetaID& id = m_members.GetPropData(lPropNum);
	ibValueTreeNode* node = m_ownerTable->GetViewData<ibValueTreeNode>(m_lineItem);
	if (node != nullptr) {
		return node->GetValue(id, pvarPropVal);
	}
	return false;
}

/////////////////////////////////////////////////////////////////////////////////////////////

ibDataViewItem ibValueListDataObjectEnumRef::FindRowValue(const ibValue& varValue, const wxString& colName) const
{
	// Paged model: rows live in the control's deque, not on the model
	// (m_nodeValues stays empty).  Build a stub row carrying just
	// m_objGuid; ibValueTableEnumRow::IsEqualTo lets PagedBootstrap's
	// freshly-fetched row match it on key, restoring focus without
	// requiring the rest of the row to be populated here.
	ibValueReferenceDataObject* pRefData = nullptr;
	if (!varValue.ConvertToValue(pRefData) || pRefData == nullptr)
		return ibDataViewItem();
	if (!pRefData->GetGuid().isValid())
		return ibDataViewItem();
	auto* stub = new ibValueTableEnumRow(pRefData->GetGuid());
	ibDataViewItem item(stub);
	stub->DecRef();
	return item;
}

ibValueListDataObjectEnumRef::ibValueListDataObjectEnumRef(const ibValueMetaObjectRecordDataEnumRef* metaObject, const ibFormID& formType, bool choiceMode) :
	ibValueListDataObject(metaObject, formType, choiceMode), m_metaObject(metaObject), m_choiceMode(choiceMode)
{
	m_members.Bind(this, &ibValueListDataObjectEnumRef::FillMembers);
	ibValueListDataObject::AppendSort(m_metaObject->GetDataOrder(), true, true, true);
	ibValueListDataObject::AppendSort(m_metaObject->GetDataReference(), true, true, true);
}

const ibSourceExplorer* ibValueListDataObjectEnumRef::GetSourceExplorer() const
{
	m_sourceExplorer.Reset(
		m_metaObject->GetName(), m_metaObject->GetSynonym(), m_metaObject->GetMetaID(),
		GetClassType(), true, true
	);
	m_sourceExplorer.AppendColumn(m_metaObject->GetDataReference(), true, true);
	return &m_sourceExplorer;
}

bool ibValueListDataObjectEnumRef::GetValueByMetaID(const ibMetaID& id, ibValue& pvarMetaVal) const
{
	if (id == m_metaObject->GetMetaID()) {
		pvarMetaVal = ibValue::GetValue(true);
		return true;
	}
	return false;
}

//events 
void ibValueListDataObjectEnumRef::ChooseValue(ibBackendValueForm* srcForm)
{
	ibValueTableEnumRow* node = GetViewData<ibValueTableEnumRow>(GetSelection());
	if (node == nullptr)
		return;
	wxASSERT(srcForm);
	ibValueReferenceDataObject* dataValueRef = m_metaObject->FindObjectValue(node->GetGuid());
	if (dataValueRef != nullptr) {
		ibValue selectedValue = dataValueRef->GetValue();
		srcForm->NotifyChoice(selectedValue);
	}
}

#include "backend/objCtor.h"

ibClassID ibValueListDataObjectEnumRef::GetClassType() const
{
	const ibCtorMetaValueType* clsFactory =
		m_metaObject->GetTypeCtor(ibCtorObjectMetaType::ibCtorObjectMetaType_List);
	wxASSERT(clsFactory);
	return clsFactory->GetClassType();
}

wxString ibValueListDataObjectEnumRef::GetClassName() const
{
	const ibCtorMetaValueType* clsFactory =
		m_metaObject->GetTypeCtor(ibCtorObjectMetaType::ibCtorObjectMetaType_List);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

wxString ibValueListDataObjectEnumRef::GetString() const
{
	const ibCtorMetaValueType* clsFactory =
		m_metaObject->GetTypeCtor(ibCtorObjectMetaType::ibCtorObjectMetaType_List);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

/////////////////////////////////////////////////////////////////////////////////////////////

ibDataViewItem ibValueListDataObjectRef::FindRowValue(const ibValue& varValue, const wxString& colName) const
{
	// Paged model: rows live in the control's deque, not on the model.
	// Build a stub row carrying:
	//   * m_objGuid               — drives IsEqualTo against a freshly-
	//                                fetched row, restoring focus after
	//                                refetch.
	//   * m_nodeValues sort cols  — drive BuildRefAnchor's cursor
	//                                predicate.  Empty values would
	//                                bind as NULL in the SQL composite
	//                                predicate (`(c1,c2,...) >= (?,?,...)`)
	//                                and exclude all rows → empty table.
	//                                Pull real values for each enabled
	//                                non-reference sort column from the
	//                                reference (lazy DB load via
	//                                GetValueByMetaID) so the cursor
	//                                positions on the new row's sort
	//                                tuple.
	ibValueReferenceDataObject* pRefData = nullptr;
	if (!varValue.ConvertToValue(pRefData) || pRefData == nullptr)
		return ibDataViewItem();
	if (!pRefData->GetGuid().isValid())
		return ibDataViewItem();
	auto* stub = new ibValueTableListRow(pRefData->GetGuid());
	for (const auto& s : m_sortOrder.m_sorts) {
		if (!s.m_sortEnable) continue;
		if (m_metaObject->IsDataReference(s.m_sortModel)) continue;
		ibValue v;
		if (pRefData->GetValueByMetaID(s.m_sortModel, v))
			stub->AppendTableValue(s.m_sortModel, v);
	}
	ibDataViewItem item(stub);   // IncRef → 2
	stub->DecRef();              // refcount=1, owned by item
	return item;
}

ibValueListDataObjectRef::ibValueListDataObjectRef(const ibValueMetaObjectRecordDataMutableRef* metaObject, const ibFormID& formType, bool choiceMode) :
	ibValueListDataObject(metaObject, formType, choiceMode), m_metaObject(metaObject), m_choiceMode(choiceMode)
{
	m_members.Bind(this, &ibValueListDataObjectRef::FillMembers);
}

const ibSourceExplorer* ibValueListDataObjectRef::GetSourceExplorer() const
{
	m_sourceExplorer.Reset(
		m_metaObject->GetName(), m_metaObject->GetSynonym(), m_metaObject->GetMetaID(),
		GetClassType(), true, true
	);

	for (const auto object : m_metaObject->GetGenericAttributeArrayObject()) {
		if (m_metaObject->IsDataReference(object->GetMetaID()))
			m_sourceExplorer.AppendColumn(object, true, false);
		else if (m_metaObject->IsDataDeletionMark(object->GetMetaID()))
			m_sourceExplorer.AppendColumn(object, true, false);
		else if (m_metaObject->IsDataVersion(object->GetMetaID()))
			m_sourceExplorer.AppendColumn(object, true, false);
		else
			m_sourceExplorer.AppendColumn(object, true, true);
	}

	return &m_sourceExplorer;
}

bool ibValueListDataObjectRef::GetValueByMetaID(const ibMetaID& id, ibValue& pvarMetaVal) const
{
	if (id == m_metaObject->GetMetaID()) {
		pvarMetaVal = ibValue::GetValue(true);
		return true;
	}
	return false;
}

//events 
void ibValueListDataObjectRef::AddValue(unsigned int before)
{
	ibValueMetaObjectRecordDataMutableRef* metaObject = nullptr;
	if (m_metaObject->ConvertToValue(metaObject)) {

		try {
			ibValuePtr<ibValueRecordDataObjectRef> dataValueObject(metaObject->CreateObjectValue());
			if (dataValueObject != nullptr)
				dataValueObject->ShowFormValue(wxEmptyString, dynamic_cast<ibBackendControlFrame*>(ibBackendValueForm::FindFormBySourceUniqueKey(m_objGuid)));
		}
		catch (const ibBackendCoreException& err) {
			ibValueSystemFunction::Alert(err.GetErrorDescription());
		}
		catch (...) {
			wxLogError(wxT("objectList: unhandled non-ibBackend exception swallowed"));
		}
	}
}

void ibValueListDataObjectRef::CopyValue()
{
	ibValueMetaObjectRecordDataMutableRef* metaObject = nullptr;
	if (m_metaObject->ConvertToValue(metaObject)) {

		ibValueTableListRow* node = GetViewData<ibValueTableListRow>(GetSelection());
		if (node == nullptr)
			return;

		try {
			ibValuePtr<ibValueRecordDataObjectRef> dataValueObject(metaObject->CopyObjectValue(node->GetGuid()));
			if (dataValueObject != nullptr) dataValueObject->ShowFormValue(wxEmptyString, dynamic_cast<ibBackendControlFrame*>(ibBackendValueForm::FindFormBySourceUniqueKey(m_objGuid)));
		}
		catch (const ibBackendCoreException& err) {
			ibValueSystemFunction::Alert(err.GetErrorDescription());
		}
		catch (...) {
			wxLogError(wxT("objectList: unhandled non-ibBackend exception swallowed"));
		}
	}
}

void ibValueListDataObjectRef::EditValue()
{
	ibValueMetaObjectRecordDataMutableRef* metaObject = nullptr;
	if (m_metaObject->ConvertToValue(metaObject)) {

		ibValueTableListRow* node = GetViewData<ibValueTableListRow>(GetSelection());
		if (node == nullptr)
			return;

		try {
			ibValueRecordDataObjectRef* dataValueObject(metaObject->CreateObjectValue(node->GetGuid()));
			if (dataValueObject != nullptr) dataValueObject->ShowFormValue(wxEmptyString, dynamic_cast<ibBackendControlFrame*>(ibBackendValueForm::FindFormBySourceUniqueKey(m_objGuid)));
		}
		catch (const ibBackendCoreException& err) {
			ibValueSystemFunction::Alert(err.GetErrorDescription());
		}
		catch (...) {
			wxLogError(wxT("objectList: unhandled non-ibBackend exception swallowed"));
		}
	}
}

void ibValueListDataObjectRef::DeleteValue()
{
	ibValueMetaObjectRecordDataMutableRef* metaObject = nullptr;
	if (m_metaObject->ConvertToValue(metaObject)) {

		ibValueTableListRow* node = GetViewData<ibValueTableListRow>(GetSelection());
		if (node == nullptr)
			return;

		try {
			ibValuePtr<ibValueRecordDataObjectRef> dataValueObject(metaObject->CreateObjectValue(node->GetGuid()));
			if (dataValueObject != nullptr)
				dataValueObject->DeleteObject();
			ibBackendValueForm* valueListForm = ibBackendValueForm::FindFormBySourceUniqueKey(m_objGuid);
			if (valueListForm != nullptr)
				valueListForm->UpdateForm();
		}
		catch (const ibBackendCoreException& err) {
			ibValueSystemFunction::Alert(err.GetErrorDescription());
		}
		catch (...) {
			wxLogError(wxT("objectList: unhandled non-ibBackend exception swallowed"));
		}
	}
}

void ibValueListDataObjectRef::MarkAsDeleteValue()
{
	ibValueMetaObjectRecordDataMutableRef* metaObject = nullptr;
	if (m_metaObject->ConvertToValue(metaObject)) {

		ibValueTableListRow* node = GetViewData<ibValueTableListRow>(GetSelection());
		if (node == nullptr)
			return;

		try {
			ibValuePtr<ibValueRecordDataObjectRef> dataValueObject(metaObject->CreateObjectValue(node->GetGuid()));
			if (dataValueObject != nullptr)
				dataValueObject->SetDeletionMark(true);
			ibBackendValueForm* valueListForm = ibBackendValueForm::FindFormBySourceUniqueKey(m_objGuid);
			if (valueListForm != nullptr)
				valueListForm->UpdateForm();
		}
		catch (const ibBackendCoreException& err) {
			ibValueSystemFunction::Alert(err.GetErrorDescription());
		}
		catch (...) {
			wxLogError(wxT("objectList: unhandled non-ibBackend exception swallowed"));
		}
	}
}

void ibValueListDataObjectRef::ChooseValue(ibBackendValueForm* srcForm)
{
	ibValueTableListRow* node = GetViewData<ibValueTableListRow>(GetSelection());
	if (node == nullptr)
		return;

	wxASSERT(srcForm);

	try {
		ibValuePtr<ibValueReferenceDataObject> dataValueRef(m_metaObject->FindObjectValue(node->GetGuid()));
		if (dataValueRef != nullptr) {
			ibValue selectedValue = dataValueRef->GetValue();
			srcForm->NotifyChoice(selectedValue);
		}
	}
	catch (const ibBackendCoreException& err) {
		ibValueSystemFunction::Alert(err.GetErrorDescription());
	}
	catch (...) {
	}
}

ibClassID ibValueListDataObjectRef::GetClassType() const
{
	const ibCtorMetaValueType* clsFactory =
		m_metaObject->GetTypeCtor(ibCtorObjectMetaType::ibCtorObjectMetaType_List);
	wxASSERT(clsFactory);
	return clsFactory->GetClassType();
}

wxString ibValueListDataObjectRef::GetClassName() const
{
	const ibCtorMetaValueType* clsFactory =
		m_metaObject->GetTypeCtor(ibCtorObjectMetaType::ibCtorObjectMetaType_List);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

wxString ibValueListDataObjectRef::GetString() const
{
	const ibCtorMetaValueType* clsFactory =
		m_metaObject->GetTypeCtor(ibCtorObjectMetaType::ibCtorObjectMetaType_List);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

/////////////////////////////////////////////////////////////////////////////////////////////

ibDataViewItem ibValueModelTreeDataObjectFolderRef::FindRowValue(const ibValue& varValue, const wxString& /*colName*/) const
{
	// Paged tree: rows aren't materialized in-memory, but we can
	// resolve a reference-typed value to its row by GUID via the
	// same one-SQL `WHERE uuid IN (?)` path used for ancestor
	// breadcrumb construction.  Used by the post-Save selection-
	// restore cascade in tableBox::OnIdle.  Non-reference values
	// (column-value lookup) aren't supported on the paged path.
	ibValueReferenceDataObject* refData = nullptr;
	if (!varValue.ConvertToValue(refData) || refData == nullptr)
		return ibDataViewItem();
	const ibGuid g = refData->GetGuid();
	if (!g.isValid()) return ibDataViewItem();
	auto rows = LoadRowsByGuids({ g });
	if (rows.empty()) return ibDataViewItem();
	auto* row = rows[0];
	ibDataViewItem item(row);   // IncRef → 2
	row->DecRef();               // refcount=1, owned by item
	return item;
}

ibValueModelTreeDataObjectFolderRef::ibValueModelTreeDataObjectFolderRef(const ibValueMetaObjectRecordDataHierarchyMutableRef* metaObject, const ibFormID& formType,
	int listMode, bool choiceMode) : ibValueModelTreeDataObject(metaObject, formType, choiceMode),
	m_metaObject(metaObject), m_listMode(listMode), m_choiceMode(choiceMode)
{
	m_members.Bind(this, &ibValueModelTreeDataObjectFolderRef::FillMembers);
	ibValueModelTreeDataObject::AppendSort(m_metaObject->GetDataCode(), true, false);
	ibValueModelTreeDataObject::AppendSort(m_metaObject->GetDataDescription(), true);
	// Reference (uuid PK) sort = system: always enabled, OnSortColumnChanged
	// preserves it as the cursor tiebreaker so the `uuid > ? / uuid < ?`
	// predicate stays meaningful even when the user picks a different
	// column-sort.
	ibValueModelTreeDataObject::AppendSort(m_metaObject->GetDataReference(),
		/*ascending=*/true, /*use=*/true,
		/*system=*/true);
}

const ibSourceExplorer* ibValueModelTreeDataObjectFolderRef::GetSourceExplorer() const
{
	m_sourceExplorer.Reset(
		m_metaObject->GetName(), m_metaObject->GetSynonym(), m_metaObject->GetMetaID(),
		GetClassType(), true, true
	);

	for (const auto object : m_metaObject->GetGenericAttributeArrayObject()) {
		if (m_metaObject->IsDataReference(object->GetMetaID()))
			m_sourceExplorer.AppendColumn(object, true, false);
		else if (m_metaObject->IsDataDeletionMark(object->GetMetaID()))
			m_sourceExplorer.AppendColumn(object, true, false);
		else if (m_metaObject->IsDataPredefinedName(object->GetMetaID()))
			m_sourceExplorer.AppendColumn(object, true, false);
		else if (m_metaObject->IsDataVersion(object->GetMetaID()))
			m_sourceExplorer.AppendColumn(object, true, false);
		else if (m_metaObject->IsDataParent(object->GetMetaID()))
			m_sourceExplorer.AppendColumn(object, true, false);
		else if (m_metaObject->IsDataFolder(object->GetMetaID()))
			m_sourceExplorer.AppendColumn(object, true, false);
		else
			m_sourceExplorer.AppendColumn(object, true, true);
	}

	return &m_sourceExplorer;
}

bool ibValueModelTreeDataObjectFolderRef::GetValueByMetaID(const ibMetaID& id, ibValue& pvarMetaVal) const
{
	if (id == m_metaObject->GetMetaID()) {
		pvarMetaVal = ibValue::GetValue(true);
		return true;
	}
	return false;
}

//events 
void ibValueModelTreeDataObjectFolderRef::ResolveParentForNew(ibValue& outParent) const
{
	// Parent inheritance — three sources, in order:
	//   1. Current selection (Tree mode + List with row selected): use
	//      selected folder itself, or selected item's parent.
	//   2. Drill chain (Hierarchical drill, no selection inside folder):
	//      the deepest crumb is the folder we're inside — use it as
	//      parent so a fresh item lands here, not at the root.
	//   3. Fallback empty: top-level catalog root.
	ibValueTreeListNode* node = GetViewData<ibValueTreeListNode>(GetSelection());
	if (node != nullptr) {
		ibValue isFolder = true;
		node->GetValue(*m_metaObject->GetDataIsFolder(), isFolder);
		if (!isFolder.GetBoolean())
			node->GetValue(*m_metaObject->GetDataParent(), outParent);
		else
			node->GetValue(*m_metaObject->GetDataReference(), outParent);
		return;
	}
	ibValueTreeListNode* drillNode = GetViewData<ibValueTreeListNode>(GetDrillParent());
	if (drillNode != nullptr)
		drillNode->GetValue(*m_metaObject->GetDataReference(), outParent);
}

void ibValueModelTreeDataObjectFolderRef::AddValue(unsigned int before)
{
	ibValue cParent;
	ResolveParentForNew(cParent);

	ibValuePtr<ibValueRecordDataObjectHierarchyRef> dataValueFolderObject(m_metaObject->CreateObjectValue(ibObjectMode::OBJECT_ITEM));

	if (dataValueFolderObject != nullptr) {
		dataValueFolderObject->SetValueByMetaID(*m_metaObject->GetDataParent(), cParent);
		dataValueFolderObject->ShowFormValue(wxEmptyString, dynamic_cast<ibBackendControlFrame*>(ibBackendValueForm::FindFormBySourceUniqueKey(m_objGuid)));
	}
}

void ibValueModelTreeDataObjectFolderRef::AddFolderValue(unsigned int before)
{
	ibValue cParent;
	ResolveParentForNew(cParent);

	try {
		ibValuePtr<ibValueRecordDataObjectHierarchyRef> dataValueFolderObject(m_metaObject->CreateObjectValue(ibObjectMode::OBJECT_FOLDER));
		if (dataValueFolderObject != nullptr) {
			dataValueFolderObject->SetValueByMetaID(*m_metaObject->GetDataParent(), cParent);
			dataValueFolderObject->ShowFormValue(wxEmptyString, dynamic_cast<ibBackendControlFrame*>(ibBackendValueForm::FindFormBySourceUniqueKey(m_objGuid)));
		}
	}
	catch (const ibBackendCoreException& err) {
		ibValueSystemFunction::Alert(err.GetErrorDescription());
	}
	catch (...) {
	}
}

void ibValueModelTreeDataObjectFolderRef::CopyValue()
{
	ibValueTreeListNode* node = GetViewData<ibValueTreeListNode>(GetSelection());
	if (node == nullptr)
		return;

	ibValue isFolder = false;
	node->GetValue(*m_metaObject->GetDataIsFolder(), isFolder);

	try {
		ibValuePtr<ibValueRecordDataObjectHierarchyRef> dataValueFolderObject(m_metaObject->CopyObjectValue(isFolder.GetBoolean() ? ibObjectMode::OBJECT_FOLDER : ibObjectMode::OBJECT_ITEM, node->GetGuid()));
		if (dataValueFolderObject != nullptr)
			dataValueFolderObject->ShowFormValue(wxEmptyString, dynamic_cast<ibBackendControlFrame*>(ibBackendValueForm::FindFormBySourceUniqueKey(m_objGuid)));
	}
	catch (const ibBackendCoreException& err) {
		ibValueSystemFunction::Alert(err.GetErrorDescription());
	}
	catch (...) {
	}
}

void ibValueModelTreeDataObjectFolderRef::EditValue()
{
	ibValueTreeListNode* node = GetViewData<ibValueTreeListNode>(GetSelection());
	if (node == nullptr)
		return;

	ibValue isFolder = false;
	node->GetValue(*m_metaObject->GetDataIsFolder(), isFolder);

	try {
		ibValuePtr<ibValueRecordDataObjectHierarchyRef> dataValueFolderObject(m_metaObject->CreateObjectValue(isFolder.GetBoolean() ? ibObjectMode::OBJECT_FOLDER : ibObjectMode::OBJECT_ITEM, node->GetGuid()));
		if (dataValueFolderObject != nullptr) dataValueFolderObject->ShowFormValue(wxEmptyString, dynamic_cast<ibBackendControlFrame*>(ibBackendValueForm::FindFormBySourceUniqueKey(m_objGuid)));
	}
	catch (const ibBackendCoreException& err) {
		ibValueSystemFunction::Alert(err.GetErrorDescription());
	}
	catch (...) {
	}
}

void ibValueModelTreeDataObjectFolderRef::DeleteValue()
{
	ibValueTreeListNode* node = GetViewData<ibValueTreeListNode>(GetSelection());
	if (node == nullptr)
		return;

	ibValue isFolder = false;
	node->GetValue(*m_metaObject->GetDataIsFolder(), isFolder);

	try {
		ibValuePtr<ibValueRecordDataObjectHierarchyRef> dataValueFolderObject(m_metaObject->CreateObjectValue(isFolder.GetBoolean() ? ibObjectMode::OBJECT_FOLDER : ibObjectMode::OBJECT_ITEM, node->GetGuid()));
		if (dataValueFolderObject != nullptr)
			dataValueFolderObject->DeleteObject();
		ibBackendValueForm* valueListForm = ibBackendValueForm::FindFormBySourceUniqueKey(m_objGuid);
		if (valueListForm != nullptr) valueListForm->UpdateForm();
	}
	catch (const ibBackendCoreException& err) {
		ibValueSystemFunction::Alert(err.GetErrorDescription());
	}
	catch (...) {
	}
}

void ibValueModelTreeDataObjectFolderRef::MarkAsDeleteValue()
{
	ibValueMetaObjectRecordDataHierarchyMutableRef* metaObject = nullptr;
	if (m_metaObject->ConvertToValue(metaObject)) {

		ibValueTreeListNode* node = GetViewData<ibValueTreeListNode>(GetSelection());
		if (node == nullptr)
			return;

		ibValue isFolder = false;
		node->GetValue(*m_metaObject->GetDataIsFolder(), isFolder);

		try {
			ibValuePtr<ibValueRecordDataObjectHierarchyRef> dataValueFolderObject(metaObject->CreateObjectValue(isFolder.GetBoolean() ? ibObjectMode::OBJECT_FOLDER : ibObjectMode::OBJECT_ITEM, node->GetGuid()));
			if (dataValueFolderObject != nullptr)
				dataValueFolderObject->SetDeletionMark(true);
			ibBackendValueForm* valueListForm = ibBackendValueForm::FindFormBySourceUniqueKey(m_objGuid);
			if (valueListForm != nullptr) valueListForm->UpdateForm();
		}
		catch (const ibBackendCoreException& err) {
			ibValueSystemFunction::Alert(err.GetErrorDescription());
		}
		catch (...) {
			wxLogError(wxT("objectList: unhandled non-ibBackend exception swallowed"));
		}
	}
}

void ibValueModelTreeDataObjectFolderRef::ChooseValue(ibBackendValueForm* srcForm)
{
	ibValueTreeListNode* node = GetViewData<ibValueTreeListNode>(GetSelection());
	if (node == nullptr)
		return;

	wxASSERT(srcForm);

	ibValue cIsFolder = false;
	node->GetValue(*m_metaObject->GetDataIsFolder(), cIsFolder);

	try {
		ibValuePtr<ibValueReferenceDataObject> dataValueFolderRef(m_metaObject->FindObjectValue(node->GetGuid()));
		ibValue selectedValue = dataValueFolderRef != nullptr ? dataValueFolderRef->GetValue() : ibValue();
		if (m_listMode == LIST_FOLDER && cIsFolder.GetBoolean())
			srcForm->NotifyChoice(selectedValue);
		else if (m_listMode == LIST_ITEM && !cIsFolder.GetBoolean())
			srcForm->NotifyChoice(selectedValue);
		else if (m_listMode == LIST_ITEM_FOLDER)
			srcForm->NotifyChoice(selectedValue);
	}
	catch (const ibBackendCoreException& err) {
		ibValueSystemFunction::Alert(err.GetErrorDescription());
	}
	catch (...) {
	}
}

#include "backend/objCtor.h"

ibClassID ibValueModelTreeDataObjectFolderRef::GetClassType() const
{
	const ibCtorMetaValueType* clsFactory =
		m_metaObject->GetTypeCtor(ibCtorObjectMetaType::ibCtorObjectMetaType_List);
	wxASSERT(clsFactory);
	return clsFactory->GetClassType();
}

wxString ibValueModelTreeDataObjectFolderRef::GetClassName() const
{
	const ibCtorMetaValueType* clsFactory =
		m_metaObject->GetTypeCtor(ibCtorObjectMetaType::ibCtorObjectMetaType_List);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

wxString ibValueModelTreeDataObjectFolderRef::GetString() const
{
	const ibCtorMetaValueType* clsFactory =
		m_metaObject->GetTypeCtor(ibCtorObjectMetaType::ibCtorObjectMetaType_List);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

/////////////////////////////////////////////////////////////////////////////////////////////

ibDataViewItem ibValueListRegisterObject::FindRowValue(const ibValue& varValue, const wxString& colName) const
{
	// Paged model: rows live in the control's deque, not on the model.
	// Build a stub key-row carrying:
	//   * m_nodeKeys                  — identity (recorder + line for
	//                                    HasRecorder, dimensions
	//                                    otherwise).  Drives
	//                                    ibValueTableKeyRow::IsEqualTo
	//                                    against the freshly-fetched
	//                                    row, restoring focus.
	//   * m_nodeValues effective cols — drive BuildRegisterAnchor's
	//                                    cursor predicate.  Empty
	//                                    values bind as NULL in the
	//                                    composite predicate and
	//                                    exclude all rows → empty
	//                                    table.  Pull real values for
	//                                    each effective sort column
	//                                    (user sort + identity tail)
	//                                    from the record manager so
	//                                    the cursor positions on the
	//                                    new row's tuple.
	ibValueRecordManagerObject* pRefData = nullptr;
	if (!varValue.ConvertToValue(pRefData) || pRefData == nullptr)
		return ibDataViewItem();

	auto* stub = new ibValueTableKeyRow;
	// Identity columns → m_nodeKeys.
	if (m_metaObject->HasRecorder()) {
		if (auto* attrRec = m_metaObject->GetRegisterRecorder())
			stub->AppendNodeValue(attrRec->GetMetaID(),
				pRefData->GetValueByMetaID(attrRec->GetMetaID()));
		if (auto* attrLine = m_metaObject->GetRegisterLineNumber())
			stub->AppendNodeValue(attrLine->GetMetaID(),
				pRefData->GetValueByMetaID(attrLine->GetMetaID()));
	}
	else {
		for (auto& dim : m_metaObject->GetGenericDimensionArrayObject())
			stub->AppendNodeValue(dim->GetMetaID(),
				pRefData->GetValueByMetaID(dim->GetMetaID()));
	}
	// Effective sort columns → m_nodeValues (read by BuildRegisterAnchor). The sort item
	// carries a COLUMN, and a column self-describes its read key (GetColumnId == the metaID
	// for an attribute column) — no ResolveAttribute, no downcast.
	for (const auto& c : EffectiveSortOrder()) {
		if (c.m_col == nullptr) continue;
		const ibMetaID metaID = c.m_col->GetColumnId();
		ibValue v;
		if (pRefData->GetValueByMetaID(metaID, v))
			stub->AppendTableValue(metaID, v);
	}

	ibDataViewItem item(stub);   // IncRef → 2
	stub->DecRef();              // refcount=1, owned by item
	return item;
}

ibValueListRegisterObject::ibValueListRegisterObject(const ibValueMetaObjectRegisterData* metaObject, const ibFormID& formType) :
	ibValueListDataObject(metaObject, formType), m_metaObject(metaObject)
{
	m_members.Bind(this, &ibValueListRegisterObject::FillMembers);
	if (m_metaObject->HasRecorder()) {
		if (m_metaObject->HasPeriod()) ibValueListDataObject::AppendSort(metaObject->GetRegisterPeriod());
		ibValueListDataObject::AppendSort(metaObject->GetRegisterRecorder());
		ibValueListDataObject::AppendSort(metaObject->GetRegisterLineNumber());
	}
	else if (m_metaObject->HasPeriod()) {
		ibValueListDataObject::AppendSort(metaObject->GetRegisterPeriod());
	}

	for (auto& dimension : m_metaObject->GetGenericDimensionArrayObject()) {
		ibValueListDataObject::AppendSort(dimension, true, true, true);
	}
}

const ibSourceExplorer* ibValueListRegisterObject::GetSourceExplorer() const
{
	m_sourceExplorer.Reset(
		m_metaObject->GetName(), m_metaObject->GetSynonym(), m_metaObject->GetMetaID(),
		GetClassType(), true, true
	);

	for (const auto object : m_metaObject->GetGenericAttributeArrayObject()) {
		m_sourceExplorer.AppendColumn(object);
	}

	return &m_sourceExplorer;
}

bool ibValueListRegisterObject::GetValueByMetaID(const ibMetaID& id, ibValue& pvarMetaVal) const
{
	if (id == m_metaObject->GetMetaID()) {
		pvarMetaVal = ibValue::GetValue(true);
		return true;
	}
	return false;
}

//events 
void ibValueListRegisterObject::AddValue(unsigned int before)
{
	if (m_metaObject != nullptr && m_metaObject->HasRecordManager()) {
		try {
			ibValuePtr<ibValueRecordManagerObject> dataValueObject(m_metaObject->CreateRecordManagerObjectValue());
			if (dataValueObject != nullptr)
				dataValueObject->ShowFormValue(wxEmptyString, dynamic_cast<ibBackendControlFrame*>(ibBackendValueForm::FindFormBySourceUniqueKey(m_objGuid)));
		}
		catch (const ibBackendCoreException& err) {
			ibValueSystemFunction::Alert(err.GetErrorDescription());
		}
		catch (...) {
			wxLogError(wxT("objectList: unhandled non-ibBackend exception swallowed"));
		}
	}
}

void ibValueListRegisterObject::CopyValue()
{
	if (m_metaObject != nullptr) {
		ibValueTableKeyRow* node = GetViewData<ibValueTableKeyRow>(GetSelection());
		if (node == nullptr) return;
		if (m_metaObject->HasRecordManager()) {
			try {
				ibValuePtr<ibValueRecordManagerObject> dataValueObject(m_metaObject->CopyRecordManagerObjectValue(node->GetUniquePairKey(m_metaObject)));
				if (dataValueObject != nullptr)
					dataValueObject->ShowFormValue(wxEmptyString, dynamic_cast<ibBackendControlFrame*>(ibBackendValueForm::FindFormBySourceUniqueKey(m_objGuid)));
			}
			catch (const ibBackendCoreException& err) {
				ibValueSystemFunction::Alert(err.GetErrorDescription());
			}
			catch (...) {
			}
		}
	}
}

void ibValueListRegisterObject::EditValue()
{
	if (m_metaObject != nullptr) {
		ibValueTableKeyRow* node = GetViewData<ibValueTableKeyRow>(GetSelection());
		if (node == nullptr) return;
		if (m_metaObject->HasRecordManager()) {
			try {
				ibValuePtr<ibValueRecordManagerObject> dataValueObject(m_metaObject->CreateRecordManagerObjectValue(node->GetUniquePairKey(m_metaObject)));
				if (dataValueObject != nullptr)
					dataValueObject->ShowFormValue(wxEmptyString, dynamic_cast<ibBackendControlFrame*>(ibBackendValueForm::FindFormBySourceUniqueKey(m_objGuid)));
			}
			catch (const ibBackendCoreException& err) {
				ibValueSystemFunction::Alert(err.GetErrorDescription());
			}
			catch (...) {
			}
		}
		else {
			try {
				const ibValueMetaObjectAttributePredefined* metaRecorder = m_metaObject->GetRegisterRecorder();
				if (metaRecorder != nullptr) {
					ibValue recorderVal = node->GetTableValue(metaRecorder->GetMetaID());
					recorderVal.ShowValue();
				}
			}
			catch (const ibBackendCoreException& err) {
				ibValueSystemFunction::Alert(err.GetErrorDescription());
			}
			catch (...) {
			}
		}
	}
}

void ibValueListRegisterObject::DeleteValue()
{
	if (m_metaObject != nullptr) {
		ibValueTableKeyRow* node = GetViewData<ibValueTableKeyRow>(GetSelection());
		if (node == nullptr) return;
		if (m_metaObject->HasRecordManager()) {
			try {
				ibValuePtr<ibValueRecordManagerObject> dataValueObject(m_metaObject->CreateRecordManagerObjectValue(node->GetUniquePairKey(m_metaObject)));
				if (dataValueObject != nullptr)
					dataValueObject->DeleteRegister();
				ibBackendValueForm* valueListForm = ibBackendValueForm::FindFormBySourceUniqueKey(m_objGuid);
				if (valueListForm != nullptr) valueListForm->UpdateForm();
			}
			catch (const ibBackendCoreException& err) {
				ibValueSystemFunction::Alert(err.GetErrorDescription());
			}
			catch (...) {
			}
		}
	}
}

ibClassID ibValueListRegisterObject::GetClassType() const
{
	const ibCtorMetaValueType* clsFactory =
		m_metaObject->GetTypeCtor(ibCtorObjectMetaType::ibCtorObjectMetaType_List);
	wxASSERT(clsFactory);
	return clsFactory->GetClassType();
}

wxString ibValueListRegisterObject::GetClassName() const
{
	const ibCtorMetaValueType* clsFactory =
		m_metaObject->GetTypeCtor(ibCtorObjectMetaType::ibCtorObjectMetaType_List);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

wxString ibValueListRegisterObject::GetString() const
{
	const ibCtorMetaValueType* clsFactory =
		m_metaObject->GetTypeCtor(ibCtorObjectMetaType::ibCtorObjectMetaType_List);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

//****************************************************************************
//*                              Support methods                             *
//****************************************************************************

void ibValueListDataObjectEnumRef::FillMembers(ibMemberTable& helper) const
{
	helper.AppendProp(wxT("ChoiceMode"));
	helper.AppendProc(wxT("Refresh"), wxT("Refresh()"));
}

bool ibValueListDataObjectEnumRef::CallAsProc(const long lMethodNum, ibValue** paParams, const long lSizeArray)
{
	switch (lMethodNum)
	{
	case enRefresh:
		RefetchAll();
		return true;
	}
	return false;
}

////////////////////////////////////////////////////////////////////////////////

void ibValueListDataObjectRef::FillMembers(ibMemberTable& helper) const
{
	helper.AppendProp(wxT("ChoiceMode"));
	helper.AppendProc(wxT("Refresh"), wxT("Refresh()"));
}

bool ibValueListDataObjectRef::CallAsProc(const long lMethodNum, ibValue** paParams, const long lSizeArray)
{
	switch (lMethodNum)
	{
	case enRefresh:
		RefetchAll();
		return true;
	}
	return false;
}

////////////////////////////////////////////////////////////////////////////////

void ibValueModelTreeDataObjectFolderRef::FillMembers(ibMemberTable& helper) const
{
	helper.AppendProp(wxT("ChoiceMode"));
	helper.AppendProc(wxT("Refresh"), wxT("Refresh()"));
}

bool ibValueModelTreeDataObjectFolderRef::CallAsProc(const long lMethodNum, ibValue** paParams, const long lSizeArray)
{
	switch (lMethodNum)
	{
	case enRefresh:
		RefetchAll();
		return true;
	}
	return false;
}

////////////////////////////////////////////////////////////////////////////////

void ibValueListRegisterObject::FillMembers(ibMemberTable& helper) const
{
	helper.AppendProc(wxT("Refresh"), wxT("Refresh()"));
}

bool ibValueListRegisterObject::CallAsProc(const long lMethodNum, ibValue** paParams, const long lSizeArray)
{
	switch (lMethodNum)
	{
	case enRefresh:
		RefetchAll();
		return true;
	}
	return false;
}

//****************************************************************************
//*                              Override object                          *
//****************************************************************************

enum
{
	enChoiceMode
};

bool ibValueListDataObjectEnumRef::SetPropVal(const long lPropNum, const ibValue& value) //setting attribute
{
	return false;
}

bool ibValueListDataObjectEnumRef::GetPropVal(const long lPropNum, ibValue& pvarPropVal) //attribute value
{
	if (lPropNum == enChoiceMode) {
		pvarPropVal = m_choiceMode;
		return true;
	}
	return false;
}

bool ibValueListDataObjectRef::SetPropVal(const long lPropNum, const ibValue& value) //setting attribute
{
	return false;
}

bool ibValueListDataObjectRef::GetPropVal(const long lPropNum, ibValue& pvarPropVal) //attribute value
{
	if (lPropNum == enChoiceMode) {
		pvarPropVal = m_choiceMode;
		return true;
	}
	return false;
}

bool ibValueModelTreeDataObjectFolderRef::SetPropVal(const long lPropNum, const ibValue& value) //setting attribute
{
	return false;
}

bool ibValueModelTreeDataObjectFolderRef::GetPropVal(const long lPropNum, ibValue& pvarPropVal) //attribute value
{
	if (lPropNum == enChoiceMode) {
		pvarPropVal = m_choiceMode;
		return true;
	}
	return false;
}

bool ibValueListRegisterObject::SetPropVal(const long lPropNum, const ibValue& value) //setting attribute
{
	return false;
}

bool ibValueListRegisterObject::GetPropVal(const long lPropNum, ibValue& pvarPropVal) //attribute value
{
	return false;
}

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

SYSTEM_TYPE_REGISTER(ibValueListDataObject::ibValueDataObjectListColumnCollection, "ListColumn", system_to_clsid("VL_LVC"));
SYSTEM_TYPE_REGISTER(ibValueListDataObject::ibValueDataObjectListColumnCollection::ibValueDataObjectListColumnInfo, "ListColumnInfo", system_to_clsid("VL_LCI"));
SYSTEM_TYPE_REGISTER(ibValueListDataObject::ibValueDataObjectListReturnLine, "ListValueRow", system_to_clsid("VL_LVR"));

SYSTEM_TYPE_REGISTER(ibValueModelTreeDataObject::ibValueDataObjectTreeColumnCollection, "TreeColumn", system_to_clsid("VL_TVC"));
SYSTEM_TYPE_REGISTER(ibValueModelTreeDataObject::ibValueDataObjectTreeColumnCollection::ibValueDataObjectTreeColumnInfo, "TreeColumnInfo", system_to_clsid("VL_TCI"));
SYSTEM_TYPE_REGISTER(ibValueModelTreeDataObject::ibValueDataObjectTreeReturnLine, "TreeValueRow", system_to_clsid("VL_TVR"));

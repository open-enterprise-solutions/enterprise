////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : list data 
////////////////////////////////////////////////////////////////////////////

#include "objectList.h"
#include "backend/srcDataObject.h"
#include "backend/system/systemManager.h"

#include "backend/appData.h"

ibValueListDataObject::ibValueListDataObject(const ibValueMetaObjectGenericData* metaObject, const ibBackendQueryable* queryable, const ibFormID& formType, bool choiceMode) :
	ibSourceDataObject(),
	m_recordColumnCollection(new ibValueDataObjectListColumnCollection(this, metaObject)),
	m_objGuid(choiceMode ? ibGuid::newGuid() : metaObject->GetGuid()) {
	// L5 — wire the composer's source ONCE here on the base. The queryable (the source HOLDER) is vended by the
	// SUBCLASS metaobject — the generic base (ibValueMetaObjectGenericData) has no GetQueryable, so the subclass
	// passes it in. This is what lets the source init live on the base instead of per-subclass (Max).
	if (queryable != nullptr)
		m_composer.FromSource(queryable);   // the subclass's own DB composer (protected member, direct)
}

ibValueListDataObject::~ibValueListDataObject()
{
}

///////////////////////////////////////////////////////////////////////////////

ibValueModelTreeDataObject::ibValueModelTreeDataObject(const ibValueMetaObjectGenericData* metaObject, const ibBackendQueryable* queryable, const ibFormID& formType, bool choiceMode) :
	ibSourceDataObject(),
	m_recordColumnCollection(new ibValueDataObjectTreeColumnCollection(this, metaObject)),
	m_objGuid(choiceMode ? ibGuid::newGuid() : metaObject->GetGuid()) {
	// L5 — source init on the base; the queryable HOLDER is passed by the subclass (see the list base ctor).
	if (queryable != nullptr)
		m_composer.FromSource(queryable);   // the subclass's own DB composer (protected member, direct)
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

bool ibValueListDataObject::ibValueDataObjectListColumnCollection::SetAt(const ibValue& varKeyValue, const ibValue& varValue) // read-only column collection - no-op (writes are not supported)
{
	return false;
}

bool ibValueListDataObject::ibValueDataObjectListColumnCollection::GetAt(const ibValue& varKeyValue, ibValue& pvarValue) // read a column-info entry by its index
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

bool ibValueModelTreeDataObject::ibValueDataObjectTreeColumnCollection::SetAt(const ibValue& varKeyValue, const ibValue& varValue) // read-only column collection - no-op (writes are not supported)
{
	return false;
}

bool ibValueModelTreeDataObject::ibValueDataObjectTreeColumnCollection::GetAt(const ibValue& varKeyValue, ibValue& pvarValue) // read a column-info entry by its index
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
	ibComposerNode* node = m_ownerTable->GetViewData<ibComposerNode>(m_lineItem);
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

ibValueListDataObjectEnumRef::ibValueListDataObjectEnumRef(const ibValueMetaObjectRecordDataEnumRef* metaObject, const ibFormID& formType, bool choiceMode) :
	ibValueListDataObject(metaObject, metaObject->GetQueryable(), formType, choiceMode), m_metaObject(metaObject), m_choiceMode(choiceMode)
{
	m_members.Bind(this, &ibValueListDataObjectEnumRef::FillMembers);
	// Default order by the enum's POSITION attribute, set STRAIGHT on the composer (the store). RunComposerPage's
	// ORDER BY over the enum's queryable yields definition order — no own Fetch / in-memory parent-position sort.
	if (ibValueMetaObjectAttributePredefined* order = m_metaObject->GetDataOrder())
		m_composer.Sort(order->GetName());
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
	const ibUniqueKey key = GetItemKey(GetSelection());
	if (!key.IsOk())
		return;
	wxASSERT(srcForm);
	ibValueReferenceDataObject* dataValueRef = m_metaObject->FindObjectValue(key);
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

ibValueListDataObjectRef::ibValueListDataObjectRef(const ibValueMetaObjectRecordDataMutableRef* metaObject, const ibFormID& formType, bool choiceMode) :
	ibValueListDataObject(metaObject, metaObject->GetQueryable(), formType, choiceMode), m_metaObject(metaObject), m_choiceMode(choiceMode)
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

// The selection ops open the object straight FROM THE KEY — GetItemKey(GetSelection()) IS the open handle (it
// converts to the guid CopyObjectValue / CreateObjectValue want). Inline guard + try/catch, same shape as the
// register ops — no file-static selection-helper indirection.
void ibValueListDataObjectRef::CopyValue()
{
	ibValueMetaObjectRecordDataMutableRef* metaObject = nullptr;
	if (!m_metaObject->ConvertToValue(metaObject)) return;
	const ibUniqueKey key = GetItemKey(GetSelection());
	if (!key.IsOk()) return;
	try {
		ibValuePtr<ibValueRecordDataObjectRef> obj(metaObject->CopyObjectValue(key));
		if (obj != nullptr) obj->ShowFormValue(wxEmptyString, dynamic_cast<ibBackendControlFrame*>(ibBackendValueForm::FindFormBySourceUniqueKey(m_objGuid)));
	}
	catch (const ibBackendCoreException& err) { ibValueSystemFunction::Alert(err.GetErrorDescription()); }
	catch (...) { wxLogError(wxT("objectList: unhandled non-ibBackend exception swallowed")); }
}

void ibValueListDataObjectRef::EditValue()
{
	ibValueMetaObjectRecordDataMutableRef* metaObject = nullptr;
	if (!m_metaObject->ConvertToValue(metaObject)) return;
	const ibUniqueKey key = GetItemKey(GetSelection());
	if (!key.IsOk()) return;
	try {
		ibValueRecordDataObjectRef* obj(metaObject->CreateObjectValue(key));
		if (obj != nullptr) obj->ShowFormValue(wxEmptyString, dynamic_cast<ibBackendControlFrame*>(ibBackendValueForm::FindFormBySourceUniqueKey(m_objGuid)));
	}
	catch (const ibBackendCoreException& err) { ibValueSystemFunction::Alert(err.GetErrorDescription()); }
	catch (...) { wxLogError(wxT("objectList: unhandled non-ibBackend exception swallowed")); }
}

void ibValueListDataObjectRef::DeleteValue()
{
	ibValueMetaObjectRecordDataMutableRef* metaObject = nullptr;
	if (!m_metaObject->ConvertToValue(metaObject)) return;
	const ibUniqueKey key = GetItemKey(GetSelection());
	if (!key.IsOk()) return;
	try {
		ibValuePtr<ibValueRecordDataObjectRef> obj(metaObject->CreateObjectValue(key));
		if (obj != nullptr) obj->DeleteObject();
		if (ibBackendValueForm* ownerForm = ibBackendValueForm::FindFormBySourceUniqueKey(m_objGuid)) ownerForm->UpdateForm();
	}
	catch (const ibBackendCoreException& err) { ibValueSystemFunction::Alert(err.GetErrorDescription()); }
	catch (...) { wxLogError(wxT("objectList: unhandled non-ibBackend exception swallowed")); }
}

void ibValueListDataObjectRef::MarkAsDeleteValue()
{
	ibValueMetaObjectRecordDataMutableRef* metaObject = nullptr;
	if (!m_metaObject->ConvertToValue(metaObject)) return;
	const ibUniqueKey key = GetItemKey(GetSelection());
	if (!key.IsOk()) return;
	try {
		ibValuePtr<ibValueRecordDataObjectRef> obj(metaObject->CreateObjectValue(key));
		if (obj != nullptr) obj->SetDeletionMark(true);
		if (ibBackendValueForm* ownerForm = ibBackendValueForm::FindFormBySourceUniqueKey(m_objGuid)) ownerForm->UpdateForm();
	}
	catch (const ibBackendCoreException& err) { ibValueSystemFunction::Alert(err.GetErrorDescription()); }
	catch (...) { wxLogError(wxT("objectList: unhandled non-ibBackend exception swallowed")); }
}

void ibValueListDataObjectRef::ChooseValue(ibBackendValueForm* srcForm)
{
	const ibUniqueKey key = GetItemKey(GetSelection());
	if (!key.IsOk())
		return;

	wxASSERT(srcForm);

	try {
		ibValuePtr<ibValueReferenceDataObject> dataValueRef(m_metaObject->FindObjectValue(key));
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

ibValueModelTreeDataObjectFolderRef::ibValueModelTreeDataObjectFolderRef(const ibValueMetaObjectRecordDataHierarchyMutableRef* metaObject, const ibFormID& formType,
	int listMode, bool choiceMode) : ibValueModelTreeDataObject(metaObject, metaObject->GetQueryable(), formType, choiceMode),
	m_metaObject(metaObject), m_listMode(listMode), m_choiceMode(choiceMode)
{
	m_members.Bind(this, &ibValueModelTreeDataObjectFolderRef::FillMembers);
	// FOLDERS ON TOP, then by Description. A folder-first sub-sort (IsFolder DESC — the boolean's TRUE
	// sorts before FALSE) goes BEFORE the name sort so each level shows folders above items — restoring the
	// pre-split order. The HIERARCHY itself is NOT a grouping any more — it is INHERENT in the queryable
	// (GetHierarchyColumn): RunComposerPage drives the parent-tree scope off it, so no "Parent" grouping is
	// injected here (it used to pollute the user's Group settings). A user may still add Elements groupings.
	// TEMP: folder-first sort OFF — the folder-flag column materializes as a TYPE_VALUE object (its guid-visible
	// value), NOT a boolean, so as a keyset column it makes `_B < <object>` degenerate → the page returns the
	// whole list and jumps to the top. Restore once the folder flag is read as a real boolean for the cursor.
	if (ibValueMetaObjectAttributePredefined* isFolder = m_metaObject->GetDataIsFolder())
		m_composer.Sort(isFolder->GetName(), /*ascending*/false);
	m_composer.Sort(m_metaObject->GetDataDescription()->GetName());
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
	ibValueModel::ibComposerNode* node = GetViewData<ibValueModel::ibComposerNode>(GetSelection());
	if (node != nullptr) {
		ibValue isFolder = true;
		node->GetValue(*m_metaObject->GetDataIsFolder(), isFolder);
		if (!isFolder.GetBoolean())
			node->GetValue(*m_metaObject->GetDataParent(), outParent);
		else
			node->GetValue(*m_metaObject->GetDataReference(), outParent);
		return;
	}
	ibValueModel::ibComposerNode* drillNode = GetViewData<ibValueModel::ibComposerNode>(GetDrillParent());
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

// Folder ops mirror the catalog ones — open FROM THE KEY (GetItemKey(sel) → the guid) — plus the folder/item MODE
// read off the selected row (isFolder), since the hierarchy CreateObjectValue/CopyObjectValue take it.
void ibValueModelTreeDataObjectFolderRef::CopyValue()
{
	const ibDataViewItem sel = GetSelection();
	const ibUniqueKey key = GetItemKey(sel);
	if (!key.IsOk()) return;
	ibValue cIsFolder = false;
	if (auto* node = GetViewData<ibValueModel::ibComposerNode>(sel))
		node->GetValue(*m_metaObject->GetDataIsFolder(), cIsFolder);
	const ibObjectMode mode = cIsFolder.GetBoolean() ? ibObjectMode::OBJECT_FOLDER : ibObjectMode::OBJECT_ITEM;
	try {
		ibValuePtr<ibValueRecordDataObjectHierarchyRef> obj(m_metaObject->CopyObjectValue(mode, key));
		if (obj != nullptr) obj->ShowFormValue(wxEmptyString, dynamic_cast<ibBackendControlFrame*>(ibBackendValueForm::FindFormBySourceUniqueKey(m_objGuid)));
	}
	catch (const ibBackendCoreException& err) { ibValueSystemFunction::Alert(err.GetErrorDescription()); }
	catch (...) { wxLogError(wxT("objectList: unhandled non-ibBackend exception swallowed")); }
}

void ibValueModelTreeDataObjectFolderRef::EditValue()
{
	const ibDataViewItem sel = GetSelection();
	const ibUniqueKey key = GetItemKey(sel);
	if (!key.IsOk()) return;
	ibValue cIsFolder = false;
	if (auto* node = GetViewData<ibValueModel::ibComposerNode>(sel))
		node->GetValue(*m_metaObject->GetDataIsFolder(), cIsFolder);
	const ibObjectMode mode = cIsFolder.GetBoolean() ? ibObjectMode::OBJECT_FOLDER : ibObjectMode::OBJECT_ITEM;
	try {
		ibValuePtr<ibValueRecordDataObjectHierarchyRef> obj(m_metaObject->CreateObjectValue(mode, key));
		if (obj != nullptr) obj->ShowFormValue(wxEmptyString, dynamic_cast<ibBackendControlFrame*>(ibBackendValueForm::FindFormBySourceUniqueKey(m_objGuid)));
	}
	catch (const ibBackendCoreException& err) { ibValueSystemFunction::Alert(err.GetErrorDescription()); }
	catch (...) { wxLogError(wxT("objectList: unhandled non-ibBackend exception swallowed")); }
}

void ibValueModelTreeDataObjectFolderRef::DeleteValue()
{
	const ibDataViewItem sel = GetSelection();
	const ibUniqueKey key = GetItemKey(sel);
	if (!key.IsOk()) return;
	ibValue cIsFolder = false;
	if (auto* node = GetViewData<ibValueModel::ibComposerNode>(sel))
		node->GetValue(*m_metaObject->GetDataIsFolder(), cIsFolder);
	const ibObjectMode mode = cIsFolder.GetBoolean() ? ibObjectMode::OBJECT_FOLDER : ibObjectMode::OBJECT_ITEM;
	try {
		ibValuePtr<ibValueRecordDataObjectHierarchyRef> obj(m_metaObject->CreateObjectValue(mode, key));
		if (obj != nullptr) obj->DeleteObject();
		if (ibBackendValueForm* ownerForm = ibBackendValueForm::FindFormBySourceUniqueKey(m_objGuid)) ownerForm->UpdateForm();
	}
	catch (const ibBackendCoreException& err) { ibValueSystemFunction::Alert(err.GetErrorDescription()); }
	catch (...) { wxLogError(wxT("objectList: unhandled non-ibBackend exception swallowed")); }
}

void ibValueModelTreeDataObjectFolderRef::MarkAsDeleteValue()
{
	const ibDataViewItem sel = GetSelection();
	const ibUniqueKey key = GetItemKey(sel);
	if (!key.IsOk()) return;
	ibValue cIsFolder = false;
	if (auto* node = GetViewData<ibValueModel::ibComposerNode>(sel))
		node->GetValue(*m_metaObject->GetDataIsFolder(), cIsFolder);
	const ibObjectMode mode = cIsFolder.GetBoolean() ? ibObjectMode::OBJECT_FOLDER : ibObjectMode::OBJECT_ITEM;
	try {
		ibValuePtr<ibValueRecordDataObjectHierarchyRef> obj(m_metaObject->CreateObjectValue(mode, key));
		if (obj != nullptr) obj->SetDeletionMark(true);
		if (ibBackendValueForm* ownerForm = ibBackendValueForm::FindFormBySourceUniqueKey(m_objGuid)) ownerForm->UpdateForm();
	}
	catch (const ibBackendCoreException& err) { ibValueSystemFunction::Alert(err.GetErrorDescription()); }
	catch (...) { wxLogError(wxT("objectList: unhandled non-ibBackend exception swallowed")); }
}

void ibValueModelTreeDataObjectFolderRef::ChooseValue(ibBackendValueForm* srcForm)
{
	const ibDataViewItem sel = GetSelection();
	ibValueModel::ibComposerNode* node = GetViewData<ibValueModel::ibComposerNode>(sel);
	if (node == nullptr)
		return;

	wxASSERT(srcForm);

	ibValue cIsFolder = false;
	node->GetValue(*m_metaObject->GetDataIsFolder(), cIsFolder);

	try {
		ibValuePtr<ibValueReferenceDataObject> dataValueFolderRef(m_metaObject->FindObjectValue(GetItemKey(sel)));
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
	// The ONLY list whose row-key is NOT the value itself: a register row is keyed by its PRIMARY-KEY columns
	// (recorder+line, or the dimensions) read off the record manager — the SAME GetPrimaryKeyColumns() the
	// fetched composer node stamps, so it matches by m_rowKey. (Reference lists inherit the base FindRowValue,
	// whose key is just {value}. The dead sort-anchor is gone — restore matches by KEY within the fetched batch.)
	ibValueRecordManagerObject* pRefData = nullptr;
	if (!varValue.ConvertToValue(pRefData) || pRefData == nullptr)
		return ibDataViewItem();
	std::vector<ibValue> rowKey;
	if (const ibBackendQueryable* q = GetSourceQueryable())
		for (const ibBackendQueryColumn* kc : q->GetPrimaryKeyColumns()) {
			if (kc == nullptr) continue;
			ibValue v;
			pRefData->GetValueByMetaID(kc->GetColumnId(), v);
			rowKey.push_back(v);
		}
	auto* stub = new ibComposerNode(std::move(rowKey));
	ibDataViewItem item(stub);   // IncRef → 2
	stub->DecRef();              // refcount=1, owned by item
	return item;
}

ibValueListRegisterObject::ibValueListRegisterObject(const ibValueMetaObjectRegisterData* metaObject, const ibFormID& formType) :
	ibValueListDataObject(metaObject, metaObject->GetQueryable(), formType), m_metaObject(metaObject)
{
	m_members.Bind(this, &ibValueListRegisterObject::FillMembers);
	// Default sort goes STRAIGHT to the composer (the store) — no helper. Recorder registers order by
	// period? + recorder + line; a plain periodic register by period. (Dimensions are NOT a default sort; the
	// recorder/period/line order + the queryable's GetIdentitySort tiebreaker are the whole sort.)
	if (m_metaObject->HasRecorder()) {
		if (m_metaObject->HasPeriod()) m_composer.Sort(metaObject->GetRegisterPeriod()->GetName());
		m_composer.Sort(metaObject->GetRegisterRecorder()->GetName());
		m_composer.Sort(metaObject->GetRegisterLineNumber()->GetName());
	}
	else if (m_metaObject->HasPeriod()) {
		m_composer.Sort(metaObject->GetRegisterPeriod()->GetName());
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

// ibValueListDataObject::GetItemKey — a regular list's row key = its primary-key REFERENCE (guid). Read the item's
// node, take the first PK column's value (the row's own-reference cell) and pull its guid. Empty when there is no
// node / no keyed source / the cell is not a reference. The key IS the open handle — it converts to guid on use.
ibUniqueKey ibValueListDataObject::GetItemKey(const ibDataViewItem& item) const
{
	ibValueModel::ibComposerNode* node = GetViewData<ibValueModel::ibComposerNode>(item);
	if (node == nullptr) return ibUniqueKey();
	const ibBackendQueryable* q = GetSourceQueryable();
	if (q == nullptr) return ibUniqueKey();
	const std::vector<const ibBackendQueryColumn*> keyCols = q->GetPrimaryKeyColumns();
	if (keyCols.empty() || keyCols.front() == nullptr) return ibUniqueKey();
	ibValue refVal;
	if (node->GetValue(keyCols.front()->GetColumnId(), refVal)) {
		ibValueReferenceDataObject* refObj = nullptr;
		if (refVal.ConvertToValue(refObj) && refObj != nullptr)
			return ibUniqueKey(refObj->GetGuid());
	}
	return ibUniqueKey();
}

// ibValueModelTreeDataObject::GetItemKey — a folder tree's row key = its primary-key REFERENCE (guid), same shape
// as a flat list (a tree is a list too; it just derives the ibValueModelCursor via the tree base, not the list one).
ibUniqueKey ibValueModelTreeDataObject::GetItemKey(const ibDataViewItem& item) const
{
	ibValueModel::ibComposerNode* node = GetViewData<ibValueModel::ibComposerNode>(item);
	if (node == nullptr) return ibUniqueKey();
	const ibBackendQueryable* q = GetSourceQueryable();
	if (q == nullptr) return ibUniqueKey();
	const std::vector<const ibBackendQueryColumn*> keyCols = q->GetPrimaryKeyColumns();
	if (keyCols.empty() || keyCols.front() == nullptr) return ibUniqueKey();
	ibValue refVal;
	if (node->GetValue(keyCols.front()->GetColumnId(), refVal)) {
		ibValueReferenceDataObject* refObj = nullptr;
		if (refVal.ConvertToValue(refObj) && refObj != nullptr)
			return ibUniqueKey(refObj->GetGuid());
	}
	return ibUniqueKey();
}

// ibValueListRegisterObject::GetItemKey — the register row key is a COMPOSITE (dimensions), not a single guid, so
// it overrides the regular-list default. Reads the recorder+line (HasRecorder) or the dimension values off the
// item's node and hands them to CreateUniqueKeyPair; the resulting Pair slices to the ibUniqueKey return WITHOUT
// losing the composite (it lives in the base). Empty (invalid) when there is no node, so the selection op no-ops.
ibUniqueKey ibValueListRegisterObject::GetItemKey(const ibDataViewItem& item) const
{
	if (m_metaObject == nullptr) return ibUniqueKey();
	ibValueModel::ibComposerNode* node = GetViewData<ibValueModel::ibComposerNode>(item);
	if (node == nullptr) return ibUniqueKey();
	ibRowMetaValues keys;
	auto take = [&](const ibValueMetaObjectAttributePredefined* attr) {
		if (attr == nullptr) return;
		ibValue v; node->GetValue(attr->GetMetaID(), v); keys.insert_or_assign(attr->GetMetaID(), v);
	};
	if (m_metaObject->HasRecorder()) {
		take(m_metaObject->GetRegisterRecorder());
		take(m_metaObject->GetRegisterLineNumber());
	}
	else {
		for (auto& dim : m_metaObject->GetGenericDimensionArrayObject()) {
			ibValue v; node->GetValue(dim->GetMetaID(), v); keys.insert_or_assign(dim->GetMetaID(), v);
		}
	}
	return m_metaObject->CreateUniqueKeyPair(keys);
}

void ibValueListRegisterObject::CopyValue()
{
	if (m_metaObject != nullptr && GetSelection().IsOk()) {
		if (m_metaObject->HasRecordManager()) {
			try {
				ibValuePtr<ibValueRecordManagerObject> dataValueObject(m_metaObject->CopyRecordManagerObjectValue(ibUniqueKeyPair(GetItemKey(GetSelection()).GetKeyValues())));
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
	if (m_metaObject != nullptr && GetSelection().IsOk()) {
		if (m_metaObject->HasRecordManager()) {
			try {
				ibValuePtr<ibValueRecordManagerObject> dataValueObject(m_metaObject->CreateRecordManagerObjectValue(ibUniqueKeyPair(GetItemKey(GetSelection()).GetKeyValues())));
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
				if (metaRecorder != nullptr)
					if (auto* node = GetViewData<ibValueModel::ibComposerNode>(GetSelection())) {
						ibValue recorderVal; node->GetValue(metaRecorder->GetMetaID(), recorderVal);
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
	if (m_metaObject != nullptr && GetSelection().IsOk()) {
		if (m_metaObject->HasRecordManager()) {
			try {
				ibValuePtr<ibValueRecordManagerObject> dataValueObject(m_metaObject->CreateRecordManagerObjectValue(ibUniqueKeyPair(GetItemKey(GetSelection()).GetKeyValues())));
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

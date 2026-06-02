////////////////////////////////////////////////////////////////////////////
//	Author		: Tetracode Dev
//	Description : chart of accounts object
////////////////////////////////////////////////////////////////////////////

#include "chartOfAccounts.h"
#include "backend/metaData.h"
#include "backend/appData.h"
#include "backend/session/session.h"
#include "reference/reference.h"
#include "backend/databaseLayer/connectionPool.h"
#include "backend/system/systemManager.h"
#include "backend/fileSystem/fs.h"

ibValueRecordDataObjectChartOfAccounts::ibValueRecordDataObjectChartOfAccounts(const ibValueMetaObjectChartOfAccounts* metaObject, const ibGuid& objGuid, ibObjectMode objMode) :
	ibValueRecordDataObjectHierarchyRef(metaObject, objGuid, objMode) {}

ibValueRecordDataObjectChartOfAccounts::ibValueRecordDataObjectChartOfAccounts(const ibValueRecordDataObjectChartOfAccounts& source) :
	ibValueRecordDataObjectHierarchyRef(source) {}

ibSourceExplorer ibValueRecordDataObjectChartOfAccounts::GetSourceExplorer() const
{
	ibSourceExplorer srcHelper(m_metaObject, GetClassType(), false);
	ibValueMetaObjectChartOfAccounts* metaRef = nullptr;
	
	if (m_metaObject->ConvertToValue(metaRef)) {
		srcHelper.AppendSource(metaRef->GetDataCode(), false);
		srcHelper.AppendSource(metaRef->GetDataDescription());
		srcHelper.AppendSource(metaRef->GetDataParent());
		srcHelper.AppendSource(metaRef->GetSubcontoKindsTable());
	}
	
	for (const auto object : m_metaObject->GetAttributeArrayObject()) {
		ibItemMode attrUse = object->GetItemMode();
		if (m_objMode == ibObjectMode::OBJECT_ITEM) {
			if (attrUse == ibItemMode::ibItemMode_Item || attrUse == ibItemMode::ibItemMode_Folder_Item) {
				if (!m_metaObject->IsDataReference(object->GetMetaID())) srcHelper.AppendSource(object);
			}
		} else {
			if (attrUse == ibItemMode::ibItemMode_Folder || attrUse == ibItemMode::ibItemMode_Folder_Item) {
				if (!m_metaObject->IsDataReference(object->GetMetaID())) srcHelper.AppendSource(object);
			}
		}
	}

	for (const auto object : m_metaObject->GetTableArrayObject()) {
		ibItemMode tableUse = object->GetTableUse();
		if (m_objMode == ibObjectMode::OBJECT_ITEM) {
			if (tableUse == ibItemMode::ibItemMode_Item || tableUse == ibItemMode::ibItemMode_Folder_Item) srcHelper.AppendSource(object);
		} else {
			if (tableUse == ibItemMode::ibItemMode_Folder || tableUse == ibItemMode::ibItemMode_Folder_Item) srcHelper.AppendSource(object);
		}
	}
	
	return srcHelper;
}

// ShowFormValue / GetFormValue moved up to HierarchyRef.

// WriteObject / DeleteObject inherited from
// ibValueRecordDataObjectHierarchyRef — see commonObjectRefQuery.cpp.

enum Func { enIsNew = 0, enCopy, enFill, enWrite, enDelete, enModified, enGetForm, enGetTemplate, enGetMetadata, enLock, enUnlock };

void ibValueRecordDataObjectChartOfAccounts::PrepareNames() const
{
	m_methodHelper->ClearHelper();
	m_methodHelper->AppendFunc(wxT("IsNew"), wxT("IsNew()"));
	m_methodHelper->AppendFunc(wxT("Copy"), wxT("Copy()"));
	m_methodHelper->AppendFunc(wxT("Fill"), 1, wxT("Fill(object)"));
	m_methodHelper->AppendFunc(wxT("Write"), wxT("Write()"));
	m_methodHelper->AppendFunc(wxT("Delete"), wxT("Delete()"));
	m_methodHelper->AppendFunc(wxT("Modified"), wxT("Modified()"));
	m_methodHelper->AppendFunc(wxT("GetFormObject"), 3, wxT("GetFormObject(name : string, owner : any , id : guid)"));
	m_methodHelper->AppendFunc(wxT("GetTemplate"), 1, wxT("GetTemplate(name : string)"));
	m_methodHelper->AppendFunc(wxT("GetMetadata"), wxT("GetMetadata()"));
	m_methodHelper->AppendProc(wxT("Lock"),   wxT("Lock()"));
	m_methodHelper->AppendProc(wxT("Unlock"), wxT("Unlock()"));
	// ThisObject is bound (BindContextVariable in InitializeObject) — no manual AppendProp.
	wxString objectName;
	for (const auto object : m_metaObject->GetGenericAttributeArrayObject()) {
		if (object->IsDeleted()) continue;
		if (!object->GetObjectNameAsString(objectName)) continue;
		m_methodHelper->AppendProp(objectName, true, !m_metaObject->IsDataReference(object->GetMetaID()), object->GetMetaID(), eProperty);
	}
	for (const auto object : m_metaObject->GetGenericTableArrayObject()) {
		if (object->IsDeleted()) continue;
		if (!object->GetObjectNameAsString(objectName)) continue;
		m_methodHelper->AppendProp(objectName, true, false, object->GetMetaID(), eTable);
	}
	ExportNamesToHelper(m_methodHelper, eProcUnit);
}

bool ibValueRecordDataObjectChartOfAccounts::SetPropVal(const long lPropNum, const ibValue& varPropVal)
{
	const long lPropAlias = m_methodHelper->GetPropAlias(lPropNum);
	if (lPropAlias == eProcUnit) { if (m_procUnit != nullptr) return m_procUnit->SetPropVal(GetPropName(lPropNum), varPropVal); }
	else if (lPropAlias == eProperty) return SetValueByMetaID(m_methodHelper->GetPropData(lPropNum), varPropVal);
	return false;
}

bool ibValueRecordDataObjectChartOfAccounts::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	const long lPropAlias = m_methodHelper->GetPropAlias(lPropNum);
	if (lPropAlias == eProcUnit) { if (m_procUnit != nullptr) return m_procUnit->GetPropVal(GetPropName(lPropNum), pvarPropVal); }
	else if (lPropAlias == eProperty || lPropAlias == eTable) {
		const long lPropData = m_methodHelper->GetPropData(lPropNum);
		if (m_metaObject->IsDataReference(lPropData)) { pvarPropVal = GetReference(); return true; }
		return GetValueByMetaID(lPropData, pvarPropVal);
	}
	return false;
}

bool ibValueRecordDataObjectChartOfAccounts::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	switch (lMethodNum) {
	case enIsNew: pvarRetValue = m_newObject; return true;
	case enCopy: pvarRetValue = CopyObject(); return true;
	case enFill: FillObject(*paParams[0]); return true;
	case enWrite: WriteObject(); return true;
	case enDelete: DeleteObject(); return true;
	case enModified: pvarRetValue = m_objModified; return true;
	case Func::enGetForm: pvarRetValue = GetFormValue(lSizeArray > 0 ? paParams[0]->GetString() : wxString(wxEmptyString), lSizeArray > 1 ? paParams[1]->ConvertToType<ibBackendControlFrame>() : nullptr); return true;
	case Func::enGetTemplate: pvarRetValue = m_metaObject->GetTemplate(paParams[0]->GetString()); return true;
	case Func::enGetMetadata: pvarRetValue = m_metaObject; return true;
	case Func::enLock:   TryAcquireFormLock(); return true;
	case Func::enUnlock: ReleaseFormLock();    return true;
	}
	return ibRuntimeModuleDataObject::ExecAsFunc(GetMethodName(lMethodNum), pvarRetValue, paParams, lSizeArray);
}

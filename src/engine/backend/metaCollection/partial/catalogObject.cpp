////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : catalog object
////////////////////////////////////////////////////////////////////////////

#include "catalog.h"
#include "backend/metaData.h"

#include "backend/appData.h"
#include "reference/reference.h"
#include "backend/databaseLayer/databaseLayer.h"
#include "backend/system/systemManager.h"

#include "backend/fileSystem/fs.h"
//*********************************************************************************************
//*                                  ObjectCatalogValue                                       *
//*********************************************************************************************

ibValueRecordDataObjectCatalog::ibValueRecordDataObjectCatalog(ibValueMetaObjectCatalog* metaObject, const ibGuid& objGuid, ibObjectMode objMode) :
	ibValueRecordDataObjectHierarchyRef(metaObject, objGuid, objMode)
{
}

ibValueRecordDataObjectCatalog::ibValueRecordDataObjectCatalog(const ibValueRecordDataObjectCatalog& source) :
	ibValueRecordDataObjectHierarchyRef(source)
{
}

ibSourceExplorer ibValueRecordDataObjectCatalog::GetSourceExplorer() const
{
	ibSourceExplorer srcHelper(
		m_metaObject, GetClassType(),
		false
	);

	ibValueMetaObjectCatalog* metaRef = nullptr;

	if (m_metaObject->ConvertToValue(metaRef)) {
		srcHelper.AppendSource(metaRef->GetDataCode(), false);
		srcHelper.AppendSource(metaRef->GetDataDescription());
		ibValueMetaObjectAttributePredefined* defOwner = metaRef->GetCatalogOwner();
		if (defOwner != nullptr && defOwner->GetClsidCount() > 0) {
			srcHelper.AppendSource(metaRef->GetCatalogOwner());
		}
		srcHelper.AppendSource(metaRef->GetDataParent());
	}

	for (const auto object : m_metaObject->GetAttributeArrayObject()) {
		ibItemMode attrUse = object->GetItemMode();
		if (m_objMode == ibObjectMode::OBJECT_ITEM) {
			if (attrUse == ibItemMode::ibItemMode_Item
				|| attrUse == ibItemMode::ibItemMode_Folder_Item) {
				if (!m_metaObject->IsDataReference(object->GetMetaID())) {
					srcHelper.AppendSource(object);
				}
			}
		}
		else {
			if (attrUse == ibItemMode::ibItemMode_Folder ||
				attrUse == ibItemMode::ibItemMode_Folder_Item) {
				if (!m_metaObject->IsDataReference(object->GetMetaID())) {
					srcHelper.AppendSource(object);
				}
			}
		}
	}

	for (const auto object : m_metaObject->GetTableArrayObject()) {
		ibItemMode tableUse = object->GetTableUse();
		if (m_objMode == ibObjectMode::OBJECT_ITEM) {
			if (tableUse == ibItemMode::ibItemMode_Item
				|| tableUse == ibItemMode::ibItemMode_Folder_Item) {
				srcHelper.AppendSource(object);
			}
		}
		else {
			if (tableUse == ibItemMode::ibItemMode_Folder ||
				tableUse == ibItemMode::ibItemMode_Folder_Item) {
				srcHelper.AppendSource(object);
			}
		}
	}

	return srcHelper;
}

#pragma region _form_builder_h_
void ibValueRecordDataObjectCatalog::ShowFormValue(const wxString& strFormName, ibBackendControlFrame* ownerControl)
{
	ibBackendValueForm* const foundedForm = GetForm();

	if (foundedForm && foundedForm->IsShown()) {
		foundedForm->ActivateForm();
		return;
	}

	//if form is not initialized then generate  
	ibBackendValueForm* const valueForm =
		GetFormValue(strFormName, ownerControl);

	if (valueForm != nullptr) {	
		valueForm->Modify(m_objModified);
		valueForm->ShowForm();
	}
}

ibBackendValueForm* ibValueRecordDataObjectCatalog::GetFormValue(const wxString& strFormName, ibBackendControlFrame* ownerControl)
{
	ibBackendValueForm* const foundedForm = GetForm();

	if (foundedForm == nullptr) {

		ibBackendValueForm* createdForm = m_metaObject->CreateAndBuildForm(
			strFormName,
			m_objMode == ibObjectMode::OBJECT_ITEM ? ibValueMetaObjectCatalog::eFormObject : ibValueMetaObjectCatalog::eFormFolder,
			ownerControl,
			this,
			m_objGuid
		);

		if (createdForm != nullptr)
			createdForm->CloseOnOwnerClose(false);
		
		return createdForm;
	}

	return foundedForm;
}
#pragma endregion

//***********************************************************************************************
//*                                   Catalog events                                            *
//***********************************************************************************************

#include "backend/backend_mainFrame.h"

bool ibValueRecordDataObjectCatalog::WriteObject()
{
	if (!appData->DesignerMode())
	{
		if (db_query != nullptr && !db_query->IsOpen())
			ibBackendCoreException::Error(_("Database is not open!"));
		else if (db_query == nullptr)
			ibBackendCoreException::Error(_("Database is not open!"));

		if (!ibBackendException::IsEvalMode()) 
		{
			if (!m_metaObject->AccessRight_Write()) {
				ibBackendAccessException::Error();
				return false;
			}

			ibTransactionGuard db_query_active_transaction = db_query;
			{
				ibBackendValueForm* const valueForm = GetForm();
				{
					db_query_active_transaction.BeginTransaction();

					{
						ibValue cancel = false;
						m_procUnit->CallAsProc(wxT("BeforeWrite"), cancel);

						if (cancel.GetBoolean()) {
							db_query_active_transaction.RollBackTransaction();
							ibBackendCoreException::Error(_("Failed to write object in db!"));
							return false;
						}
					}

					bool newObject = ibValueRecordDataObjectCatalog::IsNewObject();
					bool generateUniqueIdentifier = false;
					
					if (!IsSetUniqueIdentifier()) {
						ibValue prefix = "", standartProcessing = true;
						m_procUnit->CallAsProc(wxT("SetNewCode"), prefix, standartProcessing);
						if (standartProcessing.GetBoolean()) {
							generateUniqueIdentifier = 
								ibValueRecordDataObjectCatalog::GenerateUniqueIdentifier(prefix.GetString());
						}
					}

					if (!SaveData()) {
						if (generateUniqueIdentifier)
							ibValueRecordDataObjectCatalog::ResetUniqueIdentifier();
						db_query_active_transaction.RollBackTransaction();
						ibBackendCoreException::Error(_("Failed to write object in db!"));
						return false;
					}

					{
						ibValue cancel = false;
						m_procUnit->CallAsProc(wxT("OnWrite"), cancel);
						if (cancel.GetBoolean()) {
							if (generateUniqueIdentifier)
								ibValueRecordDataObjectCatalog::ResetUniqueIdentifier();
							db_query_active_transaction.RollBackTransaction();
							ibBackendCoreException::Error(_("Failed to write object in db!"));
							return false;
						}
					}

					db_query_active_transaction.CommitTransaction();

					if (newObject && valueForm != nullptr) valueForm->NotifyCreate(GetReference());
					else if (valueForm != nullptr) valueForm->NotifyChange(GetReference());
				}
				m_objModified = false;
			}
		}
	}

	return true;
}

bool ibValueRecordDataObjectCatalog::DeleteObject()
{
	if (!appData->DesignerMode())
	{
		if (db_query != nullptr && !db_query->IsOpen())
			ibBackendCoreException::Error(_("Database is not open!"));
		else if (db_query == nullptr)
			ibBackendCoreException::Error(_("Database is not open!"));

		if (!ibBackendException::IsEvalMode())
		{
			if (!m_metaObject->AccessRight_Delete()) {
				ibBackendAccessException::Error();
				return false;
			}

			const ibValueMetaObjectRecordDataHierarchyMutableRef* valueMetaObject = GetMetaObject();
			wxASSERT(valueMetaObject);

			const ibGuid& objGuid = GetGuid();
			const auto predefinedValue =
				valueMetaObject->FindPredefinedValue(objGuid);

			if (predefinedValue != nullptr) {
				ibBackendCoreException::Error(_("Attempting to delete a predefined element!"));
				return false;
			}

			ibTransactionGuard db_query_active_transaction = db_query;
			{
				ibBackendValueForm* const valueForm = GetForm();
				{
					db_query_active_transaction.BeginTransaction();

					{
						ibValue cancel = false;
						m_procUnit->CallAsProc(wxT("BeforeDelete"), cancel);
						if (cancel.GetBoolean()) {
							db_query_active_transaction.RollBackTransaction();
							ibBackendCoreException::Error(_("Failed to delete object in db!"));
							return false;
						}
					}

					if (!DeleteData()) {
						db_query_active_transaction.RollBackTransaction();
						ibBackendCoreException::Error(_("Failed to delete object in db!")); 
						return false;
					}

					{
						ibValue cancel = false;
						m_procUnit->CallAsProc(wxT("OnDelete"), cancel);
						if (cancel.GetBoolean()) {
							db_query_active_transaction.RollBackTransaction();
							ibBackendCoreException::Error(_("Failed to delete object in db!")); 
							return false;
						}
					}

					db_query_active_transaction.CommitTransaction();

					if (valueForm != nullptr) valueForm->NotifyDelete(GetReference());
				}
			}
		}
	}

	return true;
}

enum Func {
	enIsNew = 0,
	enCopy,
	enFill,
	enWrite,
	enDelete,
	enModified,
	enGetForm,
	enGetTemplate,
	enGetMetadata
};

//****************************************************************************
//*                              Support methods                             *
//****************************************************************************

void ibValueRecordDataObjectCatalog::PrepareNames() const
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

	m_methodHelper->AppendProp(wxT("ThisObject"), true, false, eThisObject, eSystem);

	//set object name
	wxString objectName;

	//fill custom attributes 
	for (const auto object : m_metaObject->GetGenericAttributeArrayObject()) {
		if (object->IsDeleted())
			continue;
		if (!object->GetObjectNameAsString(objectName))
			continue;
		m_methodHelper->AppendProp(
			objectName,
			true,
			!m_metaObject->IsDataReference(object->GetMetaID()),
			object->GetMetaID(),
			eProperty
		);
	}

	//fill custom tables 
	for (const auto object : m_metaObject->GetGenericTableArrayObject()) {
		if (object->IsDeleted())
			continue;
		if (!object->GetObjectNameAsString(objectName))
			continue;
		m_methodHelper->AppendProp(
			objectName,
			true,
			false,
			object->GetMetaID(),
			eTable
		);
	}
	if (m_procUnit != nullptr) {
		ibByteCode* byteCode = m_procUnit->GetByteCode();
		if (byteCode != nullptr) {
			for (auto exportFunction : byteCode->m_listExportFunc) {
				m_methodHelper->AppendMethod(
					exportFunction.first,
					byteCode->GetNParams(exportFunction.second),
					byteCode->HasRetVal(exportFunction.second),
					exportFunction.second,
					eProcUnit
				);
			}
			for (auto exportVariable : byteCode->m_listExportVar) {
				m_methodHelper->AppendProp(
					exportVariable.first,
					exportVariable.second,
					eProcUnit
				);
			}
		}
	}
}

bool ibValueRecordDataObjectCatalog::SetPropVal(const long lPropNum, const ibValue& varPropVal)
{
	const long lPropAlias = m_methodHelper->GetPropAlias(lPropNum);
	if (lPropAlias == eProcUnit) {
		if (m_procUnit != nullptr) {
			return m_procUnit->SetPropVal(
				GetPropName(lPropNum), varPropVal
			);
		}
	}
	else if (lPropAlias == eProperty) {
		return SetValueByMetaID(
			m_methodHelper->GetPropData(lPropNum),
			varPropVal
		);
	}

	return false;
}

bool ibValueRecordDataObjectCatalog::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	const long lPropAlias = m_methodHelper->GetPropAlias(lPropNum);
	if (lPropAlias == eProcUnit) {
		if (m_procUnit != nullptr) {
			return m_procUnit->GetPropVal(
				GetPropName(lPropNum), pvarPropVal
			);
		}
	}
	else if (lPropAlias == eProperty || lPropAlias == eTable) {
		const long lPropData = m_methodHelper->GetPropData(lPropNum);
		if (m_metaObject->IsDataReference(lPropData)) {
			pvarPropVal = GetReference();
			return true;
		}
		return GetValueByMetaID(lPropData, pvarPropVal);
	}
	else if (lPropAlias == eSystem) {
		switch (m_methodHelper->GetPropData(lPropNum))
		{
		case eThisObject:
			pvarPropVal = GetValue();
			return true;
		}
	}
	return false;
}

bool ibValueRecordDataObjectCatalog::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	switch (lMethodNum)
	{
	case enIsNew:
		pvarRetValue = m_newObject;
		return true;
	case enCopy:
		pvarRetValue = CopyObject();
		return true;
	case enFill:
		FillObject(*paParams[0]);
		return true;
	case enWrite:
		WriteObject();
		return true;
	case enDelete:
		DeleteObject();
		return true;
	case enModified:
		pvarRetValue = m_objModified;
		return true;
	case Func::enGetForm:
		pvarRetValue = GetFormValue(
			lSizeArray > 0 ? paParams[0]->GetString() : wxString(wxEmptyString),
			lSizeArray > 1 ? paParams[1]->ConvertToType<ibBackendControlFrame>() : nullptr
		);
		return true;
	case Func::enGetTemplate:
		pvarRetValue = m_metaObject->GetTemplate(paParams[0]->GetString());
		return true;
	case Func::enGetMetadata:
		pvarRetValue = m_metaObject;
		return true;
	}

	return ibModuleDataObject::ExecuteFunc(
		GetMethodName(lMethodNum), pvarRetValue, paParams, lSizeArray
	);
}
////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : document object
////////////////////////////////////////////////////////////////////////////

#include "document.h"
#include "backend/metaData.h"

#include "backend/appData.h"
#include "reference/reference.h"
#include "backend/databaseLayer/databaseLayer.h"
#include "backend/system/systemManager.h"

//*********************************************************************************************
//*                                  ibRecorderRegisterDocument	                              *
//*********************************************************************************************

////////////////////////////////////////////////////////////////////////////////////////////////////
wxIMPLEMENT_DYNAMIC_CLASS(ibValueRecordDataObjectDocument::ibRecorderRegisterDocument, ibValue);
////////////////////////////////////////////////////////////////////////////////////////////////////

void ibValueRecordDataObjectDocument::ibRecorderRegisterDocument::CreateRecordSet()
{
	ibValueMetaObjectDocument* metaDocument = dynamic_cast<ibValueMetaObjectDocument*>(m_document->GetMetaObject());
	wxASSERT(metaDocument);
	ibMetaData* metaData = metaDocument->GetMetaData();
	wxASSERT(metaData);

	ibRecorderRegisterDocument::ClearRecordSet();

	const ibMetaDescription& metaDesc = metaDocument->GetRecordDescription();
	for (unsigned int idx = 0; idx < metaDesc.GetTypeCount(); idx++) {
		ibValueMetaObjectRegisterData* metaObject = metaData->FindAnyObjectByFilter<ibValueMetaObjectRegisterData>(metaDesc.GetByIdx(idx));
		if (metaObject == nullptr || !metaObject->IsAllowed())
			continue;
		ibValueMetaObjectAttributePredefined* registerRecord = metaObject->GetRegisterRecorder();
		wxASSERT(registerRecord);
		ibValuePtr<ibValueRecordSetObject> recordSet(metaObject->CreateRecordSetObjectValue());
		recordSet->SetKeyValue(registerRecord->GetMetaID(), m_document->GetReference());
		m_records.insert_or_assign(metaObject->GetMetaID(), recordSet);
	}

	PrepareNames();
}

bool ibValueRecordDataObjectDocument::ibRecorderRegisterDocument::WriteRecordSet()
{
	for (auto& pair : m_records) {
		ibValueRecordSetObject* record = pair.second;
		wxASSERT(record);
		try {
			if (!record->WriteRecordSet()) {
				return false;
			}
		}
		catch (...) {
			return false;
		}
	}

	return true;
}

bool ibValueRecordDataObjectDocument::ibRecorderRegisterDocument::DeleteRecordSet()
{
	for (auto& pair : m_records) {
		ibValueRecordSetObject* record = pair.second;
		wxASSERT(record);
		try {
			if (!record->DeleteRecordSet()) {
				return false;
			}
		}
		catch (...) {
			return false;
		}
	}

	return true;
}

void ibValueRecordDataObjectDocument::ibRecorderRegisterDocument::ClearRecordSet()
{
	m_records.clear();
}

void ibValueRecordDataObjectDocument::ibRecorderRegisterDocument::RefreshRecordSet()
{
	for (auto& pair : m_records) {
		ibValueRecordSetObject* record = pair.second;
		wxASSERT(record);

		ibValueMetaObjectRegisterData* object = record->GetMetaObject();
		wxASSERT(object);
		ibBackendValueForm* backendFrame = ibBackendValueForm::FindFormBySourceUniqueKey(object->GetGuid());
		if (backendFrame != nullptr) backendFrame->UpdateForm();
	}
}

ibValueRecordDataObjectDocument::ibRecorderRegisterDocument::ibRecorderRegisterDocument(ibValueRecordDataObjectDocument* currentDoc) :
	ibValue(ibValueTypes::TYPE_VALUE), m_document(currentDoc), m_methodHelper(new ibValueMethodHelper())
{
	ibRecorderRegisterDocument::CreateRecordSet();
}

ibValueRecordDataObjectDocument::ibRecorderRegisterDocument::~ibRecorderRegisterDocument()
{
	ibRecorderRegisterDocument::ClearRecordSet();
	wxDELETE(m_methodHelper);
}

//*********************************************************************************************
//*                                  ibValueRecordDataObjectDocument	                                      *
//*********************************************************************************************

ibValueRecordDataObjectDocument::ibValueRecordDataObjectDocument(ibValueMetaObjectDocument* metaObject, const ibGuid& objGuid) :
	ibValueRecordDataObjectRef(metaObject, objGuid),
	m_registerRecords(new ibRecorderRegisterDocument(this))
{
}

ibValueRecordDataObjectDocument::ibValueRecordDataObjectDocument(const ibValueRecordDataObjectDocument& source) :
	ibValueRecordDataObjectRef(source),
	m_registerRecords(new ibRecorderRegisterDocument(this))
{
}

ibValueRecordDataObjectDocument::~ibValueRecordDataObjectDocument()
{
}

bool ibValueRecordDataObjectDocument::IsPosted() const
{
	ibValueMetaObjectDocument* metaDocRef = nullptr;
	if (m_metaObject->ConvertToValue(metaDocRef)) {
		return GetValueByMetaID(*metaDocRef->GetDocumentPosted()).GetBoolean();
	}
	return false;
}

void ibValueRecordDataObjectDocument::SetDeletionMark(bool deletionMark)
{
	WriteObject(
		ibDocumentWriteMode::ibDocumentWriteMode_UndoPosting,
		ibDocumentPostingMode::ibDocumentPostingMode_Regular
	);

	ibValueRecordDataObjectRef::SetDeletionMark(deletionMark);
}

ibSourceExplorer ibValueRecordDataObjectDocument::GetSourceExplorer() const
{
	ibSourceExplorer srcHelper(
		m_metaObject, GetClassType(),
		false
	);

	ibValueMetaObjectDocument* metaRef = nullptr;

	if (m_metaObject->ConvertToValue(metaRef)) {
		srcHelper.AppendSource(metaRef->GetDocumentNumber(), false);
		srcHelper.AppendSource(metaRef->GetDocumentDate());
	}

	for (const auto object : m_metaObject->GetAttributeArrayObject()) {
		srcHelper.AppendSource(object);
	}

	for (const auto object : m_metaObject->GetTableArrayObject()) {
		srcHelper.AppendSource(object);
	}

	return srcHelper;
}

#pragma region _form_builder_h_
void ibValueRecordDataObjectDocument::ShowFormValue(const wxString& strFormName, ibBackendControlFrame* ownerControl)
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

ibBackendValueForm* ibValueRecordDataObjectDocument::GetFormValue(const wxString& strFormName, ibBackendControlFrame* ownerControl)
{
	ibBackendValueForm* const foundedForm = GetForm();

	if (foundedForm == nullptr) {

		ibBackendValueForm* createdForm = m_metaObject->CreateAndBuildForm(
			strFormName,
			ibValueMetaObjectDocument::eFormObject,
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
//*                                   Document events                                            *
//***********************************************************************************************

#include "backend/backend_mainFrame.h"

bool ibValueRecordDataObjectDocument::WriteObject(ibDocumentWriteMode writeMode, ibDocumentPostingMode postingMode)
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

			if (writeMode == ibDocumentWriteMode::ibDocumentWriteMode_Posting) {
				ibValue deletionMark = false;
				ibValueMetaObjectAttributePredefined* attributeDeletionMark = m_metaObject->GetDataDeletionMark();
				wxASSERT(attributeDeletionMark);
				ibValueRecordDataObjectRef::GetValueByMetaID(*attributeDeletionMark, deletionMark);
				if (deletionMark.GetBoolean()) {
					ibBackendCoreException::Error(_("Failed to post object in db!"));
					return false;
				}
			}

			ibTransactionGuard db_query_active_transaction = db_query;
			{
				ibBackendValueForm* const valueForm = GetForm();
				{
					db_query_active_transaction.BeginTransaction();

					{
						ibValue cancel = false;
						m_procUnit->CallAsProc(wxT("BeforeWrite"), cancel,
							ibValue::CreateEnumObject<ibValueEnumDocumentWriteMode>(writeMode),
							ibValue::CreateEnumObject<ibValueEnumDocumentPostingMode>(postingMode)
						);

						if (cancel.GetBoolean()) {
							db_query_active_transaction.RollBackTransaction();
							ibBackendCoreException::Error(_("Failed to write object in db!"));
							return false;
						}

						ibValueMetaObjectDocument* dataRef = nullptr;
						if (m_metaObject->ConvertToValue(dataRef)) {
							ibValueMetaObjectAttributePredefined* metaPosted = dataRef->GetDocumentPosted();
							wxASSERT(metaPosted);
							if (writeMode == ibDocumentWriteMode::ibDocumentWriteMode_Posting)
								m_listObjectValue.insert_or_assign(metaPosted->GetMetaID(), true);
							else if (writeMode == ibDocumentWriteMode::ibDocumentWriteMode_UndoPosting)
								m_listObjectValue.insert_or_assign(metaPosted->GetMetaID(), false);
						}
					}

					bool newObject = ibValueRecordDataObjectDocument::IsNewObject();
					bool generateUniqueIdentifier = false;

					if (!IsSetUniqueIdentifier()) {
						ibValue prefix = "", standartProcessing = true;
						m_procUnit->CallAsProc(wxT("SetNewNumber"), prefix, standartProcessing);
						if (standartProcessing.GetBoolean()) {
							generateUniqueIdentifier =
								ibValueRecordDataObjectDocument::GenerateUniqueIdentifier(prefix.GetString());
						}
					}

					//set current date if empty 
					ibValueMetaObjectDocument* dataRef = nullptr;
					if (newObject && m_metaObject->ConvertToValue(dataRef)) {
						const ibValue& docDate = GetValueByMetaID(*dataRef->GetDocumentDate());
						if (docDate.IsEmpty()) {
							SetValueByMetaID(*dataRef->GetDocumentDate(), ibValueSystemFunction::CurrentDate());
						}
					}

					if (!ibValueRecordDataObjectDocument::SaveData()) {
						if (generateUniqueIdentifier)
							ibValueRecordDataObjectDocument::ResetUniqueIdentifier();
						db_query_active_transaction.RollBackTransaction();
						ibBackendCoreException::Error(_("Failed to write object in db!"));
						return false;
					}

					if (newObject) {
						m_registerRecords->CreateRecordSet();
					}

					if (writeMode == ibDocumentWriteMode::ibDocumentWriteMode_Posting) {

						ibValue cancel = false;
						m_procUnit->CallAsProc(wxT("Posting"), cancel,
							ibValue::CreateEnumObject<ibValueEnumDocumentPostingMode>(postingMode)
						);

						if (cancel.GetBoolean()) {
							if (generateUniqueIdentifier)
								ibValueRecordDataObjectDocument::ResetUniqueIdentifier();
							db_query_active_transaction.RollBackTransaction();
							ibBackendCoreException::Error(_("Failed to write object in db!"));
							return false;
						}

						if (!m_registerRecords->WriteRecordSet()) {
							if (generateUniqueIdentifier)
								ibValueRecordDataObjectDocument::ResetUniqueIdentifier();
							db_query_active_transaction.RollBackTransaction();
							ibBackendCoreException::Error(_("Failed to write object in db!"));
							return false;
						}
					}
					else if (writeMode == ibDocumentWriteMode::ibDocumentWriteMode_UndoPosting) {

						ibValue cancel = false;
						m_procUnit->CallAsProc(wxT("UndoPosting"), cancel);
						if (cancel.GetBoolean()) {
							if (generateUniqueIdentifier)
								ibValueRecordDataObjectDocument::ResetUniqueIdentifier();
							db_query_active_transaction.RollBackTransaction();
							ibBackendCoreException::Error(_("Failed to write object in db!"));
							return false;
						}

						if (!m_registerRecords->DeleteRecordSet()) {
							if (generateUniqueIdentifier)
								ibValueRecordDataObjectDocument::ResetUniqueIdentifier();
							db_query_active_transaction.RollBackTransaction();
							ibBackendCoreException::Error(_("Failed to write object in db!"));
							return false;
						}
					}
					{
						ibValue cancel = false;
						m_procUnit->CallAsProc(wxT("OnWrite"), cancel);
						if (cancel.GetBoolean()) {
							if (generateUniqueIdentifier)
								ibValueRecordDataObjectDocument::ResetUniqueIdentifier();
							db_query_active_transaction.RollBackTransaction();
							ibBackendCoreException::Error(_("Failed to write object in db!"));
							return false;
						}
					}

					db_query_active_transaction.CommitTransaction();

					if (newObject && valueForm != nullptr) valueForm->NotifyCreate(GetReference());
					else if (valueForm != nullptr) valueForm->NotifyChange(GetReference());

					m_registerRecords->RefreshRecordSet();
				}

				m_objModified = false;
			}
		}
	}

	return true;
}

bool ibValueRecordDataObjectDocument::DeleteObject()
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

					if (!m_registerRecords->DeleteRecordSet()) {
						db_query_active_transaction.RollBackTransaction();
						ibBackendCoreException::Error(_("Failed to write object in db!"));
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

					if (!DeleteData()) {
						db_query_active_transaction.RollBackTransaction();
						ibBackendCoreException::Error(_("Failed to delete object in db!"));
						return false;
					}

					db_query_active_transaction.CommitTransaction();

					if (valueForm != nullptr) valueForm->NotifyDelete(GetReference());

					m_registerRecords->RefreshRecordSet();
				}
			}
		}
	}

	return true;
}

///////////////////////////////////////////////////////////////////////////////

enum Func {
	eIsNew,
	eCopy,
	eFill,
	eWrite,
	eDelete,
	eModified,
	eGetFormObject,
	enGetTemplate,
	eGetMetadata
};

enum Prop {
	eThisObject,
	eRegisterRecords
};

void ibValueRecordDataObjectDocument::PrepareNames() const
{
	m_methodHelper->ClearHelper();

	m_methodHelper->AppendFunc(wxT("IsNew"), wxT("IsNew()"));
	m_methodHelper->AppendFunc(wxT("Copy"), wxT("Copy()"));
	m_methodHelper->AppendFunc(wxT("Fill"), 1, wxT("Fill(object : any)"));
	m_methodHelper->AppendFunc(wxT("Write"), 2, wxT("Write(writeMode : writeMode, postingMode : postingMode)"));
	m_methodHelper->AppendFunc(wxT("Delete"), wxT("Delete()"));
	m_methodHelper->AppendFunc(wxT("Modified"), wxT("Modified()"));
	m_methodHelper->AppendFunc(wxT("GetFormObject"), 2, wxT("GetFormObject(name : string, owner : any)"));
	m_methodHelper->AppendFunc(wxT("GetTemplate"), 1, wxT("GetTemplate(name : string)"));
	m_methodHelper->AppendFunc(wxT("GetMetadata"), wxT("GetMetadata()"));

	m_methodHelper->AppendProp(wxT("ThisObject"), true, false, eThisObject, eSystem);
	m_methodHelper->AppendProp(wxT("RegisterRecords"), true, false, eRegisterRecords, eSystem);

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
	for (const auto object : m_metaObject->GetTableArrayObject()) {
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

	m_registerRecords->PrepareNames();
}

bool ibValueRecordDataObjectDocument::SetPropVal(const long lPropNum, const ibValue& varPropVal)
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

bool ibValueRecordDataObjectDocument::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
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
		case eRegisterRecords:
			pvarPropVal = m_registerRecords->GetValue();
			return true;
		case eThisObject:
			pvarPropVal = GetValue();
			return true;
		}
	}
	return false;
}

bool ibValueRecordDataObjectDocument::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	switch (lMethodNum)
	{
	case eIsNew:
		pvarRetValue = m_newObject;
		return true;
	case eCopy:
		pvarRetValue = CopyObject();
		return true;
	case eFill:
		FillObject(*paParams[0]);
		return true;
	case eWrite:
		WriteObject(
			lSizeArray > 0 ? paParams[0]->ConvertToEnumValue<ibDocumentWriteMode>() : ibDocumentWriteMode::ibDocumentWriteMode_Write,
			lSizeArray > 1 ? paParams[1]->ConvertToEnumValue<ibDocumentPostingMode>() : ibDocumentPostingMode::ibDocumentPostingMode_RealTime
		);
		return true;
	case eDelete:
		DeleteObject();
		return true;
	case eModified:
		pvarRetValue = m_objModified;
		return true;
	case Func::eGetFormObject:
		pvarRetValue = GetFormValue(
			lSizeArray > 0 ? paParams[0]->GetString() : wxString(wxEmptyString),
			lSizeArray > 1 ? paParams[1]->ConvertToType<ibBackendControlFrame>() : nullptr
		);
		return true;
	case Func::enGetTemplate:
		pvarRetValue = m_metaObject->GetTemplate(paParams[0]->GetString());
		return true;
	case Func::eGetMetadata:
		pvarRetValue = m_metaObject;
		return true;
	}

	return ibModuleDataObject::ExecuteFunc(
		GetMethodName(lMethodNum), pvarRetValue, paParams, lSizeArray
	);
}

enum {
	enWriteRegister = 0
};

void ibValueRecordDataObjectDocument::ibRecorderRegisterDocument::PrepareNames() const
{
	m_methodHelper->ClearHelper();
	m_methodHelper->AppendFunc(wxT("Write"), wxT("Write()"));
	for (auto& pair : m_records) {
		ibValueRecordSetObject* record = pair.second;
		wxASSERT(record);
		ibValueMetaObjectRegisterData* metaObject =
			record->GetMetaObject();
		wxASSERT(metaObject);
		m_methodHelper->AppendProp(
			metaObject->GetName(), true, false, pair.first
		);
	}
}

bool ibValueRecordDataObjectDocument::ibRecorderRegisterDocument::SetPropVal(const long lPropNum, const ibValue& varPropVal)
{
	return false;
}

bool ibValueRecordDataObjectDocument::ibRecorderRegisterDocument::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	auto it = m_records.find(m_methodHelper->GetPropData(lPropNum));
	if (it != m_records.end()) {
		pvarPropVal = it->second;
		return true;
	}
	return false;
}

bool ibValueRecordDataObjectDocument::ibRecorderRegisterDocument::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	switch (lMethodNum)
	{
	case enWriteRegister:
		WriteRecordSet();
		return true;
	}

	return false;
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

SYSTEM_TYPE_REGISTER(ibValueRecordDataObjectDocument::ibRecorderRegisterDocument, "RecorderRegister", string_to_clsid("VL_RGST"));
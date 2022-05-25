////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : document object
////////////////////////////////////////////////////////////////////////////

#include "document.h"
#include "metadata/metadata.h"

#include "appData.h"
#include "reference/reference.h"
#include "frontend/visualView/controls/form.h"
#include "databaseLayer/databaseLayer.h"
#include "compiler/systemObjects.h"

#include "utils/fs/fs.h"
#include "utils/stringUtils.h"

//*********************************************************************************************
//*                                  CRecordRegister	                                      *
//*********************************************************************************************

#include "compiler/methods.h"
#include "reference/reference.h"

void CObjectDocument::CRecordRegister::CreateRecordSet()
{
	CRecordRegister::ClearRecordSet();

	CMetaObjectDocument* metaDocument = wxDynamicCast(
		m_document->GetMetaObject(), CMetaObjectDocument
	);

	wxASSERT(metaDocument);

	auto recordData = metaDocument->GetRecordData();

	IMetadata* metaData = metaDocument->GetMetadata();
	wxASSERT(metaData);
	for (auto id : recordData.GetData()) {
		IMetaObjectRegisterData* metaObject =
			dynamic_cast<IMetaObjectRegisterData*>(metaData->GetMetaObject(id));
		if (metaObject == NULL || !metaObject->IsAllowed())
			continue;
		CMetaDefaultAttributeObject* registerRecord = metaObject->GetRegisterRecorder();
		wxASSERT(registerRecord);
		IRecordSetObject* recordSet = metaObject->CreateRecordSet();
		wxASSERT(recordSet);
		recordSet->SetKeyValue(
			registerRecord->GetMetaID(), m_document->GetReference()
		);
		m_records.insert_or_assign(id, recordSet);
		recordSet->IncrRef();
	}
}

bool CObjectDocument::CRecordRegister::WriteRecordSet()
{
	for (auto pair : m_records) {

		IRecordSetObject* record = pair.second;
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

bool CObjectDocument::CRecordRegister::DeleteRecordSet()
{
	for (auto pair : m_records) {

		IRecordSetObject* record = pair.second;
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

void CObjectDocument::CRecordRegister::ClearRecordSet()
{
	for (auto pair : m_records) {
		IRecordSetObject* record = pair.second;
		wxASSERT(record);
		record->DecrRef();
	}

	m_records.clear();
}

CObjectDocument::CRecordRegister::CRecordRegister(CObjectDocument* currentDoc) :
	CValue(eValueTypes::TYPE_VALUE), m_document(currentDoc), m_methods(new CMethods())
{
	CRecordRegister::CreateRecordSet();
}

CObjectDocument::CRecordRegister::~CRecordRegister()
{
	CRecordRegister::ClearRecordSet();

	wxDELETE(m_methods);
}

//*********************************************************************************************
//*                                  CObjectDocument	                                      *
//*********************************************************************************************

CObjectDocument::CObjectDocument(const CObjectDocument& source) :
	IRecordDataObjectRef(source), m_registerRecords(new CRecordRegister(this))
{
	if (m_registerRecords) {
		m_registerRecords->IncrRef();
	}

	InitializeObject(&source);
}

CObjectDocument::CObjectDocument(CMetaObjectDocument* metaObject) :
	IRecordDataObjectRef(metaObject), m_registerRecords(new CRecordRegister(this))
{
	if (m_registerRecords) {
		m_registerRecords->IncrRef();
	}

	InitializeObject();
}

CObjectDocument::CObjectDocument(CMetaObjectDocument* metaObject, const Guid& guid) :
	IRecordDataObjectRef(metaObject, guid), m_registerRecords(new CRecordRegister(this))
{
	if (m_registerRecords) {
		m_registerRecords->IncrRef();
	}

	InitializeObject();
}

CObjectDocument::~CObjectDocument()
{
	if (m_registerRecords) {
		m_registerRecords->DecrRef();
	}
}

CSourceExplorer CObjectDocument::GetSourceExplorer() const
{
	CSourceExplorer srcHelper(
		m_metaObject, GetClassType(),
		false
	);

	CMetaObjectDocument* metaRef = NULL;

	if (m_metaObject->ConvertToValue(metaRef)) {
		srcHelper.AppendSource(metaRef->GetDocumentNumber(), false);
		srcHelper.AppendSource(metaRef->GetDocumentDate());
	}

	for (auto attribute : m_metaObject->GetObjectAttributes()) {
		srcHelper.AppendSource(attribute);
	}

	for (auto table : m_metaObject->GetObjectTables()) {
		srcHelper.AppendSource(table);
	}

	return srcHelper;
}

void CObjectDocument::ShowFormValue(const wxString& formName, IValueFrame* owner)
{
	CValueForm* foundedForm = GetForm();

	if (foundedForm && foundedForm->IsShown()) {
		foundedForm->ActivateForm();
		return;
	}

	//if form is not initialised then generate  
	CValueForm* valueForm = GetFormValue(formName, owner);

	valueForm->Modify(m_objModified);
	valueForm->ShowForm();
}

CValueForm* CObjectDocument::GetFormValue(const wxString& formName, IValueFrame* ownerControl)
{
	CValueForm* foundedForm = GetForm();

	if (foundedForm)
		return foundedForm;

	IMetaFormObject* defList = NULL;

	if (!formName.IsEmpty()) {
		for (auto metaForm : m_metaObject->GetObjectForms()) {
			if (StringUtils::CompareString(formName, metaForm->GetName())) {
				defList = metaForm; break;
			}
		}
		wxASSERT(defList);
	}
	else {
		defList = m_metaObject->GetDefaultFormByID(CMetaObjectDocument::eFormObject);
	}

	CValueForm* valueForm = NULL;

	if (defList) {
		valueForm = defList->GenerateFormAndRun(
			ownerControl, this, m_objGuid
		);
		valueForm->Modify(m_objModified);
	}
	else {
		valueForm = new CValueForm();
		valueForm->InitializeForm(ownerControl, NULL,
			this, m_objGuid
		);
		valueForm->ReadProperty();
		valueForm->BuildForm(CMetaObjectDocument::eFormObject);
		valueForm->Modify(m_objModified);
	}

	return valueForm;
}

#include "compiler/systemObjects.h"

//***********************************************************************************************
//*                                   Document events                                            *
//***********************************************************************************************

void CObjectDocument::FillObject(CValue& vFillObject)
{
	if (appData->EnterpriseMode()) {
		m_procUnit->CallFunction("Filling", vFillObject);
	}
}

CValue CObjectDocument::CopyObject()
{
	if (appData->EnterpriseMode()) {
		return CopyObjectValue();
	}

	return CValue();
}

#include "common/docManager.h"

bool CObjectDocument::WriteObject(eDocumentWriteMode writeMode, eDocumentPostingMode postingMode)
{
	if (appData->EnterpriseMode())
	{
		if (!CTranslateError::IsSimpleMode())
		{
			CValueForm* valueForm = GetForm();

			{
				databaseLayer->BeginTransaction();

				{
					CValueEnumDocumentWriteMode* wMode = new CValueEnumDocumentWriteMode(writeMode);
					wMode->IncrRef();
					CValueEnumDocumentPostingMode* pMode = new CValueEnumDocumentPostingMode(postingMode);
					pMode->IncrRef();

					CValue cancel = false;
					m_procUnit->CallFunction("BeforeWrite", cancel, CValue(wMode), CValue(pMode));

					writeMode = wMode->GetEnumValue();
					wMode->DecrRef();
					postingMode = pMode->GetEnumValue();
					pMode->DecrRef();

					if (cancel.GetBoolean()) {
						databaseLayer->RollBack(); CSystemObjects::Raise(_("failed to write object in db!"));
						return false;
					}

					CMetaObjectDocument* dataRef = NULL;

					if (m_metaObject->ConvertToValue(dataRef)) {
						CMetaDefaultAttributeObject* metaPosted = dataRef->GetDocumentPosted();
						wxASSERT(metaPosted);
						if (writeMode == eDocumentWriteMode::ePosting)
							m_aObjectValues.insert_or_assign(metaPosted->GetMetaID(), true);
						else if (writeMode == eDocumentWriteMode::eUndoPosting)
							m_aObjectValues.insert_or_assign(metaPosted->GetMetaID(), false);
					}
				}

				bool needUpdateRecords = CObjectDocument::IsNewObject();

				if (!CObjectDocument::SaveData()) {
					databaseLayer->RollBack(); CSystemObjects::Raise(_("failed to write object in db!"));
					return false;
				}

				if (needUpdateRecords) {
					m_registerRecords->CreateRecordSet();
				}

				if (writeMode == eDocumentWriteMode::ePosting) {

					CValueEnumDocumentPostingMode* pMode =
						new CValueEnumDocumentPostingMode(postingMode);
					pMode->IncrRef();

					CValue cancel = false;
					m_procUnit->CallFunction("Posting", cancel, CValue(pMode));

					postingMode = pMode->GetEnumValue();
					pMode->DecrRef();

					if (cancel.GetBoolean()) {
						databaseLayer->RollBack(); CSystemObjects::Raise(_("failed to write object in db!"));
						return false;
					}

					if (!m_registerRecords->WriteRecordSet()) {
						databaseLayer->RollBack(); CSystemObjects::Raise(_("failed to write object in db!"));
						return false;
					}
				}
				else if (writeMode == eDocumentWriteMode::eUndoPosting) {

					CValue cancel = false;
					m_procUnit->CallFunction("UndoPosting", cancel);
					if (cancel.GetBoolean()) {
						databaseLayer->RollBack(); CSystemObjects::Raise(_("failed to write object in db!"));
						return false;
					}

					if (!m_registerRecords->DeleteRecordSet()) {
						databaseLayer->RollBack(); CSystemObjects::Raise(_("failed to write object in db!"));
						return false;
					}
				}
				{
					CValue cancel = false;
					m_procUnit->CallFunction("OnWrite", cancel);
					if (cancel.GetBoolean()) {
						databaseLayer->RollBack(); CSystemObjects::Raise(_("failed to write object in db!"));
						return false;
					}
				}

				databaseLayer->Commit();

				for (auto doc : docManager->GetDocumentsVector()) {
					doc->UpdateAllViews();
				}

				if (valueForm) {
					valueForm->UpdateForm();
					valueForm->Modify(false);
				}
			}

			m_objModified = false;
		}
	}

	return true;
}

bool CObjectDocument::DeleteObject()
{
	if (appData->EnterpriseMode())
	{
		if (!CTranslateError::IsSimpleMode())
		{
			CValueForm* valueForm = GetForm();

			{
				databaseLayer->BeginTransaction();

				{
					CValue cancel = false;
					m_procUnit->CallFunction("BeforeDelete", cancel);
					if (cancel.GetBoolean()) {
						databaseLayer->RollBack(); CSystemObjects::Raise(_("failed to delete object in db!")); return false;
					}
				}

				if (!DeleteData()) {
					databaseLayer->RollBack(); CSystemObjects::Raise(_("failed to delete object in db!")); return false;
				}

				{
					CValue cancel = false;
					m_procUnit->CallFunction("OnDelete", cancel);
					if (cancel.GetBoolean()) {
						databaseLayer->RollBack(); CSystemObjects::Raise(_("failed to delete object in db!")); return false;
					}
				}

				databaseLayer->Commit();

				if (valueForm) {
					valueForm->CloseForm();
				}

				for (auto doc : docManager->GetDocumentsVector()) {
					doc->UpdateAllViews();
				}
			}
		}
	}

	return true;
}
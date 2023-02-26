////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : document object
////////////////////////////////////////////////////////////////////////////

#include "document.h"
#include "core/metadata/metadata.h"

#include "appData.h"
#include "reference/reference.h"
#include "frontend/visualView/controls/form.h"
#include <3rdparty/databaseLayer/databaseLayer.h>
#include "core/compiler/systemObjects.h"

#include "utils/fs/fs.h"
#include "utils/stringUtils.h"

//*********************************************************************************************
//*                                  CRecordRegister	                                      *
//*********************************************************************************************


#include "reference/reference.h"

void CObjectDocument::CRecordRegister::CreateRecordSet()
{
	CMetaObjectDocument* metaDocument = wxDynamicCast(
		m_document->GetMetaObject(), CMetaObjectDocument
	);
	wxASSERT(metaDocument);
	auto& recordData = metaDocument->GetRecordData();
	IMetadata* metaData = metaDocument->GetMetadata();
	wxASSERT(metaData);
	CRecordRegister::ClearRecordSet();
	for (auto id : recordData.m_data) {
		IMetaObjectRegisterData* metaObject =
			dynamic_cast<IMetaObjectRegisterData*>(metaData->GetMetaObject(id));
		if (metaObject == NULL || !metaObject->IsAllowed())
			continue;
		CMetaDefaultAttributeObject* registerRecord = metaObject->GetRegisterRecorder();
		wxASSERT(registerRecord);
		IRecordSetObject* recordSet = metaObject->CreateRecordSetObjectValue();
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
	for (auto& pair : m_records) {
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
	for (auto& pair : m_records) {
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
	for (auto& pair : m_records) {
		IRecordSetObject* record = pair.second;
		wxASSERT(record);
		record->DecrRef();
	}

	m_records.clear();
}

CObjectDocument::CRecordRegister::CRecordRegister(CObjectDocument* currentDoc) :
	CValue(eValueTypes::TYPE_VALUE), m_document(currentDoc), m_methodHelper(new CMethodHelper())
{
	CRecordRegister::CreateRecordSet();
}

CObjectDocument::CRecordRegister::~CRecordRegister()
{
	CRecordRegister::ClearRecordSet();
	wxDELETE(m_methodHelper);
}

//*********************************************************************************************
//*                                  CObjectDocument	                                      *
//*********************************************************************************************

CObjectDocument::CObjectDocument(CMetaObjectDocument* metaObject, const Guid& objGuid) :
	IRecordDataObjectRef(metaObject, objGuid), m_registerRecords(new CRecordRegister(this))
{
	if (m_registerRecords != NULL) {
		m_registerRecords->IncrRef();
	}
}

CObjectDocument::CObjectDocument(const CObjectDocument& source) :
	IRecordDataObjectRef(source), m_registerRecords(new CRecordRegister(this))
{
	if (m_registerRecords != NULL) {
		m_registerRecords->IncrRef();
	}
}

CObjectDocument::~CObjectDocument()
{
	if (m_registerRecords != NULL) {
		m_registerRecords->DecrRef();
	}
}

bool CObjectDocument::IsPosted() const
{
	CMetaObjectDocument* metaDocRef = NULL;
	if (m_metaObject->ConvertToValue(metaDocRef)) {
		return GetValueByMetaID(*metaDocRef->GetDocumentPosted()).GetBoolean();
	}
	return false;
}

void CObjectDocument::SetDeletionMark(bool deletionMark)
{
	CMetaObjectDocument* metaDocRef = NULL;
	if (m_metaObject->ConvertToValue(metaDocRef))
		SetValueByMetaID(*metaDocRef->GetDocumentPosted(), false);
	IRecordDataObjectRef::SetDeletionMark(deletionMark);
}

CSourceExplorer CObjectDocument::GetSourceExplorer() const
{
	CSourceExplorer srcHelper(
		m_metaObject, GetTypeClass(),
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

void CObjectDocument::ShowFormValue(const wxString& formName, IControlFrame* owner)
{
	CValueForm* const foundedForm = GetForm();

	if (foundedForm && foundedForm->IsShown()) {
		foundedForm->ActivateForm();
		return;
	}

	//if form is not initialised then generate  
	CValueForm* valueForm = GetFormValue(formName, owner);

	valueForm->Modify(m_objModified);
	valueForm->ShowForm();
}

CValueForm* CObjectDocument::GetFormValue(const wxString& formName, IControlFrame* ownerControl)
{
	CValueForm* const foundedForm = GetForm();

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
		valueForm = new CValueForm(ownerControl, NULL,
			this, m_objGuid
		);
		valueForm->BuildForm(CMetaObjectDocument::eFormObject);
		valueForm->Modify(m_objModified);
	}

	return valueForm;
}

#include "core/compiler/systemObjects.h"

//***********************************************************************************************
//*                                   Document events                                            *
//***********************************************************************************************

#include "core/frontend/docView/docManager.h"

bool CObjectDocument::WriteObject(eDocumentWriteMode writeMode, eDocumentPostingMode postingMode)
{
	if (appData->EnterpriseMode())
	{
		if (!CTranslateError::IsSimpleMode())
		{
			CValueForm * const valueForm = GetForm();

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
						if (writeMode == eDocumentWriteMode::eDocumentWriteMode_Posting)
							m_objectValues.insert_or_assign(metaPosted->GetMetaID(), true);
						else if (writeMode == eDocumentWriteMode::eDocumentWriteMode_UndoPosting)
							m_objectValues.insert_or_assign(metaPosted->GetMetaID(), false);
					}
				}

				bool newObject = CObjectDocument::IsNewObject();

				//set current date if empty 
				CMetaObjectDocument* dataRef = NULL;
				if (newObject && m_metaObject->ConvertToValue(dataRef)) {
					const CValue& docDate = GetValueByMetaID(*dataRef->GetDocumentDate());
					if (docDate.IsEmpty()) {
						SetValueByMetaID(*dataRef->GetDocumentDate(), CSystemObjects::CurrentDate());
					}
				}

				if (!CObjectDocument::SaveData()) {
					databaseLayer->RollBack();
					CSystemObjects::Raise(_("failed to write object in db!"));
					return false;
				}

				if (newObject)
					m_registerRecords->CreateRecordSet();

				if (writeMode == eDocumentWriteMode::eDocumentWriteMode_Posting) {

					CValueEnumDocumentPostingMode* pMode =
						new CValueEnumDocumentPostingMode(postingMode);
					pMode->IncrRef();

					CValue cancel = false;
					m_procUnit->CallFunction("Posting", cancel, CValue(pMode));

					postingMode = pMode->GetEnumValue();
					pMode->DecrRef();

					if (cancel.GetBoolean()) {
						databaseLayer->RollBack();
						CSystemObjects::Raise(_("failed to write object in db!"));
						return false;
					}

					if (!m_registerRecords->WriteRecordSet()) {
						databaseLayer->RollBack();
						CSystemObjects::Raise(_("failed to write object in db!"));
						return false;
					}
				}
				else if (writeMode == eDocumentWriteMode::eDocumentWriteMode_UndoPosting) {

					CValue cancel = false;
					m_procUnit->CallFunction("UndoPosting", cancel);
					if (cancel.GetBoolean()) {
						databaseLayer->RollBack(); CSystemObjects::Raise(_("failed to write object in db!"));
						return false;
					}

					if (!m_registerRecords->DeleteRecordSet()) {
						databaseLayer->RollBack();
						CSystemObjects::Raise(_("failed to write object in db!"));
						return false;
					}
				}
				{
					CValue cancel = false;
					m_procUnit->CallFunction("OnWrite", cancel);
					if (cancel.GetBoolean()) {
						databaseLayer->RollBack();
						CSystemObjects::Raise(_("failed to write object in db!"));
						return false;
					}
				}

				databaseLayer->Commit();

				for (auto doc : docManager->GetDocumentsVector()) {
					doc->UpdateAllViews();
				}

				if (valueForm != NULL) {
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
			CValueForm * const valueForm = GetForm();

			{
				databaseLayer->BeginTransaction();

				{
					CValue cancel = false;
					m_procUnit->CallFunction("BeforeDelete", cancel);
					if (cancel.GetBoolean()) {
						databaseLayer->RollBack();
						CSystemObjects::Raise(_("failed to delete object in db!"));
						return false;
					}
				}

				if (!DeleteData()) {
					databaseLayer->RollBack();
					CSystemObjects::Raise(_("failed to delete object in db!"));
					return false;
				}

				{
					CValue cancel = false;
					m_procUnit->CallFunction("OnDelete", cancel);
					if (cancel.GetBoolean()) {
						databaseLayer->RollBack();
						CSystemObjects::Raise(_("failed to delete object in db!"));
						return false;
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

///////////////////////////////////////////////////////////////////////////////

enum Func {
	eIsNew,
	eCopy,
	eFill,
	eWrite,
	eDelete,
	eModified,
	eGetFormObject,
	eGetMetadata
};

enum Prop {
	eThisObject,
	eRegisterRecords
};

void CObjectDocument::PrepareNames() const
{
	m_methodHelper->ClearHelper();

	m_methodHelper->AppendFunc("isNew", "isNew()");
	m_methodHelper->AppendFunc("copy", "copy()");
	m_methodHelper->AppendFunc("fill", 1, "fill(object)");
	m_methodHelper->AppendFunc("write", "write()");
	m_methodHelper->AppendFunc("delete", "delete()");
	m_methodHelper->AppendFunc("modified", "modified()");
	m_methodHelper->AppendFunc("getFormObject", 2, "getFormObject(string, owner)");
	m_methodHelper->AppendFunc("getMetadata", "getMetadata()");

	m_methodHelper->AppendProp(wxT("thisObject"), true, false, eThisObject, eSystem);
	m_methodHelper->AppendProp(wxT("registerRecords"), true, false, eRegisterRecords, eSystem);

	//fill custom attributes 
	for (auto attribute : m_metaObject->GetGenericAttributes()) {
		if (attribute->IsDeleted())
			continue;
		m_methodHelper->AppendProp(
			attribute->GetName(),
			true,
			!m_metaObject->IsDataReference(attribute->GetMetaID()),
			attribute->GetMetaID(),
			eProperty
		);
	}
	//fill custom tables 
	for (auto table : m_metaObject->GetObjectTables()) {
		if (table->IsDeleted())
			continue;
		m_methodHelper->AppendProp(
			table->GetName(),
			true,
			false,
			table->GetMetaID(),
			eTable
		);
	}
	if (m_procUnit != NULL) {
		byteCode_t* byteCode = m_procUnit->GetByteCode();
		for (auto exportFunction : byteCode->m_aExportFuncList) {
			m_methodHelper->AppendMethod(
				exportFunction.first,
				byteCode->GetNParams(exportFunction.second),
				byteCode->HasRetVal(exportFunction.second),
				exportFunction.second,
				eProcUnit
			);
		}
		for (auto exportVariable : byteCode->m_aExportVarList) {
			m_methodHelper->AppendProp(
				exportVariable.first,
				exportVariable.second,
				eProcUnit
			);
		}
	}
}

bool CObjectDocument::SetPropVal(const long lPropNum, const CValue& varPropVal)
{
	const long lPropAlias = m_methodHelper->GetPropAlias(lPropNum);
	if (lPropAlias == eProcUnit) {
		if (m_procUnit != NULL) {
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

bool CObjectDocument::GetPropVal(const long lPropNum, CValue& pvarPropVal)
{
	const long lPropAlias = m_methodHelper->GetPropAlias(lPropNum);
	if (lPropAlias == eProcUnit) {
		if (m_procUnit != NULL) {
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

bool CObjectDocument::CallAsFunc(const long lMethodNum, CValue& pvarRetValue, CValue** paParams, const long lSizeArray)
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
			lSizeArray > 0 ? paParams[0]->ConvertToEnumType<eDocumentWriteMode>() : eDocumentWriteMode::eDocumentWriteMode_Write,
			lSizeArray > 1 ? paParams[1]->ConvertToEnumType<eDocumentPostingMode>() : eDocumentPostingMode::eDocumentPostingMode_RealTime
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
			paParams[0]->GetString(),
			paParams[1]->ConvertToType<IValueFrame>()
		);
		return true;
	case Func::eGetMetadata:
		pvarRetValue = m_metaObject;
		return true;
	}

	return IModuleInfo::ExecuteFunc(
		GetMethodName(lMethodNum), pvarRetValue, paParams, lSizeArray
	);
}

enum {
	enWriteRegister = 0
};

#include "core/metadata/metadata.h"

void CObjectDocument::CRecordRegister::PrepareNames() const {
	m_methodHelper->ClearHelper();
	m_methodHelper->AppendFunc("write", "write()");
	for (auto& pair : m_records) {
		IRecordSetObject* record = pair.second;
		wxASSERT(record);
		IMetaObjectRegisterData* metaObject =
			record->GetMetaObject();
		wxASSERT(metaObject);
		m_methodHelper->AppendProp(
			metaObject->GetName(), true, false, pair.first
		);
	}
}

bool CObjectDocument::CRecordRegister::SetPropVal(const long lPropNum, const CValue& varPropVal)
{
	return false;
}

bool CObjectDocument::CRecordRegister::GetPropVal(const long lPropNum, CValue& pvarPropVal)
{
	auto foundedIt = m_records.find(m_methodHelper->GetPropData(lPropNum));
	if (foundedIt != m_records.end()) {
		pvarPropVal = foundedIt->second;
		return true;
	}
	return false;
}

bool CObjectDocument::CRecordRegister::CallAsFunc(const long lMethodNum, CValue& pvarRetValue, CValue** paParams, const long lSizeArray)
{
	switch (lMethodNum)
	{
	case enWriteRegister:
		WriteRecordSet();
		return true;
	}

	return false;
}
////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : document object
////////////////////////////////////////////////////////////////////////////

#include "document.h"
#include "backend/metaData.h"

#include "backend/appData.h"
#include "backend/session/session.h"
#include "reference/reference.h"
#include "backend/databaseLayer/connectionPool.h"
#include "backend/system/systemManager.h"


//*********************************************************************************************
//*                                  ibValueRecordDataObjectDocument	                                      *
//*********************************************************************************************

ibValueRecordDataObjectDocument::ibValueRecordDataObjectDocument(const ibValueMetaObjectDocument* metaObject, const ibGuid& objGuid) :
	ibValueRecordDataObjectRecorderRef(metaObject, objGuid)
{
	// Late-bind register cascade — must run from Document's body, not
	// RecorderRef ctor, so the virtual GetRecordDescription dispatches
	// to Document's override (see RecorderRef::InitRegisterRecords docs).
	InitRegisterRecords();
}

ibValueRecordDataObjectDocument::ibValueRecordDataObjectDocument(const ibValueRecordDataObjectDocument& source) :
	ibValueRecordDataObjectRecorderRef(source)
{
	InitRegisterRecords();
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

// SetDeletionMark moved up to ibValueRecordDataObjectRecorderRef —
// common recorder-flavour algorithm. See top of this file.

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

// ShowFormValue / GetFormValue inherited from ibValueRecordDataObject;
// CloseOnOwnerClose comes from ibValueRecordDataObjectRef::OnFormCreated.
// GetCurrentObjectFormID is inline in document.h.

//***********************************************************************************************
//*                       Document hook overrides for RecorderRef scaffold                       *
//***********************************************************************************************

bool ibValueRecordDataObjectDocument::CheckDeletionMarkOnPosting(ibDocumentWriteMode /*wm*/) const
{
	ibValue deletionMark = false;
	ibValueMetaObjectAttributePredefined* attributeDeletionMark = m_metaObject->GetDataDeletionMark();
	wxASSERT(attributeDeletionMark);
	ibValueRecordDataObjectRef::GetValueByMetaID(*attributeDeletionMark, deletionMark);
	return !deletionMark.GetBoolean();  // true = ok to proceed (no DeletionMark)
}

void ibValueRecordDataObjectDocument::ApplyPostedAttributeOnWrite(ibDocumentWriteMode writeMode)
{
	ibValueMetaObjectDocument* dataRef = nullptr;
	if (!m_metaObject->ConvertToValue(dataRef))
		return;
	ibValueMetaObjectAttributePredefined* metaPosted = dataRef->GetDocumentPosted();
	wxASSERT(metaPosted);
	if (writeMode == ibDocumentWriteMode::ibDocumentWriteMode_Posting)
		m_listObjectValue.insert_or_assign(metaPosted->GetMetaID(), true);
	else if (writeMode == ibDocumentWriteMode::ibDocumentWriteMode_UndoPosting)
		m_listObjectValue.insert_or_assign(metaPosted->GetMetaID(), false);
}

void ibValueRecordDataObjectDocument::FillDefaultDateForNew()
{
	ibValueMetaObjectDocument* dataRef = nullptr;
	if (!m_metaObject->ConvertToValue(dataRef))
		return;
	const ibValue& docDate = GetValueByMetaID(*dataRef->GetDocumentDate());
	if (docDate.IsEmpty())
		SetValueByMetaID(*dataRef->GetDocumentDate(), ibValueSystemFunction::CurrentDate());
}

const ibMetaDescription* ibValueRecordDataObjectDocument::GetRecordDescription() const
{
	ibValueMetaObjectDocument* dataRef = nullptr;
	if (!m_metaObject->ConvertToValue(dataRef))
		return nullptr;
	return &dataRef->GetRecordDescription();
}

//***********************************************************************************************
//*                                   Document events                                            *
//***********************************************************************************************
// WriteObject(wm, pm) / DeleteObject scaffold moved up to
// ibValueRecordDataObjectRecorderRef — see top of this file.
// Document keeps only the hook overrides (CheckDeletionMarkOnPosting,
// ApplyPostedAttributeOnWrite, FillDefaultDateForNew, IsPosted).


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
	eGetMetadata,
	eLock,
	eUnlock
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
	m_methodHelper->AppendProc(wxT("Lock"),   wxT("Lock()"));
	m_methodHelper->AppendProc(wxT("Unlock"), wxT("Unlock()"));

	// No system props on the document helper: ThisObject (contextual) and
	// RegisterRecords (exported) are BOUND in InitializeObject
	// (BindContextVariable / BindExportVariable) — the single source for both the
	// editor surface (compile module) and runtime (binder), like ThisForm.

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

	ExportNamesToHelper(m_methodHelper, eProcUnit);
	FillHelperFromBinds(m_methodHelper, eProcUnit);   // ThisObject.RegisterRecords

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
		if (m_procUnit != nullptr &&
			m_procUnit->GetPropVal(GetPropName(lPropNum), pvarPropVal))
			return true;
		// Bound handle (RegisterRecords) — live bind value directly (Designer/runtime).
		if (ibValue* bound = GetBoundValue(GetPropName(lPropNum))) {
			pvarPropVal = bound;
			return true;
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
	case Func::eLock:
		TryAcquireFormLock();
		return true;
	case Func::eUnlock:
		ReleaseFormLock();
		return true;
	}

	return ibRuntimeModuleDataObject::ExecAsFunc(
		GetMethodName(lMethodNum), pvarRetValue, paParams, lSizeArray
	);
}


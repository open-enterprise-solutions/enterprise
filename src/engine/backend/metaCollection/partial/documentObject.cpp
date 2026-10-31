////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : document object
////////////////////////////////////////////////////////////////////////////

#include "document.h"
#include "backend/metaData.h"
#include "backend/system/value/valuePointInTime.h"   // the moment an object can be asked for

#include "backend/appData.h"
#include "backend/session/session.h"
#include "reference/reference.h"
#include "backend/databaseLayer/connectionPool.h"
#include "backend/system/systemManager.h"
#include "backend/query/columnLayout.h"   // the date column the number's period is scanned inside


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
	m_members.Bind(this, &ibValueRecordDataObjectDocument::FillMethods);
}

ibValueRecordDataObjectDocument::ibValueRecordDataObjectDocument(const ibValueRecordDataObjectDocument& source) :
	ibValueRecordDataObjectRecorderRef(source)
{
	InitRegisterRecords();
	m_members.Bind(this, &ibValueRecordDataObjectDocument::FillMethods);
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

const ibSourceExplorer* ibValueRecordDataObjectDocument::GetSourceExplorer() const
{
	if (!m_sourceExplorer.Reset(m_metaObject->GetMetaData()->GetFactoryCountChanges(),
		ibT("Ref"), _("Ref"), m_metaObject->GetMetaID(), GetClassType(),
		false, false))
		return &m_sourceExplorer;   // built at this version of the metadata already — read as it is

	ibValueMetaObjectDocument* metaRef = nullptr;

	if (m_metaObject->ConvertToValue(metaRef)) {
		m_sourceExplorer.AppendColumn(metaRef->GetDocumentNumber()->GetQueryColumn(), false);
		m_sourceExplorer.AppendColumn(metaRef->GetDocumentDate()->GetQueryColumn());
	}

	for (const auto object : m_metaObject->GetAttributeArrayObject()) {
		m_sourceExplorer.AppendColumn(object->GetQueryColumn());
	}

	for (const auto object : m_metaObject->GetTableArrayObject()) {
		if (object != nullptr && !object->IsDeleted()) {
			ibSourceExplorer& tblNode = m_sourceExplorer.AppendTable(object->GetName(), object->GetSynonym(), object->GetMetaID(), object->GetTypeDesc());
			for (ibValueMetaObjectAttributeBase* tblCol : object->GetGenericAttributeArrayObject()) 
				tblNode.AppendColumn(tblCol->GetQueryColumn());
		}
	}

	return &m_sourceExplorer;
}

// ShowFormValue / GetFormValue inherited from ibValueRecordDataObject;
// CloseOnOwnerClose comes from ibValueRecordDataObjectRef::OnFormCreated.
// GetCurrentObjectFormID is inline in document.h.

//***********************************************************************************************
//*                       Document hook overrides for RecorderRef scaffold                       *
//***********************************************************************************************

void ibValueRecordDataObjectDocument::SetPosted(bool posted)
{
	ibValueMetaObjectDocument* dataRef = nullptr;
	if (!m_metaObject->ConvertToValue(dataRef))
		return;
	ibValueMetaObjectAttributePredefined* metaPosted = dataRef->GetDocumentPosted();
	wxASSERT(metaPosted);
	m_listObjectValue.insert_or_assign(metaPosted->GetMetaID(), posted);
}

// What this document writes into, by list — the registers it posts movements to or the sequences it
// registers in. The metatype holds both; this only forwards the question.
const ibMetaDescription* ibValueRecordDataObjectDocument::GetRecordDescription(ibRecorderWrites of) const
{
	ibValueMetaObjectDocument* dataRef = nullptr;
	if (!m_metaObject->ConvertToValue(dataRef))
		return nullptr;
	return &dataRef->GetRecordDescription(of);
}

//***********************************************************************************************
//*                                   Document events                                            *
//***********************************************************************************************
// WriteObject(wm, pm) / DeleteObject scaffold moved up to
// ibValueRecordDataObjectRecorderRef — see top of this file.
// Document keeps only the hook overrides (IsPosted, SetPosted).


///////////////////////////////////////////////////////////////////////////////

enum Func {
	ePointInTime,
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

void ibValueRecordDataObjectDocument::FillMethods(ibMemberTable& helper) const
{
	// Document's own methods; the data members come from the base FillDataMembers.
	// Order is load-bearing — CallAsFunc switches on the method index (eIsNew = 0 …).
	// ⭐ THE SAME MOMENT THE REFERENCE VENDS. An object is asked when it is exactly as its reference
	// is, and both answer with the pair already assembled -- one question, one answer, whichever
	// hand it is asked from.
	helper.AppendFunc(ibT("PointInTime"), ibT("PointInTime()"));
	helper.AppendFunc(ibT("IsNew"), ibT("IsNew()"));
	helper.AppendFunc(ibT("Copy"), ibT("Copy()"));
	helper.AppendFunc(ibT("Fill"), 1, ibT("Fill(object : any)"));
	helper.AppendFunc(ibT("Write"), 2, ibT("Write(writeMode : writeMode, postingMode : postingMode)"));
	helper.AppendFunc(ibT("Delete"), ibT("Delete()"));
	helper.AppendFunc(ibT("Modified"), ibT("Modified()"));
	helper.AppendFunc(ibT("GetFormObject"), 2, ibT("GetFormObject(name : string, owner : any)"));
	helper.AppendFunc(ibT("GetTemplate"), 1, ibT("GetTemplate(name : string)"));
	helper.AppendFunc(ibT("GetMetadata"), ibT("GetMetadata()"));
	helper.AppendProc(ibT("Lock"),   ibT("Lock()"));
	helper.AppendProc(ibT("Unlock"), ibT("Unlock()"));
}

bool ibValueRecordDataObjectDocument::SetPropVal(const long lPropNum, const ibValue& varPropVal)
{
	const long lPropAlias = m_members.GetPropAlias(lPropNum);
	if (lPropAlias == eProcUnit) {
		if (m_procUnit != nullptr) {
			return m_procUnit->SetPropVal(
				GetPropName(lPropNum), varPropVal
			);
		}
	}
	else if (lPropAlias == eProperty) {
		return SetValueByMetaID(
			m_members.GetPropData(lPropNum),
			varPropVal
		);
	}
	return false;
}

bool ibValueRecordDataObjectDocument::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	const long lPropAlias = m_members.GetPropAlias(lPropNum);
	if (lPropAlias == eProcUnit) {
		if (m_procUnit != nullptr &&
			m_procUnit->GetPropVal(GetPropName(lPropNum), pvarPropVal))
			return true;
		// Bound handle (RegisterRecords) — live bind value directly (Designer/runtime).
		if (GetBoundValue(GetPropName(lPropNum), pvarPropVal))
			return true;
	}
	else if (lPropAlias == eProperty || lPropAlias == eTable) {
		const long lPropData = m_members.GetPropData(lPropNum);
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
	case ePointInTime: {
		ibValueMetaObjectDocument* metaDocRef = nullptr;
		ibDateTime when;
		if (m_metaObject->ConvertToValue(metaDocRef) && metaDocRef != nullptr) {
			const ibValue& date = GetValueByMetaID(*metaDocRef->GetDocumentDate());
			if (date.GetType() == ibValueTypes::TYPE_DATE)
				when = date.GetDate();
		}
		pvarRetValue = new ibValuePointInTime(when, GetReference());
		return true;
	}
	case eModified:
		pvarRetValue = m_objModified;
		return true;
	case Func::eGetFormObject:
		pvarRetValue = GetFormValue(
			lSizeArray > 0 ? ibFormRequest(paParams[0]->GetString()) : ibFormRequest(),
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

ibValueRecordDataObjectRef::ibNumberSeriesKey ibValueRecordDataObjectDocument::NumberSeriesKey() const
{
	ibNumberSeriesKey key;
	const ibValueMetaObjectRecordDataRecorderRef* meta = GetMetaObject();
	if (meta == nullptr)
		return key;

	const ibNumberPeriodicity period = meta->GetNumberPeriodicity();
	if (period == ibNumberPeriodicity::ibNumberPeriodicity_Nonperiodical)
		return key;

	// The write dates the document before it asks for a number. An empty date has no period.
	const ibValue date = GetValueByMetaID(*meta->GetDocumentDate());
	if (date.GetType() != ibValueTypes::TYPE_DATE || date.GetDate().IsEmpty())
		return key;
	const ibDateTime when = date.GetDate();

	key.m_interval = ibDocumentNumberInterval(period, when);

	const ibTotalsPeriod unit = ibNumberPeriodicityUnit(period);
	key.m_from = when.BeginOfPeriod(unit).GetValue();
	key.m_to   = when.BeginOfNextPeriod(unit).GetValue();

	const ibValueMetaObjectAttributePredefined* dateAttribute = meta->GetDocumentDate();
	if (dateAttribute != nullptr && dateAttribute->GetQueryColumn() != nullptr) {
		for (const ibColumnSlot& slot : DescribeColumnLayout(dateAttribute->GetQueryColumn())) {
			if (slot.m_role == ibColumnRole::Date) {
				key.m_dateField = slot.m_name;
				break;
			}
		}
	}
	return key;
}


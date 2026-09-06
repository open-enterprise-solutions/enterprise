////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : catalog object
////////////////////////////////////////////////////////////////////////////

#include "catalog.h"
#include "backend/system/value/valuePointInTime.h"   // the moment an object can be asked for
#include "backend/metaData.h"

#include "backend/appData.h"
#include "backend/session/session.h"
#include "reference/reference.h"
#include "backend/databaseLayer/connectionPool.h"
#include "backend/system/systemManager.h"

#include "backend/fileSystem/fs.h"
//*********************************************************************************************
//*                                  ObjectCatalogValue                                       *
//*********************************************************************************************

ibValueRecordDataObjectCatalog::ibValueRecordDataObjectCatalog(const ibValueMetaObjectCatalog* metaObject, const ibGuid& objGuid, ibObjectMode objMode) :
	ibValueRecordDataObjectHierarchyRef(metaObject, objGuid, objMode)
{
	m_members.Bind(this, &ibValueRecordDataObjectCatalog::FillMethods);
}

ibValueRecordDataObjectCatalog::ibValueRecordDataObjectCatalog(const ibValueRecordDataObjectCatalog& source) :
	ibValueRecordDataObjectHierarchyRef(source)
{
	m_members.Bind(this, &ibValueRecordDataObjectCatalog::FillMethods);
}

const ibSourceExplorer* ibValueRecordDataObjectCatalog::GetSourceExplorer() const
{
	m_sourceExplorer.Reset(
		wxT("Ref"), _("Ref"), m_metaObject->GetMetaID(), GetClassType(),
		false
	);

	ibValueMetaObjectCatalog* metaRef = nullptr;

	if (m_metaObject->ConvertToValue(metaRef)) {
		m_sourceExplorer.AppendColumn(metaRef->GetDataCode()->GetQueryColumn(), false);
		m_sourceExplorer.AppendColumn(metaRef->GetDataDescription()->GetQueryColumn());
		ibValueMetaObjectAttributePredefined* defOwner = metaRef->GetCatalogOwner();
		if (defOwner != nullptr && defOwner->GetClsidCount() > 0) {
			m_sourceExplorer.AppendColumn(metaRef->GetCatalogOwner()->GetQueryColumn());
		}
		m_sourceExplorer.AppendColumn(metaRef->GetDataParent()->GetQueryColumn());
	}

	for (const auto object : m_metaObject->GetAttributeArrayObject()) {
		ibItemMode attrUse = object->GetItemMode();
		if (m_objMode == ibObjectMode::OBJECT_ITEM) {
			if (attrUse == ibItemMode::ibItemMode_Item
				|| attrUse == ibItemMode::ibItemMode_Folder_Item) {
				if (!m_metaObject->IsDataReference(object->GetMetaID())) {
					m_sourceExplorer.AppendColumn(object->GetQueryColumn());
				}
			}
		}
		else {
			if (attrUse == ibItemMode::ibItemMode_Folder ||
				attrUse == ibItemMode::ibItemMode_Folder_Item) {
				if (!m_metaObject->IsDataReference(object->GetMetaID())) {
					m_sourceExplorer.AppendColumn(object->GetQueryColumn());
				}
			}
		}
	}

	for (const auto object : m_metaObject->GetTableArrayObject()) {
		ibItemMode tableUse = object->GetTableUse();
		if (m_objMode == ibObjectMode::OBJECT_ITEM) {
			if (tableUse == ibItemMode::ibItemMode_Item
				|| tableUse == ibItemMode::ibItemMode_Folder_Item) {
				if (object != nullptr && !object->IsDeleted()) {
					ibSourceExplorer& tblNode = m_sourceExplorer.AppendTable(object->GetName(), object->GetSynonym(), object->GetMetaID(), object->GetTypeDesc());
					for (ibValueMetaObjectAttributeBase* tblCol : object->GetGenericAttributeArrayObject()) tblNode.AppendColumn(tblCol->GetQueryColumn());
				}
			}
		}
		else {
			if (tableUse == ibItemMode::ibItemMode_Folder ||
				tableUse == ibItemMode::ibItemMode_Folder_Item) {
				if (object != nullptr && !object->IsDeleted()) {
					ibSourceExplorer& tblNode = m_sourceExplorer.AppendTable(object->GetName(), object->GetSynonym(), object->GetMetaID(), object->GetTypeDesc());
					for (ibValueMetaObjectAttributeBase* tblCol : object->GetGenericAttributeArrayObject()) tblNode.AppendColumn(tblCol->GetQueryColumn());
				}
			}
		}
	}

	return &m_sourceExplorer;
}

// ShowFormValue / GetFormValue moved up to HierarchyRef — see
// commonObject.cpp. Catalog only provides GetCurrentObjectFormID
// inline in catalog.h.

//***********************************************************************************************
//*                                   Catalog events                                            *
//***********************************************************************************************

// WriteObject / DeleteObject inherited from
// ibValueRecordDataObjectHierarchyRef — see commonObjectRefQuery.cpp
// for the Phase B template-method scaffold body. Per-type behaviour
// (code generator, predefined-guard) resolves through virtual dispatch
// (GenerateUniqueIdentifier / ResetUniqueIdentifier / metaobject's
// FindPredefinedValue).

enum Func {
	enPointInTime,
	enIsNew,
	enCopy,
	enFill,
	enWrite,
	enDelete,
	enModified,
	enGetForm,
	enGetTemplate,
	enGetMetadata,
	enLock,
	enUnlock
};

//****************************************************************************
//*                              Support methods                             *
//****************************************************************************

void ibValueRecordDataObjectCatalog::FillMethods(ibMemberTable& helper) const
{
	// Catalog's own methods. The data members (attributes / tabular sections /
	// module exports) come from the base FillDataMembers. Order is load-bearing —
	// CallAsFunc switches on the method index (enIsNew = 0 …).
	// ⭐ THE MOMENT, ON EVERY REFERENCE-BASED FAMILY. Being addressed by a reference is the whole
	// qualification: an element of this kind has a place in the data's history, so it can be named
	// as a moment -- for a period boundary, for an ordering, for "everything up to THIS one". The
	// families with a date of their own add it; the rest carry the reference alone, which is an
	// identity with no point on a timeline rather than a date invented to fill the slot.
	helper.AppendFunc(wxT("PointInTime"), wxT("PointInTime()"));
	helper.AppendFunc(wxT("IsNew"), wxT("IsNew()"));
	helper.AppendFunc(wxT("Copy"), wxT("Copy()"));
	helper.AppendFunc(wxT("Fill"), 1, wxT("Fill(object)"));
	helper.AppendFunc(wxT("Write"), wxT("Write()"));
	helper.AppendFunc(wxT("Delete"), wxT("Delete()"));
	helper.AppendFunc(wxT("Modified"), wxT("Modified()"));
	helper.AppendFunc(wxT("GetFormObject"), 3, wxT("GetFormObject(name : string, owner : any , id : guid)"));
	helper.AppendFunc(wxT("GetTemplate"), 1, wxT("GetTemplate(name : string)"));
	helper.AppendFunc(wxT("GetMetadata"), wxT("GetMetadata()"));
	helper.AppendProc(wxT("Lock"),   wxT("Lock()"));
	helper.AppendProc(wxT("Unlock"), wxT("Unlock()"));
}

bool ibValueRecordDataObjectCatalog::SetPropVal(const long lPropNum, const ibValue& varPropVal)
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

bool ibValueRecordDataObjectCatalog::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	const long lPropAlias = m_members.GetPropAlias(lPropNum);
	if (lPropAlias == eProcUnit) {
		if (m_procUnit != nullptr) {
			return m_procUnit->GetPropVal(
				GetPropName(lPropNum), pvarPropVal
			);
		}
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
	case enPointInTime:
		pvarRetValue = new ibValuePointInTime(wxDateTime(), GetReference());
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
	case Func::enLock:
		TryAcquireFormLock();
		return true;
	case Func::enUnlock:
		ReleaseFormLock();
		return true;
	}

	return ibRuntimeModuleDataObject::ExecAsFunc(
		GetMethodName(lMethodNum), pvarRetValue, paParams, lSizeArray
	);
}
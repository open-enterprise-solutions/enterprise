////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : document metaData
////////////////////////////////////////////////////////////////////////////

#include "document.h"
#include "backend/serialize/dataBuilder.h"
#include "backend/system/value/valueDynamicList.h"   // ibValueDynamicList — the document list migrates onto the universal dynamic list
#include "backend/metaData.h"
#include "backend/moduleManager/moduleManager.h"


// (ibValueListDataObjectRefDocument REMOVED — the document list IS the dynamic list now; its default sort by number
//  is a creation-time setting (ibCreateList + GetDocumentNumber), serialized and user-removable. Lists-as-a-class abolished.)

//********************************************************************************************
//*                                      metaData                                            *
//********************************************************************************************

ibValueMetaObjectDocument::ibValueMetaObjectDocument() : ibValueMetaObjectRecordDataMutableRef()
{
	// BeforeWrite — 3-arg Document-specific signature (writeMode/
	// postingMode). The other 5 common hooks are duplicated from the
	// other MutableRef leaves; m_propertyObjectModule lives on each
	// leaf so we register here instead of in the (no-field) base ctor.
	(*m_propertyObjectModule)->SetDefaultProcedure(wxT("BeforeWrite"),  ibContentHelper::eProcedureHelper, { wxT("Cancel"), wxT("WriteMode"), wxT("PostingMode") });
	(*m_propertyObjectModule)->SetDefaultProcedure(wxT("OnWrite"),      ibContentHelper::eProcedureHelper, { wxT("Cancel") });
	(*m_propertyObjectModule)->SetDefaultProcedure(wxT("BeforeDelete"), ibContentHelper::eProcedureHelper, { wxT("Cancel") });
	(*m_propertyObjectModule)->SetDefaultProcedure(wxT("OnDelete"),     ibContentHelper::eProcedureHelper, { wxT("Cancel") });
	(*m_propertyObjectModule)->SetDefaultProcedure(wxT("Filling"),      ibContentHelper::eProcedureHelper, { wxT("Source"), wxT("StandartProcessing") });
	(*m_propertyObjectModule)->SetDefaultProcedure(wxT("OnCopy"),       ibContentHelper::eProcedureHelper, { wxT("Source") });
	(*m_propertyObjectModule)->SetDefaultProcedure(wxT("Posting"),      ibContentHelper::eProcedureHelper, { wxT("Cancel"), wxT("PostingMode") });
	(*m_propertyObjectModule)->SetDefaultProcedure(wxT("UndoPosting"),  ibContentHelper::eProcedureHelper, { wxT("Cancel") });
	(*m_propertyObjectModule)->SetDefaultProcedure(wxT("SetNewNumber"), ibContentHelper::eProcedureHelper, { wxT("Prefix"), wxT("StandartProcessing") });
}

ibValueMetaObjectDocument::~ibValueMetaObjectDocument()
{
	//wxDELETE((*m_propertyAttributeNumber));
	//wxDELETE((*m_propertyAttributeDate));
	//wxDELETE((*m_propertyAttributePosted));
}

ibValueMetaObjectFormBase* ibValueMetaObjectDocument::GetDefaultFormByID(const ibFormID& id) const
{
	if (id == eFormObject && m_propertyDefFormObject->GetValueAsInteger() != wxNOT_FOUND) {
		return FindFormObjectByFilter(m_propertyDefFormObject->GetValueAsInteger());
	}
	else if (id == eFormList && m_propertyDefFormList->GetValueAsInteger() != wxNOT_FOUND) {
		return FindFormObjectByFilter(m_propertyDefFormList->GetValueAsInteger());
	}
	else if (id == eFormSelect && m_propertyDefFormSelect->GetValueAsInteger() != wxNOT_FOUND) {
		return FindFormObjectByFilter(m_propertyDefFormSelect->GetValueAsInteger());
	}

	return nullptr;
}

#include "documentManager.h"

ibValueManagerDataObject* ibValueMetaObjectDocument::CreateManagerDataObjectValue() const
{
	return new ibValueManagerDataObjectDocument(this);
}

#include "backend/appData.h"

ibValueRecordDataObjectRef* ibValueMetaObjectDocument::CreateObjectRefValue(const ibGuid& objGuid) const
{
	ibValueRecordDataObjectDocument* pDataRef = nullptr;
	if (auto* cc = m_metaData->GetCompileCache()) {
		if (!cc->FindCompileModule(m_propertyObjectModule->GetMetaObject(), pDataRef))
			return new ibValueRecordDataObjectDocument(this, objGuid);
	}
	else {
		pDataRef = new ibValueRecordDataObjectDocument(this, objGuid);
	}

	return pDataRef;
}

ibSourceDataObject* ibValueMetaObjectDocument::CreateSourceObject(const ibValueMetaObjectFormBase* metaObject) const
{
	switch (metaObject->GetTypeForm())
	{
	case eFormObject: return CreateObjectValue(); break;
	case eFormList:
		return ibCreateList(GetQueryable(), GetDocumentNumber());   // migrated onto the universal dynamic list
		break;
	case eFormSelect:
		return ibCreateList(GetQueryable(), GetDocumentNumber(), ibDynamicListView_Choice);   // select front-driven — choice mode
		break;
	}

	return nullptr;
}

#pragma region _form_builder_h_
ibBackendValueForm* ibValueMetaObjectDocument::GetObjectForm(const wxString& strFormName, ibBackendControlFrame* ownerControl, const ibUniqueKey& formGuid) const
{
	return ibValueMetaObjectGenericData::CreateAndBuildForm(
		strFormName,
		ibValueMetaObjectDocument::eFormObject,
		ownerControl, CreateObjectValue(),
		formGuid
	);
}

ibBackendValueForm* ibValueMetaObjectDocument::GetListForm(const wxString& strFormName, ibBackendControlFrame* ownerControl, const ibUniqueKey& formGuid) const
{
	return ibValueMetaObjectGenericData::CreateAndBuildForm(
		strFormName,
		ibValueMetaObjectDocument::eFormList,
		ownerControl, ibCreateList(GetQueryable(), GetDocumentNumber()),   // migrated onto the universal dynamic list
		formGuid
	);
}

ibBackendValueForm* ibValueMetaObjectDocument::GetSelectForm(const wxString& strFormName, ibBackendControlFrame* ownerControl, const ibUniqueKey& formGuid) const
{
	return ibValueMetaObjectGenericData::CreateAndBuildForm(
		strFormName,
		ibValueMetaObjectDocument::eFormSelect,
		ownerControl, ibCreateList(GetQueryable(), GetDocumentNumber(), ibDynamicListView_Choice),   // select front-driven — choice mode
		formGuid
	);
}
#pragma endregion

wxString ibValueMetaObjectDocument::GetDataPresentation(const ibValueDataObject* objValue) const
{
	static ibValue vDate, vNumber;
	if (!objValue->GetValueByMetaID((*m_propertyAttributeDate)->GetMetaID(), vDate))
		return wxEmptyString;
	if (!objValue->GetValueByMetaID((*m_propertyAttributeNumber)->GetMetaID(), vNumber))
		return wxEmptyString;
	return GetSynonym() << wxT(" ") << vNumber.GetString() << wxT(" ") << vDate.GetString();
}

//***************************************************************************
//*                       Save & load metaData                              *
//***************************************************************************

bool ibValueMetaObjectDocument::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertyAttributeNumber->GetName(), m_propertyAttributeNumber->GetNodeValue());
	node.SetProperty(m_propertyAttributeDate->GetName(), m_propertyAttributeDate->GetNodeValue());
	node.SetProperty(m_propertyAttributePosted->GetName(), m_propertyAttributePosted->GetNodeValue());

	node.SetProperty(m_propertyObjectModule->GetName(), m_propertyObjectModule->GetNodeValue());
	node.SetProperty(m_propertyManagerModule->GetName(), m_propertyManagerModule->GetNodeValue());

	node.SetValue(m_propertyDefFormObject->GetName(), GetGuidByID(m_propertyDefFormObject->GetValueAsInteger()).str());
	node.SetValue(m_propertyDefFormList->GetName(), GetGuidByID(m_propertyDefFormList->GetValueAsInteger()).str());
	node.SetValue(m_propertyDefFormSelect->GetName(), GetGuidByID(m_propertyDefFormSelect->GetValueAsInteger()).str());

	node.SetProperty(m_propertyRegisterRecord->GetName(), m_propertyRegisterRecord->GetNodeValue());

	return ibValueMetaObjectRecordDataMutableRef::WriteData(node);
}

bool ibValueMetaObjectDocument::ReadData(const ibDataNode& node)
{
	m_propertyAttributeNumber->SetNodeValue(node.GetProperty(m_propertyAttributeNumber->GetName()));
	m_propertyAttributeDate->SetNodeValue(node.GetProperty(m_propertyAttributeDate->GetName()));
	m_propertyAttributePosted->SetNodeValue(node.GetProperty(m_propertyAttributePosted->GetName()));

	m_propertyObjectModule->SetNodeValue(node.GetProperty(m_propertyObjectModule->GetName()));
	m_propertyManagerModule->SetNodeValue(node.GetProperty(m_propertyManagerModule->GetName()));

	m_propertyDefFormObject->SetValue(GetIdByGuid(node.GetValue<wxString>(m_propertyDefFormObject->GetName())));
	m_propertyDefFormList->SetValue(GetIdByGuid(node.GetValue<wxString>(m_propertyDefFormList->GetName())));
	m_propertyDefFormSelect->SetValue(GetIdByGuid(node.GetValue<wxString>(m_propertyDefFormSelect->GetName())));

	m_propertyRegisterRecord->SetNodeValue(node.GetProperty(m_propertyRegisterRecord->GetName()));

	return ibValueMetaObjectRecordDataMutableRef::ReadData(node);
}

//***********************************************************************
//*                           read & save events                        *
//***********************************************************************

bool ibValueMetaObjectDocument::OnCreateMetaObject(ibMetaData* metaData, int flags)
{
	if (!ibValueMetaObjectRecordDataMutableRef::OnCreateMetaObject(metaData, flags))
		return false;

	return (*m_propertyAttributeNumber)->OnCreateMetaObject(metaData, flags) &&
		(*m_propertyAttributeDate)->OnCreateMetaObject(metaData, flags) &&
		(*m_propertyAttributePosted)->OnCreateMetaObject(metaData, flags) &&
		(*m_propertyObjectModule)->OnCreateMetaObject(metaData, flags) &&
		(*m_propertyManagerModule)->OnCreateMetaObject(metaData, flags);
}

bool ibValueMetaObjectDocument::OnLoadMetaObject(ibMetaData* metaData)
{
	if (!(*m_propertyAttributeNumber)->OnLoadMetaObject(metaData))
		return false;

	if (!(*m_propertyAttributeDate)->OnLoadMetaObject(metaData))
		return false;

	if (!(*m_propertyAttributePosted)->OnLoadMetaObject(metaData))
		return false;

	if (!(*m_propertyObjectModule)->OnLoadMetaObject(metaData))
		return false;

	if (!(*m_propertyManagerModule)->OnLoadMetaObject(metaData))
		return false;

	return ibValueMetaObjectRecordDataMutableRef::OnLoadMetaObject(metaData);
}

bool ibValueMetaObjectDocument::OnSaveMetaObject(int flags)
{
	if (!(*m_propertyAttributeNumber)->OnSaveMetaObject(flags))
		return false;

	if (!(*m_propertyAttributeDate)->OnSaveMetaObject(flags))
		return false;

	if (!(*m_propertyAttributePosted)->OnSaveMetaObject(flags))
		return false;

	if (!(*m_propertyObjectModule)->OnSaveMetaObject(flags))
		return false;

	if (!(*m_propertyManagerModule)->OnSaveMetaObject(flags))
		return false;

	return ibValueMetaObjectRecordDataMutableRef::OnSaveMetaObject(flags);
}

bool ibValueMetaObjectDocument::OnDeleteMetaObject()
{
	if (!(*m_propertyAttributeNumber)->OnDeleteMetaObject())
		return false;

	if (!(*m_propertyAttributeDate)->OnDeleteMetaObject())
		return false;

	if (!(*m_propertyAttributePosted)->OnDeleteMetaObject())
		return false;

	if (!(*m_propertyObjectModule)->OnDeleteMetaObject())
		return false;

	if (!(*m_propertyManagerModule)->OnDeleteMetaObject())
		return false;

	return ibValueMetaObjectRecordDataMutableRef::OnDeleteMetaObject();
}

bool ibValueMetaObjectDocument::OnReloadMetaObject()
{

	if (auto* cc = m_metaData->GetCompileCache()) {
		ibValueRecordDataObjectDocument* pDataRef = nullptr;
		if (!cc->FindCompileModule(m_propertyObjectModule->GetMetaObject(), pDataRef)) {
			return true;
		}

		if (pDataRef->InitializeObject()) {
			if (IsDeleted()) pDataRef->ClearRecordSet();
			else pDataRef->UpdateRecordSet();
			return true;
		}

		return false;
	}

	return true;
}

#include "backend/objCtor.h"

bool ibValueMetaObjectDocument::OnBeforeRunMetaObject(int flags)
{

	if (!(*m_propertyAttributeNumber)->OnBeforeRunMetaObject(flags))
		return false;

	if (!(*m_propertyAttributeDate)->OnBeforeRunMetaObject(flags))
		return false;

	if (!(*m_propertyAttributePosted)->OnBeforeRunMetaObject(flags))
		return false;

	if (!(*m_propertyObjectModule)->OnBeforeRunMetaObject(flags))
		return false;

	if (!(*m_propertyManagerModule)->OnBeforeRunMetaObject(flags))
		return false;

	registerSelection();
	return ibValueMetaObjectRecordDataMutableRef::OnBeforeRunMetaObject(flags);
}

bool ibValueMetaObjectDocument::OnAfterRunMetaObject(int flags)
{
	if (!(*m_propertyAttributeNumber)->OnAfterRunMetaObject(flags))
		return false;

	if (!(*m_propertyAttributeDate)->OnAfterRunMetaObject(flags))
		return false;

	if (!(*m_propertyAttributePosted)->OnAfterRunMetaObject(flags))
		return false;

	if (!(*m_propertyObjectModule)->OnAfterRunMetaObject(flags))
		return false;

	if (!(*m_propertyManagerModule)->OnAfterRunMetaObject(flags))
		return false;


	const ibMetaDescription& metaDesc = m_propertyRegisterRecord->GetValueAsMetaDesc();
	for (unsigned int idx = 0; idx < metaDesc.GetTypeCount(); idx++) {
		const ibValueMetaObjectRegisterData* registerData = m_metaData->FindAnyObjectByFilter<ibValueMetaObjectRegisterData>(metaDesc.GetByIdx(idx));
		if (registerData != nullptr) {
			ibValueMetaObjectAttributePredefined* infoRecorder = registerData->GetRegisterRecorder();
			wxASSERT(infoRecorder);
			infoRecorder->GetTypeDesc().AppendMetaType((*m_propertyAttributeReference)->GetTypeDesc());
		}
	}

	if (auto* cc = m_metaData->GetCompileCache()) {

		if (ibValueMetaObjectRecordDataMutableRef::OnAfterRunMetaObject(flags)) {
			return cc->AddCompileModule(m_propertyObjectModule->GetMetaObject(), [this]() -> ibValue* { return CreateObjectValue(); });
		}

		return false;
	}

	return ibValueMetaObjectRecordDataMutableRef::OnAfterRunMetaObject(flags);
}

bool ibValueMetaObjectDocument::OnBeforeCloseMetaObject()
{
	if (!(*m_propertyAttributeNumber)->OnBeforeCloseMetaObject())
		return false;

	if (!(*m_propertyAttributeDate)->OnBeforeCloseMetaObject())
		return false;

	if (!(*m_propertyAttributePosted)->OnBeforeCloseMetaObject())
		return false;

	if (!(*m_propertyObjectModule)->OnBeforeCloseMetaObject())
		return false;

	if (!(*m_propertyManagerModule)->OnBeforeCloseMetaObject())
		return false;


	const ibMetaDescription& metaDesc = m_propertyRegisterRecord->GetValueAsMetaDesc();
	for (unsigned int idx = 0; idx < metaDesc.GetTypeCount(); idx++) {
		const ibValueMetaObjectRegisterData* registerData = m_metaData->FindAnyObjectByFilter<ibValueMetaObjectRegisterData>(metaDesc.GetByIdx(idx));
		if (registerData != nullptr) {
			ibValueMetaObjectAttributePredefined* infoRecorder = registerData->GetRegisterRecorder();
			wxASSERT(infoRecorder);
			infoRecorder->GetTypeDesc().ClearMetaType((*m_propertyAttributeReference)->GetTypeDesc());
		}
	}

	if (auto* cc = m_metaData->GetCompileCache()) {

		if (ibValueMetaObjectRecordDataMutableRef::OnBeforeCloseMetaObject()) {
			{ cc->RemoveCompileModule(m_propertyObjectModule->GetMetaObject()); return true; }
		}

		return false;
	}

	return ibValueMetaObjectRecordDataMutableRef::OnBeforeCloseMetaObject();
}

bool ibValueMetaObjectDocument::OnAfterCloseMetaObject()
{
	if (!(*m_propertyAttributeNumber)->OnAfterCloseMetaObject())
		return false;

	if (!(*m_propertyAttributeDate)->OnAfterCloseMetaObject())
		return false;

	if (!(*m_propertyAttributePosted)->OnAfterCloseMetaObject())
		return false;

	if (!(*m_propertyObjectModule)->OnAfterCloseMetaObject())
		return false;

	if (!(*m_propertyManagerModule)->OnAfterCloseMetaObject())
		return false;

	unregisterSelection();

	return ibValueMetaObjectRecordDataMutableRef::OnAfterCloseMetaObject();
}

//***********************************************************************
//*                             form events                             *
//***********************************************************************

void ibValueMetaObjectDocument::OnCreateFormObject(ibValueMetaObjectFormBase* metaForm)
{
	if (metaForm->GetTypeForm() == ibValueMetaObjectDocument::eFormObject
		&& m_propertyDefFormObject->GetValueAsInteger() == wxNOT_FOUND)
	{
		m_propertyDefFormObject->SetValue(metaForm->GetMetaID());
	}
	else if (metaForm->GetTypeForm() == ibValueMetaObjectDocument::eFormList
		&& m_propertyDefFormList->GetValueAsInteger() == wxNOT_FOUND)
	{
		m_propertyDefFormList->SetValue(metaForm->GetMetaID());
	}
	else if (metaForm->GetTypeForm() == ibValueMetaObjectDocument::eFormSelect
		&& m_propertyDefFormSelect->GetValueAsInteger() == wxNOT_FOUND)
	{
		m_propertyDefFormSelect->SetValue(metaForm->GetMetaID());
	}
}

void ibValueMetaObjectDocument::OnRemoveMetaForm(ibValueMetaObjectFormBase* metaForm)
{
	if (metaForm->GetTypeForm() == ibValueMetaObjectDocument::eFormObject
		&& m_propertyDefFormObject->GetValueAsInteger() == metaForm->GetMetaID())
	{
		m_propertyDefFormObject->SetValue(wxNOT_FOUND);
	}
	else if (metaForm->GetTypeForm() == ibValueMetaObjectDocument::eFormList
		&& m_propertyDefFormList->GetValueAsInteger() == metaForm->GetMetaID())
	{
		m_propertyDefFormList->SetValue(wxNOT_FOUND);
	}
	else if (metaForm->GetTypeForm() == ibValueMetaObjectDocument::eFormSelect
		&& m_propertyDefFormSelect->GetValueAsInteger() == metaForm->GetMetaID())
	{
		m_propertyDefFormSelect->SetValue(wxNOT_FOUND);
	}
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

METADATA_TYPE_REGISTER(ibValueMetaObjectDocument, "Document", g_metaDocumentCLSID);
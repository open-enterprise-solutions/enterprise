#include "informationRegister.h"
#include "backend/serialize/dataBuilder.h"
#include "list/objectList.h"
#include "backend/metaData.h"
#include "backend/moduleManager/moduleManager.h"
#include "backend/query/queryableHooks.h"   // light L4 source registration hooks (slice descriptors)

//***********************************************************************
//*                         metaData                                    * 
//***********************************************************************


/////////////////////////////////////////////////////////////////////////

ibValueMetaObjectInformationRegister::ibValueMetaObjectInformationRegister() : ibValueMetaObjectRegisterData(),
m_metaRecordManager(new ibValueMetaObjectRecordManager())
{
	//set default proc
	(*m_propertyObjectModule)->SetDefaultProcedure(wxT("BeforeWrite"), ibContentHelper::eProcedureHelper, { wxT("Cancel") });
	(*m_propertyObjectModule)->SetDefaultProcedure(wxT("OnWrite"), ibContentHelper::eProcedureHelper, { wxT("Cancel") });
}

ibValueMetaObjectInformationRegister::~ibValueMetaObjectInformationRegister()
{
	wxDELETE(m_metaRecordManager);
}

ibValueMetaObjectFormBase* ibValueMetaObjectInformationRegister::GetDefaultFormByID(const ibFormID& id) const
{
	if (id == eFormRecord && m_propertyDefFormRecord->GetValueAsInteger() != wxNOT_FOUND) {
		return FindFormObjectByFilter(m_propertyDefFormRecord->GetValueAsInteger());
	}
	else if (id == eFormList && m_propertyDefFormList->GetValueAsInteger() != wxNOT_FOUND) {
		return FindFormObjectByFilter(m_propertyDefFormList->GetValueAsInteger());
	}

	return nullptr;
}

#pragma region _form_builder_h_
ibBackendValueForm* ibValueMetaObjectInformationRegister::GetRecordForm(const wxString& strFormName, ibBackendControlFrame* ownerControl, const ibUniqueKey& formGuid) const
{
	return ibValueMetaObjectGenericData::CreateAndBuildForm(
		strFormName,
		ibValueMetaObjectInformationRegister::eFormRecord,
		ownerControl, CreateRecordManagerObjectValue(),
		formGuid
	);
}

ibBackendValueForm* ibValueMetaObjectInformationRegister::GetListForm(const wxString& strFormName, ibBackendControlFrame* ownerControl, const ibUniqueKey& formGuid) const
{
	return ibValueMetaObjectGenericData::CreateAndBuildForm(
		strFormName,
		ibValueMetaObjectInformationRegister::eFormList,
		ownerControl, new ibValueListRegisterObject(this, ibValueMetaObjectInformationRegister::eFormList),
		formGuid
	);
}
#pragma endregion

//***************************************************************************
//*                       Save & load metaData                              *
//***************************************************************************

bool ibValueMetaObjectInformationRegister::WriteData(ibDataNode& node) const
{
	node.SetValue(m_propertyDefFormRecord->GetName(), GetGuidByID(m_propertyDefFormRecord->GetValueAsInteger()).str());
	node.SetValue(m_propertyDefFormList->GetName(), GetGuidByID(m_propertyDefFormList->GetValueAsInteger()).str());

	node.SetProperty(m_propertyWriteMode->GetName(), m_propertyWriteMode->GetNodeValue());
	node.SetProperty(m_propertyPeriodicity->GetName(), m_propertyPeriodicity->GetNodeValue());

	node.SetProperty(m_propertyObjectModule->GetName(), m_propertyObjectModule->GetNodeValue());
	node.SetProperty(m_propertyManagerModule->GetName(), m_propertyManagerModule->GetNodeValue());

	return ibValueMetaObjectRegisterData::WriteData(node);
}

bool ibValueMetaObjectInformationRegister::ReadData(const ibDataNode& node)
{
	m_propertyDefFormRecord->SetValue(GetIdByGuid(node.GetValue<wxString>(m_propertyDefFormRecord->GetName())));
	m_propertyDefFormList->SetValue(GetIdByGuid(node.GetValue<wxString>(m_propertyDefFormList->GetName())));

	m_propertyWriteMode->ReadNodeValue(node.GetProperty(m_propertyWriteMode->GetName()));
	m_propertyPeriodicity->ReadNodeValue(node.GetProperty(m_propertyPeriodicity->GetName()));

	m_propertyObjectModule->ReadNodeValue(node.GetProperty(m_propertyObjectModule->GetName()));
	m_propertyManagerModule->ReadNodeValue(node.GetProperty(m_propertyManagerModule->GetName()));

	return ibValueMetaObjectRegisterData::ReadData(node);
}

//***********************************************************************
//*                           read & save events                        *
//***********************************************************************

#include "backend/appData.h"

bool ibValueMetaObjectInformationRegister::OnCreateMetaObject(ibMetaData* metaData, int flags)
{
	if (!ibValueMetaObjectRegisterData::OnCreateMetaObject(metaData, flags))
		return false;

	return (*m_propertyManagerModule)->OnCreateMetaObject(metaData, flags) &&
		(*m_propertyObjectModule)->OnCreateMetaObject(metaData, flags);
}

bool ibValueMetaObjectInformationRegister::OnLoadMetaObject(ibMetaData* metaData)
{
	if (!(*m_propertyManagerModule)->OnLoadMetaObject(metaData))
		return false;

	if (!(*m_propertyObjectModule)->OnLoadMetaObject(metaData))
		return false;

	return ibValueMetaObjectRegisterData::OnLoadMetaObject(metaData);
}

bool ibValueMetaObjectInformationRegister::OnSaveMetaObject(int flags)
{
	if (!(*m_propertyManagerModule)->OnSaveMetaObject(flags))
		return false;

	if (!(*m_propertyObjectModule)->OnSaveMetaObject(flags))
		return false;

#if _USE_SAVE_METADATA_IN_TRANSACTION == 1
	if (GetWriteRegisterMode() == ibWriteRegisterMode::eSubordinateRecorder) {
		if (!((*m_propertyAttributeRecorder)->GetClsidCount() > 0)) {
			RestructureError(_("! Doesn't have any recorder ") + GetFullName());
			return false;
		}
	}
#endif

	return ibValueMetaObjectRegisterData::OnSaveMetaObject(flags);
}

bool ibValueMetaObjectInformationRegister::OnDeleteMetaObject()
{
	if (!(*m_propertyManagerModule)->OnDeleteMetaObject())
		return false;

	if (!(*m_propertyObjectModule)->OnDeleteMetaObject())
		return false;

	return ibValueMetaObjectRegisterData::OnDeleteMetaObject();
}

bool ibValueMetaObjectInformationRegister::OnReloadMetaObject()
{

	if (auto* cc = m_metaData->GetCompileCache()) {

		ibValueRecordSetObjectInformationRegister* recordSet = nullptr;
		if (cc->FindCompileModule(m_propertyObjectModule->GetMetaObject(), recordSet)) {
			if (!recordSet->InitializeObject())
				return false;
		}

		ibValueRecordManagerObjectInformationRegister* recordManager = nullptr;
		if (cc->FindCompileModule(m_metaRecordManager, recordManager)) {
			if (!recordManager->InitializeObject())
				return false;
		}
	}

	return true;
}

#include "backend/objCtor.h"

bool ibValueMetaObjectInformationRegister::OnBeforeRunMetaObject(int flags)
{
	if (!(*m_propertyManagerModule)->OnBeforeRunMetaObject(flags))
		return false;

	if (!(*m_propertyObjectModule)->OnBeforeRunMetaObject(flags))
		return false;

	registerSelection();

	return ibValueMetaObjectRegisterData::OnBeforeRunMetaObject(flags);
}

bool ibValueMetaObjectInformationRegister::OnAfterRunMetaObject(int flags)
{
	if (!(*m_propertyManagerModule)->OnAfterRunMetaObject(flags))
		return false;

	// Custom virtual-table descriptors (slices). The base records descriptor is registered by
	// ibValueMetaObjectRegisterData::OnAfterRunMetaObject below. Skip the onlyLoadFlag pass.
	if (!(flags & onlyLoadFlag)) {
		ibRegisterQueryableSource(&m_sliceLast);
		ibRegisterQueryableSource(&m_sliceFirst);
	}

	if (!(*m_propertyObjectModule)->OnAfterRunMetaObject(flags))
		return false;


	if (auto* cc = m_metaData->GetCompileCache()) {

		if (ibValueMetaObjectRegisterData::OnAfterRunMetaObject(flags)) {

			if (!cc->AddCompileModule(m_metaRecordManager, CreateRecordManagerObjectValue()))
				return false;

			if (!cc->AddCompileModule(m_propertyObjectModule->GetMetaObject(), CreateRecordSetObjectValue()))
				return false;

			return true;
		}
	}

	return ibValueMetaObjectRegisterData::OnAfterRunMetaObject(flags);
}

bool ibValueMetaObjectInformationRegister::OnBeforeCloseMetaObject()
{
	ibUnregisterQueryableSource(&m_sliceLast);
	ibUnregisterQueryableSource(&m_sliceFirst);

	if (!(*m_propertyManagerModule)->OnBeforeCloseMetaObject())
		return false;

	if (!(*m_propertyObjectModule)->OnBeforeCloseMetaObject())
		return false;


	if (auto* cc = m_metaData->GetCompileCache()) {

		if (ibValueMetaObjectRegisterData::OnBeforeCloseMetaObject()) {

			if (!cc->RemoveCompileModule(m_metaRecordManager))
				return false;

			if (!cc->RemoveCompileModule(m_propertyObjectModule->GetMetaObject()))
				return false;

			return true;
		}
	}

	return ibValueMetaObjectRegisterData::OnBeforeCloseMetaObject();
}

bool ibValueMetaObjectInformationRegister::OnAfterCloseMetaObject()
{
	if (!(*m_propertyManagerModule)->OnAfterCloseMetaObject())
		return false;

	if (!(*m_propertyObjectModule)->OnAfterCloseMetaObject())
		return false;

	unregisterSelection();

	return ibValueMetaObjectRegisterData::OnAfterCloseMetaObject();
}

//***********************************************************************
//*                             form events                             *
//***********************************************************************

void ibValueMetaObjectInformationRegister::OnCreateFormObject(ibValueMetaObjectFormBase* metaForm)
{
	if (metaForm->GetTypeForm() == ibValueMetaObjectInformationRegister::eFormRecord
		&& m_propertyDefFormRecord->GetValueAsInteger() == wxNOT_FOUND)
	{
		m_propertyDefFormRecord->SetValue(metaForm->GetMetaID());
	}
	else if (metaForm->GetTypeForm() == ibValueMetaObjectInformationRegister::eFormList
		&& m_propertyDefFormList->GetValueAsInteger() == wxNOT_FOUND)
	{
		m_propertyDefFormList->SetValue(metaForm->GetMetaID());
	}
}

void ibValueMetaObjectInformationRegister::OnRemoveMetaForm(ibValueMetaObjectFormBase* metaForm)
{
	if (metaForm->GetTypeForm() == ibValueMetaObjectInformationRegister::eFormRecord
		&& m_propertyDefFormRecord->GetValueAsInteger() == metaForm->GetMetaID())
	{
		m_propertyDefFormRecord->SetValue(wxNOT_FOUND);
	}
	else if (metaForm->GetTypeForm() == ibValueMetaObjectInformationRegister::eFormList
		&& m_propertyDefFormList->GetValueAsInteger() == metaForm->GetMetaID())
	{
		m_propertyDefFormList->SetValue(wxNOT_FOUND);
	}
}

#include "informationRegisterManager.h"

ibValueManagerDataObject* ibValueMetaObjectInformationRegister::CreateManagerDataObjectValue() const
{
	return new ibValueManagerDataObjectInformationRegister(this);
}

ibValueRecordSetObject* ibValueMetaObjectInformationRegister::CreateRecordSetObjectRegValue(const ibUniqueKeyPair& uniqueKey) const
{
	if (auto* cc = m_metaData->GetCompileCache()) {
		ibValueRecordSetObject* pDataRef = nullptr;
		if (!cc->FindCompileModule(m_propertyObjectModule->GetMetaObject(), pDataRef)) {
			return new ibValueRecordSetObjectInformationRegister(this, uniqueKey);
		}
		return pDataRef;
	}

	return new ibValueRecordSetObjectInformationRegister(this, uniqueKey);
}

ibValueRecordManagerObject* ibValueMetaObjectInformationRegister::CreateRecordManagerObjectRegValue(const ibUniqueKeyPair& uniqueKey) const
{
	if (auto* cc = m_metaData->GetCompileCache()) {
		ibValueRecordManagerObject* pDataRef = nullptr;
		if (!cc->FindCompileModule(m_metaRecordManager, pDataRef)) {
			return new ibValueRecordManagerObjectInformationRegister(this, uniqueKey);
		}
		return pDataRef;
	}
	return new ibValueRecordManagerObjectInformationRegister(this, uniqueKey);
}

ibSourceDataObject* ibValueMetaObjectInformationRegister::CreateSourceObject(const ibValueMetaObjectFormBase* metaObject) const
{
	switch (metaObject->GetTypeForm())
	{
	case eFormRecord:
		return CreateRecordManagerObjectValue();
	case eFormList:
		return new ibValueListRegisterObject(this, metaObject->GetTypeForm());
	}

	return nullptr;
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

SYSTEM_TYPE_REGISTER(ibValueMetaObjectInformationRegister::ibValueMetaObjectRecordManager, "InformationRecordManager", string_to_clsid("MT_RCMG"));
METADATA_TYPE_REGISTER(ibValueMetaObjectInformationRegister, "InformationRegister", g_metaInformationRegisterCLSID);
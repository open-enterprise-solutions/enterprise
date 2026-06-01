#include "accumulationRegister.h"
#include "list/objectList.h"
#include "backend/metadataConfiguration.h"
#include "backend/moduleManager/moduleManager.h"

//***********************************************************************
//*                         metaData                                    * 
//***********************************************************************


/////////////////////////////////////////////////////////////////////////

ibValueMetaObjectAccumulationRegister::ibValueMetaObjectAccumulationRegister() : ibValueMetaObjectRegisterData()
{
	//set default proc
	(*m_propertyObjectModule)->SetDefaultProcedure(wxT("BeforeWrite"), ibContentHelper::eProcedureHelper, { wxT("Cancel") });
	(*m_propertyObjectModule)->SetDefaultProcedure(wxT("OnWrite"), ibContentHelper::eProcedureHelper, { wxT("Cancel") });
}

ibValueMetaObjectAccumulationRegister::~ibValueMetaObjectAccumulationRegister()
{
	//wxDELETE((*m_propertyAttributibRecordType));
}

ibValueMetaObjectFormBase* ibValueMetaObjectAccumulationRegister::GetDefaultFormByID(const ibFormID& id) const 
{
	if (id == eFormList && m_propertyDefFormList->GetValueAsInteger() != wxNOT_FOUND) {
		return FindFormObjectByFilter(m_propertyDefFormList->GetValueAsInteger());
	}

	return nullptr;
}

#pragma region _form_builder_h_
ibBackendValueForm* ibValueMetaObjectAccumulationRegister::GetListForm(const wxString& strFormName, ibBackendControlFrame* ownerControl, const ibUniqueKey& formGuid) const
{
	return ibValueMetaObjectGenericData::CreateAndBuildForm(
		strFormName,
		ibValueMetaObjectAccumulationRegister::eFormList,
		ownerControl, ibValue::CreateAndPrepareValueRef<ibValueListRegisterObject>(this, ibValueMetaObjectAccumulationRegister::eFormList),
		formGuid
	);
}
#pragma endregion

//***************************************************************************
//*                       Save & load metaData                              *
//***************************************************************************

bool ibValueMetaObjectAccumulationRegister::LoadData(ibReaderMemory& dataReader)
{
	//load default attributes:
	(*m_propertyAttributibRecordType)->LoadMeta(dataReader);

	//load default form 
	m_propertyDefFormList->SetValue(GetIdByGuid(dataReader.r_stringZ()));

	//load data 
	m_propertyRegisterType->SetValue(dataReader.r_u16());

	//load object module
	(*m_propertyObjectModule)->LoadMeta(dataReader);
	(*m_propertyManagerModule)->LoadMeta(dataReader);

	return ibValueMetaObjectRegisterData::LoadData(dataReader);
}

bool ibValueMetaObjectAccumulationRegister::SaveData(ibWriterMemory& dataWritter)
{
	//save default attributes:
	(*m_propertyAttributibRecordType)->SaveMeta(dataWritter);

	//save default form 
	dataWritter.w_stringZ(GetGuidByID(m_propertyDefFormList->GetValueAsInteger()));

	//save data
	dataWritter.w_u16(m_propertyRegisterType->GetValueAsInteger());

	//Save object module
	(*m_propertyObjectModule)->SaveMeta(dataWritter);
	(*m_propertyManagerModule)->SaveMeta(dataWritter);

	//create or update table:
	return ibValueMetaObjectRegisterData::SaveData(dataWritter);
}

//***********************************************************************
//*                           read & save events                        *
//***********************************************************************

#include "backend/appData.h"

bool ibValueMetaObjectAccumulationRegister::OnCreateMetaObject(ibMetaData* metaData, int flags)
{
	if (!ibValueMetaObjectRegisterData::OnCreateMetaObject(metaData, flags))
		return false;

	return (*m_propertyAttributibRecordType)->OnCreateMetaObject(metaData, flags) &&
		(*m_propertyManagerModule)->OnCreateMetaObject(metaData, flags) &&
		(*m_propertyObjectModule)->OnCreateMetaObject(metaData, flags);
}

bool ibValueMetaObjectAccumulationRegister::OnLoadMetaObject(ibMetaData* metaData)
{
	if (!(*m_propertyAttributibRecordType)->OnLoadMetaObject(metaData))
		return false;

	if (!(*m_propertyManagerModule)->OnLoadMetaObject(metaData))
		return false;

	if (!(*m_propertyObjectModule)->OnLoadMetaObject(metaData))
		return false;

	return ibValueMetaObjectRegisterData::OnLoadMetaObject(metaData);
}

bool ibValueMetaObjectAccumulationRegister::OnSaveMetaObject(int flags)
{
	if (!(*m_propertyAttributibRecordType)->OnSaveMetaObject(flags))
		return false;

	if (!(*m_propertyManagerModule)->OnSaveMetaObject(flags))
		return false;

	if (!(*m_propertyObjectModule)->OnSaveMetaObject(flags))
		return false;

#if _USE_SAVE_METADATA_IN_TRANSACTION == 1
	if (!((*m_propertyAttributeRecorder)->GetClsidCount() > 0)) {
		s_restructureInfo.AppendError(_("! Doesn't have any recorder ") + GetFullName());
		return false;
	}
#endif 

	return ibValueMetaObjectRegisterData::OnSaveMetaObject(flags);
}

bool ibValueMetaObjectAccumulationRegister::OnDeleteMetaObject()
{
	if (!(*m_propertyAttributibRecordType)->OnDeleteMetaObject())
		return false;

	if (!(*m_propertyManagerModule)->OnDeleteMetaObject())
		return false;

	if (!(*m_propertyObjectModule)->OnDeleteMetaObject())
		return false;

	return ibValueMetaObjectRegisterData::OnDeleteMetaObject();
}

bool ibValueMetaObjectAccumulationRegister::OnReloadMetaObject()
{

	if (auto* cc = m_metaData->GetCompileCache()) {
		ibValueRecordSetObjectAccumulationRegister* recordSet = nullptr;
		if (cc->FindCompileModule(m_propertyObjectModule->GetMetaObject(), recordSet)) {
			if (!recordSet->InitializeObject())
				return false;
		}
	}

	return true;
}

#include "backend/objCtor.h"

bool ibValueMetaObjectAccumulationRegister::OnBeforeRunMetaObject(int flags)
{
	if (!(*m_propertyAttributibRecordType)->OnBeforeRunMetaObject(flags))
		return false;

	if (!(*m_propertyManagerModule)->OnBeforeRunMetaObject(flags))
		return false;

	if (!(*m_propertyObjectModule)->OnBeforeRunMetaObject(flags))
		return false;

	registerSelection();

	return ibValueMetaObjectRegisterData::OnBeforeRunMetaObject(flags);
}

bool ibValueMetaObjectAccumulationRegister::OnAfterRunMetaObject(int flags)
{
	if (!(*m_propertyAttributibRecordType)->OnAfterRunMetaObject(flags))
		return false;

	if (!(*m_propertyManagerModule)->OnAfterRunMetaObject(flags))
		return false;

	if (!(*m_propertyObjectModule)->OnAfterRunMetaObject(flags))
		return false;


	if (auto* cc = m_metaData->GetCompileCache()) {

		if (ibValueMetaObjectRegisterData::OnAfterRunMetaObject(flags)) {

			if (!cc->AddCompileModule(m_propertyObjectModule->GetMetaObject(), CreateRecordSetObjectValue()))
				return false;

			return true;
		}
	}

	return ibValueMetaObjectRegisterData::OnAfterRunMetaObject(flags);
}

bool ibValueMetaObjectAccumulationRegister::OnBeforeCloseMetaObject()
{
	if (!(*m_propertyAttributibRecordType)->OnBeforeCloseMetaObject())
		return false;

	if (!(*m_propertyManagerModule)->OnBeforeCloseMetaObject())
		return false;

	if (!(*m_propertyObjectModule)->OnBeforeCloseMetaObject())
		return false;


	if (auto* cc = m_metaData->GetCompileCache()) {

		if (ibValueMetaObjectRegisterData::OnBeforeCloseMetaObject()) {

			if (!cc->RemoveCompileModule(m_propertyObjectModule->GetMetaObject()))
				return false;

			return true;
		}
	}

	return ibValueMetaObjectRegisterData::OnBeforeCloseMetaObject();
}

bool ibValueMetaObjectAccumulationRegister::OnAfterCloseMetaObject()
{
	if (!(*m_propertyAttributibRecordType)->OnAfterCloseMetaObject())
		return false;

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

void ibValueMetaObjectAccumulationRegister::OnCreateFormObject(ibValueMetaObjectFormBase* metaForm)
{
	if (metaForm->GetTypeForm() == ibValueMetaObjectAccumulationRegister::eFormList
		&& m_propertyDefFormList->GetValueAsInteger() == wxNOT_FOUND)
	{
		m_propertyDefFormList->SetValue(metaForm->GetMetaID());
	}
}

void ibValueMetaObjectAccumulationRegister::OnRemoveMetaForm(ibValueMetaObjectFormBase* metaForm)
{
	if (metaForm->GetTypeForm() == ibValueMetaObjectAccumulationRegister::eFormList
		&& m_propertyDefFormList->GetValueAsInteger() == metaForm->GetMetaID())
	{
		m_propertyDefFormList->SetValue(wxNOT_FOUND);
	}
}
#include "accumulationRegisterManager.h"

ibValueManagerDataObject* ibValueMetaObjectAccumulationRegister::CreateManagerDataObjectValue() const
{
	return ibValue::CreateAndPrepareValueRef<ibValueManagerDataObjectAccumulationRegister>(this);
}

ibValueRecordSetObject* ibValueMetaObjectAccumulationRegister::CreateRecordSetObjectRegValue(const ibUniqueKeyPair& uniqueKey) const
{
	if (auto* cc = m_metaData->GetCompileCache()) {
		ibValueRecordSetObject* pDataRef = nullptr;
		if (!cc->FindCompileModule(m_propertyObjectModule->GetMetaObject(), pDataRef)) {
			return ibValue::CreateAndPrepareValueRef<ibValueRecordSetObjectAccumulationRegister>(this, uniqueKey);
		}
		return pDataRef;
	}
	return ibValue::CreateAndPrepareValueRef<ibValueRecordSetObjectAccumulationRegister>(this, uniqueKey);
}

ibSourceDataObject* ibValueMetaObjectAccumulationRegister::CreateSourceObject(const ibValueMetaObjectFormBase* metaObject) const
{
	switch (metaObject->GetTypeForm())
	{
	case eFormList:
		return ibValue::CreateAndPrepareValueRef<ibValueListRegisterObject>(this, metaObject->GetTypeForm());
	}

	return nullptr;
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

METADATA_TYPE_REGISTER(ibValueMetaObjectAccumulationRegister, "AccumulationRegister", g_metaAccumulationRegisterCLSID);
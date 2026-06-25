#include "accumulationRegister.h"
#include "backend/serialize/dataBuilder.h"
#include "list/objectList.h"
#include "backend/metadataConfiguration.h"
#include "backend/moduleManager/moduleManager.h"
#include "backend/query/queryableHooks.h"   // light L4 source registration hooks (balance / turnover descriptors)

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
		ownerControl, new ibValueListRegisterObject(this, ibValueMetaObjectAccumulationRegister::eFormList),
		formGuid
	);
}
#pragma endregion

//***************************************************************************
//*                       Save & load metaData                              *
//***************************************************************************

bool ibValueMetaObjectAccumulationRegister::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertyAttributibRecordType->GetName(), m_propertyAttributibRecordType->GetNodeValue());

	node.SetValue(m_propertyDefFormList->GetName(), GetGuidByID(m_propertyDefFormList->GetValueAsInteger()).str());

	node.SetProperty(m_propertyRegisterType->GetName(), m_propertyRegisterType->GetNodeValue());

	node.SetProperty(m_propertyObjectModule->GetName(), m_propertyObjectModule->GetNodeValue());
	node.SetProperty(m_propertyManagerModule->GetName(), m_propertyManagerModule->GetNodeValue());

	return ibValueMetaObjectRegisterData::WriteData(node);
}

bool ibValueMetaObjectAccumulationRegister::ReadData(const ibDataNode& node)
{
	m_propertyAttributibRecordType->ReadNodeValue(node.GetProperty(m_propertyAttributibRecordType->GetName()));

	m_propertyDefFormList->SetValue(GetIdByGuid(node.GetValue<wxString>(m_propertyDefFormList->GetName())));

	m_propertyRegisterType->ReadNodeValue(node.GetProperty(m_propertyRegisterType->GetName()));

	m_propertyObjectModule->ReadNodeValue(node.GetProperty(m_propertyObjectModule->GetName()));
	m_propertyManagerModule->ReadNodeValue(node.GetProperty(m_propertyManagerModule->GetName()));

	return ibValueMetaObjectRegisterData::ReadData(node);
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
		RestructureError(_("! Doesn't have any recorder ") + GetFullName());
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

	// Custom virtual-table descriptors (balances / turnovers). The base records descriptor is
	// registered by ibValueMetaObjectRegisterData::OnAfterRunMetaObject below. Skip onlyLoadFlag.
	if (!(flags & onlyLoadFlag)) {
		ibRegisterQueryableSource(&m_balance);
		ibRegisterQueryableSource(&m_turnover);
	}

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
	ibUnregisterQueryableSource(&m_balance);
	ibUnregisterQueryableSource(&m_turnover);

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
	return new ibValueManagerDataObjectAccumulationRegister(this);
}

ibValueRecordSetObject* ibValueMetaObjectAccumulationRegister::CreateRecordSetObjectRegValue(const ibUniqueKeyPair& uniqueKey) const
{
	if (auto* cc = m_metaData->GetCompileCache()) {
		ibValueRecordSetObject* pDataRef = nullptr;
		if (!cc->FindCompileModule(m_propertyObjectModule->GetMetaObject(), pDataRef)) {
			return new ibValueRecordSetObjectAccumulationRegister(this, uniqueKey);
		}
		return pDataRef;
	}
	return new ibValueRecordSetObjectAccumulationRegister(this, uniqueKey);
}

ibSourceDataObject* ibValueMetaObjectAccumulationRegister::CreateSourceObject(const ibValueMetaObjectFormBase* metaObject) const
{
	switch (metaObject->GetTypeForm())
	{
	case eFormList:
		return new ibValueListRegisterObject(this, metaObject->GetTypeForm());
	}

	return nullptr;
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

METADATA_TYPE_REGISTER(ibValueMetaObjectAccumulationRegister, "AccumulationRegister", g_metaAccumulationRegisterCLSID);
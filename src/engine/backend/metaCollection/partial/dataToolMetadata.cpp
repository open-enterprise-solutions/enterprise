////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : dataTool - metaData
////////////////////////////////////////////////////////////////////////////

#include "dataTool.h"
#include "core/serialize/dataBuilder.h"
#include "backend/metaData.h"
#include "backend/metadataDataTool.h"
#include "backend/moduleManager/moduleManagerExt.h"
#include "backend/session/session.h"


//********************************************************************************************
//*                                      metaData                                            *
//********************************************************************************************

ibValueMetaObjectDataTool::ibValueMetaObjectDataTool() : ibValueMetaObjectRecordDataExt()
{
	(*m_propertyManagerModule)->SetDefaultProcedure(ibT("FormGetProcessing"), ibContentHelper::eProcedureHelper, { ibT("Form"), ibT("Cancel") });
}

ibValueMetaObjectDataTool::~ibValueMetaObjectDataTool()
{
}

ibValueMetaObjectFormBase* ibValueMetaObjectDataTool::GetDefaultFormByID(const ibFormID& id) const
{
	if (id == eFormDataTool && m_propertyDefFormObject->GetValueAsInteger() != ibNOT_FOUND) {
		return FindFormObjectByFilter(m_propertyDefFormObject->GetValueAsInteger());
	}

	return nullptr;
}

#include "dataToolManager.h"

ibValuePtr<ibValueManagerDataObject> ibValueMetaObjectDataTool::CreateManagerDataObjectValue() const
{
	return ibValuePtr<ibValueManagerDataObject>(new ibValueManagerDataObjectDataTool(this));
}

#include "backend/appData.h"

ibValuePtr<ibValueRecordDataObjectExt> ibValueMetaObjectDataTool::CreateObjectExtValue() const
{
	if (IsExternalCreate()) {
		// External data tool — m_objectValue lives on the data tool's own moduleManager,
		// not on session's main-application mm. Pull it from m_metaData
		// (= ibMetaDataDataTool for external data tools).
		auto* extMeta = dynamic_cast<ibMetaDataDataTool*>(m_metaData);
		ibValueModuleManager* mm = extMeta ? extMeta->GetManagerModule() : nullptr;
		return ibValuePtr<ibValueRecordDataObjectExt>(mm ? dynamic_cast<ibValueRecordDataObjectExt*>(mm->GetObjectValue()) : nullptr);
	}

	ibValueRecordDataObjectDataTool* pDataRef = nullptr;
	if (auto* cc = m_metaData->GetCompileCache()) {
		if (cc->FindCompileModule(m_propertyObjectModule->GetMetaObject(), pDataRef))
			return ibValuePtr<ibValueRecordDataObjectExt>(pDataRef);
	}
	return ibValuePtr<ibValueRecordDataObjectExt>(new ibValueRecordDataObjectDataTool(this));
}

ibSourcePtr<ibSourceDataObject> ibValueMetaObjectDataTool::CreateSourceObject(const ibCreateRequest& request, const ibFormID& form_id) const
{
	switch (form_id)
	{
	case eFormDataTool:
		return ibSourcePtr<ibSourceDataObject>(CreateObjectValue());
	}

	return nullptr;
}

#pragma region _form_builder_h_
ibFormPtr<ibBackendValueForm> ibValueMetaObjectDataTool::GetObjectForm(const ibFormRequest& request, ibBackendControlFrame* ownerControl) const
{
	return ibValueMetaObjectGenericData::CreateAndBuildForm(
		request,
		ibValueMetaObjectDataTool::eFormDataTool,
		ownerControl,
		CreateObjectValue()
	);
}
#pragma endregion

//***************************************************************************
//*                       Save & load metaData                              *
//***************************************************************************

bool ibValueMetaObjectDataTool::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertyObjectModule->GetName(), m_propertyObjectModule->GetNodeValue());
	node.SetProperty(m_propertyManagerModule->GetName(), m_propertyManagerModule->GetNodeValue());

	node.SetValue(m_propertyDefFormObject->GetName(), GetGuidByID(m_propertyDefFormObject->GetValueAsInteger()).str());

	return true;
}

bool ibValueMetaObjectDataTool::ReadData(const ibDataNode& node)
{
	m_propertyObjectModule->SetNodeValue(node.GetProperty(m_propertyObjectModule->GetName()));
	m_propertyManagerModule->SetNodeValue(node.GetProperty(m_propertyManagerModule->GetName()));

	m_propertyDefFormObject->SetValue(GetIdByGuid(node.GetValue<ibString>(m_propertyDefFormObject->GetName())));

	return true;
}

//***********************************************************************
//*                           read & save events                        *
//***********************************************************************

#include "backend/appData.h"

bool ibValueMetaObjectDataTool::OnCreateMetaObject(ibMetaData* metaData, int flags)
{
	if (!ibValueMetaObjectRecordDataExt::OnCreateMetaObject(metaData, flags))
		return false;

	return (!IsExternalCreate() ? (*m_propertyManagerModule)->OnCreateMetaObject(metaData, flags) : true) &&
		(*m_propertyObjectModule)->OnCreateMetaObject(metaData, flags);
}

bool ibValueMetaObjectDataTool::OnLoadMetaObject(ibMetaData* metaData)
{
	if (!IsExternalCreate()) {

		if (!(*m_propertyManagerModule)->OnLoadMetaObject(metaData))
			return false;

		if (!(*m_propertyObjectModule)->OnLoadMetaObject(metaData))
			return false;
	}
	else {

		if (!(*m_propertyObjectModule)->OnLoadMetaObject(metaData))
			return false;
	}

	return ibValueMetaObjectRecordDataExt::OnLoadMetaObject(metaData);
}

bool ibValueMetaObjectDataTool::OnSaveMetaObject(int flags)
{
	if (!IsExternalCreate()) {
		if (!(*m_propertyManagerModule)->OnSaveMetaObject(flags))
			return false;
	}

	if (!(*m_propertyObjectModule)->OnSaveMetaObject(flags))
		return false;

	return ibValueMetaObjectRecordDataExt::OnSaveMetaObject(flags);
}

bool ibValueMetaObjectDataTool::OnDeleteMetaObject()
{
	if (!IsExternalCreate()) {
		if (!(*m_propertyManagerModule)->OnDeleteMetaObject())
			return false;
	}

	if (!(*m_propertyObjectModule)->OnDeleteMetaObject())
		return false;

	return ibValueMetaObjectRecordDataExt::OnDeleteMetaObject();
}

bool ibValueMetaObjectDataTool::OnReloadMetaObject()
{
	if (auto* cc = m_metaData->GetCompileCache()) {
		ibValueRecordDataObjectDataTool* pDataRef = nullptr;
		if (!cc->FindCompileModule(m_propertyObjectModule->GetMetaObject(), pDataRef)) {
			return true;
		}
		return pDataRef->InitializeObject();
	}

	return true;
}

bool ibValueMetaObjectDataTool::OnBeforeRunMetaObject(int flags)
{
	if (!IsExternalCreate()) {
		if (!(*m_propertyManagerModule)->OnBeforeRunMetaObject(flags)) {
			return false;
		}
	}

	if (!(*m_propertyObjectModule)->OnBeforeRunMetaObject(flags)) {
		return false;
	}

	return ibValueMetaObjectRecordDataExt::OnBeforeRunMetaObject(flags);
}

bool ibValueMetaObjectDataTool::OnAfterRunMetaObject(int flags)
{
	if (!IsExternalCreate()) {
		if (!(*m_propertyManagerModule)->OnAfterRunMetaObject(flags)) {
			return false;
		}
	}

	if (!(*m_propertyObjectModule)->OnAfterRunMetaObject(flags)) {
		return false;
	}

	if (auto* cc = m_metaData->GetCompileCache()) {
		if (ibValueMetaObjectRecordDataExt::OnAfterRunMetaObject(flags))
			return cc->AddCompileModule(m_propertyObjectModule->GetMetaObject(), [this]() -> ibValue { return CreateObjectValue(); });
		return false;
	}

	return ibValueMetaObjectRecordDataExt::OnAfterRunMetaObject(flags);
}

bool ibValueMetaObjectDataTool::OnBeforeCloseMetaObject()
{
	if (!IsExternalCreate()) {
		if (!(*m_propertyManagerModule)->OnBeforeCloseMetaObject()) {
			return false;
		}
	}

	if (!(*m_propertyObjectModule)->OnBeforeCloseMetaObject()) {
		return false;
	}

	if (auto* cc = m_metaData->GetCompileCache()) {
		if (ibValueMetaObjectRecordDataExt::OnBeforeCloseMetaObject())
			{ cc->RemoveCompileModule(m_propertyObjectModule->GetMetaObject()); return true; }
		return false;
	}

	return ibValueMetaObjectRecordDataExt::OnBeforeCloseMetaObject();
}

bool ibValueMetaObjectDataTool::OnAfterCloseMetaObject()
{
	if (!IsExternalCreate()) {
		if (!(*m_propertyManagerModule)->OnAfterCloseMetaObject())
			return false;
	}

	if (!(*m_propertyObjectModule)->OnAfterCloseMetaObject()) {
		return false;
	}

	return ibValueMetaObjectRecordDataExt::OnAfterCloseMetaObject();
}

//***********************************************************************
//*                             form events                             *
//***********************************************************************

void ibValueMetaObjectDataTool::OnCreateFormObject(ibValueMetaObjectFormBase* metaForm)
{
	if (metaForm->GetTypeForm() == ibValueMetaObjectDataTool::eFormDataTool
		&& m_propertyDefFormObject->GetValueAsInteger() == ibNOT_FOUND)
	{
		m_propertyDefFormObject->SetValue(metaForm->GetMetaID());
	}
}

void ibValueMetaObjectDataTool::OnRemoveMetaForm(ibValueMetaObjectFormBase* metaForm)
{
	if (metaForm->GetTypeForm() == ibValueMetaObjectDataTool::eFormDataTool
		&& m_propertyDefFormObject->GetValueAsInteger() == metaForm->GetMetaID())
	{
		m_propertyDefFormObject->SetValue(ibNOT_FOUND);
	}
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

METADATA_TYPE_REGISTER(ibValueMetaObjectDataTool, "DataTool", g_metaDataToolCLSID);
METADATA_TYPE_REGISTER(ibValueMetaObjectExternalDataTool, "ExternalDataTool", g_metaExternalDataToolCLSID);

////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : dataProcessor - metaData
////////////////////////////////////////////////////////////////////////////

#include "dataProcessor.h"
#include "backend/serialize/dataBuilder.h"
#include "backend/metaData.h"
#include "backend/metadataDataProcessor.h"
#include "backend/moduleManager/moduleManagerExt.h"
#include "backend/session/session.h"


//********************************************************************************************
//*                                      metaData                                            *
//********************************************************************************************

ibValueMetaObjectDataProcessor::ibValueMetaObjectDataProcessor() : ibValueMetaObjectRecordDataExt()
{
}

ibValueMetaObjectDataProcessor::~ibValueMetaObjectDataProcessor()
{
}

ibValueMetaObjectFormBase* ibValueMetaObjectDataProcessor::GetDefaultFormByID(const ibFormID& id) const
{
	if (id == eFormDataProcessor && m_propertyDefFormObject->GetValueAsInteger() != wxNOT_FOUND) {
		return FindFormObjectByFilter(m_propertyDefFormObject->GetValueAsInteger());
	}

	return nullptr;
}

#include "dataProcessorManager.h"

ibValueManagerDataObject* ibValueMetaObjectDataProcessor::CreateManagerDataObjectValue() const
{
	return new ibValueManagerDataObjectDataProcessor(this);
}

#include "backend/appData.h"

ibValueRecordDataObjectExt* ibValueMetaObjectDataProcessor::CreateObjectExtValue() const
{
	if (IsExternalCreate()) {
		// External DP — m_objectValue lives on the DP's own moduleManager,
		// not on session's main-config mm. Pull it from m_metaData
		// (= ibMetaDataDataProcessor for external DPs).
		auto* extMeta = dynamic_cast<ibMetaDataDataProcessor*>(m_metaData);
		ibValueModuleManager* mm = extMeta ? extMeta->GetManagerModule() : nullptr;
		return mm ? dynamic_cast<ibValueRecordDataObjectExt*>(mm->GetObjectValue()) : nullptr;
	}

	ibValueRecordDataObjectDataProcessor* pDataRef = nullptr;
	if (auto* cc = m_metaData->GetCompileCache()) {
		if (cc->FindCompileModule(m_propertyObjectModule->GetMetaObject(), pDataRef))
			return pDataRef;
	}
	return new ibValueRecordDataObjectDataProcessor(this);
}

ibSourceDataObject* ibValueMetaObjectDataProcessor::CreateSourceObject(const ibValueMetaObjectFormBase* metaObject) const
{
	switch (metaObject->GetTypeForm())
	{
	case eFormDataProcessor:
		return CreateObjectValue();
	}

	return nullptr;
}

#pragma region _form_builder_h_
ibBackendValueForm* ibValueMetaObjectDataProcessor::GetObjectForm(const wxString& strFormName, ibBackendControlFrame* ownerControl, const ibUniqueKey& formGuid) const
{
	return ibValueMetaObjectGenericData::CreateAndBuildForm(
		strFormName,
		ibValueMetaObjectDataProcessor::eFormDataProcessor,
		ownerControl,
		CreateObjectValue(),
		formGuid
	);
}
#pragma endregion

//***************************************************************************
//*                       Save & load metaData                              *
//***************************************************************************

bool ibValueMetaObjectDataProcessor::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertyObjectModule->GetName(), m_propertyObjectModule->GetNodeValue());
	node.SetProperty(m_propertyManagerModule->GetName(), m_propertyManagerModule->GetNodeValue());

	node.SetValue(m_propertyDefFormObject->GetName(), GetGuidByID(m_propertyDefFormObject->GetValueAsInteger()).str());

	return true;
}

bool ibValueMetaObjectDataProcessor::ReadData(const ibDataNode& node)
{
	m_propertyObjectModule->SetNodeValue(node.GetProperty(m_propertyObjectModule->GetName()));
	m_propertyManagerModule->SetNodeValue(node.GetProperty(m_propertyManagerModule->GetName()));

	m_propertyDefFormObject->SetValue(GetIdByGuid(node.GetValue<wxString>(m_propertyDefFormObject->GetName())));

	return true;
}

//***********************************************************************
//*                           read & save events                        *
//***********************************************************************

#include "backend/appData.h"

bool ibValueMetaObjectDataProcessor::OnCreateMetaObject(ibMetaData* metaData, int flags)
{
	if (!ibValueMetaObjectRecordDataExt::OnCreateMetaObject(metaData, flags))
		return false;

	return (!IsExternalCreate() ? (*m_propertyManagerModule)->OnCreateMetaObject(metaData, flags) : true) &&
		(*m_propertyObjectModule)->OnCreateMetaObject(metaData, flags);
}

bool ibValueMetaObjectDataProcessor::OnLoadMetaObject(ibMetaData* metaData)
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

bool ibValueMetaObjectDataProcessor::OnSaveMetaObject(int flags)
{
	if (!IsExternalCreate()) {
		if (!(*m_propertyManagerModule)->OnSaveMetaObject(flags))
			return false;
	}

	if (!(*m_propertyObjectModule)->OnSaveMetaObject(flags))
		return false;

	return ibValueMetaObjectRecordDataExt::OnSaveMetaObject(flags);
}

bool ibValueMetaObjectDataProcessor::OnDeleteMetaObject()
{
	if (!IsExternalCreate()) {
		if (!(*m_propertyManagerModule)->OnDeleteMetaObject())
			return false;
	}

	if (!(*m_propertyObjectModule)->OnDeleteMetaObject())
		return false;

	return ibValueMetaObjectRecordDataExt::OnDeleteMetaObject();
}

bool ibValueMetaObjectDataProcessor::OnReloadMetaObject()
{
	if (auto* cc = m_metaData->GetCompileCache()) {
		ibValueRecordDataObjectDataProcessor* pDataRef = nullptr;
		if (!cc->FindCompileModule(m_propertyObjectModule->GetMetaObject(), pDataRef)) {
			return true;
		}
		return pDataRef->InitializeObject();
	}

	return true;
}

bool ibValueMetaObjectDataProcessor::OnBeforeRunMetaObject(int flags)
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

bool ibValueMetaObjectDataProcessor::OnAfterRunMetaObject(int flags)
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
			return cc->AddCompileModule(m_propertyObjectModule->GetMetaObject(), [this]() -> ibValue* { return CreateObjectValue(); });
		return false;
	}

	return ibValueMetaObjectRecordDataExt::OnAfterRunMetaObject(flags);
}

bool ibValueMetaObjectDataProcessor::OnBeforeCloseMetaObject()
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

bool ibValueMetaObjectDataProcessor::OnAfterCloseMetaObject()
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

void ibValueMetaObjectDataProcessor::OnCreateFormObject(ibValueMetaObjectFormBase* metaForm)
{
	if (metaForm->GetTypeForm() == ibValueMetaObjectDataProcessor::eFormDataProcessor
		&& m_propertyDefFormObject->GetValueAsInteger() == wxNOT_FOUND)
	{
		m_propertyDefFormObject->SetValue(metaForm->GetMetaID());
	}
}

void ibValueMetaObjectDataProcessor::OnRemoveMetaForm(ibValueMetaObjectFormBase* metaForm)
{
	if (metaForm->GetTypeForm() == ibValueMetaObjectDataProcessor::eFormDataProcessor
		&& m_propertyDefFormObject->GetValueAsInteger() == metaForm->GetMetaID())
	{
		m_propertyDefFormObject->SetValue(wxNOT_FOUND);
	}
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

METADATA_TYPE_REGISTER(ibValueMetaObjectDataProcessor, "DataProcessor", g_metaDataProcessorCLSID);
METADATA_TYPE_REGISTER(ibValueMetaObjectExternalDataProcessor, "ExternalDataProcessor", g_metaExternalDataProcessorCLSID);

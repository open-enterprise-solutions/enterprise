////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : report - metaData
////////////////////////////////////////////////////////////////////////////

#include "dataReport.h"
#include "backend/serialize/dataBuilder.h"
#include "backend/metaData.h"
#include "backend/metadataReport.h"
#include "backend/moduleManager/moduleManagerExt.h"
#include "backend/session/session.h"


//********************************************************************************************
//*                                      metaData                                            *
//********************************************************************************************

ibValueMetaObjectReport::ibValueMetaObjectReport() : ibValueMetaObjectRecordDataExt()
{
}

ibValueMetaObjectReport::~ibValueMetaObjectReport()
{
}

ibValueMetaObjectFormBase* ibValueMetaObjectReport::GetDefaultFormByID(const ibFormID& id) const
{
	if (id == eFormReport && m_propertyDefFormObject->GetValueAsInteger() != wxNOT_FOUND) {
		return FindFormObjectByFilter(m_propertyDefFormObject->GetValueAsInteger());
	}

	return nullptr;
}

#include "dataReportManager.h"

ibValueManagerDataObject* ibValueMetaObjectReport::CreateManagerDataObjectValue() const
{
	return new ibValueManagerDataObjectReport(this);
}

#include "backend/appData.h"

ibValueRecordDataObjectExt* ibValueMetaObjectReport::CreateObjectExtValue() const
{
	if (IsExternalCreate()) {
		// External DP — m_objectValue lives on the DP's own moduleManager,
		// not on session's main-config mm. Pull it from m_metaData
		// (= ibMetaDataDataProcessor for external DPs).
		auto* extMeta = dynamic_cast<ibMetaDataReport*>(m_metaData);
		ibValueModuleManager* mm = extMeta ? extMeta->GetManagerModule() : nullptr;
		return mm ? dynamic_cast<ibValueRecordDataObjectExt*>(mm->GetObjectValue()) : nullptr;
	}

	ibValueRecordDataObjectReport* pDataRef = nullptr;
	if (auto* cc = m_metaData->GetCompileCache()) {
		if (cc->FindCompileModule(m_propertyObjectModule->GetMetaObject(), pDataRef))
			return pDataRef;
	}
	return new ibValueRecordDataObjectReport(this);
}

ibSourceDataObject* ibValueMetaObjectReport::CreateSourceObject(const ibValueMetaObjectFormBase* metaObject) const
{
	switch (metaObject->GetTypeForm())
	{
	case eFormReport:
		return CreateObjectValue();
	}

	return nullptr;
}

#pragma region _form_builder_h_
ibBackendValueForm* ibValueMetaObjectReport::GetObjectForm(const wxString& strFormName, ibBackendControlFrame* ownerControl, const ibUniqueKey& formGuid) const
{
	return ibValueMetaObjectGenericData::CreateAndBuildForm(
		strFormName,
		ibValueMetaObjectReport::eFormReport,
		ownerControl, CreateObjectValue(),
		formGuid
	);
}
#pragma endregion

//***************************************************************************
//*                       Save & load metaData                              *
//***************************************************************************

bool ibValueMetaObjectReport::WriteData(ibDataNode& node)
{
	node.SetProperty(m_propertyObjectModule->GetName(), m_propertyObjectModule->GetNodeValue());
	node.SetProperty(m_propertyManagerModule->GetName(), m_propertyManagerModule->GetNodeValue());

	node.SetValue(m_propertyDefFormObject->GetName(), GetGuidByID(m_propertyDefFormObject->GetValueAsInteger()).str());

	return true;
}

bool ibValueMetaObjectReport::ReadData(const ibDataNode& node)
{
	m_propertyObjectModule->ReadNodeValue(node.GetProperty(m_propertyObjectModule->GetName()));
	m_propertyManagerModule->ReadNodeValue(node.GetProperty(m_propertyManagerModule->GetName()));

	m_propertyDefFormObject->SetValue(GetIdByGuid(node.GetValue<wxString>(m_propertyDefFormObject->GetName())));

	return true;
}

//***********************************************************************
//*                           read & save events                        *
//***********************************************************************

bool ibValueMetaObjectReport::OnCreateMetaObject(ibMetaData* metaData, int flags)
{
	if (!ibValueMetaObjectRecordDataExt::OnCreateMetaObject(metaData, flags))
		return false;

	return (!IsExternalCreate() ? (*m_propertyManagerModule)->OnCreateMetaObject(metaData, flags) : true) &&
		(*m_propertyObjectModule)->OnCreateMetaObject(metaData, flags);
}

bool ibValueMetaObjectReport::OnLoadMetaObject(ibMetaData* metaData)
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

bool ibValueMetaObjectReport::OnSaveMetaObject(int flags)
{
	if (!IsExternalCreate()) {
		if (!(*m_propertyManagerModule)->OnSaveMetaObject(flags))
			return false;
	}

	if (!(*m_propertyObjectModule)->OnSaveMetaObject(flags))
		return false;

	return ibValueMetaObjectRecordDataExt::OnSaveMetaObject(flags);
}

bool ibValueMetaObjectReport::OnDeleteMetaObject()
{
	if (!IsExternalCreate()) {
		if (!(*m_propertyManagerModule)->OnDeleteMetaObject())
			return false;
	}

	if (!(*m_propertyObjectModule)->OnDeleteMetaObject())
		return false;

	return ibValueMetaObjectRecordDataExt::OnDeleteMetaObject();
}

bool ibValueMetaObjectReport::OnReloadMetaObject()
{
	if (auto* cc = m_metaData->GetCompileCache()) {
		ibValueRecordDataObjectReport* pDataRef = nullptr;
		if (!cc->FindCompileModule(m_propertyObjectModule->GetMetaObject(), pDataRef)) {
			return true;
		}
		return pDataRef->InitializeObject();
	}

	return true;
}

bool ibValueMetaObjectReport::OnBeforeRunMetaObject(int flags)
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

bool ibValueMetaObjectReport::OnAfterRunMetaObject(int flags)
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
			return cc->AddCompileModule(m_propertyObjectModule->GetMetaObject(), CreateObjectValue());
		return false;
	}

	return ibValueMetaObjectRecordDataExt::OnAfterRunMetaObject(flags);
}

bool ibValueMetaObjectReport::OnBeforeCloseMetaObject()
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
			return cc->RemoveCompileModule(m_propertyObjectModule->GetMetaObject());
		return false;
	}

	return ibValueMetaObjectRecordDataExt::OnBeforeCloseMetaObject();
}

bool ibValueMetaObjectReport::OnAfterCloseMetaObject()
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

void ibValueMetaObjectReport::OnCreateFormObject(ibValueMetaObjectFormBase* metaForm)
{
	if (metaForm->GetTypeForm() == ibValueMetaObjectReport::eFormReport
		&& m_propertyDefFormObject->GetValueAsInteger() == wxNOT_FOUND)
	{
		m_propertyDefFormObject->SetValue(metaForm->GetMetaID());
	}
}

void ibValueMetaObjectReport::OnRemoveMetaForm(ibValueMetaObjectFormBase* metaForm)
{
	if (metaForm->GetTypeForm() == ibValueMetaObjectReport::eFormReport
		&& m_propertyDefFormObject->GetValueAsInteger() == metaForm->GetMetaID())
	{
		m_propertyDefFormObject->SetValue(wxNOT_FOUND);
	}
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

METADATA_TYPE_REGISTER(ibValueMetaObjectReport, "Report", g_metaReportCLSID);
METADATA_TYPE_REGISTER(ibValueMetaObjectExternalReport, "ExternalReport", g_metaExternalReportCLSID);

#include "accumulationRegister.h"
#include "backend/serialize/dataBuilder.h"
#include "backend/system/value/valueDynamicList.h"   // ibValueDynamicList — the standard list migrates onto the universal dynamic list
#include "backend/metadataConfiguration.h"
#include "backend/moduleManager/moduleManager.h"
#include "backend/metaData.h"   // ibMetaData::RegisterSource — the register registers its balance / turnover into its OWN config

//***********************************************************************
//*                         metaData                                    * 
//***********************************************************************


/////////////////////////////////////////////////////////////////////////

ibValueMetaObjectAccumulationRegister::ibValueMetaObjectAccumulationRegister() : ibValueMetaObjectRegisterData()
{
	// The two totals tables. Created HERE, as predefined children — which is what reserves their ids:
	// GenerateNewID walks every child in the tree, so from this point on nothing else can be handed
	// the same number. They carry no state beyond that identity, so there is no property to hang them
	// on; the reference is the whole of what the register needs.
	m_totalsBalances = CreateMetaObjectAndSetParent<ibValueMetaObjectTotals>(wxT("BalanceTotals"), _("Balance totals"));
	m_totalsTurnovers = CreateMetaObjectAndSetParent<ibValueMetaObjectTotals>(wxT("TurnoverTotals"), _("Turnover totals"));

	//set default proc
	(*m_propertyObjectModule)->SetDefaultProcedure(wxT("BeforeWrite"), ibContentHelper::eProcedureHelper, { wxT("Cancel") });
	(*m_propertyObjectModule)->SetDefaultProcedure(wxT("OnWrite"), ibContentHelper::eProcedureHelper, { wxT("Cancel") });
}

ibValueMetaObjectAccumulationRegister::~ibValueMetaObjectAccumulationRegister()
{
	//wxDELETE((*m_propertyAttributeRecordType));
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
		ownerControl, ibCreateList(GetQueryable(), GetRegisterPeriod()),   // migrated onto the universal dynamic list
		formGuid
	);
}
#pragma endregion

//***************************************************************************
//*                       Save & load metaData                              *
//***************************************************************************

bool ibValueMetaObjectAccumulationRegister::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertyAttributeRecordType->GetName(), m_propertyAttributeRecordType->GetNodeValue());

	node.SetValue(m_propertyDefFormList->GetName(), GetGuidByID(m_propertyDefFormList->GetValueAsInteger()).str());

	node.SetProperty(m_propertyRegisterType->GetName(), m_propertyRegisterType->GetNodeValue());

	node.SetProperty(m_propertySplitTotals->GetName(), m_propertySplitTotals->GetNodeValue());

	// The two totals tables are written for their IDENTITY alone: the sub-node carries each object's
	// metaID, which is what makes the id survive a save and what the schema differ matches the
	// physical table by. Losing them here would hand every register the same (zero) id on the next
	// load, and ibSchemaSnapshot::Shared would then pour one register's columns into another's table.
	m_totalsBalances->SaveNode(node.Child(wxT("BalanceTotals")));
	m_totalsTurnovers->SaveNode(node.Child(wxT("TurnoverTotals")));

	node.SetProperty(m_propertyObjectModule->GetName(), m_propertyObjectModule->GetNodeValue());
	node.SetProperty(m_propertyManagerModule->GetName(), m_propertyManagerModule->GetNodeValue());

	return ibValueMetaObjectRegisterData::WriteData(node);
}

bool ibValueMetaObjectAccumulationRegister::ReadData(const ibDataNode& node)
{
	m_propertyAttributeRecordType->ReadNodeValue(node.GetProperty(m_propertyAttributeRecordType->GetName()));

	m_propertyDefFormList->SetValue(GetIdByGuid(node.GetValue<wxString>(m_propertyDefFormList->GetName())));

	m_propertyRegisterType->ReadNodeValue(node.GetProperty(m_propertyRegisterType->GetName()));

	m_propertySplitTotals->ReadNodeValue(node.GetProperty(m_propertySplitTotals->GetName()));

	// Absent sub-node = a configuration written before the totals tables existed. The object keeps
	// the id it was given at construction rather than being left at zero.
	if (const ibDataNode* totals = node.FindChild(wxT("BalanceTotals")))
		m_totalsBalances->LoadNode(*totals);
	if (const ibDataNode* totals = node.FindChild(wxT("TurnoverTotals")))
		m_totalsTurnovers->LoadNode(*totals);

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

	// The totals tables are stamped HERE and only here: this is where a predefined child takes its
	// metaID from the configuration's counter, which is also what reserves it against every later
	// GenerateNewID. On a load the id comes from the saved sub-node instead, which is why both
	// directions of WriteData / ReadData carry them.
	return (*m_propertyAttributeRecordType)->OnCreateMetaObject(metaData, flags) &&
		m_totalsBalances->OnCreateMetaObject(metaData, flags) &&
		m_totalsTurnovers->OnCreateMetaObject(metaData, flags) &&
		(*m_propertyManagerModule)->OnCreateMetaObject(metaData, flags) &&
		(*m_propertyObjectModule)->OnCreateMetaObject(metaData, flags);
}

bool ibValueMetaObjectAccumulationRegister::OnLoadMetaObject(ibMetaData* metaData)
{
	if (!(*m_propertyAttributeRecordType)->OnLoadMetaObject(metaData))
		return false;

	if (!m_totalsBalances->OnLoadMetaObject(metaData))
		return false;

	if (!m_totalsTurnovers->OnLoadMetaObject(metaData))
		return false;

	if (!(*m_propertyManagerModule)->OnLoadMetaObject(metaData))
		return false;

	if (!(*m_propertyObjectModule)->OnLoadMetaObject(metaData))
		return false;

	return ibValueMetaObjectRegisterData::OnLoadMetaObject(metaData);
}

bool ibValueMetaObjectAccumulationRegister::OnSaveMetaObject(int flags)
{
	if (!(*m_propertyAttributeRecordType)->OnSaveMetaObject(flags))
		return false;

	if (!m_totalsBalances->OnSaveMetaObject(flags))
		return false;

	if (!m_totalsTurnovers->OnSaveMetaObject(flags))
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
	if (!(*m_propertyAttributeRecordType)->OnDeleteMetaObject())
		return false;

	if (!m_totalsBalances->OnDeleteMetaObject())
		return false;

	if (!m_totalsTurnovers->OnDeleteMetaObject())
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
	if (!(*m_propertyAttributeRecordType)->OnBeforeRunMetaObject(flags))
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
	if (!(*m_propertyAttributeRecordType)->OnAfterRunMetaObject(flags))
		return false;

	if (!(*m_propertyManagerModule)->OnAfterRunMetaObject(flags))
		return false;

	if (!(*m_propertyObjectModule)->OnAfterRunMetaObject(flags))
		return false;

	// Custom virtual-table descriptors (balances / turnovers). The base records descriptor is registered by
	// ibValueMetaObjectRegisterData::OnAfterRunMetaObject below. Register ALWAYS — the factory is PER-CONFIG
	// (in the metadata), so a read-only DB load (onlyLoadFlag) still registers its OWN sources into its OWN factory.
	m_metaData->RegisterSource(&m_balance);
	m_metaData->RegisterSource(&m_turnover);
	m_metaData->RegisterSource(&m_balanceAndTurnover);

	if (auto* cc = m_metaData->GetCompileCache()) {

		if (ibValueMetaObjectRegisterData::OnAfterRunMetaObject(flags)) {

			if (!cc->AddCompileModule(m_propertyObjectModule->GetMetaObject(), [this]() -> ibValue* { return CreateRecordSetObjectValue(); }))
				return false;

			return true;
		}
	}

	return ibValueMetaObjectRegisterData::OnAfterRunMetaObject(flags);
}

bool ibValueMetaObjectAccumulationRegister::OnBeforeCloseMetaObject()
{
	m_metaData->UnregisterSource(&m_balance);
	m_metaData->UnregisterSource(&m_turnover);
	m_metaData->UnregisterSource(&m_balanceAndTurnover);

	if (!(*m_propertyAttributeRecordType)->OnBeforeCloseMetaObject())
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
	if (!(*m_propertyAttributeRecordType)->OnAfterCloseMetaObject())
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
		return ibCreateList(GetQueryable(), GetRegisterPeriod());   // migrated onto the universal dynamic list
	}

	return nullptr;
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

METADATA_TYPE_REGISTER(ibValueMetaObjectAccumulationRegister, "AccumulationRegister", g_metaAccumulationRegisterCLSID);

// The totals table's own type. Registered because every metaobject is built through the factory —
// CreateMetaObjectAndSetParent goes through it too — not because anything ever asks for one by name:
// it is nested in the register, absent from ResolveChild, and therefore unreachable from the
// metadata tree, from copy/paste and from script. Hence no named clsid constant either: the id is
// the hash of the name right here, and there is no second place that needs to spell it.
METADATA_TYPE_REGISTER(ibValueMetaObjectAccumulationRegister::ibValueMetaObjectTotals, "AccumulationRegisterTotals");
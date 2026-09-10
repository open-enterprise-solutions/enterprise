#include "calculationRegister.h"
#include "backend/serialize/dataBuilder.h"
#include "backend/system/value/valueDynamicList.h"   // ibValueDynamicList — the standard list migrates onto the universal dynamic list
#include "backend/metaData.h"
#include "backend/moduleManager/moduleManager.h"
#include "backend/objCtor.h"   // ibCtorMetaValueType — the type the metadata REGISTERED for the chart
#include "backend/clsid.h"   // reference_to_clsid — the calc-type attribute's type is a reference into the bound chart
#include "chartOfCalculationTypes.h"   // the bound chart — resolved, asked for its metaID and for its relations

// Bind the register to a chart of calculation types: the CalculationType standard attribute becomes a
// reference into that chart. Empty metaID leaves it untyped (no chart bound yet).
//
// 🛑⭐⭐ THE TYPE IS ASKED FOR, NOT COMPUTED. The import spelled this `reference_to_clsid(metaID)` —
// building the class id from the metaobject's id by hand. That id is not the one the metadata REGISTERED
// for the chart, and the difference is invisible until something derives a COLUMN LAYOUT from it: the
// register's table was then created with one set of reference fields and re-declared with another, so
// every apply after the first tried to ADD `fld<id>_RTRef` to a table that already had it and the whole
// restructuring rolled back. MEASURED 2026-09-10: a minimal calculation register applied once cleanly
// and refused every apply afterwards, while an accounting register — same shape, chart-bound reference
// attributes — survived, because it asks `GetTypeCtor(chart, Reference)` for the type.
void ibValueMetaObjectCalculationRegister::SetChartOfCalculationTypes(const ibMetaID& chartMetaID)
{
	if (chartMetaID == wxNOT_FOUND || m_metaData == nullptr)
		return;
	const ibValueMetaObject* chart = m_metaData->FindAnyObjectByFilter(chartMetaID);
	if (chart == nullptr)
		return;
	const ibCtorMetaValueType* ctor =
		m_metaData->GetTypeCtor(chart, ibCtorObjectMetaType::ibCtorObjectMetaType_Reference);
	if (ctor == nullptr)
		return;
	m_propertyAttributeCalculationType->GetMetaObject()->GetTypeDesc().SetDefaultMetaType(ctor->GetClassType());
}

// WHICH chart, resolved from the property. Exactly one: a register's calculation types, their
// displacement relation and therefore their priorities all come from one chart, and two would answer
// those questions twice with the engine picking one silently. Shaped after
// ibValueMetaObjectAccountingRegister::GetChartOfAccounts.
const ibValueMetaObjectChartOfCalculationTypes* ibValueMetaObjectCalculationRegister::GetChartOfCalculationTypes() const
{
	const ibMetaDescription& metaDesc = m_propertyChartOfCalculationTypes->GetValueAsMetaDesc();
	for (unsigned int idx = 0; idx < metaDesc.GetTypeCount(); idx++) {
		const ibValueMetaObject* found = m_metaData != nullptr
			? m_metaData->FindAnyObjectByFilter(metaDesc.GetByIdx(idx)) : nullptr;
		const ibValueMetaObjectChartOfCalculationTypes* chart = nullptr;
		if (found != nullptr && found->ConvertToValue(chart) && chart != nullptr)
			return chart;   // the first one IS the one — the save below refuses a second
	}
	return nullptr;
}

// 🛑⭐ ONE PLACE, AND IT IS THE RUN. The neighbour settles this: the accounting register declares its
// Account type in OnAfterRunMetaObject and nowhere else. I first spread this over load, reload and
// property-change — "wherever it can have changed" — and that is the wrong shape for a DECLARATION:
// each of those roads re-states the type at a different moment relative to when the schema is taken,
// so the schema can be taken between two statements of it. The run is the moment everything that
// reads the type comes up, so declaring it there answers every reader once.
//
// The property-change road stays, and only that one: a designer who picks a chart must see the type
// follow immediately rather than after a restart.
void ibValueMetaObjectCalculationRegister::ApplyChartBinding()
{
	const ibValueMetaObjectChartOfCalculationTypes* chart = GetChartOfCalculationTypes();
	if (chart == nullptr)
		return;
	SetChartOfCalculationTypes(chart->GetMetaID());
}

// THE DISPLACEMENT RELATION, as the chart declares it: one edge per row of a calculation type's Displacing
// section — {the type the row belongs to, the type the row names}. Read as DATA, in one statement over the
// whole section, the way the accounting register reads its chart's analytics kinds
// (accountingRegisterMetadataTotals.cpp): opening each type's card instead would materialise a runtime
// object per type, which needs a session a background write may not have.
void ibValueMetaObjectCalculationRegister::ReadDisplacementRelation(std::map<ibValue, int>& typeIndex,
                                                                    std::vector<std::pair<int, int>>& edges) const
{
	const ibValueMetaObjectChartOfCalculationTypes* chart = GetChartOfCalculationTypes();
	if (chart == nullptr)
		return;
	// {owner of the row, type the row names} = {displaced, displacer}
	chart->ReadRelation(chart->GetDisplacingTable(), typeIndex, edges);
}

//***********************************************************************
//*                         metaData                                    *
//***********************************************************************


/////////////////////////////////////////////////////////////////////////

ibValueMetaObjectCalculationRegister::ibValueMetaObjectCalculationRegister() : ibValueMetaObjectRegisterData()
{
	//set default proc
	(*m_propertyObjectModule)->SetDefaultProcedure(wxT("BeforeWrite"), ibContentHelper::eProcedureHelper, { wxT("Cancel") });
	(*m_propertyObjectModule)->SetDefaultProcedure(wxT("OnWrite"), ibContentHelper::eProcedureHelper, { wxT("Cancel") });
}

ibValueMetaObjectCalculationRegister::~ibValueMetaObjectCalculationRegister()
{
}

ibValueMetaObjectFormBase* ibValueMetaObjectCalculationRegister::GetDefaultFormByID(const ibFormID& id) const
{
	if (id == eFormList && m_propertyDefFormList->GetValueAsInteger() != wxNOT_FOUND)
		return FindFormObjectByFilter(m_propertyDefFormList->GetValueAsInteger());

	return nullptr;
}

#pragma region _form_builder_h_
// The list is ordered by the registration period — the register's one period (see HasPeriod).
ibBackendValueForm* ibValueMetaObjectCalculationRegister::GetListForm(const wxString& strFormName, ibBackendControlFrame* ownerControl, const ibUniqueKey& formGuid) const
{
	return ibValueMetaObjectGenericData::CreateAndBuildForm(
		strFormName,
		ibValueMetaObjectCalculationRegister::eFormList,
		ownerControl, ibCreateList(GetQueryable(), GetRegistrationPeriod()->GetQueryColumn()),   // migrated onto the universal dynamic list
		formGuid
	);
}
#pragma endregion

//***************************************************************************
//*                       Save & load metaData                              *
//***************************************************************************

bool ibValueMetaObjectCalculationRegister::WriteData(ibDataNode& node) const
{
	node.SetValue(m_propertyDefFormList->GetName(), GetGuidByID(m_propertyDefFormList->GetValueAsInteger()).str());

	node.SetProperty(m_propertyChartOfCalculationTypes->GetName(), m_propertyChartOfCalculationTypes->GetNodeValue());
	node.SetProperty(m_propertyPeriodicity->GetName(), m_propertyPeriodicity->GetNodeValue());
	node.SetProperty(m_propertyUseActionPeriod->GetName(), m_propertyUseActionPeriod->GetNodeValue());

	// Action-period standard attributes — persist their identity (metaID) so column names stay stable.
	node.SetProperty(m_propertyAttributeActionPeriod->GetName(),       m_propertyAttributeActionPeriod->GetNodeValue());
	node.SetProperty(m_propertyAttributeActionPeriodStart->GetName(),  m_propertyAttributeActionPeriodStart->GetNodeValue());
	node.SetProperty(m_propertyAttributeActionPeriodEnd->GetName(),    m_propertyAttributeActionPeriodEnd->GetNodeValue());
	node.SetProperty(m_propertyAttributeRegistrationPeriod->GetName(), m_propertyAttributeRegistrationPeriod->GetNodeValue());
	node.SetProperty(m_propertyAttributeStorno->GetName(),             m_propertyAttributeStorno->GetNodeValue());
	node.SetProperty(m_propertyUseBasePeriod->GetName(), m_propertyUseBasePeriod->GetNodeValue());
	node.SetProperty(m_propertyAttributeBasePeriodStart->GetName(), m_propertyAttributeBasePeriodStart->GetNodeValue());
	node.SetProperty(m_propertyAttributeBasePeriodEnd->GetName(),   m_propertyAttributeBasePeriodEnd->GetNodeValue());
	node.SetProperty(m_propertyAttributeCalculationType->GetName(), m_propertyAttributeCalculationType->GetNodeValue());

	// The actual action periods' table is written for its IDENTITY alone (see the accumulation
	// register's totals: the id is what the differ matches the physical table by).
	m_actualPeriods->SaveNode(node.Child(wxT("ActualActionPeriods")));

	node.SetProperty(m_propertyObjectModule->GetName(), m_propertyObjectModule->GetNodeValue());
	node.SetProperty(m_propertyManagerModule->GetName(), m_propertyManagerModule->GetNodeValue());

	return ibValueMetaObjectRegisterData::WriteData(node);
}

bool ibValueMetaObjectCalculationRegister::ReadData(const ibDataNode& node)
{
	m_propertyDefFormList->SetValue(GetIdByGuid(node.GetValue<wxString>(m_propertyDefFormList->GetName())));

	m_propertyChartOfCalculationTypes->SetNodeValue(node.GetProperty(m_propertyChartOfCalculationTypes->GetName()));
	m_propertyPeriodicity->SetNodeValue(node.GetProperty(m_propertyPeriodicity->GetName()));
	m_propertyUseActionPeriod->SetNodeValue(node.GetProperty(m_propertyUseActionPeriod->GetName()));

	m_propertyAttributeActionPeriod->SetNodeValue(node.GetProperty(m_propertyAttributeActionPeriod->GetName()));
	m_propertyAttributeActionPeriodStart->SetNodeValue(node.GetProperty(m_propertyAttributeActionPeriodStart->GetName()));
	m_propertyAttributeActionPeriodEnd->SetNodeValue(node.GetProperty(m_propertyAttributeActionPeriodEnd->GetName()));
	m_propertyAttributeRegistrationPeriod->SetNodeValue(node.GetProperty(m_propertyAttributeRegistrationPeriod->GetName()));
	m_propertyAttributeStorno->SetNodeValue(node.GetProperty(m_propertyAttributeStorno->GetName()));
	m_propertyUseBasePeriod->SetNodeValue(node.GetProperty(m_propertyUseBasePeriod->GetName()));
	m_propertyAttributeBasePeriodStart->SetNodeValue(node.GetProperty(m_propertyAttributeBasePeriodStart->GetName()));
	m_propertyAttributeBasePeriodEnd->SetNodeValue(node.GetProperty(m_propertyAttributeBasePeriodEnd->GetName()));
	m_propertyAttributeCalculationType->SetNodeValue(node.GetProperty(m_propertyAttributeCalculationType->GetName()));

	if (const ibDataNode* actualPeriods = node.FindChild(wxT("ActualActionPeriods")))
		m_actualPeriods->LoadNode(*actualPeriods);

	m_propertyObjectModule->SetNodeValue(node.GetProperty(m_propertyObjectModule->GetName()));
	m_propertyManagerModule->SetNodeValue(node.GetProperty(m_propertyManagerModule->GetName()));

	return ibValueMetaObjectRegisterData::ReadData(node);
}

//***********************************************************************
//*                           read & save events                        *
//***********************************************************************

#include "backend/appData.h"

bool ibValueMetaObjectCalculationRegister::OnCreateMetaObject(ibMetaData* metaData, int flags)
{
	if (!ibValueMetaObjectRegisterData::OnCreateMetaObject(metaData, flags))
		return false;

	return (*m_propertyAttributeActionPeriod)->OnCreateMetaObject(metaData, flags) &&
		(*m_propertyAttributeActionPeriodStart)->OnCreateMetaObject(metaData, flags) &&
		(*m_propertyAttributeActionPeriodEnd)->OnCreateMetaObject(metaData, flags) &&
		(*m_propertyAttributeRegistrationPeriod)->OnCreateMetaObject(metaData, flags) &&
		(*m_propertyAttributeStorno)->OnCreateMetaObject(metaData, flags) &&
		(*m_propertyAttributeBasePeriodStart)->OnCreateMetaObject(metaData, flags) &&
		(*m_propertyAttributeBasePeriodEnd)->OnCreateMetaObject(metaData, flags) &&
		m_actualPeriods->OnCreateMetaObject(metaData, flags) &&
		(*m_propertyAttributeCalculationType)->OnCreateMetaObject(metaData, flags) &&
		(*m_propertyManagerModule)->OnCreateMetaObject(metaData, flags) &&
		(*m_propertyObjectModule)->OnCreateMetaObject(metaData, flags);
}

bool ibValueMetaObjectCalculationRegister::OnLoadMetaObject(ibMetaData* metaData)
{
	if (!(*m_propertyManagerModule)->OnLoadMetaObject(metaData))
		return false;

	if (!(*m_propertyObjectModule)->OnLoadMetaObject(metaData))
		return false;

	if (!(*m_propertyAttributeActionPeriod)->OnLoadMetaObject(metaData)) return false;
	if (!(*m_propertyAttributeActionPeriodStart)->OnLoadMetaObject(metaData)) return false;
	if (!(*m_propertyAttributeActionPeriodEnd)->OnLoadMetaObject(metaData)) return false;
	if (!(*m_propertyAttributeRegistrationPeriod)->OnLoadMetaObject(metaData)) return false;
	if (!(*m_propertyAttributeStorno)->OnLoadMetaObject(metaData)) return false;
	if (!(*m_propertyAttributeBasePeriodStart)->OnLoadMetaObject(metaData)) return false;
	if (!(*m_propertyAttributeBasePeriodEnd)->OnLoadMetaObject(metaData)) return false;
	if (!m_actualPeriods->OnLoadMetaObject(metaData)) return false;
	if (!(*m_propertyAttributeCalculationType)->OnLoadMetaObject(metaData)) return false;

	return ibValueMetaObjectRegisterData::OnLoadMetaObject(metaData);
}

bool ibValueMetaObjectCalculationRegister::OnSaveMetaObject(int flags)
{
	// 🛑 NO CHART, NO REGISTER. Without the binding the CalculationType column has no type, so every
	// record would name a calculation type the engine cannot resolve, the displacement relation has
	// nowhere to be read from, and the priority the fold needs cannot be derived at all. Saving that
	// state produces a register nothing can be written into.
	//
	// It refuses the way the accounting register refuses a missing chart of accounts: a line in the
	// ledger and a false return, with no exception leaving this function — so the configuration write
	// transaction closes normally and the next save is not met by a deadlock.
	if (m_propertyChartOfCalculationTypes->IsEmptyProperty()) {
		RestructureError(wxString::Format(
			_("%s: a chart of calculation types is required - the calculation type, the displacement relation and every priority derived from it all come from it"), GetName()));
		return false;
	}

	// AN ACTION PERIOD IS THE CHART'S TO GRANT. Whether a calculation type is in force over days is a fact
	// about the type, so the chart says it (UseActionPeriod) and a register on it may only keep what its
	// types have. Refused the same way as above, and for the same reason.
	if (IsUseActionPeriod()) {
		const ibValueMetaObjectChartOfCalculationTypes* chart = GetChartOfCalculationTypes();
		if (chart != nullptr && !chart->IsUseActionPeriod()) {
			RestructureError(wxString::Format(
				_("%s keeps action periods, but its chart of calculation types %s does not use them - turn 'Use action period' on for the chart, or off for the register"),
				GetName(), chart->GetName()));
			return false;
		}
	}

	if (!(*m_propertyManagerModule)->OnSaveMetaObject(flags))
		return false;

	if (!(*m_propertyObjectModule)->OnSaveMetaObject(flags))
		return false;

	if (!(*m_propertyAttributeActionPeriod)->OnSaveMetaObject(flags)) return false;
	if (!(*m_propertyAttributeActionPeriodStart)->OnSaveMetaObject(flags)) return false;
	if (!(*m_propertyAttributeActionPeriodEnd)->OnSaveMetaObject(flags)) return false;
	if (!(*m_propertyAttributeRegistrationPeriod)->OnSaveMetaObject(flags)) return false;
	if (!(*m_propertyAttributeStorno)->OnSaveMetaObject(flags)) return false;
	if (!(*m_propertyAttributeBasePeriodStart)->OnSaveMetaObject(flags)) return false;
	if (!(*m_propertyAttributeBasePeriodEnd)->OnSaveMetaObject(flags)) return false;
	if (!m_actualPeriods->OnSaveMetaObject(flags)) return false;
	if (!(*m_propertyAttributeCalculationType)->OnSaveMetaObject(flags)) return false;

	// A calculation register is always subordinate to a recorder, but at IMPORT (or before the posting
	// documents are linked) the recorder type is legitimately empty. The base treats that as a WARNING,
	// not a refusal (commonObject.cpp OnSaveMetaObject) — the register is created and simply cannot be
	// written to until a recorder appears. Delegate instead of refusing here.
	return ibValueMetaObjectRegisterData::OnSaveMetaObject(flags);
}

bool ibValueMetaObjectCalculationRegister::OnDeleteMetaObject()
{
	if (!(*m_propertyManagerModule)->OnDeleteMetaObject())
		return false;

	if (!(*m_propertyObjectModule)->OnDeleteMetaObject())
		return false;

	if (!(*m_propertyAttributeActionPeriod)->OnDeleteMetaObject()) return false;
	if (!(*m_propertyAttributeActionPeriodStart)->OnDeleteMetaObject()) return false;
	if (!(*m_propertyAttributeActionPeriodEnd)->OnDeleteMetaObject()) return false;
	if (!(*m_propertyAttributeRegistrationPeriod)->OnDeleteMetaObject()) return false;
	if (!(*m_propertyAttributeStorno)->OnDeleteMetaObject()) return false;
	if (!(*m_propertyAttributeBasePeriodStart)->OnDeleteMetaObject()) return false;
	if (!(*m_propertyAttributeBasePeriodEnd)->OnDeleteMetaObject()) return false;
	if (!m_actualPeriods->OnDeleteMetaObject()) return false;
	if (!(*m_propertyAttributeCalculationType)->OnDeleteMetaObject()) return false;

	return ibValueMetaObjectRegisterData::OnDeleteMetaObject();
}

bool ibValueMetaObjectCalculationRegister::OnReloadMetaObject()
{
	if (auto* cc = m_metaData->GetCompileCache()) {

		ibValueRecordSetObjectCalculationRegister* recordSet = nullptr;
		if (cc->FindCompileModule(m_propertyObjectModule->GetMetaObject(), recordSet)) {
			if (!recordSet->InitializeObject())
				return false;
		}
	}

	return true;
}

#include "backend/objCtor.h"

bool ibValueMetaObjectCalculationRegister::OnBeforeRunMetaObject(int flags)
{
	if (!(*m_propertyManagerModule)->OnBeforeRunMetaObject(flags))
		return false;

	if (!(*m_propertyObjectModule)->OnBeforeRunMetaObject(flags))
		return false;

	registerSelection();

	return ibValueMetaObjectRegisterData::OnBeforeRunMetaObject(flags);
}

bool ibValueMetaObjectCalculationRegister::OnAfterRunMetaObject(int flags)
{
	if (!(*m_propertyManagerModule)->OnAfterRunMetaObject(flags))
		return false;

	if (!(*m_propertyObjectModule)->OnAfterRunMetaObject(flags))
		return false;

	// THE CALCULATION TYPE'S TYPE, declared here and only here — the same moment and the same reason
	// the accounting register declares its Account (accountingRegisterMetadata.cpp OnAfterRunMetaObject).
	ApplyChartBinding();

	// The actual action periods as a query source — built afresh on every run, because the columns it
	// publishes are the register's own and a run is where they may have changed.
	m_actualPeriodsQueryable.reset();
	if (IsUseActionPeriod())
		m_metaData->RegisterSource(&m_actualPeriodsSource);


	if (auto* cc = m_metaData->GetCompileCache()) {

		if (ibValueMetaObjectRegisterData::OnAfterRunMetaObject(flags)) {

			if (!cc->AddCompileModule(m_propertyObjectModule->GetMetaObject(), [this]() -> ibValue* { return CreateRecordSetObjectValue(); }))
				return false;

			return true;
		}
	}

	return ibValueMetaObjectRegisterData::OnAfterRunMetaObject(flags);
}

bool ibValueMetaObjectCalculationRegister::OnBeforeCloseMetaObject()
{
	m_metaData->UnregisterSource(&m_actualPeriodsSource);   // mirror of the run's RegisterSource

	if (!(*m_propertyManagerModule)->OnBeforeCloseMetaObject())
		return false;

	if (!(*m_propertyObjectModule)->OnBeforeCloseMetaObject())
		return false;


	if (auto* cc = m_metaData->GetCompileCache()) {

		if (ibValueMetaObjectRegisterData::OnBeforeCloseMetaObject()) {

			cc->RemoveCompileModule(m_propertyObjectModule->GetMetaObject());

			return true;
		}
	}

	return ibValueMetaObjectRegisterData::OnBeforeCloseMetaObject();
}

bool ibValueMetaObjectCalculationRegister::OnAfterCloseMetaObject()
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

void ibValueMetaObjectCalculationRegister::OnCreateFormObject(ibValueMetaObjectFormBase* metaForm)
{
	if (metaForm->GetTypeForm() == ibValueMetaObjectCalculationRegister::eFormList
		&& m_propertyDefFormList->GetValueAsInteger() == wxNOT_FOUND)
	{
		m_propertyDefFormList->SetValue(metaForm->GetMetaID());
	}
}

void ibValueMetaObjectCalculationRegister::OnRemoveMetaForm(ibValueMetaObjectFormBase* metaForm)
{
	if (metaForm->GetTypeForm() == ibValueMetaObjectCalculationRegister::eFormList
		&& m_propertyDefFormList->GetValueAsInteger() == metaForm->GetMetaID())
	{
		m_propertyDefFormList->SetValue(wxNOT_FOUND);
	}
}

#include "calculationRegisterManager.h"

ibValueManagerDataObject* ibValueMetaObjectCalculationRegister::CreateManagerDataObjectValue() const
{
	return new ibValueManagerDataObjectCalculationRegister(this);
}

ibValueRecordSetObject* ibValueMetaObjectCalculationRegister::CreateRecordSetObjectRegValue(const ibUniqueKeyPair& uniqueKey) const
{
	if (auto* cc = m_metaData->GetCompileCache()) {
		ibValueRecordSetObject* pDataRef = nullptr;
		if (!cc->FindCompileModule(m_propertyObjectModule->GetMetaObject(), pDataRef)) {
			return new ibValueRecordSetObjectCalculationRegister(this, uniqueKey);
		}
		return pDataRef;
	}

	return new ibValueRecordSetObjectCalculationRegister(this, uniqueKey);
}

ibSourceDataObject* ibValueMetaObjectCalculationRegister::CreateSourceObject(const ibValueMetaObjectFormBase* metaObject) const
{
	switch (metaObject->GetTypeForm())
	{
	case eFormList:
		return ibCreateList(GetQueryable(), GetRegistrationPeriod()->GetQueryColumn());   // migrated onto the universal dynamic list
	}

	return nullptr;
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

METADATA_TYPE_REGISTER(ibValueMetaObjectCalculationRegister, "CalculationRegister", g_metaCalculationRegisterCLSID);

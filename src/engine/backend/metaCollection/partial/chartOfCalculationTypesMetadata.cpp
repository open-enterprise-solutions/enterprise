////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : chart of calculation types metaData
////////////////////////////////////////////////////////////////////////////

#include "chartOfCalculationTypes.h"
#include "backend/serialize/dataBuilder.h"
#include "backend/system/value/valueDynamicList.h"   // ibValueDynamicList — the standard list migrates onto the universal dynamic list
#include "backend/metaData.h"
#include "backend/moduleManager/moduleManager.h"
#include "backend/objCtor.h"                                // ibCtorMetaValueType — the chart's own reference type
#include "backend/query/dataQueryBuilder.h"                 // L3 door — a relation read as data, in one statement

#include <algorithm>

//********************************************************************************************
//*										 metaData											 *
//********************************************************************************************

// Moved here from the calculation register, which read the Displacing section only: a relation is the
// CHART'S, and the base and the leading relations are read exactly the same way.
void ibValueMetaObjectChartOfCalculationTypes::ReadRelation(const ibValueMetaObjectCalculationTypeRelationTable* table,
	std::map<ibValue, int>& typeIndex, std::vector<std::pair<int, int>>& edges) const
{
	if (table == nullptr || !table->IsAllowed())   // deleted, or never saved into this configuration
		return;
	const ibBackendQueryable* rows = table->GetQueryable();
	const ibValueMetaObjectAttributeBase* named = table->GetCalculationType();
	if (rows == nullptr || named == nullptr)
		return;
	const ibBackendQueryColumn* ownerCol = rows->ResolveColumnByName(wxT("Ref"));   // the owning type — `Ref`, as a query names it
	const ibBackendQueryColumn* namedCol = rows->ResolveColumnByName(named->GetName());
	if (namedCol == nullptr)
		namedCol = named->GetQueryColumn();   // an attribute HOLDS a query column rather than being one
	if (ownerCol == nullptr || namedCol == nullptr)
		return;

	const auto ordinal = [&typeIndex](const ibValue& type) -> int {
		const auto it = typeIndex.find(type);
		if (it != typeIndex.end())
			return it->second;
		const int next = (int)typeIndex.size();
		typeIndex.emplace(type, next);
		return next;
	};

	ibDataQueryBuilder b;
	b.From(rows);
	b.WithAccessPolicy(nullptr);   // the chart's relation, not the reader's — see the declaration
	b.Select(ownerCol, wxT("Owner"));
	b.Select(namedCol, named->GetName());
	ibDataQueryResult sel = b.Execute(ibReadPageRequest{});
	while (sel.Next()) {
		const ibValue owner = sel.GetValue(ownerCol);
		const ibValue other = sel.GetValue(namedCol);
		if (owner.IsEmpty() || other.IsEmpty())
			continue;   // an edge to nothing names nobody
		edges.push_back({ ordinal(owner), ordinal(other) });   // {owner of the row, type the row names}
	}
}


//********************************************************************************************
//*                                      metaData                                            *
//********************************************************************************************

ibValueMetaObjectChartOfCalculationTypes::ibValueMetaObjectChartOfCalculationTypes() : ibValueMetaObjectRecordDataHierarchyMutableRef()
{
	(*m_propertyObjectModule)->SetDefaultProcedure(wxT("BeforeWrite"),  ibContentHelper::eProcedureHelper, { wxT("Cancel") });
	(*m_propertyObjectModule)->SetDefaultProcedure(wxT("OnWrite"),      ibContentHelper::eProcedureHelper, { wxT("Cancel") });
	(*m_propertyObjectModule)->SetDefaultProcedure(wxT("BeforeDelete"), ibContentHelper::eProcedureHelper, { wxT("Cancel") });
	(*m_propertyObjectModule)->SetDefaultProcedure(wxT("OnDelete"),     ibContentHelper::eProcedureHelper, { wxT("Cancel") });
	(*m_propertyObjectModule)->SetDefaultProcedure(wxT("Filling"),      ibContentHelper::eProcedureHelper, { wxT("Source"), wxT("StandartProcessing") });
	(*m_propertyObjectModule)->SetDefaultProcedure(wxT("OnCopy"),       ibContentHelper::eProcedureHelper, { wxT("Source") });
	(*m_propertyObjectModule)->SetDefaultProcedure(wxT("SetNewCode"),   ibContentHelper::eProcedureHelper, { wxT("Prefix"), wxT("StandartProcessing") });
}

ibValueMetaObjectChartOfCalculationTypes::~ibValueMetaObjectChartOfCalculationTypes()
{
}

ibValueMetaObjectFormBase* ibValueMetaObjectChartOfCalculationTypes::GetDefaultFormByID(const ibFormID& id) const
{
	if (id == eFormObject && m_propertyDefFormObject->GetValueAsInteger() != wxNOT_FOUND) {
		return FindFormObjectByFilter(m_propertyDefFormObject->GetValueAsInteger());
	}
	else if (id == eFormFolder && m_propertyDefFormFolder->GetValueAsInteger() != wxNOT_FOUND) {
		return FindFormObjectByFilter(m_propertyDefFormFolder->GetValueAsInteger());
	}
	else if (id == eFormList && m_propertyDefFormList->GetValueAsInteger() != wxNOT_FOUND) {
		return FindFormObjectByFilter(m_propertyDefFormList->GetValueAsInteger());
	}
	else if (id == eFormSelect && m_propertyDefFormSelect->GetValueAsInteger() != wxNOT_FOUND) {
		return FindFormObjectByFilter(m_propertyDefFormSelect->GetValueAsInteger());
	}
	else if (id == eFormFolderSelect && m_propertyDefFormFolderSelect->GetValueAsInteger() != wxNOT_FOUND) {
		return FindFormObjectByFilter(m_propertyDefFormFolderSelect->GetValueAsInteger());
	}

	return nullptr;
}

#include "chartOfCalculationTypesManager.h"

ibValueManagerDataObject* ibValueMetaObjectChartOfCalculationTypes::CreateManagerDataObjectValue() const
{
	return new ibValueManagerDataObjectChartOfCalculationTypes(this);
}

#include "backend/appData.h"
#include "backend/objCtor.h"   // registerSelection / unregisterSelection macros + full ibCtorMetaValueType

ibValueRecordDataObjectHierarchyRef* ibValueMetaObjectChartOfCalculationTypes::CreateObjectRefValue(ibObjectMode mode, const ibGuid& guid) const
{
	ibValueRecordDataObjectChartOfCalculationTypes* pDataRef = nullptr;
	if (auto* cc = m_metaData->GetCompileCache()) {
		if (!cc->FindCompileModule(m_propertyObjectModule->GetMetaObject(), pDataRef)) {
			return new ibValueRecordDataObjectChartOfCalculationTypes(this, guid, mode);
		}
	}
	else {
		pDataRef = new ibValueRecordDataObjectChartOfCalculationTypes(this, guid, mode);
	}

	return pDataRef;
}

ibSourceDataObject* ibValueMetaObjectChartOfCalculationTypes::CreateSourceObject(const ibValueMetaObjectFormBase* metaObject) const
{
	switch (metaObject->GetTypeForm())
	{
	case eFormObject:
		return CreateObjectValue(ibObjectMode::OBJECT_ITEM);
	case eFormFolder:
		return CreateObjectValue(ibObjectMode::OBJECT_FOLDER);
	case eFormList:
		return ibCreateHierarchyList(GetQueryable(), GetDataIsFolder()->GetQueryColumn(), GetDataDescription()->GetQueryColumn());   // migrated onto the universal dynamic list (hierarchy via queryable)
	case eFormSelect:
		return ibCreateHierarchyList(GetQueryable(), GetDataIsFolder()->GetQueryColumn(), GetDataDescription()->GetQueryColumn(), ibDynamicListView_Choice);   // select front-driven — choice mode
	case eFormFolderSelect:
		return ibCreateFolderList(GetQueryable(), GetDataIsFolder()->GetQueryColumn(), GetDataDescription()->GetQueryColumn(), ibDynamicListView_Choice);   // folder-select = choice + IsFolder = true
	}

	return nullptr;
}

#pragma region _form_builder_h_
ibBackendValueForm* ibValueMetaObjectChartOfCalculationTypes::GetObjectForm(const wxString& strFormName, ibBackendControlFrame* ownerControl, const ibUniqueKey& formGuid) const
{
	return ibValueMetaObjectGenericData::CreateAndBuildForm(
		strFormName,
		ibValueMetaObjectChartOfCalculationTypes::eFormObject,
		ownerControl, CreateObjectValue(ibObjectMode::OBJECT_ITEM),
		formGuid
	);
}

ibBackendValueForm* ibValueMetaObjectChartOfCalculationTypes::GetFolderForm(const wxString& strFormName, ibBackendControlFrame* ownerControl, const ibUniqueKey& formGuid) const
{
	return ibValueMetaObjectGenericData::CreateAndBuildForm(
		strFormName,
		ibValueMetaObjectChartOfCalculationTypes::eFormFolder,
		ownerControl, CreateObjectValue(ibObjectMode::OBJECT_FOLDER),
		formGuid
	);
}

ibBackendValueForm* ibValueMetaObjectChartOfCalculationTypes::GetListForm(const wxString& strFormName, ibBackendControlFrame* ownerControl, const ibUniqueKey& formGuid) const
{
	return ibValueMetaObjectGenericData::CreateAndBuildForm(
		strFormName,
		ibValueMetaObjectChartOfCalculationTypes::eFormList,
		ownerControl, ibCreateHierarchyList(GetQueryable(), GetDataIsFolder()->GetQueryColumn(), GetDataDescription()->GetQueryColumn()),   // migrated onto the universal dynamic list (hierarchy via queryable)
		formGuid
	);
}

ibBackendValueForm* ibValueMetaObjectChartOfCalculationTypes::GetSelectForm(const wxString& strFormName, ibBackendControlFrame* ownerControl, const ibUniqueKey& formGuid) const
{
	return ibValueMetaObjectGenericData::CreateAndBuildForm(
		strFormName,
		ibValueMetaObjectChartOfCalculationTypes::eFormSelect,
		ownerControl, ibCreateHierarchyList(GetQueryable(), GetDataIsFolder()->GetQueryColumn(), GetDataDescription()->GetQueryColumn(), ibDynamicListView_Choice),   // select front-driven — choice mode
		formGuid
	);
}

ibBackendValueForm* ibValueMetaObjectChartOfCalculationTypes::GetFolderSelectForm(const wxString& strFormName, ibBackendControlFrame* ownerControl, const ibUniqueKey& formGuid) const
{
	return ibValueMetaObjectGenericData::CreateAndBuildForm(
		strFormName,
		ibValueMetaObjectChartOfCalculationTypes::eFormFolderSelect,
		ownerControl, ibCreateFolderList(GetQueryable(), GetDataIsFolder()->GetQueryColumn(), GetDataDescription()->GetQueryColumn(), ibDynamicListView_Choice),   // folder-select = choice + IsFolder = true
		formGuid
	);
}
#pragma endregion

wxString ibValueMetaObjectChartOfCalculationTypes::GetDataPresentation(const ibValueDataObject* objValue) const
{
	static ibValue vDescription;
	if (objValue->GetValueByMetaID((*m_propertyAttributeDescription)->GetMetaID(), vDescription))
		return vDescription.GetString();
	return wxEmptyString;
}

//***************************************************************************
//*                       Save & load metaData                              *
//***************************************************************************

bool ibValueMetaObjectChartOfCalculationTypes::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertyObjectModule->GetName(), m_propertyObjectModule->GetNodeValue());
	node.SetProperty(m_propertyManagerModule->GetName(), m_propertyManagerModule->GetNodeValue());

	node.SetProperty(m_propertyUseActionPeriod->GetName(), m_propertyUseActionPeriod->GetNodeValue());
	node.SetProperty(m_propertyBaseDependence->GetName(), m_propertyBaseDependence->GetNodeValue());
	node.SetProperty(m_propertyBaseCharts->GetName(), m_propertyBaseCharts->GetNodeValue());
	for (auto* relation : GetRelationProperties())
		node.SetProperty(relation->GetName(), relation->GetNodeValue());

	node.SetValue(m_propertyDefFormObject->GetName(), GetGuidByID(m_propertyDefFormObject->GetValueAsInteger()).str());
	node.SetValue(m_propertyDefFormFolder->GetName(), GetGuidByID(m_propertyDefFormFolder->GetValueAsInteger()).str());
	node.SetValue(m_propertyDefFormList->GetName(), GetGuidByID(m_propertyDefFormList->GetValueAsInteger()).str());
	node.SetValue(m_propertyDefFormSelect->GetName(), GetGuidByID(m_propertyDefFormSelect->GetValueAsInteger()).str());
	node.SetValue(m_propertyDefFormFolderSelect->GetName(), GetGuidByID(m_propertyDefFormFolderSelect->GetValueAsInteger()).str());

	return ibValueMetaObjectRecordDataHierarchyMutableRef::WriteData(node);
}

bool ibValueMetaObjectChartOfCalculationTypes::ReadData(const ibDataNode& node)
{
	m_propertyObjectModule->SetNodeValue(node.GetProperty(m_propertyObjectModule->GetName()));
	m_propertyManagerModule->SetNodeValue(node.GetProperty(m_propertyManagerModule->GetName()));

	m_propertyUseActionPeriod->SetNodeValue(node.GetProperty(m_propertyUseActionPeriod->GetName()));
	m_propertyBaseDependence->SetNodeValue(node.GetProperty(m_propertyBaseDependence->GetName()));
	m_propertyBaseCharts->SetNodeValue(node.GetProperty(m_propertyBaseCharts->GetName()));
	// A section absent from the node keeps id 0 here; it is stamped at run (StampIfNeverSaved).
	for (auto* relation : GetRelationProperties())
		relation->SetNodeValue(node.GetProperty(relation->GetName()));

	m_propertyDefFormObject->SetValue(GetIdByGuid(node.GetValue<wxString>(m_propertyDefFormObject->GetName())));
	m_propertyDefFormFolder->SetValue(GetIdByGuid(node.GetValue<wxString>(m_propertyDefFormFolder->GetName())));
	m_propertyDefFormList->SetValue(GetIdByGuid(node.GetValue<wxString>(m_propertyDefFormList->GetName())));
	m_propertyDefFormSelect->SetValue(GetIdByGuid(node.GetValue<wxString>(m_propertyDefFormSelect->GetName())));
	m_propertyDefFormFolderSelect->SetValue(GetIdByGuid(node.GetValue<wxString>(m_propertyDefFormFolderSelect->GetName())));

	return ibValueMetaObjectRecordDataHierarchyMutableRef::ReadData(node);
}

//***********************************************************************
//*                           read & save events                        *
//***********************************************************************

bool ibValueMetaObjectChartOfCalculationTypes::OnCreateMetaObject(ibMetaData* metaData, int flags)
{
	if (!ibValueMetaObjectRecordDataHierarchyMutableRef::OnCreateMetaObject(metaData, flags))
		return false;

	if (!(*m_propertyObjectModule)->OnCreateMetaObject(metaData, flags) ||
		!(*m_propertyManagerModule)->OnCreateMetaObject(metaData, flags))
		return false;

	for (auto* relation : GetRelationTables())
		if (!relation->OnCreateMetaObject(metaData, flags))
			return false;
	return true;
}

bool ibValueMetaObjectChartOfCalculationTypes::OnLoadMetaObject(ibMetaData* metaData)
{
	if (!(*m_propertyObjectModule)->OnLoadMetaObject(metaData))
		return false;

	if (!(*m_propertyManagerModule)->OnLoadMetaObject(metaData))
		return false;

	for (auto* relation : GetRelationTables())
		if (!relation->OnLoadMetaObject(metaData))
			return false;

	return ibValueMetaObjectRecordDataHierarchyMutableRef::OnLoadMetaObject(metaData);
}

bool ibValueMetaObjectChartOfCalculationTypes::OnSaveMetaObject(int flags)
{
	if (!(*m_propertyObjectModule)->OnSaveMetaObject(flags))
		return false;

	if (!(*m_propertyManagerModule)->OnSaveMetaObject(flags))
		return false;

	for (auto* relation : GetRelationTables())
		if (!relation->OnSaveMetaObject(flags))
			return false;

	return ibValueMetaObjectRecordDataHierarchyMutableRef::OnSaveMetaObject(flags);
}

bool ibValueMetaObjectChartOfCalculationTypes::OnDeleteMetaObject()
{
	if (!(*m_propertyObjectModule)->OnDeleteMetaObject())
		return false;

	if (!(*m_propertyManagerModule)->OnDeleteMetaObject())
		return false;

	for (auto* relation : GetRelationTables())
		if (!relation->OnDeleteMetaObject())
			return false;

	return ibValueMetaObjectRecordDataHierarchyMutableRef::OnDeleteMetaObject();
}

bool ibValueMetaObjectChartOfCalculationTypes::OnReloadMetaObject()
{

	if (auto* cc = m_metaData->GetCompileCache()) {
		ibValueRecordDataObjectChartOfCalculationTypes* pDataRef = nullptr;
		if (!cc->FindCompileModule(m_propertyObjectModule->GetMetaObject(), pDataRef)) {
			return true;
		}
		return pDataRef->InitializeObject();
	}

	return true;
}

bool ibValueMetaObjectChartOfCalculationTypes::OnBeforeRunMetaObject(int flags)
{
	if (!(*m_propertyObjectModule)->OnBeforeRunMetaObject(flags))
		return false;

	if (!(*m_propertyManagerModule)->OnBeforeRunMetaObject(flags))
		return false;

	// Stamped BEFORE they run: a section's value ctor is registered under its id, and an id of 0 would
	// register all three under one. Stamped only by the copy that saves itself — the copy that mirrors
	// the database (the designer's baseline, the running application) has to go on NOT having a section
	// the database has no table for, or the diff never creates one (see StampIfNeverSaved). A section it
	// leaves at 0 does not run either — the section refuses that itself, since the tree's own walk
	// (RunSubtree) reaches it without passing through here.
	//
	// WHICH COPY THIS IS, THE RUN ALREADY SAYS: the designer's baseline is run with loadConfigFlag, and
	// the running application is not the designer — the pair a module's debugger set-up asks by
	// (metaModuleObject.cpp). It was a cast of the metadata to a configuration, to read its type.
	const bool savesItself = (flags & loadConfigFlag) == 0 && appData->DesignerMode();
	for (auto* relation : GetRelationTables()) {
		if (savesItself && !relation->StampIfNeverSaved(m_metaData))
			return false;
		if (!relation->OnBeforeRunMetaObject(flags))
			return false;
	}

	registerSelection();

	if (!ibValueMetaObjectRecordDataHierarchyMutableRef::OnBeforeRunMetaObject(flags))
		return false;

	const ibCtorMetaValueType* typeCtor =
		m_metaData->GetTypeCtor(this, ibCtorObjectMetaType::ibCtorObjectMetaType_Reference);

	if (typeCtor != nullptr && !(*m_propertyAttributeParent)->ContainType(typeCtor->GetClassType())) {
		(*m_propertyAttributeParent)->SetDefaultMetaType(typeCtor->GetClassType());
	}

	// 🛑⭐⭐ THE TYPE A RELATION NAMES IS A REFERENCE TO THIS SAME CHART, typed the way Parent is typed just
	// above and for the same reason — a self-reference whose target is known the moment the chart exists,
	// asked of the type factory (GetTypeCtor) rather than computed.
	//
	// The Displacing section's comment promised this binding and nothing performed it. The column was
	// created with CreateEmptyType and stayed that way, so the table grew ONE field for it
	// (`fld<id>_TYPE`) instead of the three a reference needs (`_TYPE`/`_RTRef`/`_RRRef`). MEASURED
	// 2026-09-10 from the tech journal: `INSERT INTO ...VT... (Row_RRRef, fld1472_TYPE, fld1472_N,
	// fld1473_TYPE)`. A row written as "Absence displaces Salary" kept only a type tag, the relation read
	// back empty, and the record-set write — now reading the relation instead of the line order — found
	// no edges at all and displaced nothing. It looked order-independent because it cut nothing.
	if (typeCtor != nullptr) {
		for (auto* relation : GetRelationTables()) {
			ibValueMetaObjectAttributePredefined* named = relation != nullptr ? relation->GetCalculationType() : nullptr;
			if (named != nullptr && !named->ContainType(typeCtor->GetClassType()))
				named->SetDefaultMetaType(typeCtor->GetClassType());
		}
	}

	return true;
}

bool ibValueMetaObjectChartOfCalculationTypes::OnAfterRunMetaObject(int flags)
{
	if (!(*m_propertyObjectModule)->OnAfterRunMetaObject(flags))
		return false;

	if (!(*m_propertyManagerModule)->OnAfterRunMetaObject(flags))
		return false;

	for (auto* relation : GetRelationTables())
		if (!relation->OnAfterRunMetaObject(flags))
			return false;

	TypeBaseAndLeading();


	if (auto* cc = m_metaData->GetCompileCache()) {

		if (ibValueMetaObjectRecordDataHierarchyMutableRef::OnAfterRunMetaObject(flags))
			return cc->AddCompileModule(m_propertyObjectModule->GetMetaObject(), [this]() -> ibValue* { return CreateObjectValue(ibObjectMode::OBJECT_ITEM); });

		return false;
	}

	return ibValueMetaObjectRecordDataHierarchyMutableRef::OnAfterRunMetaObject(flags);
}

// ⭐ A BASE — AND A LEAD — MAY COME FROM ANOTHER CHART, the one this chart NAMES. A deduction is computed
// from accruals, and accruals are another chart's types: the income tax of a month is a percent of that
// month's salary and bonus, and it is stale the moment one of them changes. Neither reader needs anything
// more for it — GetBase and the recalculations match records by the type a row names, whichever chart
// that type is from. What decides WHICH charts is the chart itself (BaseCharts, plus its own): the
// sections used to take any chart of the configuration, so a type could be named as a base that nothing
// had declared a base could come from.
// Run after every chart has registered its reference type in its own before-run — a chart earlier in
// the tree would not yet find a later one's. Displacing stays typed within the chart (the before-run):
// it cuts records of one register against each other.
void ibValueMetaObjectChartOfCalculationTypes::TypeBaseAndLeading()
{
	if (m_metaData == nullptr)
		return;

	std::vector<ibClassID> named;
	const auto admit = [&](const ibValueMetaObject* chart) {
		const ibCtorMetaValueType* ctor = chart != nullptr
			? m_metaData->GetTypeCtor(chart, ibCtorObjectMetaType::ibCtorObjectMetaType_Reference) : nullptr;
		if (ctor != nullptr && std::find(named.begin(), named.end(), ctor->GetClassType()) == named.end())
			named.push_back(ctor->GetClassType());
	};
	admit(this);
	const ibMetaDescription& listed = GetBaseCharts();
	for (unsigned int idx = 0; idx < listed.GetTypeCount(); idx++)   // a chart deleted since is simply not found
		admit(m_metaData->FindAnyObjectByFilter<ibValueMetaObjectChartOfCalculationTypes>(listed.GetByIdx(idx), g_metaChartOfCalculationTypesCLSID));
	if (named.empty())
		return;

	for (ibValueMetaObjectCalculationTypeRelationTable* relation : { GetBaseTable(), GetLeadingTable() }) {
		ibValueMetaObjectAttributePredefined* column = relation != nullptr ? relation->GetCalculationType() : nullptr;
		if (column == nullptr)
			continue;
		bool same = column->GetTypeDesc().GetClsidCount() == named.size();
		for (const ibClassID clsid : named)
			same = same && column->ContainType(clsid);
		if (!same)
			column->SetDefaultMetaType(named);
	}
}

bool ibValueMetaObjectChartOfCalculationTypes::OnBeforeCloseMetaObject()
{
	if (!(*m_propertyObjectModule)->OnBeforeCloseMetaObject())
		return false;

	if (!(*m_propertyManagerModule)->OnBeforeCloseMetaObject())
		return false;

	for (auto* relation : GetRelationTables())
		if (!relation->OnBeforeCloseMetaObject())
			return false;


	if (auto* cc = m_metaData->GetCompileCache()) {

		if (ibValueMetaObjectRecordDataHierarchyMutableRef::OnBeforeCloseMetaObject())
			{ cc->RemoveCompileModule(m_propertyObjectModule->GetMetaObject()); return true; }

		return false;
	}

	return ibValueMetaObjectRecordDataHierarchyMutableRef::OnBeforeCloseMetaObject();
}

bool ibValueMetaObjectChartOfCalculationTypes::OnAfterCloseMetaObject()
{
	if (!(*m_propertyObjectModule)->OnAfterCloseMetaObject())
		return false;

	if (!(*m_propertyManagerModule)->OnAfterCloseMetaObject())
		return false;

	for (auto* relation : GetRelationTables())
		if (!relation->OnAfterCloseMetaObject())
			return false;

	unregisterSelection();

	return ibValueMetaObjectRecordDataHierarchyMutableRef::OnAfterCloseMetaObject();
}

//***********************************************************************
//*                             form events                             *
//***********************************************************************

void ibValueMetaObjectChartOfCalculationTypes::OnCreateFormObject(ibValueMetaObjectFormBase* metaForm)
{
	if (metaForm->GetTypeForm() == eFormObject
		&& m_propertyDefFormObject->GetValueAsInteger() == wxNOT_FOUND)
	{
		m_propertyDefFormObject->SetValue(metaForm->GetMetaID());
	}
	else if (metaForm->GetTypeForm() == eFormFolder
		&& m_propertyDefFormFolder->GetValueAsInteger() == wxNOT_FOUND)
	{
		m_propertyDefFormFolder->SetValue(metaForm->GetMetaID());
	}
	else if (metaForm->GetTypeForm() == eFormList
		&& m_propertyDefFormList->GetValueAsInteger() == wxNOT_FOUND)
	{
		m_propertyDefFormList->SetValue(metaForm->GetMetaID());
	}
	else if (metaForm->GetTypeForm() == eFormSelect
		&& m_propertyDefFormSelect->GetValueAsInteger() == wxNOT_FOUND)
	{
		m_propertyDefFormSelect->SetValue(metaForm->GetMetaID());
	}
	else if (metaForm->GetTypeForm() == eFormFolderSelect
		&& m_propertyDefFormFolderSelect->GetValueAsInteger() == wxNOT_FOUND)
	{
		m_propertyDefFormFolderSelect->SetValue(metaForm->GetMetaID());
	}
}

void ibValueMetaObjectChartOfCalculationTypes::OnRemoveMetaForm(ibValueMetaObjectFormBase* metaForm)
{
	if (metaForm->GetTypeForm() == eFormObject
		&& m_propertyDefFormObject->GetValueAsInteger() == metaForm->GetMetaID())
	{
		m_propertyDefFormObject->SetValue(wxNOT_FOUND);
	}
	else if (metaForm->GetTypeForm() == eFormFolder
		&& m_propertyDefFormFolder->GetValueAsInteger() == metaForm->GetMetaID())
	{
		m_propertyDefFormFolder->SetValue(wxNOT_FOUND);
	}
	else if (metaForm->GetTypeForm() == eFormList
		&& m_propertyDefFormList->GetValueAsInteger() == metaForm->GetMetaID())
	{
		m_propertyDefFormList->SetValue(wxNOT_FOUND);
	}
	else if (metaForm->GetTypeForm() == eFormSelect
		&& m_propertyDefFormSelect->GetValueAsInteger() == metaForm->GetMetaID())
	{
		m_propertyDefFormSelect->SetValue(wxNOT_FOUND);
	}
	else if (metaForm->GetTypeForm() == eFormFolderSelect
		&& m_propertyDefFormFolderSelect->GetValueAsInteger() == metaForm->GetMetaID())
	{
		m_propertyDefFormFolderSelect->SetValue(wxNOT_FOUND);
	}
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

METADATA_TYPE_REGISTER(ibValueMetaObjectChartOfCalculationTypes, "ChartOfCalculationTypes", g_metaChartOfCalculationTypesCLSID);

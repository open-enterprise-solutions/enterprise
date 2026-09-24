////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : chart of characteristic types metaData
////////////////////////////////////////////////////////////////////////////

#include "chartOfCharacteristicTypes.h"
#include "backend/serialize/dataBuilder.h"
#include "backend/system/value/valueDynamicList.h"   // ibValueDynamicList — the standard list migrates onto the universal dynamic list
#include "backend/metaData.h"
#include "backend/moduleManager/moduleManager.h"

//********************************************************************************************
//*										 metaData											 *
//********************************************************************************************


//********************************************************************************************
//*                                      metaData                                            *
//********************************************************************************************

ibValueMetaObjectChartOfCharacteristicTypes::ibValueMetaObjectChartOfCharacteristicTypes() : ibValueMetaObjectRecordDataHierarchyMutableRef()
{
	(*m_propertyObjectModule)->SetDefaultProcedure(wxT("BeforeWrite"),  ibContentHelper::eProcedureHelper, { wxT("Cancel") });
	(*m_propertyObjectModule)->SetDefaultProcedure(wxT("OnWrite"),      ibContentHelper::eProcedureHelper, { wxT("Cancel") });
	(*m_propertyObjectModule)->SetDefaultProcedure(wxT("BeforeDelete"), ibContentHelper::eProcedureHelper, { wxT("Cancel") });
	(*m_propertyObjectModule)->SetDefaultProcedure(wxT("OnDelete"),     ibContentHelper::eProcedureHelper, { wxT("Cancel") });
	(*m_propertyObjectModule)->SetDefaultProcedure(wxT("Filling"),      ibContentHelper::eProcedureHelper, { wxT("Source"), wxT("StandartProcessing") });
	(*m_propertyObjectModule)->SetDefaultProcedure(wxT("OnCopy"),       ibContentHelper::eProcedureHelper, { wxT("Source") });
	(*m_propertyObjectModule)->SetDefaultProcedure(wxT("SetNewCode"),   ibContentHelper::eProcedureHelper, { wxT("Prefix"), wxT("StandartProcessing") });
}

ibValueMetaObjectChartOfCharacteristicTypes::~ibValueMetaObjectChartOfCharacteristicTypes()
{
}

ibValueMetaObjectFormBase* ibValueMetaObjectChartOfCharacteristicTypes::GetDefaultFormByID(const ibFormID& id) const
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

#include "chartOfCharacteristicTypesManager.h"

ibValuePtr<ibValueManagerDataObject> ibValueMetaObjectChartOfCharacteristicTypes::CreateManagerDataObjectValue() const
{
	return ibValuePtr<ibValueManagerDataObject>(new ibValueManagerDataObjectChartOfCharacteristicTypes(this));
}

#include "backend/appData.h"
#include "backend/metaCollection/partial/declaredPresentation.h"   // how a reference reads in the designer

ibValuePtr<ibValueRecordDataObjectHierarchyRef> ibValueMetaObjectChartOfCharacteristicTypes::CreateObjectRefValue(ibObjectMode mode, const ibGuid& guid) const
{
	ibValueRecordDataObjectChartOfCharacteristicTypes* pDataRef = nullptr;
	if (auto* cc = m_metaData->GetCompileCache()) {
		if (!cc->FindCompileModule(m_propertyObjectModule->GetMetaObject(), pDataRef)) {
			pDataRef = new ibValueRecordDataObjectChartOfCharacteristicTypes(this, guid, mode);
		}
	}
	else {
		pDataRef = new ibValueRecordDataObjectChartOfCharacteristicTypes(this, guid, mode);
	}

	return ibValuePtr<ibValueRecordDataObjectHierarchyRef>(pDataRef);
}

ibSourcePtr<ibSourceDataObject> ibValueMetaObjectChartOfCharacteristicTypes::CreateSourceObject(const ibCreateRequest& request, const ibFormID& form_id) const
{
	switch (form_id)
	{
	case eFormObject:
		return ibSourcePtr<ibSourceDataObject>(CreateObjectValue(ibObjectMode::OBJECT_ITEM));
	case eFormFolder:
		return ibSourcePtr<ibSourceDataObject>(CreateObjectValue(ibObjectMode::OBJECT_FOLDER));
	case eFormList:
		return ibSourcePtr<ibSourceDataObject>(ibCreateHierarchyList(request, GetQueryable(), GetDataIsFolder()->GetQueryColumn(), GetDataPresentationAttribute()->GetQueryColumn()));   // migrated onto the universal dynamic list (hierarchy via queryable)
	case eFormSelect:
		return ibSourcePtr<ibSourceDataObject>(ibCreateHierarchyList(request, GetQueryable(), GetDataIsFolder()->GetQueryColumn(), GetDataPresentationAttribute()->GetQueryColumn(), ibDynamicListView_Choice));   // select front-driven — choice mode
	case eFormFolderSelect:
		return ibSourcePtr<ibSourceDataObject>(ibCreateFolderList(request, GetQueryable(), GetDataIsFolder()->GetQueryColumn(), GetDataPresentationAttribute()->GetQueryColumn(), ibDynamicListView_Choice));   // folder-select = choice + IsFolder = true
	}

	return nullptr;
}

#pragma region _form_builder_h_
ibBackendValueForm* ibValueMetaObjectChartOfCharacteristicTypes::GetObjectForm(const ibFormRequest& request, ibBackendControlFrame* ownerControl) const
{
	return ibValueMetaObjectGenericData::CreateAndBuildForm(
		request,
		ibValueMetaObjectChartOfCharacteristicTypes::eFormObject,
		ownerControl, CreateObjectValue(ibObjectMode::OBJECT_ITEM)
	);
}

ibBackendValueForm* ibValueMetaObjectChartOfCharacteristicTypes::GetFolderForm(const ibFormRequest& request, ibBackendControlFrame* ownerControl) const
{
	return ibValueMetaObjectGenericData::CreateAndBuildForm(
		request,
		ibValueMetaObjectChartOfCharacteristicTypes::eFormFolder,
		ownerControl, CreateObjectValue(ibObjectMode::OBJECT_FOLDER)
	);
}

ibBackendValueForm* ibValueMetaObjectChartOfCharacteristicTypes::GetListForm(const ibFormRequest& request, ibBackendControlFrame* ownerControl) const
{
	return ibValueMetaObjectGenericData::CreateAndBuildForm(
		request,
		ibValueMetaObjectChartOfCharacteristicTypes::eFormList,
		ownerControl, ibCreateHierarchyList(request.m_create, GetQueryable(), GetDataIsFolder()->GetQueryColumn(), GetDataPresentationAttribute()->GetQueryColumn())   // migrated onto the universal dynamic list (hierarchy via queryable)
	);
}

ibBackendValueForm* ibValueMetaObjectChartOfCharacteristicTypes::GetSelectForm(const ibFormRequest& request, ibBackendControlFrame* ownerControl) const
{
	return ibValueMetaObjectGenericData::CreateAndBuildForm(
		request,
		ibValueMetaObjectChartOfCharacteristicTypes::eFormSelect,
		ownerControl, CreateSourceObject(request.m_create, eFormSelect)   // select front-driven — choice mode
	);
}

ibBackendValueForm* ibValueMetaObjectChartOfCharacteristicTypes::GetFolderSelectForm(const ibFormRequest& request, ibBackendControlFrame* ownerControl) const
{
	return ibValueMetaObjectGenericData::CreateAndBuildForm(
		request,
		ibValueMetaObjectChartOfCharacteristicTypes::eFormFolderSelect,
		ownerControl, CreateSourceObject(request.m_create, eFormFolderSelect)   // folder-select = choice + IsFolder = true
	);
}
#pragma endregion

//***************************************************************************
//*                       Save & load metaData                              *
//***************************************************************************

bool ibValueMetaObjectChartOfCharacteristicTypes::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertyAttributeType->GetName(), m_propertyAttributeType->GetNodeValue());

	node.SetProperty(m_propertyObjectModule->GetName(), m_propertyObjectModule->GetNodeValue());
	node.SetProperty(m_propertyManagerModule->GetName(), m_propertyManagerModule->GetNodeValue());

	node.SetProperty(m_propertyTypesOfCharacteristics->GetName(), m_propertyTypesOfCharacteristics->GetNodeValue());

	node.SetValue(m_propertyDefFormObject->GetName(), GetGuidByID(m_propertyDefFormObject->GetValueAsInteger()).str());
	node.SetValue(m_propertyDefFormFolder->GetName(), GetGuidByID(m_propertyDefFormFolder->GetValueAsInteger()).str());
	node.SetValue(m_propertyDefFormList->GetName(), GetGuidByID(m_propertyDefFormList->GetValueAsInteger()).str());
	node.SetValue(m_propertyDefFormSelect->GetName(), GetGuidByID(m_propertyDefFormSelect->GetValueAsInteger()).str());
	node.SetValue(m_propertyDefFormFolderSelect->GetName(), GetGuidByID(m_propertyDefFormFolderSelect->GetValueAsInteger()).str());

	return ibValueMetaObjectRecordDataHierarchyMutableRef::WriteData(node);
}

bool ibValueMetaObjectChartOfCharacteristicTypes::ReadData(const ibDataNode& node)
{
	m_propertyAttributeType->SetNodeValue(node.GetProperty(m_propertyAttributeType->GetName()));

	m_propertyObjectModule->SetNodeValue(node.GetProperty(m_propertyObjectModule->GetName()));
	m_propertyManagerModule->SetNodeValue(node.GetProperty(m_propertyManagerModule->GetName()));

	m_propertyTypesOfCharacteristics->SetNodeValue(node.GetProperty(m_propertyTypesOfCharacteristics->GetName()));

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

bool ibValueMetaObjectChartOfCharacteristicTypes::OnCreateMetaObject(ibMetaData* metaData, int flags)
{
	if (!ibValueMetaObjectRecordDataHierarchyMutableRef::OnCreateMetaObject(metaData, flags))
		return false;

	return (*m_propertyAttributeType)->OnCreateMetaObject(metaData, flags) && 
		(*m_propertyObjectModule)->OnCreateMetaObject(metaData, flags) &&
		(*m_propertyManagerModule)->OnCreateMetaObject(metaData, flags);
}

bool ibValueMetaObjectChartOfCharacteristicTypes::OnLoadMetaObject(ibMetaData* metaData)
{
	if (!(*m_propertyAttributeType)->OnLoadMetaObject(metaData))
		return false;

	if (!(*m_propertyObjectModule)->OnLoadMetaObject(metaData))
		return false;

	if (!(*m_propertyManagerModule)->OnLoadMetaObject(metaData))
		return false;

	return ibValueMetaObjectRecordDataHierarchyMutableRef::OnLoadMetaObject(metaData);
}

bool ibValueMetaObjectChartOfCharacteristicTypes::OnSaveMetaObject(int flags)
{
	if (!(*m_propertyAttributeType)->OnSaveMetaObject(flags))
		return false;

	if (!(*m_propertyObjectModule)->OnSaveMetaObject(flags))
		return false;

	if (!(*m_propertyManagerModule)->OnSaveMetaObject(flags))
		return false;

	return ibValueMetaObjectRecordDataHierarchyMutableRef::OnSaveMetaObject(flags);
}

bool ibValueMetaObjectChartOfCharacteristicTypes::OnDeleteMetaObject()
{
	if (!(*m_propertyAttributeType)->OnDeleteMetaObject())
		return false;

	if (!(*m_propertyObjectModule)->OnDeleteMetaObject())
		return false;

	if (!(*m_propertyManagerModule)->OnDeleteMetaObject())
		return false;

	return ibValueMetaObjectRecordDataHierarchyMutableRef::OnDeleteMetaObject();
}

bool ibValueMetaObjectChartOfCharacteristicTypes::OnReloadMetaObject()
{

	if (auto* cc = m_metaData->GetCompileCache()) {
		ibValueRecordDataObjectChartOfCharacteristicTypes* pDataRef = nullptr;
		if (!cc->FindCompileModule(m_propertyObjectModule->GetMetaObject(), pDataRef)) {
			return true;
		}
		return pDataRef->InitializeObject();
	}

	return true;
}

#include "backend/characteristicCtor.h"

bool ibValueMetaObjectChartOfCharacteristicTypes::OnBeforeRunMetaObject(int flags)
{
	if (!(*m_propertyAttributeType)->OnBeforeRunMetaObject(flags))
		return false;

	if (!(*m_propertyObjectModule)->OnBeforeRunMetaObject(flags))
		return false;

	if (!(*m_propertyManagerModule)->OnBeforeRunMetaObject(flags))
		return false;

	registerSelection();
	registerCharacteristic();

	if (!ibValueMetaObjectRecordDataHierarchyMutableRef::OnBeforeRunMetaObject(flags))
		return false;

	const ibCtorMetaValueType* typeCtor =
		m_metaData->GetTypeCtor(this, ibCtorObjectMetaType::ibCtorObjectMetaType_Reference);

	if (typeCtor != nullptr && !(*m_propertyAttributeParent)->ContainType(typeCtor->GetClassType())) {
		(*m_propertyAttributeParent)->SetDefaultMetaType(typeCtor->GetClassType());
	}

	return true;
}

bool ibValueMetaObjectChartOfCharacteristicTypes::OnAfterRunMetaObject(int flags)
{
	if (!(*m_propertyAttributeType)->OnAfterRunMetaObject(flags))
		return false;

	if (!(*m_propertyObjectModule)->OnAfterRunMetaObject(flags))
		return false;

	if (!(*m_propertyManagerModule)->OnAfterRunMetaObject(flags))
		return false;


	if (auto* cc = m_metaData->GetCompileCache()) {

		if (ibValueMetaObjectRecordDataHierarchyMutableRef::OnAfterRunMetaObject(flags))
			return cc->AddCompileModule(m_propertyObjectModule->GetMetaObject(), [this]() -> ibValue { return CreateObjectValue(ibObjectMode::OBJECT_ITEM); });

		return false;
	}

	return ibValueMetaObjectRecordDataHierarchyMutableRef::OnAfterRunMetaObject(flags);
}

bool ibValueMetaObjectChartOfCharacteristicTypes::OnBeforeCloseMetaObject()
{
	if (!(*m_propertyAttributeType)->OnBeforeCloseMetaObject())
		return false;

	if (!(*m_propertyObjectModule)->OnBeforeCloseMetaObject())
		return false;

	if (!(*m_propertyManagerModule)->OnBeforeCloseMetaObject())
		return false;


	if (auto* cc = m_metaData->GetCompileCache()) {

		if (ibValueMetaObjectRecordDataHierarchyMutableRef::OnBeforeCloseMetaObject())
			{ cc->RemoveCompileModule(m_propertyObjectModule->GetMetaObject()); return true; }

		return false;
	}

	return ibValueMetaObjectRecordDataHierarchyMutableRef::OnBeforeCloseMetaObject();
}

bool ibValueMetaObjectChartOfCharacteristicTypes::OnAfterCloseMetaObject()
{
	if (!(*m_propertyAttributeType)->OnAfterCloseMetaObject())
		return false;

	if (!(*m_propertyObjectModule)->OnAfterCloseMetaObject())
		return false;

	if (!(*m_propertyManagerModule)->OnAfterCloseMetaObject())
		return false;

	unregisterSelection();
	unregisterCharacteristic();

	return ibValueMetaObjectRecordDataHierarchyMutableRef::OnAfterCloseMetaObject();
}

//***********************************************************************
//*                             form events                             *
//***********************************************************************

void ibValueMetaObjectChartOfCharacteristicTypes::OnCreateFormObject(ibValueMetaObjectFormBase* metaForm)
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

void ibValueMetaObjectChartOfCharacteristicTypes::OnRemoveMetaForm(ibValueMetaObjectFormBase* metaForm)
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

// ⭐⭐ WHAT A KIND OF THIS CHART ALLOWS — the one reading of `Type` in the whole tree, and the only place
// that knows a characteristic has one. Every road that narrows a value by a kind comes here: a subconto
// written into an accounting register, a field filled under a link by type, a value set by a script.
//
// The element stands for a record of this chart; its `Type` is read by the id the chart declares for it,
// so nothing is searched for and no wrong attribute can be picked.
bool ibValueMetaObjectChartOfCharacteristicTypes::AdjustOutValue(const ibValueDataObject& element,
	const ibValue& varValue, ibValue& out) const
{
	// 🛑 A CHART WITH NO `Type` OF ITS OWN GOVERNS NOTHING — and it says so by handing the question BACK
	// to the base, not by refusing: a kind is then an ordinary reference and narrows to its own class.
	// Refusing here would leave `out` unfilled, and the caller writes what it finds there.
	const ibValueMetaObjectAttributePredefined* type = GetDataType();
	if (type == nullptr)
		return ibValueMetaObjectGenericData::AdjustOutValue(element, varValue, out);

	// ⭐ A KIND THAT WAS NEVER TOLD A TYPE NARROWS NOTHING — the value comes back as it came, which is a
	// different answer from the refusal above and has to be: the field's own contour is then the only
	// bound, and that is what a subconto with an unfilled kind has always done.
	ibValue declared;
	if (!element.GetValueByMetaID(type->GetMetaID(), declared) || declared.IsEmpty()) {
		out = varValue;
		return true;
	}

	// And that value narrows the incoming one by itself: a type description takes what it describes,
	// qualifiers and all (valueType.h). Nothing here knows that is what it is.
	return declared.AdjustOutValue(varValue, out);
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

METADATA_TYPE_REGISTER(ibValueMetaObjectChartOfCharacteristicTypes, "ChartOfCharacteristicTypes", g_metaChartOfCharacteristicTypesCLSID);

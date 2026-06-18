////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : enumeration metaData
////////////////////////////////////////////////////////////////////////////

#include "enumeration.h"
#include "backend/serialize/dataBuilder.h"
#include "backend/metaData.h"
#include "list/objectList.h"


//********************************************************************************************

#include "databaseLayer/databaseLayer.h"
#include "backend/appData.h"

//********************************************************************************************
//*                                      metaData                                            *
//********************************************************************************************

ibValueMetaObjectEnumeration::ibValueMetaObjectEnumeration() : ibValueMetaObjectRecordDataEnumRef()
{
	m_propertyQuickChoice->SetValue(true);
}

ibValueMetaObjectEnumeration::~ibValueMetaObjectEnumeration()
{
}

ibValueMetaObjectFormBase* ibValueMetaObjectEnumeration::GetDefaultFormByID(const ibFormID& id) const
{
	if (id == eFormList && m_propertyDefFormList->GetValueAsInteger() != wxNOT_FOUND) {
		return FindFormObjectByFilter(m_propertyDefFormList->GetValueAsInteger());
	}
	else if (id == eFormSelect && m_propertyDefFormSelect->GetValueAsInteger() != wxNOT_FOUND) {
		return FindFormObjectByFilter(m_propertyDefFormSelect->GetValueAsInteger());
	}

	return nullptr;
}

#include "enumerationManager.h"

ibValueManagerDataObject* ibValueMetaObjectEnumeration::CreateManagerDataObjectValue() const
{
	return new ibValueManagerDataObjectEnumeration(this);
}

ibSourceDataObject* ibValueMetaObjectEnumeration::CreateSourceObject(const ibValueMetaObjectFormBase* metaObject) const
{
	switch (metaObject->GetTypeForm())
	{
	case eFormList:
		return new ibValueListDataObjectEnumRef(this, metaObject->GetTypeForm());
	case eFormSelect:
		return new ibValueListDataObjectEnumRef(this, metaObject->GetTypeForm(), true);
	}

	return nullptr;
}

#pragma region _form_builder_h_
ibBackendValueForm* ibValueMetaObjectEnumeration::GetListForm(const wxString& strFormName, ibBackendControlFrame* ownerControl, const ibUniqueKey& formGuid) const
{
	return ibValueMetaObjectGenericData::CreateAndBuildForm(
		strFormName,
		ibValueMetaObjectEnumeration::eFormList,
		ownerControl, new ibValueListDataObjectEnumRef(this, ibValueMetaObjectEnumeration::eFormList),
		formGuid
	);
}

ibBackendValueForm* ibValueMetaObjectEnumeration::GetSelectForm(const wxString& strFormName, ibBackendControlFrame* ownerControl, const ibUniqueKey& formGuid) const
{
	return ibValueMetaObjectGenericData::CreateAndBuildForm(
		strFormName,
		ibValueMetaObjectEnumeration::eFormSelect,
		ownerControl, new ibValueListDataObjectEnumRef(this, ibValueMetaObjectEnumeration::eFormSelect, true),
		formGuid
	);
}
#pragma endregion

wxString ibValueMetaObjectEnumeration::GetDataPresentation(const ibValueDataObject* objValue) const
{
	for (auto obj : GetEnumObjectArray()) {
		if (objValue->GetGuid() == obj->GetGuid()) {
			return obj->GetSynonym();
		}
	}
	return wxEmptyString;
}

//***************************************************************************
//*                       Save & load metaData                              *
//***************************************************************************

bool ibValueMetaObjectEnumeration::WriteData(ibDataNode& node)
{
	node.SetProperty(m_propertyManagerModule->GetName(), m_propertyManagerModule->GetNodeValue());

	node.SetValue(m_propertyDefFormList->GetName(), GetGuidByID(m_propertyDefFormList->GetValueAsInteger()).str());
	node.SetValue(m_propertyDefFormSelect->GetName(), GetGuidByID(m_propertyDefFormSelect->GetValueAsInteger()).str());

	return ibValueMetaObjectRecordDataEnumRef::WriteData(node);
}

bool ibValueMetaObjectEnumeration::ReadData(const ibDataNode& node)
{
	m_propertyManagerModule->ReadNodeValue(node.GetProperty(m_propertyManagerModule->GetName()));

	m_propertyDefFormList->SetValue(GetIdByGuid(node.GetValue<wxString>(m_propertyDefFormList->GetName())));
	m_propertyDefFormSelect->SetValue(GetIdByGuid(node.GetValue<wxString>(m_propertyDefFormSelect->GetName())));

	return ibValueMetaObjectRecordDataEnumRef::ReadData(node);
}

//***********************************************************************
//*                           read & save events                        *
//***********************************************************************

bool ibValueMetaObjectEnumeration::OnCreateMetaObject(ibMetaData* metaData, int flags)
{
	if (!ibValueMetaObjectRecordDataEnumRef::OnCreateMetaObject(metaData, flags))
		return false;

	return (*m_propertyManagerModule)->OnCreateMetaObject(metaData, flags);
}

bool ibValueMetaObjectEnumeration::OnLoadMetaObject(ibMetaData* metaData)
{
	if (!(*m_propertyManagerModule)->OnLoadMetaObject(metaData))
		return false;

	return ibValueMetaObjectRecordDataEnumRef::OnLoadMetaObject(metaData);
}

bool ibValueMetaObjectEnumeration::OnSaveMetaObject(int flags)
{
	if (!(*m_propertyManagerModule)->OnSaveMetaObject(flags))
		return false;

#if _USE_SAVE_METADATA_IN_TRANSACTION == 1
	if (GetEnumObjectArray().size() == 0) {
		RestructureError(_("! Doesn't have any enumeration ") + GetFullName());
		return false;
	}
#endif 

	return ibValueMetaObjectRecordDataEnumRef::OnSaveMetaObject(flags);
}

bool ibValueMetaObjectEnumeration::OnDeleteMetaObject()
{
	if (!(*m_propertyManagerModule)->OnDeleteMetaObject())
		return false;

	return ibValueMetaObjectRecordDataEnumRef::OnDeleteMetaObject();
}

bool ibValueMetaObjectEnumeration::OnReloadMetaObject()
{
	return true;
}

bool ibValueMetaObjectEnumeration::OnBeforeRunMetaObject(int flags)
{
	if (!(*m_propertyManagerModule)->OnBeforeRunMetaObject(flags))
		return false;

	return ibValueMetaObjectRecordDataEnumRef::OnBeforeRunMetaObject(flags);
}

bool ibValueMetaObjectEnumeration::OnAfterRunMetaObject(int flags)
{
	if (!(*m_propertyManagerModule)->OnAfterRunMetaObject(flags))
		return false;

	return ibValueMetaObjectRecordDataEnumRef::OnAfterRunMetaObject(flags);
}

bool ibValueMetaObjectEnumeration::OnBeforeCloseMetaObject()
{
	if (!(*m_propertyManagerModule)->OnBeforeCloseMetaObject())
		return false;

	return ibValueMetaObjectRecordDataEnumRef::OnBeforeCloseMetaObject();
}

bool ibValueMetaObjectEnumeration::OnAfterCloseMetaObject()
{
	if (!(*m_propertyManagerModule)->OnAfterCloseMetaObject())
		return false;

	return ibValueMetaObjectRecordDataEnumRef::OnAfterCloseMetaObject();
}

//***********************************************************************
//*                             form events                             *
//***********************************************************************

void ibValueMetaObjectEnumeration::OnCreateFormObject(ibValueMetaObjectFormBase* metaForm)
{
	if (metaForm->GetTypeForm() == ibValueMetaObjectEnumeration::eFormList
		&& m_propertyDefFormList->GetValueAsInteger() == wxNOT_FOUND)
	{
		m_propertyDefFormList->SetValue(metaForm->GetMetaID());
	}
	else if (metaForm->GetTypeForm() == ibValueMetaObjectEnumeration::eFormSelect
		&& m_propertyDefFormSelect->GetValueAsInteger() == wxNOT_FOUND)
	{
		m_propertyDefFormSelect->SetValue(metaForm->GetMetaID());
	}
}

void ibValueMetaObjectEnumeration::OnRemoveMetaForm(ibValueMetaObjectFormBase* metaForm)
{
	if (metaForm->GetTypeForm() == ibValueMetaObjectEnumeration::eFormList
		&& m_propertyDefFormList->GetValueAsInteger() == metaForm->GetMetaID())
	{
		m_propertyDefFormList->SetValue(wxNOT_FOUND);
	}
	else if (metaForm->GetTypeForm() == ibValueMetaObjectEnumeration::eFormSelect
		&& m_propertyDefFormSelect->GetValueAsInteger() == metaForm->GetMetaID())
	{
		m_propertyDefFormSelect->SetValue(wxNOT_FOUND);
	}
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

METADATA_TYPE_REGISTER(ibValueMetaObjectEnumeration, "Enumeration", g_metaEnumerationCLSID);
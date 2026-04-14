////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : common classes for catalogs, docs etc..  
////////////////////////////////////////////////////////////////////////////

#include "commonObject.h"
#include "backend/metaData.h"
#include "backend/srcExplorer.h"
#include "backend/system/systemManager.h"
#include "backend/objCtor.h"

#include "backend/metaCollection/partial/reference/reference.h"

//***********************************************************************
//*								 metaData                               * 
//***********************************************************************

wxIMPLEMENT_ABSTRACT_CLASS(ibValueMetaObjectGenericData, ibValueMetaObjectCompositeData);
wxIMPLEMENT_ABSTRACT_CLASS(ibValueMetaObjectRegisterData, ibValueMetaObjectGenericData);

wxIMPLEMENT_ABSTRACT_CLASS(ibValueMetaObjectRecordData, ibValueMetaObjectGenericData);
wxIMPLEMENT_ABSTRACT_CLASS(ibValueMetaObjectRecordDataExt, ibValueMetaObjectRecordData);
wxIMPLEMENT_ABSTRACT_CLASS(ibValueMetaObjectRecordDataRef, ibValueMetaObjectRecordData);
wxIMPLEMENT_ABSTRACT_CLASS(ibValueMetaObjectRecordDataEnumRef, ibValueMetaObjectRecordDataRef);
wxIMPLEMENT_ABSTRACT_CLASS(ibValueMetaObjectRecordDataMutableRef, ibValueMetaObjectRecordDataRef);
wxIMPLEMENT_ABSTRACT_CLASS(ibValueMetaObjectRecordDataHierarchyMutableRef, ibValueMetaObjectRecordDataMutableRef);

//***********************************************************************
//*							ibValueMetaObjectGenericData				    *
//***********************************************************************

#pragma region _form_builder_h_
ibBackendValueForm* ibValueMetaObjectGenericData::GetGenericForm(const wxString& strFormName, ibBackendControlFrame* ownerControl, const ibUniqueKey& formGuid)
{
	return CreateAndBuildForm(strFormName, defaultFormType, ownerControl, nullptr, formGuid);
}
#pragma endregion
#pragma region _form_creator_h_
ibBackendValueForm* ibValueMetaObjectGenericData::CreateAndBuildForm(const wxString& strFormName, const ibFormID& form_id, ibBackendControlFrame* ownerControl, ibSourceDataObject* srcObject, const ibUniqueKey& formGuid)
{
#pragma region _source_guard_
	class ibSourceDataObjectGuard {
	public:

		ibSourceDataObjectGuard(ibSourceDataObject* srcObject) : m_srcObject(srcObject) {
			if (m_srcObject != nullptr) m_srcObject->SourceIncrRef();
		}

		~ibSourceDataObjectGuard() {
			if (m_srcObject != nullptr) m_srcObject->SourceDecrRef();
		}

	private:
		ibSourceDataObject* m_srcObject;
	};

	ibSourceDataObjectGuard sourceGuard(srcObject);
#pragma endregion

	ibValueMetaObjectFormBase* creator = nullptr;

	if (!strFormName.IsEmpty()) {

		creator = FindFormObjectByFilter(strFormName, form_id);

		if (creator == nullptr) {
			ibBackendCoreException::Error(_("Form not found '%s'"), strFormName);
			return nullptr;
		}
	}

	if (!AccessRight_Show()) {
		ibBackendAccessException::Error();
		return nullptr;
	}

	ibBackendValueForm* result = ibBackendValueForm::FindFormByUniqueKey(ownerControl, srcObject, formGuid);

	if (result == nullptr) {

		result = ibValueMetaObjectFormBase::CreateAndBuildForm(
			creator != nullptr ? creator : GetDefaultFormByID(form_id),
			form_id,
			ownerControl, srcObject, formGuid
		);
	}

	return result;
}

#pragma endregion
#pragma region _template_builder_h_

ibValueSpreadsheetDocument* ibValueMetaObjectGenericData::GetTemplate(const wxString& strTemplateName)
{
	ibValueMetaObjectSpreadsheetBase* creator = nullptr;

	if (!strTemplateName.IsEmpty()) {

		creator = FindTemplateObjectByFilter(strTemplateName);

		if (creator == nullptr) {
			ibBackendCoreException::Error(_("Template not found '%s'"), strTemplateName);
			return nullptr;
		}

		ibValueSpreadsheetDocument* valueSpreadsheetDocument =
			new ibValueSpreadsheetDocument(creator->GetSpreadsheetDesc());

		valueSpreadsheetDocument->PrepareNames();
		return valueSpreadsheetDocument;
	}

	ibBackendCoreException::Error(_("Template not found!"));
	return nullptr;
}

#pragma endregion

//***********************************************************************
//*                           ibValueMetaObjectRecordData				*
//***********************************************************************

#include "backend/fileSystem/fs.h"

//***********************************************************************
//*                           read & save events                        *
//***********************************************************************

bool ibValueMetaObjectRecordData::OnLoadMetaObject(ibMetaData* metaData)
{
	return ibValueMetaObject::OnLoadMetaObject(metaData);
}

bool ibValueMetaObjectRecordData::OnSaveMetaObject(int flags)
{
	return ibValueMetaObject::OnSaveMetaObject(flags);
}

bool ibValueMetaObjectRecordData::OnDeleteMetaObject()
{
	return ibValueMetaObject::OnDeleteMetaObject();
}

bool ibValueMetaObjectRecordData::OnBeforeRunMetaObject(int flags)
{
	return ibValueMetaObject::OnBeforeRunMetaObject(flags);
}

bool ibValueMetaObjectRecordData::OnAfterCloseMetaObject()
{
	return ibValueMetaObject::OnAfterCloseMetaObject();
}

//***********************************************************************
//*						ibValueMetaObjectRecordDataExt					*
//***********************************************************************

ibValueMetaObjectRecordDataExt::ibValueMetaObjectRecordDataExt() :
	ibValueMetaObjectRecordData()
{
}

ibValueRecordDataObjectExt* ibValueMetaObjectRecordDataExt::CreateObjectValue()
{
	ibValueRecordDataObjectExt* createdValue = CreateObjectExtValue();
	if (!IsExternalCreate()) {
		if (createdValue && !createdValue->InitializeObject()) {
			wxDELETE(createdValue);
			return nullptr;
		}
	}
	return createdValue;
}

ibValueRecordDataObjectExt* ibValueMetaObjectRecordDataExt::CreateObjectValue(ibValueRecordDataObjectExt* objSrc)
{
	ibValueRecordDataObjectExt* createdValue = CreateObjectExtValue();
	if (!IsExternalCreate()) {
		if (createdValue && !createdValue->InitializeObject(objSrc)) {
			wxDELETE(createdValue);
			return nullptr;
		}
	}
	return createdValue;
}

ibValueRecordDataObject* ibValueMetaObjectRecordDataExt::CreateRecordDataObjectValue()
{
	return CreateObjectValue();
}

//***********************************************************************
//*                           read & save events                        *
//***********************************************************************

bool ibValueMetaObjectRecordDataExt::OnBeforeRunMetaObject(int flags)
{
	if (IsExternalCreate()) {
		registerExternalObject();
		registerExternalManager();
	}
	else {
		registerObject();
		registerManager();
	}

	return ibValueMetaObjectRecordData::OnBeforeRunMetaObject(flags);
}

bool ibValueMetaObjectRecordDataExt::OnAfterCloseMetaObject()
{
	unregisterObject();
	unregisterManager();

	return ibValueMetaObjectRecordData::OnAfterCloseMetaObject();
}

//***********************************************************************
//*						ibValueMetaObjectRecordDataRef					*
//***********************************************************************

ibValueMetaObjectRecordDataRef::ibValueMetaObjectRecordDataRef() : ibValueMetaObjectRecordData()
{
}

ibValueMetaObjectRecordDataRef::~ibValueMetaObjectRecordDataRef()
{
	//wxDELETE((*m_propertyAttributeReference));
}

//***************************************************************************
//*                       Save & load metaData                              *
//***************************************************************************

bool ibValueMetaObjectRecordDataRef::LoadData(ibReaderMemory& dataReader)
{
	//get quick choice
	m_propertyQuickChoice->SetValue(dataReader.r_u8());

	//load default attributes:
	(*m_propertyAttributeReference)->LoadMeta(dataReader);

	return ibValueMetaObjectRecordData::LoadData(dataReader);
}

bool ibValueMetaObjectRecordDataRef::SaveData(ibWriterMemory& dataWritter)
{
	//set quick choice
	dataWritter.w_u8(m_propertyQuickChoice->GetValueAsBoolean());

	//save default attributes:
	(*m_propertyAttributeReference)->SaveMeta(dataWritter);

	//create or update table:
	return ibValueMetaObjectRecordData::SaveData(dataWritter);
}

//***********************************************************************
//*                           read & save events                        *
//***********************************************************************

bool ibValueMetaObjectRecordDataRef::OnCreateMetaObject(ibMetaData* metaData, int flags)
{
	if (!ibValueMetaObjectRecordData::OnCreateMetaObject(metaData, flags))
		return false;

	return (*m_propertyAttributeReference)->OnCreateMetaObject(metaData, flags);
}

#include "backend/appData.h"
#include "databaseLayer/databaseLayer.h"

bool ibValueMetaObjectRecordDataRef::OnLoadMetaObject(ibMetaData* metaData)
{
	if (!(*m_propertyAttributeReference)->OnLoadMetaObject(metaData))
		return false;

	return ibValueMetaObjectRecordData::OnLoadMetaObject(metaData);
}

bool ibValueMetaObjectRecordDataRef::OnSaveMetaObject(int flags)
{
	if (!(*m_propertyAttributeReference)->OnSaveMetaObject(flags))
		return false;

	return ibValueMetaObjectRecordData::OnSaveMetaObject(flags);
}

bool ibValueMetaObjectRecordDataRef::OnDeleteMetaObject()
{
	if (!(*m_propertyAttributeReference)->OnDeleteMetaObject())
		return false;

	return ibValueMetaObjectRecordData::OnDeleteMetaObject();
}

bool ibValueMetaObjectRecordDataRef::OnBeforeRunMetaObject(int flags)
{
	if (!(*m_propertyAttributeReference)->OnBeforeRunMetaObject(flags))
		return false;

	registerReference();
	registerRefList();
	registerManager();

	const ibCtorMetaValueType* typeCtor = m_metaData->GetTypeCtor(this, ibCtorObjectMetaType::ibCtorObjectMetaType_Reference);
	if (typeCtor != nullptr)
		(*m_propertyAttributeReference)->SetDefaultMetaType(typeCtor->GetClassType());

	return ibValueMetaObjectRecordData::OnBeforeRunMetaObject(flags);
}

bool ibValueMetaObjectRecordDataRef::OnAfterRunMetaObject(int flags)
{
	return ibValueMetaObjectRecordData::OnAfterRunMetaObject(flags);
}

bool ibValueMetaObjectRecordDataRef::OnBeforeCloseMetaObject()
{
	return ibValueMetaObjectRecordData::OnBeforeCloseMetaObject();
}

bool ibValueMetaObjectRecordDataRef::OnAfterCloseMetaObject()
{
	if (!(*m_propertyAttributeReference)->OnAfterCloseMetaObject())
		return false;

	if (m_propertyAttributeReference != nullptr)
		(*m_propertyAttributeReference)->SetDefaultMetaType(ibValueTypes::TYPE_EMPTY);

	unregisterReference();
	unregisteRefList();
	unregisterManager();

	return ibValueMetaObjectRecordData::OnAfterCloseMetaObject();
}

//***********************************************************************
//*                             form events                             *
//***********************************************************************

//process choice 
bool ibValueMetaObjectRecordDataRef::ProcessChoice(ibBackendControlFrame* ownerValue, const wxString& strFormName, ibSelectMode selMode)
{
	ibBackendValueForm* const selectChoiceForm = GetSelectForm(strFormName, ownerValue);
	if (selectChoiceForm == nullptr)
		return false;

	selectChoiceForm->ShowForm();
	return true;
}

ibValueReferenceDataObject* ibValueMetaObjectRecordDataRef::FindObjectValue(const ibGuid& objGuid)
{
	if (!objGuid.isValid())
		return nullptr;
	return ibValueReferenceDataObject::Create(this, objGuid);
}

//***********************************************************************
//*						ibValueMetaObjectRecordDataEnumRef					*
//***********************************************************************

///////////////////////////////////////////////////////////////////////////////////////////////

ibValueMetaObjectRecordDataEnumRef::ibValueMetaObjectRecordDataEnumRef() : ibValueMetaObjectRecordDataRef()
{
}

ibValueMetaObjectRecordDataEnumRef::~ibValueMetaObjectRecordDataEnumRef()
{
	//wxDELETE((*m_propertyAttributeOrder));
}

//***************************************************************************
//*                       Save & load metaData                              *
//***************************************************************************

bool ibValueMetaObjectRecordDataEnumRef::LoadData(ibReaderMemory& dataReader)
{
	//load default attributes:
	(*m_propertyAttributeOrder)->LoadMeta(dataReader);
	return ibValueMetaObjectRecordDataRef::LoadData(dataReader);
}

bool ibValueMetaObjectRecordDataEnumRef::SaveData(ibWriterMemory& dataWritter)
{
	//save default attributes:
	(*m_propertyAttributeOrder)->SaveMeta(dataWritter);
	return ibValueMetaObjectRecordDataRef::SaveData(dataWritter);
}

//***********************************************************************
//*                           read & save events                        *
//***********************************************************************

bool ibValueMetaObjectRecordDataEnumRef::OnCreateMetaObject(ibMetaData* metaData, int flags)
{
	if (!ibValueMetaObjectRecordDataRef::OnCreateMetaObject(metaData, flags))
		return false;

	return (*m_propertyAttributeOrder)->OnCreateMetaObject(metaData, flags);
}

bool ibValueMetaObjectRecordDataEnumRef::OnLoadMetaObject(ibMetaData* metaData)
{
	if (!(*m_propertyAttributeOrder)->OnLoadMetaObject(metaData))
		return false;

	return ibValueMetaObjectRecordDataRef::OnLoadMetaObject(metaData);
}

bool ibValueMetaObjectRecordDataEnumRef::OnSaveMetaObject(int flags)
{
	if (!(*m_propertyAttributeOrder)->OnSaveMetaObject(flags))
		return false;

	return ibValueMetaObjectRecordDataRef::OnSaveMetaObject(flags);
}

bool ibValueMetaObjectRecordDataEnumRef::OnDeleteMetaObject()
{
	if (!(*m_propertyAttributeOrder)->OnDeleteMetaObject())
		return false;

	return ibValueMetaObjectRecordDataRef::OnDeleteMetaObject();
}

bool ibValueMetaObjectRecordDataEnumRef::OnBeforeRunMetaObject(int flags)
{
	if (!(*m_propertyAttributeOrder)->OnBeforeRunMetaObject(flags))
		return false;

	return ibValueMetaObjectRecordDataRef::OnBeforeRunMetaObject(flags);
}

bool ibValueMetaObjectRecordDataEnumRef::OnAfterRunMetaObject(int flags)
{
	return ibValueMetaObjectRecordDataRef::OnAfterRunMetaObject(flags);
}

bool ibValueMetaObjectRecordDataEnumRef::OnBeforeCloseMetaObject()
{
	return ibValueMetaObjectRecordDataRef::OnBeforeCloseMetaObject();
}

bool ibValueMetaObjectRecordDataEnumRef::OnAfterCloseMetaObject()
{
	if (!(*m_propertyAttributeOrder)->OnAfterCloseMetaObject())
		return false;

	return ibValueMetaObjectRecordDataRef::OnAfterCloseMetaObject();
}

//***********************************************************************
//*						ibValueMetaObjectRecordDataMutableRef					*
//***********************************************************************

ibValueMetaObjectRecordDataMutableRef::ibValueMetaObjectRecordDataMutableRef() : ibValueMetaObjectRecordDataRef()
{
}

ibValueMetaObjectRecordDataMutableRef::~ibValueMetaObjectRecordDataMutableRef()
{
	//wxDELETE((*m_propertyAttributeDataVersion);
	//wxDELETE((*m_propertyAttributeDeletionMark));
}

//***************************************************************************
//*                       Save & load metaData                              *
//***************************************************************************

bool ibValueMetaObjectRecordDataMutableRef::LoadData(ibReaderMemory& dataReader)
{
	//load default attributes:
	(*m_propertyAttributeDataVersion)->LoadMeta(dataReader);
	(*m_propertyAttributeDeletionMark)->LoadMeta(dataReader);

	if (!ibValueMetaObjectRecordDataRef::LoadData(dataReader))
		return false;

	return m_propertyGeneration->LoadData(dataReader);
}

bool ibValueMetaObjectRecordDataMutableRef::SaveData(ibWriterMemory& dataWritter)
{
	//save default attributes:
	(*m_propertyAttributeDataVersion)->SaveMeta(dataWritter);
	(*m_propertyAttributeDeletionMark)->SaveMeta(dataWritter);

	//create or update table:
	if (!ibValueMetaObjectRecordDataRef::SaveData(dataWritter))
		return false;

	return m_propertyGeneration->SaveData(dataWritter);
}

//***********************************************************************
//*                           read & save events                        *
//***********************************************************************

bool ibValueMetaObjectRecordDataMutableRef::OnCreateMetaObject(ibMetaData* metaData, int flags)
{
	if (!ibValueMetaObjectRecordDataRef::OnCreateMetaObject(metaData, flags))
		return false;

	return (*m_propertyAttributeDataVersion)->OnCreateMetaObject(metaData, flags)
		&& (*m_propertyAttributeDeletionMark)->OnCreateMetaObject(metaData, flags);
}

bool ibValueMetaObjectRecordDataMutableRef::OnLoadMetaObject(ibMetaData* metaData)
{
	if (!(*m_propertyAttributeDataVersion)->OnLoadMetaObject(metaData))
		return false;

	if (!(*m_propertyAttributeDeletionMark)->OnLoadMetaObject(metaData))
		return false;

	return ibValueMetaObjectRecordDataRef::OnLoadMetaObject(metaData);
}

bool ibValueMetaObjectRecordDataMutableRef::OnSaveMetaObject(int flags)
{
	if (!(*m_propertyAttributeDataVersion)->OnSaveMetaObject(flags))
		return false;

	if (!(*m_propertyAttributeDeletionMark)->OnSaveMetaObject(flags))
		return false;

	return ibValueMetaObjectRecordDataRef::OnSaveMetaObject(flags);
}

bool ibValueMetaObjectRecordDataMutableRef::OnDeleteMetaObject()
{
	if (!(*m_propertyAttributeDataVersion)->OnDeleteMetaObject())
		return false;

	if (!(*m_propertyAttributeDeletionMark)->OnDeleteMetaObject())
		return false;

	return ibValueMetaObjectRecordDataRef::OnDeleteMetaObject();
}

bool ibValueMetaObjectRecordDataMutableRef::OnBeforeRunMetaObject(int flags)
{
	if (!(*m_propertyAttributeDataVersion)->OnBeforeRunMetaObject(flags))
		return false;

	if (!(*m_propertyAttributeDeletionMark)->OnBeforeRunMetaObject(flags))
		return false;

	registerObject();
	return ibValueMetaObjectRecordDataRef::OnBeforeRunMetaObject(flags);
}

bool ibValueMetaObjectRecordDataMutableRef::OnAfterRunMetaObject(int flags)
{
	if (!(*m_propertyAttributeDataVersion)->OnAfterRunMetaObject(flags))
		return false;

	if (!(*m_propertyAttributeDeletionMark)->OnAfterRunMetaObject(flags))
		return false;

	return ibValueMetaObjectRecordDataRef::OnAfterRunMetaObject(flags);
}

bool ibValueMetaObjectRecordDataMutableRef::OnBeforeCloseMetaObject()
{
	if (!(*m_propertyAttributeDataVersion)->OnBeforeCloseMetaObject())
		return false;

	if (!(*m_propertyAttributeDeletionMark)->OnBeforeCloseMetaObject())
		return false;

	return ibValueMetaObjectRecordDataRef::OnBeforeCloseMetaObject();
}

bool ibValueMetaObjectRecordDataMutableRef::OnAfterCloseMetaObject()
{
	if (!(*m_propertyAttributeDataVersion)->OnAfterCloseMetaObject())
		return false;

	if (!(*m_propertyAttributeDeletionMark)->OnAfterCloseMetaObject())
		return false;

	unregisterObject();
	return ibValueMetaObjectRecordDataRef::OnAfterCloseMetaObject();
}

///////////////////////////////////////////////////////////////////////////////

ibValueRecordDataObjectRef* ibValueMetaObjectRecordDataMutableRef::CreateObjectValue()
{
	ibValueRecordDataObjectRef* createdValue = CreateObjectRefValue();
	if (createdValue && !createdValue->InitializeObject()) {
		wxDELETE(createdValue);
		return nullptr;
	}

	return createdValue;
}

ibValueRecordDataObjectRef* ibValueMetaObjectRecordDataMutableRef::CreateObjectValue(const ibGuid& guid)
{
	ibValueRecordDataObjectRef* createdValue = CreateObjectRefValue(guid);
	if (createdValue && !createdValue->InitializeObject()) {
		wxDELETE(createdValue);
		return nullptr;
	}
	return createdValue;
}

ibValueRecordDataObjectRef* ibValueMetaObjectRecordDataMutableRef::CreateObjectValue(ibValueRecordDataObjectRef* objSrc, bool generate)
{
	if (objSrc == nullptr)
		return nullptr;
	ibValueRecordDataObjectRef* createdValue = CreateObjectRefValue();
	if (createdValue && !createdValue->InitializeObject(objSrc, generate)) {
		wxDELETE(createdValue);
		return nullptr;
	}
	return createdValue;
}

ibValueRecordDataObjectRef* ibValueMetaObjectRecordDataMutableRef::CopyObjectValue(const ibGuid& srcGuid)
{
	ibValueRecordDataObjectRef* createdValue = CreateObjectRefValue();
	if (createdValue && !createdValue->InitializeObject(srcGuid)) {
		wxDELETE(createdValue);
		return nullptr;
	}
	return createdValue;
}

ibValueRecordDataObject* ibValueMetaObjectRecordDataMutableRef::CreateRecordDataObjectValue()
{
	return CreateObjectValue();
}

//***********************************************************************
//*						ibValueMetaObjectRecordDataHierarchyMutableRef	*
//***********************************************************************

ibValueMetaObjectRecordDataHierarchyMutableRef::ibValueMetaObjectRecordDataHierarchyMutableRef()
	: ibValueMetaObjectRecordDataMutableRef()
{
}

ibValueMetaObjectRecordDataHierarchyMutableRef::~ibValueMetaObjectRecordDataHierarchyMutableRef()
{
	//wxDELETE(m_propertyAttributeCode);
	//wxDELETE(m_propertyAttributeDescription);
	//wxDELETE(m_propertyAttributeParent);
	//wxDELETE(m_propertyAttributeIsFolder);
}

////////////////////////////////////////////////////////////////////////////////////////////////

ibValueRecordDataObjectHierarchyRef* ibValueMetaObjectRecordDataHierarchyMutableRef::CreateObjectValue(ibObjectMode mode)
{
	ibValueRecordDataObjectHierarchyRef* createdValue = CreateObjectRefValue(mode);
	if (createdValue && !createdValue->InitializeObject()) {
		wxDELETE(createdValue);
		return nullptr;
	}
	return createdValue;
}

ibValueRecordDataObjectHierarchyRef* ibValueMetaObjectRecordDataHierarchyMutableRef::CreateObjectValue(ibObjectMode mode, const ibGuid& guid)
{
	ibValueRecordDataObjectHierarchyRef* createdValue = CreateObjectRefValue(mode, guid);
	if (createdValue && !createdValue->InitializeObject()) {
		wxDELETE(createdValue);
		return nullptr;
	}
	return createdValue;
}

ibValueRecordDataObjectHierarchyRef* ibValueMetaObjectRecordDataHierarchyMutableRef::CreateObjectValue(ibObjectMode mode, ibValueRecordDataObjectRef* objSrc, bool generate)
{
	ibValueRecordDataObjectHierarchyRef* createdValue = CreateObjectRefValue(mode);
	if (createdValue && !createdValue->InitializeObject(objSrc, generate)) {
		wxDELETE(createdValue);
		return nullptr;
	}
	return createdValue;
}

ibValueRecordDataObjectHierarchyRef* ibValueMetaObjectRecordDataHierarchyMutableRef::CopyObjectValue(ibObjectMode mode, const ibGuid& srcGuid)
{
	ibValueRecordDataObjectHierarchyRef* createdValue = CreateObjectRefValue(mode);
	if (createdValue && !createdValue->InitializeObject(srcGuid)) {
		wxDELETE(createdValue);
		return nullptr;
	}
	return createdValue;
}

//***************************************************************************
//*                       Predefined values                                 *
//***************************************************************************

#define predefinedBlock 0x1234532

//append predefined value
void ibValueMetaObjectRecordDataHierarchyMutableRef::AppendPredefinedValue(const wxString& strPredefinedName,
	const wxString& strCode, const wxString& strDescription,
	bool valueIsFolder, const wxObjectDataPtr<ibPredefinedValueObject>& valueParent)
{
	m_predefinedObjectVector.emplace_back(
		new ibPredefinedValueObject(wxNewUniqueGuid, strPredefinedName,
			strCode, strDescription, valueIsFolder, valueParent));

	m_metaData->Modify(true);
}

void ibValueMetaObjectRecordDataHierarchyMutableRef::SetPredefinedValue(const ibGuid& predefinedGuid,
	const wxString& strPredefinedName,
	const wxString& strCode, const wxString& strDescription,
	bool valueIsFolder, const wxObjectDataPtr<ibPredefinedValueObject>& valueParent)
{
	wxObjectDataPtr<ibPredefinedValueObject> foundedPredefinedValue = FindPredefinedValue(predefinedGuid);

	if (foundedPredefinedValue != nullptr) {
		
		foundedPredefinedValue->m_strPredefinedName = strPredefinedName;
		foundedPredefinedValue->m_strCode = strCode;
		foundedPredefinedValue->m_strDescription = strDescription;
		foundedPredefinedValue->m_valueIsFolder = valueIsFolder;
		foundedPredefinedValue->m_valueParent = valueParent;
		
		m_metaData->Modify(true);
		return;
	}

	m_predefinedObjectVector.emplace_back(
		new ibPredefinedValueObject(predefinedGuid, strPredefinedName,
			strCode, strDescription, valueIsFolder, valueParent));

	m_metaData->Modify(true);
}

void ibValueMetaObjectRecordDataHierarchyMutableRef::DeletePredefinedValue(const ibGuid& predefinedGuid) {
	
	m_predefinedObjectVector.erase(
		std::remove_if(m_predefinedObjectVector.begin(), m_predefinedObjectVector.end(),
			[predefinedGuid](const auto value) { return predefinedGuid == value->GetPredefinedGuid(); }), m_predefinedObjectVector.end());

	m_metaData->Modify(true);
}

//***************************************************************************
//*                       Save & load metaData                              *
//***************************************************************************

bool ibValueMetaObjectRecordDataHierarchyMutableRef::LoadData(ibReaderMemory& dataReader)
{
	//load predefined value 
	wxMemoryBuffer predefined_buffer;
	if (!dataReader.r_chunk(predefinedBlock, predefined_buffer))
		return false;

	ibReaderMemory predefinedReader(predefined_buffer);

	unsigned int size = predefinedReader.r_u32();
	for (unsigned int i = 0; i < size; i++)
	{
		const ibGuid& valueGuid =
			predefinedReader.r_stringZ();

		wxString valueName = predefinedReader.r_stringZ();
		wxString valueCode = predefinedReader.r_stringZ();
		wxString valueDescription = predefinedReader.r_stringZ();

		bool valueIsFolder = predefinedReader.r_u8();

		m_predefinedObjectVector.emplace_back(
			new ibPredefinedValueObject(valueGuid, valueName, valueCode, valueDescription));
	}

	//load default attributes:
	(*m_propertyAttributePredefined)->LoadMeta(dataReader);
	(*m_propertyAttributeCode)->LoadMeta(dataReader);
	(*m_propertyAttributeDescription)->LoadMeta(dataReader);
	(*m_propertyAttributeParent)->LoadMeta(dataReader);
	(*m_propertyAttributeIsFolder)->LoadMeta(dataReader);

	return ibValueMetaObjectRecordDataMutableRef::LoadData(dataReader);
}

bool ibValueMetaObjectRecordDataHierarchyMutableRef::SaveData(ibWriterMemory& dataWritter)
{
	//save predefined value 
	ibWriterMemory predefinedWritter;
	predefinedWritter.w_u32(m_predefinedObjectVector.size());

	for (const auto& value : m_predefinedObjectVector)
	{
		predefinedWritter.w_stringZ(value->GetPredefinedGuid());
		predefinedWritter.w_stringZ(value->GetPredefinedName());

		predefinedWritter.w_stringZ(value->GetPredefinedCode());
		predefinedWritter.w_stringZ(value->GetPredefinedDescription());

		predefinedWritter.w_u8(value->IsPredefinedFolder());
	}

	dataWritter.w_chunk(predefinedBlock, predefinedWritter.buffer());

	//save default attributes:
	(*m_propertyAttributePredefined)->SaveMeta(dataWritter);
	(*m_propertyAttributeCode)->SaveMeta(dataWritter);
	(*m_propertyAttributeDescription)->SaveMeta(dataWritter);
	(*m_propertyAttributeParent)->SaveMeta(dataWritter);
	(*m_propertyAttributeIsFolder)->SaveMeta(dataWritter);

	//create or update table:
	return ibValueMetaObjectRecordDataMutableRef::SaveData(dataWritter);
}

//***********************************************************************
//*                           read & save events                        *
//***********************************************************************

bool ibValueMetaObjectRecordDataHierarchyMutableRef::OnCreateMetaObject(ibMetaData* metaData, int flags)
{
	if (!ibValueMetaObjectRecordDataMutableRef::OnCreateMetaObject(metaData, flags))
		return false;

	return (*m_propertyAttributePredefined)->OnCreateMetaObject(metaData, flags) &&
		(*m_propertyAttributeCode)->OnCreateMetaObject(metaData, flags) &&
		(*m_propertyAttributeDescription)->OnCreateMetaObject(metaData, flags) &&
		(*m_propertyAttributeParent)->OnCreateMetaObject(metaData, flags) &&
		(*m_propertyAttributeIsFolder)->OnCreateMetaObject(metaData, flags);
}

bool ibValueMetaObjectRecordDataHierarchyMutableRef::OnLoadMetaObject(ibMetaData* metaData)
{
	if (!(*m_propertyAttributePredefined)->OnLoadMetaObject(metaData))
		return false;

	if (!(*m_propertyAttributeCode)->OnLoadMetaObject(metaData))
		return false;

	if (!(*m_propertyAttributeDescription)->OnLoadMetaObject(metaData))
		return false;

	if (!(*m_propertyAttributeParent)->OnLoadMetaObject(metaData))
		return false;

	if (!(*m_propertyAttributeIsFolder)->OnLoadMetaObject(metaData))
		return false;

	return ibValueMetaObjectRecordDataMutableRef::OnLoadMetaObject(metaData);
}

bool ibValueMetaObjectRecordDataHierarchyMutableRef::OnSaveMetaObject(int flags)
{
	if (!(*m_propertyAttributePredefined)->OnSaveMetaObject(flags))
		return false;

	if (!(*m_propertyAttributeCode)->OnSaveMetaObject(flags))
		return false;

	if (!(*m_propertyAttributeDescription)->OnSaveMetaObject(flags))
		return false;

	if (!(*m_propertyAttributeParent)->OnSaveMetaObject(flags))
		return false;

	if (!(*m_propertyAttributeIsFolder)->OnSaveMetaObject(flags))
		return false;

	return ibValueMetaObjectRecordDataMutableRef::OnSaveMetaObject(flags);
}

bool ibValueMetaObjectRecordDataHierarchyMutableRef::OnDeleteMetaObject()
{
	if (!(*m_propertyAttributePredefined)->OnDeleteMetaObject())
		return false;

	if (!(*m_propertyAttributeCode)->OnDeleteMetaObject())
		return false;

	if (!(*m_propertyAttributeDescription)->OnDeleteMetaObject())
		return false;

	if (!(*m_propertyAttributeParent)->OnDeleteMetaObject())
		return false;

	if (!(*m_propertyAttributeIsFolder)->OnDeleteMetaObject())
		return false;

	return ibValueMetaObjectRecordDataMutableRef::OnDeleteMetaObject();
}

bool ibValueMetaObjectRecordDataHierarchyMutableRef::OnBeforeRunMetaObject(int flags)
{
	if (!(*m_propertyAttributePredefined)->OnBeforeRunMetaObject(flags))
		return false;

	if (!(*m_propertyAttributeCode)->OnBeforeRunMetaObject(flags))
		return false;

	if (!(*m_propertyAttributeDescription)->OnBeforeRunMetaObject(flags))
		return false;

	if (!(*m_propertyAttributeParent)->OnBeforeRunMetaObject(flags))
		return false;

	if (!(*m_propertyAttributeIsFolder)->OnBeforeRunMetaObject(flags))
		return false;

	if (!ibValueMetaObjectRecordDataMutableRef::OnBeforeRunMetaObject(flags))
		return false;

	return true;
}

bool ibValueMetaObjectRecordDataHierarchyMutableRef::OnAfterRunMetaObject(int flags)
{
	if (!(*m_propertyAttributePredefined)->OnAfterRunMetaObject(flags))
		return false;

	if (!(*m_propertyAttributeCode)->OnAfterRunMetaObject(flags))
		return false;

	if (!(*m_propertyAttributeDescription)->OnAfterRunMetaObject(flags))
		return false;

	if (!(*m_propertyAttributeParent)->OnAfterRunMetaObject(flags))
		return false;

	if (!(*m_propertyAttributeIsFolder)->OnAfterRunMetaObject(flags))
		return false;

	return ibValueMetaObjectRecordDataMutableRef::OnAfterRunMetaObject(flags);
}

bool ibValueMetaObjectRecordDataHierarchyMutableRef::OnBeforeCloseMetaObject()
{
	if (!(*m_propertyAttributePredefined)->OnBeforeCloseMetaObject())
		return false;

	if (!(*m_propertyAttributeCode)->OnBeforeCloseMetaObject())
		return false;

	if (!(*m_propertyAttributeDescription)->OnBeforeCloseMetaObject())
		return false;

	if (!(*m_propertyAttributeParent)->OnBeforeCloseMetaObject())
		return false;

	if (!(*m_propertyAttributeIsFolder)->OnBeforeCloseMetaObject())
		return false;

	return ibValueMetaObjectRecordDataMutableRef::OnBeforeCloseMetaObject();
}

bool ibValueMetaObjectRecordDataHierarchyMutableRef::OnAfterCloseMetaObject()
{
	if (!(*m_propertyAttributePredefined)->OnAfterCloseMetaObject())
		return false;

	if (!(*m_propertyAttributeCode)->OnAfterCloseMetaObject())
		return false;

	if (!(*m_propertyAttributeDescription)->OnAfterCloseMetaObject())
		return false;

	if (!(*m_propertyAttributeParent)->OnAfterCloseMetaObject())
		return false;

	if (!(*m_propertyAttributeIsFolder)->OnAfterCloseMetaObject())
		return false;

	return ibValueMetaObjectRecordDataMutableRef::OnAfterCloseMetaObject();
}

//////////////////////////////////////////////////////////////////////

bool ibValueMetaObjectRecordDataHierarchyMutableRef::ProcessChoice(ibBackendControlFrame* ownerValue, const wxString& strFormName, ibSelectMode selMode)
{
	if (ownerValue == nullptr)
		return false;

	ibBackendValueForm* selectChoiceForm = nullptr;

	if (selectChoiceForm == nullptr && selMode == ibSelectMode::ibSelectMode_Items || selMode == ibSelectMode::ibSelectMode_FoldersAndItems) {
		selectChoiceForm = GetSelectForm(strFormName, ownerValue);
	}
	else if (selectChoiceForm == nullptr && selMode == ibSelectMode::ibSelectMode_Folders) {
		selectChoiceForm = GetFolderSelectForm(strFormName, ownerValue);
	}

	if (selectChoiceForm == nullptr)
		return false;

	selectChoiceForm->ShowForm();
	return true;
}

//////////////////////////////////////////////////////////////////////

ibValueRecordDataObjectRef* ibValueMetaObjectRecordDataHierarchyMutableRef::CreateObjectRefValue(const ibGuid& objGuid)
{
	return CreateObjectRefValue(ibObjectMode::OBJECT_ITEM, objGuid);
}

//***********************************************************************
//*                      ibValueMetaObjectRegisterData						*
//***********************************************************************

ibValueMetaObjectRegisterData::ibValueMetaObjectRegisterData() : ibValueMetaObjectGenericData()
{
}

ibValueMetaObjectRegisterData::~ibValueMetaObjectRegisterData()
{
	//wxDELETE((*m_propertyAttributeLineActive));
	//wxDELETE((*m_propertyAttributePeriod));
	//wxDELETE((*m_propertyAttributeRecorder));
	//wxDELETE((*m_propertyAttributeLineNumber));
}

//***************************************************************************
//*                       Save & load metaData                              *
//***************************************************************************

bool ibValueMetaObjectRegisterData::LoadData(ibReaderMemory& dataReader)
{
	//load default attributes:
	(*m_propertyAttributeLineActive)->LoadMeta(dataReader);
	(*m_propertyAttributePeriod)->LoadMeta(dataReader);
	(*m_propertyAttributeRecorder)->LoadMeta(dataReader);
	(*m_propertyAttributeLineNumber)->LoadMeta(dataReader);

	return ibValueMetaObjectGenericData::LoadData(dataReader);
}

bool ibValueMetaObjectRegisterData::SaveData(ibWriterMemory& dataWritter)
{
	//save default attributes:
	(*m_propertyAttributeLineActive)->SaveMeta(dataWritter);
	(*m_propertyAttributePeriod)->SaveMeta(dataWritter);
	(*m_propertyAttributeRecorder)->SaveMeta(dataWritter);
	(*m_propertyAttributeLineNumber)->SaveMeta(dataWritter);

	//create or update table:
	return ibValueMetaObjectGenericData::SaveData(dataWritter);
}

//***********************************************************************
//*                           read & save events                        *
//***********************************************************************

bool ibValueMetaObjectRegisterData::OnCreateMetaObject(ibMetaData* metaData, int flags)
{
	if (!ibValueMetaObject::OnCreateMetaObject(metaData, flags))
		return false;

	return (*m_propertyAttributeLineActive)->OnCreateMetaObject(metaData, flags) &&
		(*m_propertyAttributePeriod)->OnCreateMetaObject(metaData, flags) &&
		(*m_propertyAttributeRecorder)->OnCreateMetaObject(metaData, flags) &&
		(*m_propertyAttributeLineNumber)->OnCreateMetaObject(metaData, flags);
}

bool ibValueMetaObjectRegisterData::OnLoadMetaObject(ibMetaData* metaData)
{
	if (!(*m_propertyAttributeLineActive)->OnLoadMetaObject(metaData))
		return false;

	if (!(*m_propertyAttributePeriod)->OnLoadMetaObject(metaData))
		return false;

	if (!(*m_propertyAttributeRecorder)->OnLoadMetaObject(metaData))
		return false;

	if (!(*m_propertyAttributeLineNumber)->OnLoadMetaObject(metaData))
		return false;

	return ibValueMetaObject::OnLoadMetaObject(metaData);
}

bool ibValueMetaObjectRegisterData::OnSaveMetaObject(int flags)
{
	if (!(*m_propertyAttributeLineActive)->OnSaveMetaObject(flags))
		return false;

	if (!(*m_propertyAttributePeriod)->OnSaveMetaObject(flags))
		return false;

	if (!(*m_propertyAttributeRecorder)->OnSaveMetaObject(flags))
		return false;

	if (!(*m_propertyAttributeLineNumber)->OnSaveMetaObject(flags))
		return false;

	return ibValueMetaObject::OnSaveMetaObject(flags);
}

bool ibValueMetaObjectRegisterData::OnDeleteMetaObject()
{
	if (!(*m_propertyAttributeLineActive)->OnDeleteMetaObject())
		return false;

	if (!(*m_propertyAttributePeriod)->OnDeleteMetaObject())
		return false;

	if (!(*m_propertyAttributeRecorder)->OnDeleteMetaObject())
		return false;

	if (!(*m_propertyAttributeLineNumber)->OnDeleteMetaObject())
		return false;

	return ibValueMetaObject::OnDeleteMetaObject();
}

bool ibValueMetaObjectRegisterData::OnBeforeRunMetaObject(int flags)
{
	if (!(*m_propertyAttributeLineActive)->OnBeforeRunMetaObject(flags))
		return false;

	if (!(*m_propertyAttributePeriod)->OnBeforeRunMetaObject(flags))
		return false;

	if (!(*m_propertyAttributeRecorder)->OnBeforeRunMetaObject(flags))
		return false;

	if (!(*m_propertyAttributeLineNumber)->OnBeforeRunMetaObject(flags))
		return false;

	registerManager();
	registerRegList();
	registerRecordKey();
	registerRecordSet();
	registerRecordSet_String();

	registerRecordManager();

	return ibValueMetaObject::OnBeforeRunMetaObject(flags);
}

bool ibValueMetaObjectRegisterData::OnAfterCloseMetaObject()
{
	if (!(*m_propertyAttributeLineActive)->OnAfterCloseMetaObject())
		return false;

	if (!(*m_propertyAttributePeriod)->OnAfterCloseMetaObject())
		return false;

	if (!(*m_propertyAttributeRecorder)->OnAfterCloseMetaObject())
		return false;

	if (!(*m_propertyAttributeLineNumber)->OnAfterCloseMetaObject())
		return false;

	unregisterManager();
	unregisterRegList();
	unregisterRecordKey();
	unregisterRecordSet();
	unregisterRecordSet_String();

	unregisterRecordManager();

	return ibValueMetaObject::OnAfterCloseMetaObject();
}

//***********************************************************************
//*								ARRAY									*
//***********************************************************************

ibValueRecordKeyObject* ibValueMetaObjectRegisterData::CreateRecordKeyObjectValue()
{
	return ibValue::CreateAndPrepareValueRef<ibValueRecordKeyObject>(this);
}

ibValueRecordSetObject* ibValueMetaObjectRegisterData::CreateRecordSetObjectValue(bool needInitialize)
{
	ibValueRecordSetObject* createdValue = CreateRecordSetObjectRegValue();
	if (!needInitialize)
		return createdValue;
	if (createdValue && !createdValue->InitializeObject(nullptr, true)) {
		wxDELETE(createdValue);
		return nullptr;
	}
	return createdValue;
}

ibValueRecordSetObject* ibValueMetaObjectRegisterData::CreateRecordSetObjectValue(const ibUniqueKeyPair& uniqueKey, bool needInitialize)
{
	ibValueRecordSetObject* createdValue = CreateRecordSetObjectRegValue(uniqueKey);
	if (!needInitialize)
		return createdValue;
	if (createdValue && !createdValue->InitializeObject(nullptr, false)) {
		wxDELETE(createdValue);
		return nullptr;
	}
	return createdValue;
}

ibValueRecordSetObject* ibValueMetaObjectRegisterData::CreateRecordSetObjectValue(ibValueRecordSetObject* source, bool needInitialize)
{
	ibValueRecordSetObject* createdValue = CreateRecordSetObjectRegValue();
	if (!needInitialize)
		return createdValue;
	if (createdValue && !createdValue->InitializeObject(source, true)) {
		wxDELETE(createdValue);
		return nullptr;
	}
	return createdValue;
}

ibValueRecordSetObject* ibValueMetaObjectRegisterData::CopyRecordSetObjectValue(const ibUniqueKeyPair& uniqueKey)
{
	ibValueRecordSetObject* createdValue = CreateRecordSetObjectRegValue(uniqueKey);
	if (createdValue && !createdValue->InitializeObject(nullptr, true)) {
		wxDELETE(createdValue);
		return nullptr;
	}
	return createdValue;
}

ibValueRecordManagerObject* ibValueMetaObjectRegisterData::CreateRecordManagerObjectValue()
{
	ibValueRecordManagerObject* createdValue = CreateRecordManagerObjectRegValue();
	if (createdValue && !createdValue->InitializeObject(nullptr, true)) {
		wxDELETE(createdValue);
		return nullptr;
	}
	return createdValue;
}

ibValueRecordManagerObject* ibValueMetaObjectRegisterData::CreateRecordManagerObjectValue(const ibUniqueKeyPair& uniqueKey)
{
	ibValueRecordManagerObject* createdValue = CreateRecordManagerObjectRegValue(uniqueKey);
	if (createdValue && !createdValue->InitializeObject(nullptr, false)) {
		wxDELETE(createdValue);
		return nullptr;
	}
	return createdValue;
}

ibValueRecordManagerObject* ibValueMetaObjectRegisterData::CreateRecordManagerObjectValue(ibValueRecordManagerObject* source)
{
	ibValueRecordManagerObject* createdValue = CreateRecordManagerObjectRegValue();
	if (createdValue && !createdValue->InitializeObject(source, true)) {
		wxDELETE(createdValue);
		return nullptr;
	}
	return createdValue;
}

ibValueRecordManagerObject* ibValueMetaObjectRegisterData::CopyRecordManagerObjectValue(const ibUniqueKeyPair& uniqueKey)
{
	ibValueRecordManagerObject* createdValue = CreateRecordManagerObjectRegValue();
	if (createdValue && !createdValue->InitializeObject(uniqueKey)) {
		wxDELETE(createdValue);
		return nullptr;
	}
	return createdValue;
}

//***********************************************************************
//*                        ibValueManagerDataObject						*
//***********************************************************************

void ibValueManagerDataObject::PrepareNames() const
{
	const ibValueMetaObjectGenericData* valueMetaObject = GetMetaObject();
	wxASSERT(valueMetaObject);

	const ibMetaData* metaData = valueMetaObject->GetMetaData();
	wxASSERT(metaData);

	const ibValueModuleManager* moduleManager = metaData->GetModuleManager();
	wxASSERT(moduleManager);

	m_methodHelper->ClearHelper();

	ibValue* pRefData = moduleManager->FindCommonModule(GetModuleManager());
	if (pRefData != nullptr) {
		// add methods from context
		for (long idx = 0; idx < pRefData->GetNMethods(); idx++) {
			m_methodHelper->CopyMethod(pRefData->GetPMethods(), idx);
		}
	}
}

bool ibValueManagerDataObject::CallAsProc(const long lMethodNum, ibValue** paParams, const long lSizeArray)
{
	const ibValueMetaObjectGenericData* valueMetaObject = GetMetaObject();
	wxASSERT(valueMetaObject);

	const ibMetaData* metaData = valueMetaObject->GetMetaData();
	wxASSERT(metaData);

	const ibValueModuleManager* moduleManager = metaData->GetModuleManager();
	wxASSERT(moduleManager);

	ibValue* pRefData =
		moduleManager->FindCommonModule(GetModuleManager());

	if (pRefData != nullptr)
		return pRefData->CallAsProc(lMethodNum, paParams, lSizeArray);

	return false;
}

bool ibValueManagerDataObject::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	const ibValueMetaObjectGenericData* valueMetaObject = GetMetaObject();
	wxASSERT(valueMetaObject);

	const ibMetaData* metaData = valueMetaObject->GetMetaData();
	wxASSERT(metaData);

	const ibValueModuleManager* moduleManager = metaData->GetModuleManager();
	wxASSERT(moduleManager);

	ibValue* pRefData =
		moduleManager->FindCommonModule(GetModuleManager());

	if (pRefData != nullptr)
		return pRefData->CallAsFunc(lMethodNum, pvarRetValue, paParams, lSizeArray);

	return false;
}

ibClassID ibValueManagerDataObject::GetClassType() const
{
	const ibValueMetaObjectGenericData* valueMetaObject = GetMetaObject();
	wxASSERT(valueMetaObject);

	const ibCtorMetaValueType* clsFactory =
		valueMetaObject->GetTypeCtor(ibCtorObjectMetaType::ibCtorObjectMetaType_Manager);
	wxASSERT(clsFactory);
	return clsFactory->GetClassType();
}

wxString ibValueManagerDataObject::GetClassName() const
{
	const ibValueMetaObjectGenericData* valueMetaObject = GetMetaObject();
	wxASSERT(valueMetaObject);

	const ibCtorMetaValueType* clsFactory =
		valueMetaObject->GetTypeCtor(ibCtorObjectMetaType::ibCtorObjectMetaType_Manager);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

wxString ibValueManagerDataObject::GetString() const
{
	const ibValueMetaObjectGenericData* valueMetaObject = GetMetaObject();
	wxASSERT(valueMetaObject);

	const ibCtorMetaValueType* clsFactory =
		valueMetaObject->GetTypeCtor(ibCtorObjectMetaType::ibCtorObjectMetaType_Manager);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

//***********************************************************************
//*                        ibValueManagerDataObjectPredefined			*
//***********************************************************************


void ibValueManagerDataObjectPredefined::PrepareNames() const
{
	const ibValueMetaObjectRecordDataHierarchyMutableRef* valueMetaObject = GetMetaObject();
	wxASSERT(valueMetaObject);

	ibValueManagerDataObject::PrepareNames();

	//fill custom values 
	for (const auto object : valueMetaObject->GetPredefinedValueArray()) {
		m_methodHelper->AppendProp(object->GetPredefinedName(), true, false);
	}
}

bool ibValueManagerDataObjectPredefined::SetPropVal(const long lPropNum, ibValue& cValue)
{
	return false;
}

bool ibValueManagerDataObjectPredefined::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	ibValueMetaObjectRecordDataHierarchyMutableRef* valueMetaObject = GetMetaObject();
	wxASSERT(valueMetaObject);

	const auto& predefinedValue =
		valueMetaObject->FindPredefinedValue(m_methodHelper->GetPropName(lPropNum));
	if (predefinedValue == nullptr)
		return false;
	pvarPropVal = ibValueReferenceDataObject::Create(valueMetaObject, predefinedValue->GetPredefinedGuid());
	return true;
}

//***********************************************************************
//*                        ibValueRecordDataObject						*
//***********************************************************************

wxIMPLEMENT_ABSTRACT_CLASS(ibValueRecordDataObject, ibValue);

ibValueRecordDataObject::ibValueRecordDataObject(const ibGuid& objGuid, bool newObject) :
	ibValue(ibValueTypes::TYPE_VALUE), ibValueDataObject(objGuid, newObject),
	m_methodHelper(new ibValueMethodHelper())
{
}

ibValueRecordDataObject::ibValueRecordDataObject(const ibValueRecordDataObject& source) :
	ibValue(ibValueTypes::TYPE_VALUE), ibValueDataObject(wxNewUniqueGuid, true),
	m_methodHelper(new ibValueMethodHelper())
{
}

ibValueRecordDataObject::~ibValueRecordDataObject()
{
	wxDELETE(m_methodHelper);
}

ibBackendValueForm* ibValueRecordDataObject::GetForm() const
{
	if (!m_objGuid.isValid())
		return nullptr;
	return ibBackendValueForm::FindFormByUniqueKey(m_objGuid);
}

ibClassID ibValueRecordDataObject::GetClassType() const
{
	ibValueMetaObjectRecordData* metaObject = GetMetaObject();
	wxASSERT(metaObject);
	const ibCtorMetaValueType* clsFactory =
		metaObject->GetTypeCtor(ibCtorObjectMetaType::ibCtorObjectMetaType_Object);
	wxASSERT(clsFactory);
	return clsFactory->GetClassType();
}

wxString ibValueRecordDataObject::GetClassName() const
{
	ibValueMetaObjectRecordData* metaObject = GetMetaObject();
	wxASSERT(metaObject);
	const ibCtorMetaValueType* clsFactory =
		metaObject->GetTypeCtor(ibCtorObjectMetaType::ibCtorObjectMetaType_Object);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

wxString ibValueRecordDataObject::GetString() const
{
	ibValueMetaObjectRecordData* metaObject = GetMetaObject();
	wxASSERT(metaObject);
	const ibCtorMetaValueType* clsFactory =
		metaObject->GetTypeCtor(ibCtorObjectMetaType::ibCtorObjectMetaType_Object);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

ibSourceExplorer ibValueRecordDataObject::GetSourceExplorer() const
{
	ibValueMetaObjectRecordData* metaObject = GetMetaObject();

	ibSourceExplorer srcHelper(
		metaObject, GetClassType(),
		false
	);

	for (const auto object : metaObject->GetGenericAttributeArrayObject()) {
		srcHelper.AppendSource(object);
	}

	for (const auto object : metaObject->GetGenericTableArrayObject()) {
		srcHelper.AppendSource(object);
	}

	return srcHelper;
}

#include "backend/metaCollection/partial/tabularSection/tabularSection.h"

bool ibValueRecordDataObject::GetModel(ibValueModel*& tableValue, const ibMetaID& id)
{
	auto it = m_listObjectValue.find(id);
	if (it != m_listObjectValue.end()) {
		const ibValue& cTabularSection = it->second;
		return cTabularSection.ConvertToValue(tableValue);
	};

	tableValue = nullptr;
	return false;
}

bool ibValueRecordDataObject::SetValueByMetaID(const ibMetaID& id, const ibValue& varMetaVal)
{
	auto it = m_listObjectValue.find(id);
	wxASSERT(it != m_listObjectValue.end());
	if (it != m_listObjectValue.end()) {

		ibValueMetaObjectRecordData* metaObjectValue = GetMetaObject();
		wxASSERT(metaObjectValue);

		const ibValueMetaObjectAttributeBase* attribute = metaObjectValue->FindAnyAttributeObjectByFilter(id);
		wxASSERT(attribute);
		it->second = attribute->AdjustValue(varMetaVal);
		return true;
	}
	return false;
}

bool ibValueRecordDataObject::GetValueByMetaID(const ibMetaID& id, ibValue& pvarMetaVal) const
{
	auto it = m_listObjectValue.find(id);
	wxASSERT(it != m_listObjectValue.end());
	if (it != m_listObjectValue.end()) {
		pvarMetaVal = it->second;
		return true;
	}
	return false;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////

ibValueModelTableBase* ibValueRecordDataObject::GetTableByMetaID(const ibMetaID& id) const
{
	const ibValue& cTable = GetValueByMetaID(id); ibValueModelTableBase* retTable = nullptr;
	if (cTable.ConvertToValue(retTable))
		return retTable;
	return nullptr;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////

#define thisObject wxT("ThisObject")

void ibValueRecordDataObject::PrepareEmptyObject()
{
	ibValueMetaObjectRecordData* metaObject = GetMetaObject();
	wxASSERT(metaObject);

	m_listObjectValue.clear();

	//attrbutes can refValue 
	for (const auto object : metaObject->GetGenericAttributeArrayObject()) {
		if (object->IsDeleted())
			continue;
		m_listObjectValue.insert_or_assign(object->GetMetaID(), object->CreateValue());
	}

	// table is collection values 
	for (const auto object : metaObject->GetGenericTableArrayObject()) {
		if (object->IsDeleted())
			continue;
		m_listObjectValue.insert_or_assign(object->GetMetaID(), ibValue::CreateAndPrepareValueRef<ibValueTabularSectionDataObject>(this, object));
	}
}

/////////////////////////////////////////////////////////////////////////////////////////////////////

void ibValueRecordDataObject::PrepareNames() const
{
	m_methodHelper->ClearHelper();

	m_methodHelper->AppendFunc(wxT("GetFormObject"), 2, wxT("GetFormObject(name : string, owner : any)"));
	m_methodHelper->AppendFunc(wxT("GetTemplate"), 1, wxT("GetTemplate(name : string)"));
	m_methodHelper->AppendFunc(wxT("GetMetadata"), wxT("GetMetadata()"));

	m_methodHelper->AppendProp(thisObject,
		true, false, eThisObject, eSystem
	);

	ibValueMetaObjectRecordData* metaObject = GetMetaObject();
	wxASSERT(metaObject);

	if (metaObject != nullptr) {

		wxString objectName;

		//fill custom attributes 
		for (const auto object : metaObject->GetGenericAttributeArrayObject()) {
			if (object->IsDeleted())
				continue;
			if (!object->GetObjectNameAsString(objectName))
				continue;
			m_methodHelper->AppendProp(
				objectName,
				object->GetMetaID(),
				eProperty
			);
		}

		//fill custom tables 
		for (const auto object : metaObject->GetGenericTableArrayObject()) {
			if (object->IsDeleted())
				continue;
			if (!object->GetObjectNameAsString(objectName))
				continue;
			m_methodHelper->AppendProp(
				objectName,
				true,
				false,
				object->GetMetaID(),
				eTable
			);
		}
	}

	if (m_procUnit != nullptr) {
		ibByteCode* byteCode = m_procUnit->GetByteCode();
		if (byteCode != nullptr) {
			for (auto exportFunction : byteCode->m_listExportFunc) {
				m_methodHelper->AppendMethod(
					exportFunction.first,
					byteCode->GetNParams(exportFunction.second),
					byteCode->HasRetVal(exportFunction.second),
					exportFunction.second,
					eProcUnit
				);
			}
			for (auto exportVariable : byteCode->m_listExportVar) {
				m_methodHelper->AppendProp(
					exportVariable.first,
					exportVariable.second,
					eProcUnit
				);
			}
		}
	}
}

bool ibValueRecordDataObject::SetPropVal(const long lPropNum, const ibValue& varPropVal)
{
	const long lPropAlias = m_methodHelper->GetPropAlias(lPropNum);
	if (lPropAlias == eProcUnit) {
		if (m_procUnit != nullptr) {
			return m_procUnit->SetPropVal(
				GetPropName(lPropNum), varPropVal
			);
		}
	}
	else if (lPropAlias == eProperty) {
		return SetValueByMetaID(
			m_methodHelper->GetPropData(lPropNum),
			varPropVal
		);
	}
	return false;
}

bool ibValueRecordDataObject::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	const long lPropAlias = m_methodHelper->GetPropAlias(lPropNum);
	if (lPropAlias == eProcUnit) {
		if (m_procUnit != nullptr) {
			return m_procUnit->GetPropVal(
				GetPropName(lPropNum), pvarPropVal
			);
		}
	}
	else if (lPropAlias == eProperty || lPropAlias == eTable) {
		return GetValueByMetaID(
			m_methodHelper->GetPropData(lPropNum), pvarPropVal
		);
	}
	else if (lPropAlias == eSystem) {
		switch (m_methodHelper->GetPropData(lPropNum))
		{
		case eThisObject:
			pvarPropVal = GetValue();
			return true;
		}
	}
	return false;
}

bool ibValueRecordDataObject::CallAsProc(const long lMethodNum, ibValue** paParams, const long lSizeArray)
{
	const long lMethodAlias = m_methodHelper->GetPropAlias(lMethodNum);
	if (lMethodAlias == eProcUnit) {
		return ibModuleDataObject::ExecuteProc(
			GetMethodName(lMethodNum), paParams, lSizeArray
		);
	}

	return false;
}

bool ibValueRecordDataObject::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	const long lMethodAlias = m_methodHelper->GetPropAlias(lMethodNum);
	if (lMethodAlias == eProcUnit) {
		return ibModuleDataObject::ExecuteFunc(
			GetMethodName(lMethodNum), pvarRetValue, paParams, lSizeArray
		);
	}

	switch (lMethodNum)
	{
	case eGetFormObject:
		pvarRetValue = GetFormValue(
			lSizeArray > 0 ? paParams[0]->GetString() : wxString(wxEmptyString),
			lSizeArray > 1 ? paParams[1]->ConvertToType<ibBackendControlFrame>() : nullptr
		);
		return true;
	case enGetTemplate:
		pvarRetValue = GetMetaObject()->GetTemplate(paParams[0]->GetString());
		return true;
	case eGetMetadata:
		pvarRetValue = GetMetaObject();
		return true;
	}

	return false;
}

//***********************************************************************
//*                        ibValueRecordDataObjectExt							*           
//***********************************************************************

wxIMPLEMENT_ABSTRACT_CLASS(ibValueRecordDataObjectExt, ibValueRecordDataObject);

ibValueRecordDataObjectExt::ibValueRecordDataObjectExt(ibValueMetaObjectRecordDataExt* metaObject) :
	ibValueRecordDataObject(wxNewUniqueGuid, true), m_metaObject(metaObject)
{
}

ibValueRecordDataObjectExt::ibValueRecordDataObjectExt(const ibValueRecordDataObjectExt& source) :
	ibValueRecordDataObject(source), m_metaObject(source.m_metaObject)
{
}

ibValueRecordDataObjectExt::~ibValueRecordDataObjectExt()
{
	if (m_metaObject->IsExternalCreate()) {
		if (!appData->DesignerMode()) {
			ibMetaData* metaData = m_metaObject->GetMetaData();
			if (!metaData->CloseDatabase(forceCloseFlag)) {
				wxASSERT_MSG(false, "m_moduleManager->CloseDatabase() == false");
			}
			wxDELETE(metaData);
		}
	}
}

bool ibValueRecordDataObjectExt::InitializeObject()
{
	if (!m_metaObject->IsExternalCreate()) {

		if (!m_metaObject->AccessRight_Use()) {
			ibBackendAccessException::Error();
			return false;
		}

		const ibMetaData* metaData = m_metaObject->GetMetaData();
		wxASSERT(metaData);
		const ibValueModuleManager* moduleManager = metaData->GetModuleManager();
		wxASSERT(moduleManager);

		if (!m_compileModule) {
			m_compileModule = new ibCompileModule(m_metaObject->GetModuleObject());
			m_compileModule->SetParent(moduleManager->GetCompileModule());
			m_compileModule->AddContextVariable(thisObject, this);
		}

		if (!appData->DesignerMode()) {
			try {
				m_compileModule->Compile();
			}
			catch (const ibBackendException* err) {
				if (!appData->DesignerMode())
					throw(err);
				return false;
			};

			m_procUnit = new ibProcUnit();
			m_procUnit->SetParent(moduleManager->GetProcUnit());
		}
	}

	PrepareEmptyObject();

	if (!m_metaObject->IsExternalCreate()) {
		if (!appData->DesignerMode()) {
			m_procUnit->Execute(m_compileModule->m_cByteCode);
		}
	}

	PrepareNames();

	//is Ok
	return true;
}

bool ibValueRecordDataObjectExt::InitializeObject(ibValueRecordDataObjectExt* source)
{
	//if (m_metaObject->AccessRight("use", appData->GetUserName()))
	//{

	//}

	if (!m_metaObject->IsExternalCreate()) {
		const ibMetaData* metaData = m_metaObject->GetMetaData();
		wxASSERT(metaData);
		const ibValueModuleManager* moduleManager = metaData->GetModuleManager();
		wxASSERT(moduleManager);

		if (m_compileModule == nullptr) {
			m_compileModule = new ibCompileModule(m_metaObject->GetModuleObject());
			m_compileModule->SetParent(moduleManager->GetCompileModule());
			m_compileModule->AddContextVariable(thisObject, this);
		}

		if (!appData->DesignerMode()) {
			try {
				m_compileModule->Compile();
			}
			catch (const ibBackendException* err) {
				if (!appData->DesignerMode())
					throw(err);
				return false;
			};

			m_procUnit = new ibProcUnit();
			m_procUnit->SetParent(moduleManager->GetProcUnit());
		}
	}

	PrepareEmptyObject();

	if (!m_metaObject->IsExternalCreate()) {
		if (!appData->DesignerMode()) {
			m_procUnit->Execute(m_compileModule->m_cByteCode);
		}
	}

	PrepareNames();

	//is Ok
	return true;
}

ibValueRecordDataObjectExt* ibValueRecordDataObjectExt::CopyObjectValue()
{
	return m_metaObject->CreateObjectValue(this);
}

//***********************************************************************
//*                        ibValueRecordDataObjectRef							*           
//***********************************************************************

wxIMPLEMENT_ABSTRACT_CLASS(ibValueRecordDataObjectRef, ibValueRecordDataObject);

ibValueRecordDataObjectRef::ibValueRecordDataObjectRef(ibValueMetaObjectRecordDataMutableRef* metaObject, const ibGuid& objGuid) :
	ibValueRecordDataObject(objGuid.isValid() ? objGuid : ibGuid::newGuid(GUID_TIME_BASED), !objGuid.isValid()),
	m_metaObject(metaObject),
	m_reference_impl(nullptr),
	m_objModified(false)
{
	if (m_metaObject != nullptr)
		m_reference_impl = new ibReference(m_metaObject->GetMetaID(), m_objGuid);
}

ibValueRecordDataObjectRef::ibValueRecordDataObjectRef(const ibValueRecordDataObjectRef& src) :
	ibValueRecordDataObject(src),
	m_metaObject(src.m_metaObject),
	m_reference_impl(nullptr),
	m_objModified(false)
{
	if (m_metaObject != nullptr)
		m_reference_impl = new ibReference(m_metaObject->GetMetaID(), m_objGuid);
}

ibValueRecordDataObjectRef::~ibValueRecordDataObjectRef()
{
	wxDELETE(m_reference_impl);
}

bool ibValueRecordDataObjectRef::InitializeObject(const ibGuid& copyGuid)
{
	if (!m_metaObject->AccessRight_Read()) {
		ibBackendAccessException::Error();
		return false;
	}

	ibMetaData* metaData = m_metaObject->GetMetaData();
	wxASSERT(metaData);
	ibValueModuleManager* moduleManager = metaData->GetModuleManager();
	wxASSERT(moduleManager);
	if (m_compileModule == nullptr) {
		m_compileModule = new ibCompileModule(m_metaObject->GetModuleObject());
		m_compileModule->SetParent(moduleManager->GetCompileModule());
		m_compileModule->AddContextVariable(thisObject, this);
	}
	if (!appData->DesignerMode()) {
		try {
			m_compileModule->Compile();
		}
		catch (const ibBackendException* err) {
			if (!appData->DesignerMode())
				throw(err);
			return false;
		};
	}
	bool succes = true;
	if (!appData->DesignerMode()) {
		if (m_newObject && !copyGuid.isValid()) {
			PrepareEmptyObject();
		}
		else if (m_newObject && copyGuid.isValid()) {
			succes = ReadData(copyGuid);
			if (succes) {
				ibValueMetaObjectAttributeBase* codeAttribute = m_metaObject->GetAttributeForCode();
				wxASSERT(codeAttribute);
				m_listObjectValue[codeAttribute->GetMetaID()] = codeAttribute->CreateValue();
			}
			m_objModified = true;
		}
		else {
			if (!ReadData()) PrepareEmptyObject();
		}
		if (!succes) return succes;
	}
	else {
		PrepareEmptyObject();
	}
	if (!appData->DesignerMode()) {
		wxASSERT(m_procUnit == nullptr);
		m_procUnit = new ibProcUnit();
		m_procUnit->SetParent(moduleManager->GetProcUnit());
		m_procUnit->Execute(m_compileModule->m_cByteCode);
		if (m_newObject) {
			succes = Filling();
		}
	}

	PrepareNames();

	//is Ok
	return succes;
}

bool ibValueRecordDataObjectRef::InitializeObject(ibValueRecordDataObjectRef* source, bool generate)
{
	const ibMetaData* metaData = m_metaObject->GetMetaData();
	wxASSERT(metaData);
	const ibValueModuleManager* moduleManager = metaData->GetModuleManager();
	wxASSERT(moduleManager);
	if (m_compileModule == nullptr) {
		m_compileModule = new ibCompileModule(m_metaObject->GetModuleObject());
		m_compileModule->SetParent(moduleManager->GetCompileModule());
		m_compileModule->AddContextVariable(thisObject, this);
	}
	if (!appData->DesignerMode()) {
		try {
			m_compileModule->Compile();
		}
		catch (const ibBackendException* err) {
			if (!appData->DesignerMode())
				throw(err);
			return false;
		};
	}

	if (!generate && source != nullptr)
		PrepareEmptyObject(source);
	else
		PrepareEmptyObject();

	bool succes = true;
	if (!appData->DesignerMode()) {
		wxASSERT(m_procUnit == nullptr);
		m_procUnit = new ibProcUnit();
		m_procUnit->SetParent(moduleManager->GetProcUnit());
		m_procUnit->Execute(m_compileModule->m_cByteCode);
		if (m_newObject && source != nullptr && !generate) {
			m_procUnit->CallAsProc(wxT("OnCopy"), source->GetValue());
		}
		else if (m_newObject && source == nullptr) {
			succes = Filling();
		}
		else if (generate) {
			ibValuePtr<ibValueReferenceDataObject> refPtr(
				source != nullptr ? source->GetReference() : nullptr);
			succes = Filling(refPtr);
		}
	}

	PrepareNames();

	//is Ok
	return succes;
}

ibClassID ibValueRecordDataObjectRef::GetClassType() const
{
	return ibValueRecordDataObject::GetClassType();
}

wxString ibValueRecordDataObjectRef::GetClassName() const
{
	return ibValueRecordDataObject::GetClassName();
}

wxString ibValueRecordDataObjectRef::GetString() const
{
	return m_metaObject->GetDataPresentation(this);
}

ibSourceExplorer ibValueRecordDataObjectRef::GetSourceExplorer() const
{
	ibSourceExplorer srcHelper(
		m_metaObject, GetClassType(),
		false
	);

	ibValueMetaObjectAttributeBase* attribute = m_metaObject->GetAttributeForCode();

	for (const auto object : m_metaObject->GetGenericAttributeArrayObject()) {
		if (!m_metaObject->IsDataReference(object->GetMetaID())) {
			srcHelper.AppendSource(object, object != attribute);
		}
	}

	for (const auto object : m_metaObject->GetGenericTableArrayObject()) {
		srcHelper.AppendSource(object);
	}

	return srcHelper;
}

bool ibValueRecordDataObjectRef::GetModel(ibValueModel*& tableValue, const ibMetaID& id)
{
	auto it = m_listObjectValue.find(id);
	if (it != m_listObjectValue.end()) {
		const ibValue& cTabularSection = it->second;
		return cTabularSection.ConvertToValue(tableValue);
	};
	tableValue = nullptr;
	return false;
}

void ibValueRecordDataObjectRef::Modify(bool mod)
{
	ibBackendValueForm* const foundedForm = ibBackendValueForm::FindFormByUniqueKey(m_objGuid);

	if (foundedForm != nullptr)
		foundedForm->Modify(mod);

	m_objModified = mod;
}

bool ibValueRecordDataObjectRef::Generate()
{
	if (m_newObject)
		return false;

	ibBackendValueForm* const foundedForm = ibBackendValueForm::FindFormByUniqueKey(m_objGuid);
	if (foundedForm != nullptr)
		return foundedForm->GenerateForm(this);

	return false;
}

bool ibValueRecordDataObjectRef::Filling(ibValue cValue) const
{
	ibValue standartProcessing = true;
	if (m_procUnit != nullptr) {
		m_procUnit->CallAsProc(wxT("Filling"), cValue, standartProcessing);
	}
	return standartProcessing.GetBoolean();
}

bool ibValueRecordDataObjectRef::SetValueByMetaID(const ibMetaID& id, const ibValue& varMetaVal)
{
	if (varMetaVal != ibValueRecordDataObject::GetValueByMetaID(id)) {
		if (ibValueRecordDataObject::SetValueByMetaID(id, varMetaVal)) {
			ibValueRecordDataObjectRef::Modify(true);
			return true;
		}
		return false;
	}
	return true;
}

bool ibValueRecordDataObjectRef::GetValueByMetaID(const ibMetaID& id, ibValue& pvarMetaVal) const
{
	return ibValueRecordDataObject::GetValueByMetaID(id, pvarMetaVal);
}

ibValueRecordDataObjectRef* ibValueRecordDataObjectRef::CopyObjectValue()
{
	return m_metaObject->CreateObjectValue(this);
}

void ibValueRecordDataObjectRef::PrepareEmptyObject()
{
	m_listObjectValue.clear();
	//attrbutes can refValue 
	for (const auto object : m_metaObject->GetGenericAttributeArrayObject()) {
		if (object->IsDeleted())
			continue;
		if (!m_metaObject->IsDataReference(object->GetMetaID())) {
			m_listObjectValue.insert_or_assign(object->GetMetaID(), object->CreateValue());
		}
	}
	// table is collection values 
	for (const auto object : m_metaObject->GetGenericTableArrayObject()) {
		if (object->IsDeleted())
			continue;
		m_listObjectValue.insert_or_assign(object->GetMetaID(), ibValue::CreateAndPrepareValueRef<ibValueTabularSectionDataObjectRef>(this, object));
	}
	m_objModified = true;
}

void ibValueRecordDataObjectRef::PrepareEmptyObject(const ibValueRecordDataObjectRef* source)
{
	m_listObjectValue.clear();

	ibValueMetaObjectAttributeBase* codeAttribute = m_metaObject->GetAttributeForCode();
	wxASSERT(codeAttribute);

	//attributes can refValue 
	for (const auto object : m_metaObject->GetGenericAttributeArrayObject()) {
		if (object->IsDeleted())
			continue;
		if (object != codeAttribute && !m_metaObject->IsDataReference(object->GetMetaID())) {
			source->GetValueByMetaID(object->GetMetaID(), m_listObjectValue[object->GetMetaID()]);
		}
	}

	m_listObjectValue[codeAttribute->GetMetaID()] = codeAttribute->CreateValue();

	// table is collection values 
	for (const auto object : m_metaObject->GetGenericTableArrayObject()) {
		if (object->IsDeleted())
			continue;
		ibValueTabularSectionDataObjectRef* tableSection = ibValue::CreateAndPrepareValueRef<ibValueTabularSectionDataObjectRef>(this, object);
		if (tableSection->LoadDataFromTable(source->GetTableByMetaID(object->GetMetaID())))
			m_listObjectValue.insert_or_assign(object->GetMetaID(), tableSection);
		else
			wxDELETE(tableSection);
	}
	m_objModified = true;
}

ibValueReferenceDataObject* ibValueRecordDataObjectRef::GetReference() const
{
	if (m_newObject) {
		return ibValueReferenceDataObject::Create(m_metaObject);
	}

	return ibValueReferenceDataObject::Create(m_metaObject, m_objGuid);
}

//***********************************************************************
//*                        ibValueRecordDataObjectHierarchyRef					*           
//***********************************************************************

wxIMPLEMENT_ABSTRACT_CLASS(ibValueRecordDataObjectHierarchyRef, ibValueRecordDataObjectRef);

ibValueRecordDataObjectHierarchyRef::ibValueRecordDataObjectHierarchyRef(ibValueMetaObjectRecordDataHierarchyMutableRef* metaObject, const ibGuid& objGuid, ibObjectMode objMode)
	: ibValueRecordDataObjectRef(metaObject, objGuid), m_objMode(objMode)
{
}

ibValueRecordDataObjectHierarchyRef::ibValueRecordDataObjectHierarchyRef(const ibValueRecordDataObjectHierarchyRef& source)
	: ibValueRecordDataObjectRef(source), m_objMode(source.m_objMode)
{
}

ibValueRecordDataObjectHierarchyRef::~ibValueRecordDataObjectHierarchyRef()
{
}

ibSourceExplorer ibValueRecordDataObjectHierarchyRef::GetSourceExplorer() const
{
	ibSourceExplorer srcHelper(
		m_metaObject, GetClassType(),
		false
	);
	ibValueMetaObjectAttributeBase* attribute = m_metaObject->GetAttributeForCode();
	for (const auto object : m_metaObject->GetGenericAttributeArrayObject()) {
		ibItemMode attrUse = object->GetItemMode();
		if (m_objMode == ibObjectMode::OBJECT_ITEM) {
			if (attrUse == ibItemMode::ibItemMode_Item
				|| attrUse == ibItemMode::ibItemMode_Folder_Item) {
				if (!m_metaObject->IsDataReference(object->GetMetaID())) {
					srcHelper.AppendSource(object, object != attribute);
				}
			}
		}
		else {
			if (attrUse == ibItemMode::ibItemMode_Folder ||
				attrUse == ibItemMode::ibItemMode_Folder_Item) {
				if (!m_metaObject->IsDataReference(object->GetMetaID())) {
					srcHelper.AppendSource(object, object != attribute);
				}
			}
		}
	}

	for (const auto object : m_metaObject->GetGenericTableArrayObject()) {
		ibItemMode tableUse = object->GetTableUse();
		if (m_objMode == ibObjectMode::OBJECT_ITEM) {
			if (tableUse == ibItemMode::ibItemMode_Item
				|| tableUse == ibItemMode::ibItemMode_Folder_Item) {
				srcHelper.AppendSource(object);
			}
		}
		else {
			if (tableUse == ibItemMode::ibItemMode_Folder ||
				tableUse == ibItemMode::ibItemMode_Folder_Item) {
				srcHelper.AppendSource(object);
			}
		}
	}

	return srcHelper;
}

bool ibValueRecordDataObjectHierarchyRef::GetModel(ibValueModel*& tableValue, const ibMetaID& id)
{
	auto it = m_listObjectValue.find(id);
	if (it != m_listObjectValue.end()) {
		const ibValue& cTabularSection = it->second;
		return cTabularSection.ConvertToValue(tableValue);
	};

	tableValue = nullptr;
	return false;
}

ibValueRecordDataObjectRef* ibValueRecordDataObjectHierarchyRef::CopyObjectValue()
{
	return GetMetaObject()->CreateObjectValue(m_objMode, this);
}

bool ibValueRecordDataObjectHierarchyRef::SetValueByMetaID(const ibMetaID& id, const ibValue& varMetaVal)
{
	const ibValue& cOldValue = ibValueRecordDataObjectRef::GetValueByMetaID(id);
	if (cOldValue.GetType() == TYPE_NULL)
		return false;

	const ibValueMetaObjectRecordDataHierarchyMutableRef* valueMetaObject = GetMetaObject();
	wxASSERT(valueMetaObject);

	if (valueMetaObject->IsDataParent(id) && varMetaVal == GetReference() && !varMetaVal.IsEmpty()) {
		ibBackendCoreException::Error(_("You can't change your parent to yourself!"));
		return false;
	}

	if (valueMetaObject->IsDataPredefinedName(id)) {
		ibBackendCoreException::Error(_("You cannot change predefined value!"));
		return false;
	}

	return ibValueRecordDataObjectRef::SetValueByMetaID(id, varMetaVal);
}

bool ibValueRecordDataObjectHierarchyRef::GetValueByMetaID(const ibMetaID& id, ibValue& pvarMetaVal) const
{
	return ibValueRecordDataObjectRef::GetValueByMetaID(id, pvarMetaVal);
}

void ibValueRecordDataObjectHierarchyRef::PrepareEmptyObject()
{
	m_listObjectValue.clear();
	//attrbutes can refValue 
	for (const auto object : m_metaObject->GetGenericAttributeArrayObject()) {
		if (object->IsDeleted())
			continue;
		ibItemMode attrUse = object->GetItemMode();
		if (m_objMode == ibObjectMode::OBJECT_ITEM) {
			if (attrUse == ibItemMode::ibItemMode_Item ||
				attrUse == ibItemMode::ibItemMode_Folder_Item) {
				m_listObjectValue.insert_or_assign(object->GetMetaID(), object->CreateValue());
			}
			else {
				m_listObjectValue.insert_or_assign(object->GetMetaID(), ibValueTypes::TYPE_NULL);
			}
		}
		else {
			if (attrUse == ibItemMode::ibItemMode_Folder ||
				attrUse == ibItemMode::ibItemMode_Folder_Item) {
				m_listObjectValue.insert_or_assign(object->GetMetaID(), object->CreateValue());
			}
			else {
				m_listObjectValue.insert_or_assign(object->GetMetaID(), ibValueTypes::TYPE_NULL);
			}
		}
	}
	ibValueMetaObjectRecordDataHierarchyMutableRef* metaFolder = GetMetaObject();
	wxASSERT(metaFolder);
	if (m_objMode == ibObjectMode::OBJECT_ITEM) {
		m_listObjectValue.insert_or_assign(*metaFolder->GetDataIsFolder(), false);
	}
	else if (m_objMode == ibObjectMode::OBJECT_FOLDER) {
		m_listObjectValue.insert_or_assign(*metaFolder->GetDataIsFolder(), true);
	}
	// table is collection values 
	for (const auto object : m_metaObject->GetGenericTableArrayObject()) {
		if (object->IsDeleted())
			continue;
		ibItemMode tableUse = object->GetTableUse();
		if (m_objMode == ibObjectMode::OBJECT_ITEM) {
			if (tableUse == ibItemMode::ibItemMode_Item ||
				tableUse == ibItemMode::ibItemMode_Folder_Item) {
				m_listObjectValue.insert_or_assign(object->GetMetaID(), ibValue::CreateAndPrepareValueRef<ibValueTabularSectionDataObjectRef>(this, object));
			}
			else {
				m_listObjectValue.insert_or_assign(object->GetMetaID(), ibValueTypes::TYPE_NULL);
			}
		}
		else {
			if (tableUse == ibItemMode::ibItemMode_Folder ||
				tableUse == ibItemMode::ibItemMode_Folder_Item) {
				m_listObjectValue.insert_or_assign(object->GetMetaID(), ibValue::CreateAndPrepareValueRef<ibValueTabularSectionDataObjectRef>(this, object));
			}
			else {
				m_listObjectValue.insert_or_assign(object->GetMetaID(), ibValueTypes::TYPE_NULL);
			}
		}
	}
	m_objModified = true;
}

void ibValueRecordDataObjectHierarchyRef::PrepareEmptyObject(const ibValueRecordDataObjectRef* source)
{
	m_listObjectValue.clear();
	ibValueMetaObjectAttributeBase* codeAttribute = m_metaObject->GetAttributeForCode();
	wxASSERT(codeAttribute);
	m_listObjectValue[codeAttribute->GetMetaID()] = codeAttribute->CreateValue();
	//attributes can refValue 
	for (const auto object : m_metaObject->GetGenericAttributeArrayObject()) {
		if (object->IsDeleted())
			continue;
		ibValueMetaObjectAttribute* metaAttr = nullptr; ibItemMode attrUse = ibItemMode::ibItemMode_Folder_Item;
		if (object->ConvertToValue(metaAttr)) {
			attrUse = metaAttr->GetItemMode();
		}
		if (object != codeAttribute && !m_metaObject->IsDataReference(object->GetMetaID())) {
			source->GetValueByMetaID(object->GetMetaID(), m_listObjectValue[object->GetMetaID()]);
		}
	}
	ibValueMetaObjectRecordDataHierarchyMutableRef* metaFolder = GetMetaObject();
	wxASSERT(metaFolder);
	if (m_objMode == ibObjectMode::OBJECT_ITEM) {
		m_listObjectValue.insert_or_assign(*metaFolder->GetDataIsFolder(), false);
	}
	else if (m_objMode == ibObjectMode::OBJECT_FOLDER) {
		m_listObjectValue.insert_or_assign(*metaFolder->GetDataIsFolder(), true);
	}
	// table is collection values 
	for (const auto object : m_metaObject->GetGenericTableArrayObject()) {
		if (object->IsDeleted())
			continue;
		ibValueMetaObjectTableData* metaTable = nullptr; ibItemMode tableUse = ibItemMode::ibItemMode_Folder_Item;
		if (object->ConvertToValue(metaTable))
			tableUse = metaTable->GetTableUse();
		ibValueTabularSectionDataObjectRef* tableSection = ibValue::CreateAndPrepareValueRef <ibValueTabularSectionDataObjectRef>(this, object);
		if (tableSection->LoadDataFromTable(source->GetTableByMetaID(object->GetMetaID())))
			m_listObjectValue.insert_or_assign(object->GetMetaID(), tableSection);
		else
			wxDELETE(tableSection);
	}
	m_objModified = true;
}

//***********************************************************************
//*						     metaData									* 
//***********************************************************************

wxIMPLEMENT_ABSTRACT_CLASS(ibValueRecordSetObject, ibValueModelTableBase);
wxIMPLEMENT_ABSTRACT_CLASS(ibValueRecordManagerObject, ibValue);

wxIMPLEMENT_ABSTRACT_CLASS(ibValueRecordKeyObject, ibValue);

wxIMPLEMENT_DYNAMIC_CLASS(ibValueRecordSetObject::ibValueRecordSetObjectRegisterKeyValue, ibValue);
wxIMPLEMENT_DYNAMIC_CLASS(ibValueRecordSetObject::ibValueRecordSetObjectRegisterKeyValue::ibValueRecordSetObjectRegisterKeyDescriptionValue, ibValue);

//***********************************************************************
//*                      Record key & set								*
//***********************************************************************

//////////////////////////////////////////////////////////////////////
//						  ibValueRecordKeyObject							//
//////////////////////////////////////////////////////////////////////

ibValueRecordKeyObject::ibValueRecordKeyObject(ibValueMetaObjectRegisterData* metaObject) : ibValue(ibValueTypes::TYPE_VALUE),
m_metaObject(metaObject), m_methodHelper(new ibValueMethodHelper())
{
}

ibValueRecordKeyObject::~ibValueRecordKeyObject()
{
	wxDELETE(m_methodHelper);
}

bool ibValueRecordKeyObject::IsEmpty() const
{
	for (auto value : m_keyValues) {
		const ibValue& cValue = value.second;
		if (!cValue.IsEmpty())
			return false;
	}

	return true;
}

ibClassID ibValueRecordKeyObject::GetClassType() const
{
	const ibCtorMetaValueType* clsFactory =
		m_metaObject->GetTypeCtor(ibCtorObjectMetaType::ibCtorObjectMetaType_RecordKey);
	wxASSERT(clsFactory);
	return clsFactory->GetClassType();
}

wxString ibValueRecordKeyObject::GetClassName() const
{
	const ibCtorMetaValueType* clsFactory =
		m_metaObject->GetTypeCtor(ibCtorObjectMetaType::ibCtorObjectMetaType_RecordKey);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

wxString ibValueRecordKeyObject::GetString() const
{
	const ibCtorMetaValueType* clsFactory =
		m_metaObject->GetTypeCtor(ibCtorObjectMetaType::ibCtorObjectMetaType_RecordKey);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

//////////////////////////////////////////////////////////////////////
//						  ibValueRecordManagerObject						//
//////////////////////////////////////////////////////////////////////

void ibValueRecordManagerObject::CreateEmptyKey()
{
	m_recordSet->CreateEmptyKey();
}

bool ibValueRecordManagerObject::InitializeObject(const ibValueRecordManagerObject* source, bool newRecord)
{
	if (!m_recordSet->InitializeObject(source ? source->GetRecordSet() : nullptr, newRecord))
		return false;

	if (!appData->DesignerMode()) {
		if (!newRecord && !ReadData(m_objGuid)) {
			PrepareEmptyObject(source);
		}
		else if (newRecord) {
			PrepareEmptyObject(source);
		}
	}
	else {
		PrepareEmptyObject(source);
	}

	PrepareNames();

	//is Ok
	return true;
}

bool ibValueRecordManagerObject::InitializeObject(const ibUniqueKeyPair& key)
{
	if (!m_recordSet->InitializeObject(nullptr, true))
		return false;

	if (!appData->DesignerMode()) {
		if (ReadData(key)) {
			m_recordSet->m_selected = false; // is new 
			m_recordSet->Modify(true); // and modify
		}
		else {
			PrepareEmptyObject(nullptr);
		}
	}
	else {
		PrepareEmptyObject(nullptr);
	}

	PrepareNames();

	//is Ok
	return true;
}

ibValueRecordManagerObject* ibValueRecordManagerObject::CopyRegisterValue()
{
	return m_metaObject->CreateRecordManagerObjectValue(this);
}

ibValueRecordManagerObject::ibValueRecordManagerObject(ibValueMetaObjectRegisterData* metaObject, const ibUniqueKeyPair& uniqueKey) : ibValue(ibValueTypes::TYPE_VALUE),
m_metaObject(metaObject), m_methodHelper(new ibValueMethodHelper()),
m_recordSet(m_metaObject->CreateRecordSetObjectValue(uniqueKey, false)), m_recordLine(nullptr),
m_objGuid(uniqueKey)
{
}

ibValueRecordManagerObject::ibValueRecordManagerObject(const ibValueRecordManagerObject& source) : ibValue(ibValueTypes::TYPE_VALUE),
m_metaObject(source.m_metaObject), m_methodHelper(new ibValueMethodHelper()),
m_recordSet(m_metaObject->CreateRecordSetObjectValue(source.m_recordSet, false)), m_recordLine(nullptr),
m_objGuid(source.m_metaObject)
{
}

ibValueRecordManagerObject::~ibValueRecordManagerObject()
{
	wxDELETE(m_methodHelper);
}

ibBackendValueForm* ibValueRecordManagerObject::GetForm() const
{
	if (!m_objGuid.isValid())
		return nullptr;
	if (m_recordSet->m_selected)
		return ibBackendValueForm::FindFormByUniqueKey(m_objGuid);
	return nullptr;
}

bool ibValueRecordManagerObject::IsEmpty() const
{
	return m_recordSet->IsEmpty();
}

ibSourceExplorer ibValueRecordManagerObject::GetSourceExplorer() const
{
	ibSourceExplorer srcHelper(
		m_metaObject, GetClassType(), false
	);

	for (const auto object : m_metaObject->GetGenericAttributeArrayObject()) {
		srcHelper.AppendSource(object);
	}

	return srcHelper;
}

bool ibValueRecordManagerObject::GetModel(ibValueModel*& tableValue, const ibMetaID& id)
{
	return false;
}

void ibValueRecordManagerObject::Modify(bool mod)
{
	ibBackendValueForm* const foundedForm = ibBackendValueForm::FindFormByUniqueKey(m_objGuid);
	if (foundedForm != nullptr)
		foundedForm->Modify(mod);
	m_recordSet->Modify(mod);
}

bool ibValueRecordManagerObject::IsModified() const
{
	return m_recordSet->IsModified();
}

bool ibValueRecordManagerObject::SetValueByMetaID(const ibMetaID& id, const ibValue& varMetaVal)
{
	if (varMetaVal != ibValueRecordManagerObject::GetValueByMetaID(id)) {
		bool result = m_recordLine->SetValueByMetaID(id, varMetaVal);
		ibValueRecordManagerObject::Modify(true);
		return result;
	}
	return true;
}

bool ibValueRecordManagerObject::GetValueByMetaID(const ibMetaID& id, ibValue& pvarMetaVal) const
{
	return m_recordLine->GetValueByMetaID(id, pvarMetaVal);
}

ibClassID ibValueRecordManagerObject::GetClassType() const
{
	const ibCtorMetaValueType* clsFactory =
		m_metaObject->GetTypeCtor(ibCtorObjectMetaType::ibCtorObjectMetaType_RecordManager);
	wxASSERT(clsFactory);
	return clsFactory->GetClassType();
}

wxString ibValueRecordManagerObject::GetClassName() const
{
	const ibCtorMetaValueType* clsFactory =
		m_metaObject->GetTypeCtor(ibCtorObjectMetaType::ibCtorObjectMetaType_RecordManager);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

wxString ibValueRecordManagerObject::GetString() const
{
	const ibCtorMetaValueType* clsFactory =
		m_metaObject->GetTypeCtor(ibCtorObjectMetaType::ibCtorObjectMetaType_RecordManager);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

/////////////////////////////////////////////////////////////////////

void ibValueRecordManagerObject::PrepareEmptyObject(const ibValueRecordManagerObject* source)
{
	m_recordLine = nullptr;

	if (source == nullptr) {
		m_recordLine = ibValue::CreateAndPrepareValueRef<ibValueRecordSetObject::ibValueRecordSetObjectRegisterReturnLine>(
			m_recordSet,
			m_recordSet->GetItem(
				m_recordSet->AppendRow()
			)
		);
	}
	else if (source != nullptr) {
		m_recordLine = m_recordSet->GetRowAt(
			m_recordSet->GetItem(0)
		);
	}

	m_recordSet->Modify(true);
}

//////////////////////////////////////////////////////////////////////
//						  ibValueRecordSetObject							//
//////////////////////////////////////////////////////////////////////

void ibValueRecordSetObject::CreateEmptyKey()
{
	m_keyValues.clear();
	for (const auto object : m_metaObject->GetGenericDimentionArrayObject()) {
		if (object->IsDeleted())
			continue;
		m_keyValues.insert_or_assign(
			object->GetMetaID(), object->CreateValue()
		);
	}
}

bool ibValueRecordSetObject::InitializeObject(const ibValueRecordSetObject* source, bool newRecord)
{
	if (!m_metaObject->AccessRight_Read()) {
		ibBackendAccessException::Error();
		return false;
	}

	const ibMetaData* metaData = m_metaObject->GetMetaData();
	wxASSERT(metaData);

	const ibValueModuleManager* moduleManager = metaData->GetModuleManager();
	wxASSERT(moduleManager);

	if (m_compileModule == nullptr) {
		m_compileModule = new ibCompileModule(m_metaObject->GetModuleObject());
		m_compileModule->SetParent(moduleManager->GetCompileModule());
		m_compileModule->AddContextVariable(thisObject, this);
	}

	if (!appData->DesignerMode()) {
		try {
			m_compileModule->Compile();
		}
		catch (const ibBackendException* err) {
			if (!appData->DesignerMode())
				throw(err);
			return false;
		};
	}

	if (source != nullptr) {
		for (long row = 0; row < source->GetRowCount(); row++) {
			ibValueTableRow* node = source->GetViewData<ibValueTableRow>(source->GetItem(row));
			wxASSERT(node);
			ibValueModelTableBase::Append(new ibValueTableRow(*node), false);
		}
	}

	if (!appData->DesignerMode()) {
		if (!newRecord) ReadData();
	}

	if (!appData->DesignerMode()) {
		wxASSERT(m_procUnit == nullptr);
		m_procUnit = new ibProcUnit();
		m_procUnit->SetParent(moduleManager->GetProcUnit());
		m_procUnit->Execute(m_compileModule->m_cByteCode);
	}

	PrepareNames();

	//is Ok
	return true;
}

///////////////////////////////////////////////////////////////////////////////////

ibValueRecordSetObject* ibValueRecordSetObject::CopyRegisterValue()
{
	return m_metaObject->CreateRecordSetObjectValue(this);
}

///////////////////////////////////////////////////////////////////////////////////

ibValueRecordSetObject::ibValueRecordSetObject(ibValueMetaObjectRegisterData* metaObject, const ibUniqueKeyPair& uniqueKey) : ibValueModelTableBase(),
m_recordColumnCollection(ibValue::CreateAndPrepareValueRef<ibValueRecordSetObjectRegisterColumnCollection>(this)), m_recordSetKeyValue(ibValue::CreateAndPrepareValueRef<ibValueRecordSetObjectRegisterKeyValue>(this)),
m_metaObject(metaObject), m_keyValues(uniqueKey.IsOk() ? uniqueKey : metaObject), m_objModified(false), m_selected(false),
m_methodHelper(new ibValueMethodHelper())
{
}

ibValueRecordSetObject::ibValueRecordSetObject(const ibValueRecordSetObject& source) : ibValueModelTableBase(),
m_recordColumnCollection(ibValue::CreateAndPrepareValueRef<ibValueRecordSetObjectRegisterColumnCollection>(this)), m_recordSetKeyValue(ibValue::CreateAndPrepareValueRef<ibValueRecordSetObjectRegisterKeyValue>(this)),
m_metaObject(source.m_metaObject), m_keyValues(source.m_keyValues), m_objModified(true), m_selected(false),
m_methodHelper(new ibValueMethodHelper())
{
	for (long row = 0; row < source.GetRowCount(); row++) {
		ibValueTableRow* node = source.GetViewData<ibValueTableRow>(source.GetItem(row));
		wxASSERT(node);
		ibValueModelTableBase::Append(new ibValueTableRow(*node), false);
	}
}

ibValueRecordSetObject::~ibValueRecordSetObject()
{
	wxDELETE(m_methodHelper);
}

bool ibValueRecordSetObject::GetAt(const ibValue& varKeyValue, ibValue& pvarValue)
{
	long index = varKeyValue.GetUInteger();
	if (index >= GetRowCount() && !appData->DesignerMode()) {
		ibBackendCoreException::Error(_("Array index out of bounds"));
		return false;
	}
	pvarValue = ibValue::CreateAndPrepareValueRef<ibValueRecordSetObjectRegisterReturnLine>(this, GetItem(index));
	return true;
}

ibClassID ibValueRecordSetObject::GetClassType() const
{
	const ibCtorMetaValueType* clsFactory =
		m_metaObject->GetTypeCtor(ibCtorObjectMetaType::ibCtorObjectMetaType_RecordSet);
	wxASSERT(clsFactory);
	return clsFactory->GetClassType();
}

wxString ibValueRecordSetObject::GetClassName() const
{
	const ibCtorMetaValueType* clsFactory =
		m_metaObject->GetTypeCtor(ibCtorObjectMetaType::ibCtorObjectMetaType_RecordSet);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

wxString ibValueRecordSetObject::GetString() const
{
	const ibCtorMetaValueType* clsFactory =
		m_metaObject->GetTypeCtor(ibCtorObjectMetaType::ibCtorObjectMetaType_RecordSet);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

#include "backend/system/value/valueTable.h"

bool ibValueRecordSetObject::LoadDataFromTable(ibValueModelTableBase* srcTable)
{
	ibValueModelColumnCollection* colData = srcTable->GetColumnCollection();

	if (colData == nullptr)
		return false;
	wxArrayString columnName;
	for (unsigned int idx = 0; idx < colData->GetColumnCount() - 1; idx++) {
		ibValueModelColumnCollection::ibValueModelColumnInfo* colInfo = colData->GetColumnInfo(idx);
		wxASSERT(colInfo);
		if (m_recordColumnCollection->GetColumnByName(colInfo->GetColumnName()) != nullptr) {
			columnName.push_back(colInfo->GetColumnName());
		}
	}
	unsigned int rowCount = srcTable->GetRowCount();
	for (unsigned int row = 0; row < rowCount; row++) {
		const ibDataViewItem& srcItem = srcTable->GetItem(row);
		const ibDataViewItem& dstItem = GetItem(AppendRow());
		for (auto colName : columnName) {
			ibValue cRetValue;
			if (srcTable->GetValueByMetaID(srcItem, srcTable->GetColumnIDByName(colName), cRetValue)) {
				const ibMetaID& id = GetColumnIDByName(colName);
				if (id != wxNOT_FOUND) SetValueByMetaID(dstItem, id, cRetValue);
			}
		}
	}

	return true;
}

ibValueModelTableBase* ibValueRecordSetObject::SaveDataToTable() const
{
	ibValueModelTable* valueTable = ibValue::CreateAndConvertObjectRef<ibValueModelTable>();

	ibValueModelColumnCollection* colData = valueTable->GetColumnCollection();
	for (unsigned int idx = 0; idx < m_recordColumnCollection->GetColumnCount() - 1; idx++) {
		ibValueModelColumnCollection::ibValueModelColumnInfo* colInfo = m_recordColumnCollection->GetColumnInfo(idx);
		wxASSERT(colInfo);
		ibValueModelColumnCollection::ibValueModelColumnInfo* newColInfo = colData->AddColumn(
			colInfo->GetColumnName(), colInfo->GetColumnType(), colInfo->GetColumnCaption(), colInfo->GetColumnWidth()
		);
		newColInfo->SetColumnID(colInfo->GetColumnID());
	}
	valueTable->PrepareNames();
	for (long row = 0; row < GetRowCount(); row++) {
		const ibDataViewItem& srcItem = GetItem(row);
		const ibDataViewItem& dstItem = valueTable->GetItem(valueTable->AppendRow());
		for (unsigned int col = 0; col < colData->GetColumnCount(); col++) {
			ibValue cRetValue;
			ibValueModelColumnCollection::ibValueModelColumnInfo* colInfo = colData->GetColumnInfo(col);
			wxASSERT(colInfo);
			if (GetValueByMetaID(srcItem, colInfo->GetColumnID(), cRetValue)) {
				const ibMetaID& id = GetColumnIDByName(colInfo->GetColumnName());
				if (id != wxNOT_FOUND) valueTable->SetValueByMetaID(dstItem, id, cRetValue);
			}
		}
	}

	return valueTable;
}

bool ibValueRecordSetObject::SetValueByMetaID(const ibDataViewItem& item, const ibMetaID& id, const ibValue& varMetaVal)
{
	if (!appData->DesignerMode()) {
		ibValueTableRow* node = GetViewData<ibValueTableRow>(item);
		if (node != nullptr) {
			const ibValueMetaObjectAttributeBase* attribute = m_metaObject->FindAnyAttributeObjectByFilter(id);
			if (attribute != nullptr) {
				return node->SetValue(
					id, attribute->AdjustValue(varMetaVal), true
				);
			}
		}
	}

	return false;
}

bool ibValueRecordSetObject::GetValueByMetaID(const ibDataViewItem& item, const ibMetaID& id, ibValue& pvarMetaVal) const
{
	if (appData->DesignerMode()) {
		const ibValueMetaObjectAttributeBase* attribute = m_metaObject->FindAnyAttributeObjectByFilter(id);
		if (attribute != nullptr) {
			pvarMetaVal = attribute->CreateValue();
			return true;
		}
		return false;
	}

	ibValueTableRow* node = GetViewData<ibValueTableRow>(item);
	if (node == nullptr)
		return false;
	return node->GetValue(id, pvarMetaVal);
}

//////////////////////////////////////////////////////////////////////
//					ibValueRecordSetObjectRegisterColumnCollection				//
//////////////////////////////////////////////////////////////////////



wxIMPLEMENT_DYNAMIC_CLASS(ibValueRecordSetObject::ibValueRecordSetObjectRegisterColumnCollection, ibValueModelTableBase::ibValueModelColumnCollection);

ibValueRecordSetObject::ibValueRecordSetObjectRegisterColumnCollection::ibValueRecordSetObjectRegisterColumnCollection() :
	ibValueModelColumnCollection(),
	m_ownerTable(nullptr),
	m_methodHelper(nullptr)
{
}

ibValueRecordSetObject::ibValueRecordSetObjectRegisterColumnCollection::ibValueRecordSetObjectRegisterColumnCollection(ibValueRecordSetObject* ownerTable) :
	ibValueModelColumnCollection(),
	m_ownerTable(ownerTable),
	m_methodHelper(new ibValueMethodHelper())
{
	ibValueMetaObjectGenericData* metaObject = m_ownerTable->GetMetaObject();
	wxASSERT(metaObject);

	for (const auto object : metaObject->GetGenericAttributeArrayObject()) {
		m_listColumnInfo.insert_or_assign(object->GetMetaID(),
			ibValue::CreateAndPrepareValueRef<ibValueRecordSetRegisterColumnInfo>(object));
	}
}

ibValueRecordSetObject::ibValueRecordSetObjectRegisterColumnCollection::~ibValueRecordSetObjectRegisterColumnCollection()
{
	wxDELETE(m_methodHelper);
}

bool ibValueRecordSetObject::ibValueRecordSetObjectRegisterColumnCollection::SetAt(const ibValue& varKeyValue, const ibValue& varValue)//������ ������� ������ ���������� � 0
{
	return false;
}

bool ibValueRecordSetObject::ibValueRecordSetObjectRegisterColumnCollection::GetAt(const ibValue& varKeyValue, ibValue& pvarValue) //������ ������� ������ ���������� � 0
{
	unsigned int index = varKeyValue.GetUInteger();
	if ((index < 0 || index >= m_listColumnInfo.size() && !appData->DesignerMode())) {
		ibBackendCoreException::Error(_("Index goes beyond array"));
		return false;
	}

	auto it = m_listColumnInfo.begin();
	std::advance(it, index);
	pvarValue = it->second;
	return true;
}

//////////////////////////////////////////////////////////////////////
//					ibValueRecordSetRegisterColumnInfo               //
//////////////////////////////////////////////////////////////////////

wxIMPLEMENT_DYNAMIC_CLASS(ibValueRecordSetObject::ibValueRecordSetObjectRegisterColumnCollection::ibValueRecordSetRegisterColumnInfo, ibValueModelTableBase::ibValueModelColumnCollection::ibValueModelColumnInfo);

ibValueRecordSetObject::ibValueRecordSetObjectRegisterColumnCollection::ibValueRecordSetRegisterColumnInfo::ibValueRecordSetRegisterColumnInfo() :
	ibValueModelColumnInfo(), m_metaAttribute(nullptr)
{
}

ibValueRecordSetObject::ibValueRecordSetObjectRegisterColumnCollection::ibValueRecordSetRegisterColumnInfo::ibValueRecordSetRegisterColumnInfo(ibValueMetaObjectAttributeBase* attribute) :
	ibValueModelColumnInfo(), m_metaAttribute(attribute)
{
}

ibValueRecordSetObject::ibValueRecordSetObjectRegisterColumnCollection::ibValueRecordSetRegisterColumnInfo::~ibValueRecordSetRegisterColumnInfo()
{
}

//////////////////////////////////////////////////////////////////////
//					 ibValueRecordSetObjectRegisterReturnLine					//
//////////////////////////////////////////////////////////////////////

wxIMPLEMENT_DYNAMIC_CLASS(ibValueRecordSetObject::ibValueRecordSetObjectRegisterReturnLine, ibValueModelTableBase::ibValueModelReturnLine);

ibValueRecordSetObject::ibValueRecordSetObjectRegisterReturnLine::ibValueRecordSetObjectRegisterReturnLine(ibValueRecordSetObject* ownerTable, const ibDataViewItem& line)
	: ibValueModelReturnLine(line), m_ownerTable(ownerTable), m_methodHelper(new ibValueMethodHelper())
{
}

ibValueRecordSetObject::ibValueRecordSetObjectRegisterReturnLine::~ibValueRecordSetObjectRegisterReturnLine()
{
	wxDELETE(m_methodHelper);
}

void ibValueRecordSetObject::ibValueRecordSetObjectRegisterReturnLine::PrepareNames() const
{
	m_methodHelper->ClearHelper();

	const ibValueMetaObjectGenericData* metaObject = m_ownerTable->GetMetaObject();
	if (metaObject != nullptr) {
		wxString objectName;
		for (const auto object : metaObject->GetGenericAttributeArrayObject()) {
			if (object->IsDeleted())
				continue;
			if (!object->GetObjectNameAsString(objectName))
				continue;
			m_methodHelper->AppendProp(
				objectName,
				object->GetMetaID()
			);
		}
	}
}

//////////////////////////////////////////////////////////////////////
//				       ibValueRecordSetObjectRegisterKeyValue					//
//////////////////////////////////////////////////////////////////////

ibValueRecordSetObject::ibValueRecordSetObjectRegisterKeyValue::ibValueRecordSetObjectRegisterKeyValue(ibValueRecordSetObject* recordSet) : ibValue(ibValueTypes::TYPE_VALUE, true),
m_methodHelper(new ibValueMethodHelper()), m_recordSet(recordSet)
{
}

ibValueRecordSetObject::ibValueRecordSetObjectRegisterKeyValue::~ibValueRecordSetObjectRegisterKeyValue()
{
	wxDELETE(m_methodHelper);
}

//////////////////////////////////////////////////////////////////////
//						ibValueRecordSetObjectRegisterKeyDescriptionValue		//
//////////////////////////////////////////////////////////////////////

ibValueRecordSetObject::ibValueRecordSetObjectRegisterKeyValue::ibValueRecordSetObjectRegisterKeyDescriptionValue::ibValueRecordSetObjectRegisterKeyDescriptionValue(ibValueRecordSetObject* recordSet, const ibMetaID& id) : ibValue(ibValueTypes::TYPE_VALUE),
m_methodHelper(new ibValueMethodHelper()), m_recordSet(recordSet), m_metaId(id)
{
}

ibValueRecordSetObject::ibValueRecordSetObjectRegisterKeyValue::ibValueRecordSetObjectRegisterKeyDescriptionValue::~ibValueRecordSetObjectRegisterKeyDescriptionValue()
{
	wxDELETE(m_methodHelper);
}

//////////////////////////////////////////////////////////////////////////////////////

long ibValueRecordSetObject::AppendRow(unsigned int before)
{
	ibValueTableRow* rowData = new ibValueTableRow();

	ibValueMetaObjectRegisterData* metaObject = GetMetaObject();
	wxASSERT(metaObject);
	ibMetaData* metaData = metaObject->GetMetaData();
	for (const auto object : metaObject->GetGenericAttributeArrayObject()) {
		rowData->AppendTableValue(object->GetMetaID(), object->CreateValue());
	}

	if (before > 0)
		return ibValueModelTableBase::Insert(rowData, before, !ibBackendException::IsEvalMode());

	return ibValueModelTableBase::Append(rowData, !ibBackendException::IsEvalMode());
}

enum Func
{
	enAdd = 0,
	enCount,
	enClear,
	enLoad,
	enUnload,
	enWrite,
	enModified,
	enRead,
	enSelected,
	enGetMetadata,
};

enum
{
	enEmpty,
	enMetadata,
};

enum
{
	enSet,
};

//****************************************************************************
//*                              Override attribute                          *
//****************************************************************************

bool ibValueRecordKeyObject::SetPropVal(const long lPropNum, const ibValue& varPropVal)
{
	return false;
}

bool ibValueRecordKeyObject::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	return false;
}

////////////////////////////////////////////////////////////////////////////

bool ibValueRecordSetObject::ibValueRecordSetObjectRegisterReturnLine::SetPropVal(const long lPropNum, const ibValue& varPropVal)
{
	const ibMetaID& id = m_methodHelper->GetPropData(lPropNum);
	if (id != wxNOT_FOUND)
		return SetValueByMetaID(id, varPropVal);
	return false;
}

bool ibValueRecordSetObject::ibValueRecordSetObjectRegisterReturnLine::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	const ibMetaID& id = m_methodHelper->GetPropData(lPropNum);
	if (id != wxNOT_FOUND) {
		return GetValueByMetaID(id, pvarPropVal);
	}
	return false;
}

////////////////////////////////////////////////////////////////////////////

ibClassID ibValueRecordSetObject::ibValueRecordSetObjectRegisterReturnLine::GetClassType() const
{
	const ibValueMetaObject* metaTable = m_ownerTable->GetMetaObject();
	const ibMetaData* metaData = metaTable->GetMetaData();
	wxASSERT(metaData);
	const ibCtorMetaValueType* clsFactory =
		metaData->GetTypeCtor(metaTable, ibCtorObjectMetaType::ibCtorObjectMetaType_RecordSet_String);
	wxASSERT(clsFactory);
	return clsFactory->GetClassType();
}

wxString ibValueRecordSetObject::ibValueRecordSetObjectRegisterReturnLine::GetClassName() const
{
	const ibValueMetaObject* metaTable = m_ownerTable->GetMetaObject();
	const ibMetaData* metaData = metaTable->GetMetaData();
	wxASSERT(metaData);
	const ibCtorMetaValueType* clsFactory =
		metaData->GetTypeCtor(metaTable, ibCtorObjectMetaType::ibCtorObjectMetaType_RecordSet_String);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

wxString ibValueRecordSetObject::ibValueRecordSetObjectRegisterReturnLine::GetString() const
{
	const ibValueMetaObject* metaTable = m_ownerTable->GetMetaObject();
	const ibMetaData* metaData = metaTable->GetMetaData();
	wxASSERT(metaData);
	const ibCtorMetaValueType* clsFactory =
		metaData->GetTypeCtor(metaTable, ibCtorObjectMetaType::ibCtorObjectMetaType_RecordSet_String);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

////////////////////////////////////////////////////////////////////////////

bool ibValueRecordSetObject::ibValueRecordSetObjectRegisterKeyValue::SetPropVal(const long lPropNum, const ibValue& varPropVal)
{
	return false;
}

bool ibValueRecordSetObject::ibValueRecordSetObjectRegisterKeyValue::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	const ibMetaID& id = m_methodHelper->GetPropData(lPropNum);
	if (id != wxNOT_FOUND) {
		pvarPropVal = ibValue::CreateAndPrepareValueRef<ibValueRecordSetObjectRegisterKeyDescriptionValue>(m_recordSet, id);
		return true;
	}
	return false;
}

//****************************************************************************
//*                              Support methods                             *
//****************************************************************************

void ibValueRecordKeyObject::PrepareNames() const
{
	m_methodHelper->ClearHelper();
	m_methodHelper->AppendFunc(wxT("IsEmpty"), wxT("IsEmpty()"));
	m_methodHelper->AppendFunc(wxT("Metadata"), wxT("Metadata()"));

	wxString objectName;

	//fill custom attributes 
	for (const auto object : m_metaObject->GetGenericDimentionArrayObject()) {
		if (object->IsDeleted())
			continue;
		if (!object->GetObjectNameAsString(objectName))
			continue;
		m_methodHelper->AppendProp(
			objectName,
			object->GetMetaID()
		);
	}
}

//////////////////////////////////////////////////////////////

void ibValueRecordSetObject::ibValueRecordSetObjectRegisterKeyValue::PrepareNames() const
{
	m_methodHelper->ClearHelper();

	ibValueMetaObjectRegisterData* metaObject = m_recordSet->GetMetaObject();
	if (metaObject != nullptr) {
		wxString objectName;
		for (const auto object : metaObject->GetGenericDimentionArrayObject()) {
			if (object->IsDeleted())
				continue;
			if (!object->GetObjectNameAsString(objectName))
				continue;
			m_methodHelper->AppendProp(
				objectName,
				object->GetMetaID()
			);
		}
	}
}

//////////////////////////////////////////////////////////////

void ibValueRecordSetObject::ibValueRecordSetObjectRegisterKeyValue::ibValueRecordSetObjectRegisterKeyDescriptionValue::PrepareNames() const
{
	m_methodHelper->ClearHelper();
	m_methodHelper->AppendFunc(wxT("Set"), 1, wxT("Set(value: any)"));

	m_methodHelper->AppendProp(wxT("Value"), m_metaId);
	m_methodHelper->AppendProp(wxT("Use"));
}

enum Prop
{
	eValue,
	eUse
};

bool ibValueRecordSetObject::ibValueRecordSetObjectRegisterKeyValue::ibValueRecordSetObjectRegisterKeyDescriptionValue::SetPropVal(const long lPropNum, const ibValue& varPropVal)
{
	const ibValueMetaObjectRegisterData* metaObject = m_recordSet->GetMetaObject();
	wxASSERT(metaObject);

	const ibValueMetaObjectAttributeBase* attribute = metaObject->FindAnyAttributeObjectByFilter(m_metaId);
	wxASSERT(attribute);

	switch (lPropNum) {
	case eValue:
		m_recordSet->SetKeyValue(m_metaId, varPropVal);
		return true;
	case eUse:
		if (varPropVal.GetBoolean())
			m_recordSet->SetKeyValue(m_metaId, attribute->CreateValue());
		else
			m_recordSet->EraseKeyValue(m_metaId);
		return true;
	}

	return false;
}

bool ibValueRecordSetObject::ibValueRecordSetObjectRegisterKeyValue::ibValueRecordSetObjectRegisterKeyDescriptionValue::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	const ibValueMetaObjectRegisterData* metaObject = m_recordSet->GetMetaObject();
	wxASSERT(metaObject);

	const ibValueMetaObjectAttributeBase* attribute = metaObject->FindAnyAttributeObjectByFilter(m_metaId);
	wxASSERT(attribute);

	switch (lPropNum) {
	case eValue:
		if (m_recordSet->FindKeyValue(m_metaId))
			pvarPropVal = m_recordSet->GetKeyValue(m_metaId);
		else
			pvarPropVal = attribute->CreateValue();
		return true;
	case eUse:
		pvarPropVal = m_recordSet->FindKeyValue(m_metaId);
		return true;
	}

	return false;
}

//////////////////////////////////////////////////////////////

bool ibValueRecordKeyObject::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	switch (lMethodNum)
	{
	case enEmpty:
		pvarRetValue = IsEmpty();
		return true;
	case enMetadata:
		pvarRetValue = m_metaObject;
		return true;
	}
	return false;
}

//////////////////////////////////////////////////////////////

bool ibValueRecordSetObject::ibValueRecordSetObjectRegisterKeyValue::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	return false;
}

//////////////////////////////////////////////////////////////

bool ibValueRecordSetObject::ibValueRecordSetObjectRegisterKeyValue::ibValueRecordSetObjectRegisterKeyDescriptionValue::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	switch (lMethodNum)
	{
	case enSet:
		m_recordSet->SetKeyValue(m_metaId, paParams[0]);
		return true;
	}
	return false;
}


//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

SYSTEM_TYPE_REGISTER(ibValueRecordSetObject::ibValueRecordSetObjectRegisterColumnCollection, "RecordSetRegisterColumn", string_to_clsid("VL_RSCL"));
SYSTEM_TYPE_REGISTER(ibValueRecordSetObject::ibValueRecordSetObjectRegisterColumnCollection::ibValueRecordSetRegisterColumnInfo, "RecordSetRegisterColumnInfo", string_to_clsid("VL_RSCI"));

SYSTEM_TYPE_REGISTER(ibValueRecordSetObject::ibValueRecordSetObjectRegisterKeyValue, "RecordSetRegisterKey", string_to_clsid("VL_RSCK"));
SYSTEM_TYPE_REGISTER(ibValueRecordSetObject::ibValueRecordSetObjectRegisterKeyValue::ibValueRecordSetObjectRegisterKeyDescriptionValue, "RecordSetRegisterKeyDescription", string_to_clsid("VL_RDVL"));
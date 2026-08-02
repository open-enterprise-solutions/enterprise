////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : common classes for catalogs, docs etc..  
////////////////////////////////////////////////////////////////////////////

#include "commonObject.h"
#include "backend/metaData.h"   // ibMetaData::RegisterSource — the metaobject registers its source into its OWN config
#include "backend/srcDataObject.h"
#include "backend/system/systemManager.h"
#include "backend/objCtor.h"
#include "backend/session/session.h"
#include "backend/serialize/dataBuilder.h"   // node serialization (WriteData / ReadData)

#include "backend/metaCollection/partial/reference/reference.h"

//***********************************************************************
//*								 metaData                               *
//***********************************************************************

//***********************************************************************
//*					ibSourceDataObject — path walk                       *
//***********************************************************************

// One traversal shared by every source kind. The first id is read off the
// source itself (GetValueByMetaID, which each kind resolves its own way —
// RAM / list / object / record set / manager). Each further id steps into the
// previous reference value by attribute name: the source's metadata resolves the
// id to its name (config-wide, so a nested reference's field is found) and member
// access reads it off the value. Read-only navigation.



//***********************************************************************
//*							ibValueMetaObjectGenericData				    *
//***********************************************************************

#pragma region _form_builder_h_
ibBackendValueForm* ibValueMetaObjectGenericData::GetGenericForm(const wxString& strFormName, ibBackendControlFrame* ownerControl, const ibUniqueKey& formGuid) const
{
	return CreateAndBuildForm(strFormName, defaultFormType, ownerControl, nullptr, formGuid);
}
#pragma endregion
#pragma region _form_creator_h_
ibBackendValueForm* ibValueMetaObjectGenericData::CreateAndBuildForm(const wxString& strFormName, const ibFormID& form_id, ibBackendControlFrame* ownerControl, ibSourceDataObject* srcObject, const ibUniqueKey& formGuid) const
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
		ibBackendAccessException::Error(wxString::Format(_("opening '%s'"), GetSynonym()));
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

ibValueSpreadsheetDocument* ibValueMetaObjectGenericData::GetTemplate(const wxString& strTemplateName) const
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

		valueSpreadsheetDocument->InvalidateNames();
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

ibValueRecordDataObjectExt* ibValueMetaObjectRecordDataExt::CreateObjectValue() const
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

ibValueRecordDataObjectExt* ibValueMetaObjectRecordDataExt::CreateObjectValue(ibValueRecordDataObjectExt* objSrc) const
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

ibValueRecordDataObject* ibValueMetaObjectRecordDataExt::CreateRecordDataObjectValue() const
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

// Node form. Base ibValueMetaObjectRecordData has no data of its own, so the chain
// bottoms out HERE — no base WriteData call (it would hit the $data bridge).

bool ibValueMetaObjectRecordDataRef::ReadData(const ibDataNode& node)
{
	m_propertyQuickChoice->ReadNodeValue(node.GetProperty(m_propertyQuickChoice->GetName()));
	m_propertyAttributeReference->ReadNodeValue(node.GetProperty(m_propertyAttributeReference->GetName()));
	return true;
}

bool ibValueMetaObjectRecordDataRef::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertyQuickChoice->GetName(), m_propertyQuickChoice->GetNodeValue());
	node.SetProperty(m_propertyAttributeReference->GetName(), m_propertyAttributeReference->GetNodeValue());
	return true;
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
#include "backend/logger/logger.h"
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
	registerManager();

	const ibCtorMetaValueType* typeCtor = m_metaData->GetTypeCtor(this, ibCtorObjectMetaType::ibCtorObjectMetaType_Reference);
	if (typeCtor != nullptr)
		(*m_propertyAttributeReference)->SetDefaultMetaType(typeCtor->GetClassType());

	return ibValueMetaObjectRecordData::OnBeforeRunMetaObject(flags);
}

bool ibValueMetaObjectRecordDataRef::OnAfterRunMetaObject(int flags)
{
	if (!ibValueMetaObjectRecordData::OnAfterRunMetaObject(flags))
		return false;
	// Register this record (catalog / document / charts / enum — subtypes chain up here) as
	// an L4 query source: it OWNS its descriptor field m_sourceDescriptor. Register ALWAYS — the factory
	// lives PER-CONFIG in the metadata (not the old global singleton), so EVERY config, incl. a read-only DB
	// load (onlyLoadFlag), must register its OWN sources into its OWN factory or its forms can't resolve them.
	m_metaData->RegisterSource(&m_queryable);
	return true;
}

bool ibValueMetaObjectRecordDataRef::OnBeforeCloseMetaObject()   // un-resolve — mirror of OnRun's RegisterSource
{
	m_metaData->UnregisterSource(&m_queryable);
	return ibValueMetaObjectRecordData::OnBeforeCloseMetaObject();
}

bool ibValueMetaObjectRecordDataRef::OnAfterCloseMetaObject()
{
	if (!(*m_propertyAttributeReference)->OnAfterCloseMetaObject())
		return false;

	if (m_propertyAttributeReference != nullptr)
		(*m_propertyAttributeReference)->SetDefaultMetaType(ibValueTypes::TYPE_EMPTY);

	unregisterReference();
	unregisterManager();

	return ibValueMetaObjectRecordData::OnAfterCloseMetaObject();
}

//***********************************************************************
//*                             form events                             *
//***********************************************************************

//process choice 
bool ibValueMetaObjectRecordDataRef::ProcessChoice(ibBackendControlFrame* ownerValue, const wxString& strFormName, ibSelectMode selMode) const
{
	ibBackendValueForm* const selectChoiceForm = GetSelectForm(strFormName, ownerValue);
	if (selectChoiceForm == nullptr)
		return false;

	selectChoiceForm->ShowForm();
	return true;
}

ibValueReferenceDataObject* ibValueMetaObjectRecordDataRef::FindObjectValue(const ibGuid& objGuid) const
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

bool ibValueMetaObjectRecordDataEnumRef::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertyAttributeOrder->GetName(), m_propertyAttributeOrder->GetNodeValue());
	return ibValueMetaObjectRecordDataRef::WriteData(node);
}

bool ibValueMetaObjectRecordDataEnumRef::ReadData(const ibDataNode& node)
{
	m_propertyAttributeOrder->ReadNodeValue(node.GetProperty(m_propertyAttributeOrder->GetName()));
	return ibValueMetaObjectRecordDataRef::ReadData(node);
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
	// m_propertyObjectModule is declared on the leaf metaobjects
	// (Catalog / Document / ChartOf*) and isn't visible from this
	// base ctor — common-default SetDefaultProcedure registration
	// stays in each leaf's own ctor where the field is in scope.
}

ibValueMetaObjectRecordDataMutableRef::~ibValueMetaObjectRecordDataMutableRef()
{
	//wxDELETE((*m_propertyAttributeDataVersion);
	//wxDELETE((*m_propertyAttributeDeletionMark));
}

//***************************************************************************
//*                       Save & load metaData                              *
//***************************************************************************

bool ibValueMetaObjectRecordDataMutableRef::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertyAttributeDataVersion->GetName(), m_propertyAttributeDataVersion->GetNodeValue());
	node.SetProperty(m_propertyAttributeDeletionMark->GetName(), m_propertyAttributeDeletionMark->GetNodeValue());
	if (!ibValueMetaObjectRecordDataRef::WriteData(node))
		return false;
	node.SetProperty(m_propertyGeneration->GetName(), m_propertyGeneration->GetNodeValue());
	return true;
}

bool ibValueMetaObjectRecordDataMutableRef::ReadData(const ibDataNode& node)
{
	m_propertyAttributeDataVersion->ReadNodeValue(node.GetProperty(m_propertyAttributeDataVersion->GetName()));
	m_propertyAttributeDeletionMark->ReadNodeValue(node.GetProperty(m_propertyAttributeDeletionMark->GetName()));
	if (!ibValueMetaObjectRecordDataRef::ReadData(node))
		return false;
	m_propertyGeneration->ReadNodeValue(node.GetProperty(m_propertyGeneration->GetName()));
	return true;
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

ibValueRecordDataObjectRef* ibValueMetaObjectRecordDataMutableRef::CreateObjectValue() const
{
	ibValueRecordDataObjectRef* createdValue = CreateObjectRefValue();
	if (createdValue && !createdValue->InitializeObject()) {
		wxDELETE(createdValue);
		return nullptr;
	}

	return createdValue;
}

ibValueRecordDataObjectRef* ibValueMetaObjectRecordDataMutableRef::CreateObjectValue(const ibGuid& guid) const
{
	ibValueRecordDataObjectRef* createdValue = CreateObjectRefValue(guid);
	if (createdValue && !createdValue->InitializeObject()) {
		wxDELETE(createdValue);
		return nullptr;
	}
	return createdValue;
}

ibValueRecordDataObjectRef* ibValueMetaObjectRecordDataMutableRef::CreateObjectValue(ibValueRecordDataObjectRef* objSrc, bool generate) const
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

ibValueRecordDataObjectRef* ibValueMetaObjectRecordDataMutableRef::CopyObjectValue(const ibGuid& srcGuid) const
{
	ibValueRecordDataObjectRef* createdValue = CreateObjectRefValue();
	if (createdValue && !createdValue->InitializeObject(srcGuid)) {
		wxDELETE(createdValue);
		return nullptr;
	}
	return createdValue;
}

ibValueRecordDataObject* ibValueMetaObjectRecordDataMutableRef::CreateRecordDataObjectValue() const
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

ibValueRecordDataObjectHierarchyRef* ibValueMetaObjectRecordDataHierarchyMutableRef::CreateObjectValue(ibObjectMode mode) const
{
	ibValueRecordDataObjectHierarchyRef* createdValue = CreateObjectRefValue(mode);
	if (createdValue && !createdValue->InitializeObject()) {
		wxDELETE(createdValue);
		return nullptr;
	}
	return createdValue;
}

ibValueRecordDataObjectHierarchyRef* ibValueMetaObjectRecordDataHierarchyMutableRef::CreateObjectValue(ibObjectMode mode, const ibGuid& guid) const
{
	ibValueRecordDataObjectHierarchyRef* createdValue = CreateObjectRefValue(mode, guid);
	if (createdValue && !createdValue->InitializeObject()) {
		wxDELETE(createdValue);
		return nullptr;
	}
	return createdValue;
}

ibValueRecordDataObjectHierarchyRef* ibValueMetaObjectRecordDataHierarchyMutableRef::CreateObjectValue(ibObjectMode mode, ibValueRecordDataObjectRef* objSrc, bool generate) const
{
	ibValueRecordDataObjectHierarchyRef* createdValue = CreateObjectRefValue(mode);
	if (createdValue && !createdValue->InitializeObject(objSrc, generate)) {
		wxDELETE(createdValue);
		return nullptr;
	}
	return createdValue;
}

ibValueRecordDataObjectHierarchyRef* ibValueMetaObjectRecordDataHierarchyMutableRef::CopyObjectValue(ibObjectMode mode, const ibGuid& srcGuid) const
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

bool ibValueMetaObjectRecordDataHierarchyMutableRef::WriteData(ibDataNode& node) const
{
	// predefined values -> an Array of Child{ guid, name, code, description, isFolder }
	std::vector<ibDataValue> predefined;
	for (const auto& value : m_predefinedObjectVector) {
		auto pv = std::make_shared<ibDataNode>();
		pv->SetValue(wxT("Guid"), value->GetPredefinedGuid());
		pv->SetValue(wxT("Name"), value->GetPredefinedName());
		pv->SetValue(wxT("Code"), value->GetPredefinedCode());
		pv->SetValue(wxT("Description"), value->GetPredefinedDescription());
		pv->SetValue(wxT("IsFolder"), (bool)value->IsPredefinedFolder());
		predefined.push_back(ibDataValue::Child(pv));
	}
	node.SetProperty(wxT("Predefined"), ibDataValue::Array(predefined));

	node.SetProperty(m_propertyAttributePredefined->GetName(), m_propertyAttributePredefined->GetNodeValue());
	node.SetProperty(m_propertyAttributeCode->GetName(), m_propertyAttributeCode->GetNodeValue());
	node.SetProperty(m_propertyAttributeDescription->GetName(), m_propertyAttributeDescription->GetNodeValue());
	node.SetProperty(m_propertyAttributeParent->GetName(), m_propertyAttributeParent->GetNodeValue());
	node.SetProperty(m_propertyAttributeIsFolder->GetName(), m_propertyAttributeIsFolder->GetNodeValue());

	return ibValueMetaObjectRecordDataMutableRef::WriteData(node);
}

bool ibValueMetaObjectRecordDataHierarchyMutableRef::ReadData(const ibDataNode& node)
{
	const ibDataValue predefinedVal = node.GetProperty(wxT("Predefined"));
	if (predefinedVal.Kind() == ibDataKind::Array) {
		for (const ibDataValue& item : predefinedVal.AsArray()) {
			const std::shared_ptr<ibDataNode>& pv = item.AsChild();
			if (!pv)
				continue;
			const ibGuid valueGuid = pv->GetValue<ibGuid>(wxT("Guid"));
			wxString valueName = pv->GetValue<wxString>(wxT("Name"));
			wxString valueCode = pv->GetValue<wxString>(wxT("Code"));
			wxString valueDescription = pv->GetValue<wxString>(wxT("Description"));
			m_predefinedObjectVector.emplace_back(
				new ibPredefinedValueObject(valueGuid, valueName, valueCode, valueDescription));
		}
	}

	m_propertyAttributePredefined->ReadNodeValue(node.GetProperty(m_propertyAttributePredefined->GetName()));
	m_propertyAttributeCode->ReadNodeValue(node.GetProperty(m_propertyAttributeCode->GetName()));
	m_propertyAttributeDescription->ReadNodeValue(node.GetProperty(m_propertyAttributeDescription->GetName()));
	m_propertyAttributeParent->ReadNodeValue(node.GetProperty(m_propertyAttributeParent->GetName()));
	m_propertyAttributeIsFolder->ReadNodeValue(node.GetProperty(m_propertyAttributeIsFolder->GetName()));

	return ibValueMetaObjectRecordDataMutableRef::ReadData(node);
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

bool ibValueMetaObjectRecordDataHierarchyMutableRef::ProcessChoice(ibBackendControlFrame* ownerValue, const wxString& strFormName, ibSelectMode selMode) const
{
	if (ownerValue == nullptr)
		return false;

	ibBackendValueForm* selectChoiceForm = nullptr;

	// The `selectChoiceForm == nullptr` guards are gone: the variable is initialised to
	// nullptr on the line above and nothing touches it in between, so both were always true.
	// They also made the first condition read as "(null AND items) OR foldersAndItems"
	// (&& binds tighter), which is what GCC flagged. Behaviour is unchanged — the branch
	// was, and is, chosen purely by selMode.
	if (selMode == ibSelectMode::ibSelectMode_Items || selMode == ibSelectMode::ibSelectMode_FoldersAndItems) {
		selectChoiceForm = GetSelectForm(strFormName, ownerValue);
	}
	else if (selMode == ibSelectMode::ibSelectMode_Folders) {
		selectChoiceForm = GetFolderSelectForm(strFormName, ownerValue);
	}

	if (selectChoiceForm == nullptr)
		return false;

	selectChoiceForm->ShowForm();
	return true;
}

//////////////////////////////////////////////////////////////////////

ibValueRecordDataObjectRef* ibValueMetaObjectRecordDataHierarchyMutableRef::CreateObjectRefValue(const ibGuid& objGuid) const
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

// Base ibValueMetaObjectGenericData has no data of its own — chain bottoms here.
bool ibValueMetaObjectRegisterData::ReadData(const ibDataNode& node)
{
	m_propertyAttributeLineActive->ReadNodeValue(node.GetProperty(m_propertyAttributeLineActive->GetName()));
	m_propertyAttributePeriod->ReadNodeValue(node.GetProperty(m_propertyAttributePeriod->GetName()));
	m_propertyAttributeRecorder->ReadNodeValue(node.GetProperty(m_propertyAttributeRecorder->GetName()));
	m_propertyAttributeLineNumber->ReadNodeValue(node.GetProperty(m_propertyAttributeLineNumber->GetName()));
	return true;
}

bool ibValueMetaObjectRegisterData::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertyAttributeLineActive->GetName(), m_propertyAttributeLineActive->GetNodeValue());
	node.SetProperty(m_propertyAttributePeriod->GetName(), m_propertyAttributePeriod->GetNodeValue());
	node.SetProperty(m_propertyAttributeRecorder->GetName(), m_propertyAttributeRecorder->GetNodeValue());
	node.SetProperty(m_propertyAttributeLineNumber->GetName(), m_propertyAttributeLineNumber->GetNodeValue());
	return true;
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
	registerRecordKey();
	registerRecordSet();
	registerRecordSet_String();

	registerRecordManager();

	return ibValueMetaObject::OnBeforeRunMetaObject(flags);
}

bool ibValueMetaObjectRegisterData::OnAfterRunMetaObject(int flags)
{
	if (!ibValueMetaObjectGenericData::OnAfterRunMetaObject(flags))
		return false;
	// The register OWNS its main (records) descriptor field. On load it ADDITIONALLY registers
	// its balance / turnover / slice descriptors (separate parameterized descriptors — TODO),
	// and drops them on unload. Register ALWAYS — the factory is PER-CONFIG (in the metadata), so a
	// read-only DB load (onlyLoadFlag) must still register its OWN sources into its OWN factory.
	m_metaData->RegisterSource(&m_queryable);
	return true;
}

bool ibValueMetaObjectRegisterData::OnBeforeCloseMetaObject()   // un-resolve — mirror of OnRun's RegisterSource
{
	m_metaData->UnregisterSource(&m_queryable);
	return ibValueMetaObjectGenericData::OnBeforeCloseMetaObject();
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
	unregisterRecordKey();
	unregisterRecordSet();
	unregisterRecordSet_String();

	unregisterRecordManager();

	return ibValueMetaObject::OnAfterCloseMetaObject();
}

//***********************************************************************
//*								ARRAY									*
//***********************************************************************

ibValueRecordKeyObject* ibValueMetaObjectRegisterData::CreateRecordKeyObjectValue() const
{
	return new ibValueRecordKeyObject(this);
}

ibValueRecordKeyObject* ibValueMetaObjectRegisterData::CreateRecordKeyObjectValue(const ibRowMetaValues& keyValues) const
{
	return new ibValueRecordKeyObject(this, keyValues);
}

ibValueRecordSetObject* ibValueMetaObjectRegisterData::CreateRecordSetObjectValue(bool needInitialize) const
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

ibValueRecordSetObject* ibValueMetaObjectRegisterData::CreateRecordSetObjectValue(const ibUniqueKeyPair& uniqueKey, bool needInitialize) const
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

ibValueRecordSetObject* ibValueMetaObjectRegisterData::CreateRecordSetObjectValue(ibValueRecordSetObject* source, bool needInitialize) const
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

ibValueRecordManagerObject* ibValueMetaObjectRegisterData::CreateRecordManagerObjectValue() const
{
	ibValueRecordManagerObject* createdValue = CreateRecordManagerObjectRegValue();
	if (createdValue && !createdValue->InitializeObject(nullptr, true)) {
		wxDELETE(createdValue);
		return nullptr;
	}
	return createdValue;
}

ibValueRecordManagerObject* ibValueMetaObjectRegisterData::CreateRecordManagerObjectValue(const ibUniqueKeyPair& uniqueKey) const
{
	ibValueRecordManagerObject* createdValue = CreateRecordManagerObjectRegValue(uniqueKey);
	if (createdValue && !createdValue->InitializeObject(nullptr, false)) {
		wxDELETE(createdValue);
		return nullptr;
	}
	return createdValue;
}

ibValueRecordManagerObject* ibValueMetaObjectRegisterData::CreateRecordManagerObjectValue(ibValueRecordManagerObject* source) const
{
	ibValueRecordManagerObject* createdValue = CreateRecordManagerObjectRegValue();
	if (createdValue && !createdValue->InitializeObject(source, true)) {
		wxDELETE(createdValue);
		return nullptr;
	}
	return createdValue;
}

ibValueRecordManagerObject* ibValueMetaObjectRegisterData::CopyRecordManagerObjectValue(const ibUniqueKeyPair& uniqueKey) const
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

// Manager-module methods. Surfaces the common module's exported methods through its
// runtime descriptor (ExportMethodsToHelper) rather than copying its whole helper
// table — the former CopyMethod wart. The bytecode-function index it keys on is
// exactly what CallAsProc/Func below feed back into pRefData, so dispatch lines up
// with no duplicated table.
void ibValueManagerDataObject::FillMembers(ibMemberTable& helper) const
{
	const ibValueMetaObjectGenericData* valueMetaObject = GetMetaObject();
	wxASSERT(valueMetaObject);

	const ibMetaData* metaData = valueMetaObject->GetMetaData();
	wxASSERT(metaData);

	// Manager module's compiled unit — designer mm in the Designer, session root
	// mm at runtime. FindCommonModule returns the typed descriptor (ibValueModuleUnit).
	auto* moduleManager = ibSession::EditModuleManagerFor(metaData);
	auto* pRefData = moduleManager ? moduleManager->FindCommonModule(GetManagerModule()) : nullptr;

	if (pRefData != nullptr)
		pRefData->ExportMethodsToHelper(&helper, g_aliasExport);
}

bool ibValueManagerDataObject::CallAsProc(const long lMethodNum, ibValue** paParams, const long lSizeArray)
{
	const ibValueMetaObjectGenericData* valueMetaObject = GetMetaObject();
	wxASSERT(valueMetaObject);

	const ibMetaData* metaData = valueMetaObject->GetMetaData();
	wxASSERT(metaData);

	auto* moduleManager = ibSession::EditModuleManagerFor(metaData);
	auto* pRefData = moduleManager ? moduleManager->FindCommonModule(GetManagerModule()) : nullptr;

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

	auto* moduleManager = ibSession::EditModuleManagerFor(metaData);
	auto* pRefData = moduleManager ? moduleManager->FindCommonModule(GetManagerModule()) : nullptr;

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


// Predefined-value props. Composes ONTO FillMembers (both bound along the ctor
// chain), so no base call here — Build() runs FillMembers first, then this.
void ibValueManagerDataObjectPredefined::FillPredefined(ibMemberTable& helper) const
{
	const ibValueMetaObjectRecordDataHierarchyMutableRef* valueMetaObject = GetMetaObject();
	wxASSERT(valueMetaObject);

	//fill custom values
	for (const auto& object : valueMetaObject->GetPredefinedValueArray()) {
		helper.AppendProp(object->GetPredefinedName(), true, false);
	}
}

bool ibValueManagerDataObjectPredefined::SetPropVal(const long lPropNum, ibValue& cValue)
{
	return false;
}

bool ibValueManagerDataObjectPredefined::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	const ibValueMetaObjectRecordDataHierarchyMutableRef* valueMetaObject = GetMetaObject();
	wxASSERT(valueMetaObject);

	const auto& predefinedValue =
		valueMetaObject->FindPredefinedValue(m_members.GetPropName(lPropNum));
	if (predefinedValue == nullptr)
		return false;
	pvarPropVal = ibValueReferenceDataObject::Create(valueMetaObject, predefinedValue->GetPredefinedGuid());
	return true;
}

//***********************************************************************
//*                        ibValueRecordDataObject						*
//***********************************************************************


ibValueRecordDataObject::ibValueRecordDataObject(const ibGuid& objGuid, bool newObject) :
	ibValueDynamicMembers(ibValueTypes::TYPE_VALUE), ibValueDataObject(objGuid, newObject),
	ibRuntimeModuleDataObject(m_members, this)
{
	// Common data surface for every record leaf; leaves add their own methods. Module
	// exports autobind in the ibRuntimeModuleDataObject ctor (as the helper's tail).
	m_members.Bind(this, &ibValueRecordDataObject::FillDataMembers);
}

ibValueRecordDataObject::ibValueRecordDataObject(const ibValueRecordDataObject& source) :
	ibValueDynamicMembers(ibValueTypes::TYPE_VALUE), ibValueDataObject(wxNewUniqueGuid, true),
	ibRuntimeModuleDataObject(m_members, this)
{
	m_members.Bind(this, &ibValueRecordDataObject::FillDataMembers);
}

ibValueRecordDataObject::~ibValueRecordDataObject()
{
}

// MY form — the one showing THIS object. Found by SOURCE, not by form key: the key is the
// form's own identity and a caller may set it to anything (an element placed on the start page
// gets a fresh one of its own, so several copies never collide). What never changes is that
// the form's source object is me. Searching by the key only worked while it happened to fall
// back to the source guid — a coincidence, and one that broke the moment a caller supplied a
// key.
ibBackendValueForm* ibValueRecordDataObject::GetForm() const
{
	if (!m_objGuid.isValid())
		return nullptr;
	return ibBackendValueForm::FindFormBySourceUniqueKey(m_objGuid);
}

//----------------------------------------------------------------------
// Universal form-open trampolines (ShowFormValue / GetFormValue).
// Promoted from the per-leaf duplicates in HierarchyRef, Document,
// DataProcessor and Report. Per-leaf variation collapses to two
// virtual hooks (GetCurrentObjectFormID + OnFormCreated). See header.
//----------------------------------------------------------------------

void ibValueRecordDataObject::ShowFormValue(const wxString& strFormName, ibBackendControlFrame* ownerControl)
{
	ibBackendValueForm* const foundedForm = GetForm();
	if (foundedForm && foundedForm->IsShown()) {
		foundedForm->ActivateForm();
		return;
	}

	ibBackendValueForm* const valueForm = GetFormValue(strFormName, ownerControl);
	if (valueForm != nullptr) {
		valueForm->Modify(IsModified());
		valueForm->ShowForm();
	}
}

ibBackendValueForm* ibValueRecordDataObject::GetFormValue(const wxString& strFormName, ibBackendControlFrame* ownerControl)
{
	ibBackendValueForm* const foundedForm = GetForm();
	if (foundedForm != nullptr)
		return foundedForm;

	ibBackendValueForm* createdForm = GetMetaObject()->CreateAndBuildForm(
		strFormName,
		GetCurrentObjectFormID(),
		ownerControl,
		this,
		m_objGuid
	);
	// Ref-flavour leaves used to set CloseOnOwnerClose(false) per-leaf;
	// Ext (DataProcessor / Report) didn't. The flag is harmless when
	// the form has no owner-close interplay — set unconditionally to
	// keep the universal path simple.
	if (createdForm != nullptr)
		createdForm->CloseOnOwnerClose(false);
	return createdForm;
}

ibClassID ibValueRecordDataObject::GetClassType() const
{
	const ibValueMetaObjectRecordData* metaObject = GetMetaObject();
	wxASSERT(metaObject);
	const ibCtorMetaValueType* clsFactory =
		metaObject->GetTypeCtor(ibCtorObjectMetaType::ibCtorObjectMetaType_Object);
	wxASSERT(clsFactory);
	return clsFactory->GetClassType();
}

wxString ibValueRecordDataObject::GetClassName() const
{
	const ibValueMetaObjectRecordData* metaObject = GetMetaObject();
	wxASSERT(metaObject);
	const ibCtorMetaValueType* clsFactory =
		metaObject->GetTypeCtor(ibCtorObjectMetaType::ibCtorObjectMetaType_Object);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

wxString ibValueRecordDataObject::GetString() const
{
	const ibValueMetaObjectRecordData* metaObject = GetMetaObject();
	wxASSERT(metaObject);
	const ibCtorMetaValueType* clsFactory =
		metaObject->GetTypeCtor(ibCtorObjectMetaType::ibCtorObjectMetaType_Object);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}


const ibSourceExplorer* ibValueRecordDataObject::GetSourceExplorer() const
{
	const ibValueMetaObjectRecordData* metaObject = GetMetaObject();

	m_sourceExplorer.Reset(
		wxT("Ref"), _("Ref"), metaObject->GetMetaID(), GetClassType(),
		false, false
	);

	for (const auto object : metaObject->GetGenericAttributeArrayObject()) {
		m_sourceExplorer.AppendColumn(object);
	}

	for (const auto object : metaObject->GetGenericTableArrayObject()) {
		if (object != nullptr && !object->IsDeleted()) {
			ibSourceExplorer& tblNode = m_sourceExplorer.AppendTable(object->GetName(), object->GetSynonym(), object->GetMetaID(), object->GetTypeDesc());
			for (ibValueMetaObjectAttributeBase* tblCol : object->GetGenericAttributeArrayObject()) tblNode.AppendColumn(tblCol);
		}
	}

	return &m_sourceExplorer;
}

#include "backend/metaCollection/partial/tabularSection/tabularSection.h"

bool ibValueRecordDataObject::SetValueByMetaID(const ibMetaID& id, const ibValue& varMetaVal)
{
	auto it = m_listObjectValue.find(id);
	wxASSERT(it != m_listObjectValue.end());
	if (it != m_listObjectValue.end()) {

		const ibValueMetaObjectRecordData* metaObjectValue = GetMetaObject();
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

// ibSourceDataObject hop gate — reads the id, then filters the pin by the field's LIVE declared type
// (FindAnyAttributeObjectByFilter -> GetTypeDesc): a composite field's UNDEFINED resolves to the pinned twin,
// a field retyped away from the pin does not. Empty type (attribute not found) skips the check.
bool ibValueRecordDataObject::GetValueBySourceHop(const ibSourceHop& hop, ibValue& out) const
{
	const bool got = GetValueByMetaID(hop.m_id, out);
	const ibValueMetaObjectAttributeBase* attribute = GetMetaObject()->FindAnyAttributeObjectByFilter(hop.m_id);
	return ibValueReferenceDataObject::CoerceHopType(hop, out, attribute != nullptr ? attribute->GetTypeDesc() : ibTypeDescription(), GetSourceMetaData()) || got;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////

ibValueModel* ibValueRecordDataObject::GetTableByMetaID(const ibMetaID& id) const
{
	const ibValue& cTable = GetValueByMetaID(id); ibValueModel* retTable = nullptr;
	if (cTable.ConvertToValue(retTable))
		return retTable;
	return nullptr;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////

#define thisObject wxT("ThisObject")

void ibValueRecordDataObject::PrepareEmptyObject()
{
	const ibValueMetaObjectRecordData* metaObject = GetMetaObject();
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
		m_listObjectValue.insert_or_assign(object->GetMetaID(), new ibValueTabularSectionDataObject(this, object));
	}
}

/////////////////////////////////////////////////////////////////////////////////////////////////////

// Fixed methods for leaves with no API of their own (DataProcessor, Report); they
// bind this. Leaves with their own method set bind their own FillMethods instead.
void ibValueRecordDataObject::FillBaseMethods(ibMemberTable& helper) const
{
	helper.AppendFunc(wxT("GetFormObject"), 2, wxT("GetFormObject(name : string, owner : any)"));
	helper.AppendFunc(wxT("GetTemplate"), 1, wxT("GetTemplate(name : string)"));
	helper.AppendFunc(wxT("GetMetadata"), wxT("GetMetadata()"));
}

// Shared data surface bound by the base ctor: the metaobject's attributes
// (eProperty) + tabular sections (eTable) + data-object module exports (eProcUnit).
// Attribute writability follows IsDataReference (a self-reference attribute is
// read-only; default false for non-reference metaobjects). ThisObject is bound via
// BindContextVariable in InitializeObject — no manual AppendProp.
void ibValueRecordDataObject::FillDataMembers(ibMemberTable& helper) const
{
	const ibValueMetaObjectRecordData* metaObject = GetMetaObject();
	wxASSERT(metaObject);
	if (metaObject == nullptr)
		return;

	wxString objectName;

	//fill custom attributes
	for (const auto object : metaObject->GetGenericAttributeArrayObject()) {
		if (object->IsDeleted())
			continue;
		if (!object->GetObjectNameAsString(objectName))
			continue;
		helper.AppendProp(
			objectName,
			true,
			!metaObject->IsDataReference(object->GetMetaID()),
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
		helper.AppendProp(
			objectName,
			true,
			false,
			object->GetMetaID(),
			eTable
		);
	}
	// Module exports surface via the descriptor autobind (ExportThunk), not here.
}

bool ibValueRecordDataObject::SetPropVal(const long lPropNum, const ibValue& varPropVal)
{
	const long lPropAlias = m_members.GetPropAlias(lPropNum);
	if (lPropAlias == eProcUnit) {
		if (m_procUnit != nullptr) {
			return m_procUnit->SetPropVal(
				GetPropName(lPropNum), varPropVal
			);
		}
	}
	else if (lPropAlias == eProperty) {
		return SetValueByMetaID(
			m_members.GetPropData(lPropNum),
			varPropVal
		);
	}
	return false;
}

bool ibValueRecordDataObject::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	const long lPropAlias = m_members.GetPropAlias(lPropNum);
	if (lPropAlias == eProcUnit) {
		if (m_procUnit != nullptr) {
			return m_procUnit->GetPropVal(
				GetPropName(lPropNum), pvarPropVal
			);
		}
	}
	else if (lPropAlias == eProperty || lPropAlias == eTable) {
		return GetValueByMetaID(
			m_members.GetPropData(lPropNum), pvarPropVal
		);
	}
	return false;
}

bool ibValueRecordDataObject::CallAsProc(const long lMethodNum, ibValue** paParams, const long lSizeArray)
{
	const long lMethodAlias = m_members.GetPropAlias(lMethodNum);
	if (lMethodAlias == eProcUnit) {
		return ibRuntimeModuleDataObject::ExecAsProc(
			GetMethodName(lMethodNum), paParams, lSizeArray
		);
	}

	return false;
}

bool ibValueRecordDataObject::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	const long lMethodAlias = m_members.GetPropAlias(lMethodNum);
	if (lMethodAlias == eProcUnit) {
		return ibRuntimeModuleDataObject::ExecAsFunc(
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


ibValueRecordDataObjectExt::ibValueRecordDataObjectExt(const ibValueMetaObjectRecordDataExt* metaObject) :
	ibValueRecordDataObject(wxNewUniqueGuid, true), m_metaObject(metaObject)
{
	// External data objects (DataProcessor / Report) expose only the fixed methods;
	// the data members come from the base FillDataMembers.
	m_members.Bind(this, &ibValueRecordDataObject::FillBaseMethods);
}

ibValueRecordDataObjectExt::ibValueRecordDataObjectExt(const ibValueRecordDataObjectExt& source) :
	ibValueRecordDataObject(source), m_metaObject(source.m_metaObject)
{
	m_members.Bind(this, &ibValueRecordDataObject::FillBaseMethods);
}

ibValueRecordDataObjectExt::~ibValueRecordDataObjectExt()
{
}

ibExternalOwnerHelper::~ibExternalOwnerHelper()
{
	if (m_externalMetadata != nullptr) {
		if (!m_externalMetadata->CloseDatabase(forceCloseFlag)) {
			wxASSERT_MSG(false, "external metadata CloseDatabase() == false");
		}
		wxDELETE(m_externalMetadata);
	}
}

bool ibValueRecordDataObjectExt::InitializeObject()
{
	if (!m_metaObject->IsExternalCreate()) {

		if (!m_metaObject->AccessRight_Use()) {
			ibBackendAccessException::Error(wxString::Format(_("using '%s'"), m_metaObject->GetSynonym()));
			return false;
		}

		ibValueModuleManager* moduleManager = ibSession::EditModuleManagerFor(m_metaObject->GetMetaData());
		wxASSERT(moduleManager);

		// Imperative: parent first, then lazy compile + runtime slot
		// pick up parent automatically on creation.
		ibRuntimeModuleDataObject::SetParent(moduleManager);
		BindContextVariable(thisObject, this);
		InitializeRuntime();

		try {
			Compile();
		}
		catch (const ibBackendException&) {
			if (!appData->DesignerMode())
				throw;
			return false;
		};
	}

	PrepareEmptyObject();

	if (!m_metaObject->IsExternalCreate())
		Run();

	InvalidateNames();

	//is Ok
	return true;
}

bool ibValueRecordDataObjectExt::InitializeObject(ibValueRecordDataObjectExt* source)
{
	if (!m_metaObject->IsExternalCreate()) {
		ibValueModuleManager* moduleManager = ibSession::EditModuleManagerFor(m_metaObject->GetMetaData());
		wxASSERT(moduleManager);

		ibRuntimeModuleDataObject::SetParent(moduleManager);
		BindContextVariable(thisObject, this);
		InitializeRuntime();

		try {
			Compile();
		}
		catch (const ibBackendException&) {
			if (!appData->DesignerMode())
				throw;
			return false;
		};
	}

	PrepareEmptyObject();

	if (!m_metaObject->IsExternalCreate())
		Run();

	InvalidateNames();

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


ibValueRecordDataObjectRef::ibValueRecordDataObjectRef(const ibValueMetaObjectRecordDataMutableRef* metaObject, const ibGuid& objGuid) :
	// A new object mints a PLAIN unique guid (pure identity — the type is carried by _RTRef / the metaObject,
	// never baked into the key). An existing object keeps its stored guid.
	ibValueRecordDataObject(objGuid.isValid() ? objGuid : ibGuid(ibGuid::newGuid(GUID_RANDOM)),
		!objGuid.isValid()),
	m_objModified(false),
	m_metaObject(metaObject),
	m_reference_impl(nullptr)
{
	if (m_metaObject != nullptr)
		m_reference_impl = new ibReference(m_objGuid);   // pure guid; type is the metaObject / _RTRef
}

ibValueRecordDataObjectRef::ibValueRecordDataObjectRef(const ibValueRecordDataObjectRef& src) :
	ibValueRecordDataObject(src),
	m_objModified(false),
	m_metaObject(src.m_metaObject),
	m_reference_impl(nullptr)
{
	if (m_metaObject != nullptr)
		m_reference_impl = new ibReference(m_objGuid);   // pure guid; type is the metaObject / _RTRef
}

ibValueRecordDataObjectRef::~ibValueRecordDataObjectRef()
{
	wxDELETE(m_reference_impl);
}

bool ibValueRecordDataObjectRef::InitializeObject(const ibGuid& copyGuid)
{
	if (!m_metaObject->AccessRight_Read()) {
		ibBackendAccessException::Error(wxString::Format(_("reading '%s'"), m_metaObject->GetSynonym()));
		return false;
	}

	// Parent the object's compile module to the module manager whose context it
	// should see — designer manager in the Designer, session root mm at runtime.
	ibValueModuleManager* moduleManager = ibSession::EditModuleManagerFor(m_metaObject->GetMetaData());
	wxASSERT(moduleManager);

	ibRuntimeModuleDataObject::SetParent(moduleManager);
	BindContextVariable(thisObject, this);

	try {
		Compile();
	}
	catch (const ibBackendException&) {
		if (!appData->DesignerMode())
			throw;
		return false;
	};

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
		InitializeRuntime();
		m_procUnit->SetParent(moduleManager->GetProcUnit().get());
		Execute();
		if (m_newObject) {
			succes = Filling();
		}
	}

	InvalidateNames();

	//is Ok
	return succes;
}

bool ibValueRecordDataObjectRef::InitializeObject(ibValueRecordDataObjectRef* source, bool generate)
{
	ibValueModuleManager* moduleManager = ibSession::EditModuleManagerFor(m_metaObject->GetMetaData());
	wxASSERT(moduleManager);

	ibRuntimeModuleDataObject::SetParent(moduleManager);
	BindContextVariable(thisObject, this);

	try {
		Compile();
	}
	catch (const ibBackendException&) {
		if (!appData->DesignerMode())
			throw;
		return false;
	};

	if (!generate && source != nullptr)
		PrepareEmptyObject(source);
	else
		PrepareEmptyObject();

	bool succes = true;
	if (!appData->DesignerMode()) {
		wxASSERT(m_procUnit == nullptr);
		InitializeRuntime();
		// Re-apply descriptor SetParent so cascade picks up the now-
		// existent procUnit and wires its parent too. Compile-side
		// parent is already set from the first call above — rewriting
		// to the same value.
		ibRuntimeModuleDataObject::SetParent(moduleManager);
		Execute();
		// OnCopy / Filling run user script with side effects. Skip them under a
		// debugger watch/eval, the same way BeginWriteScope/BeginDeleteScope skip
		// eval — evaluating a watch must not fire user handlers. (DesignerMode is
		// already excluded by the enclosing guard.)
		if (!ibBackendException::IsEvalMode()) {
			if (m_newObject && source != nullptr && !generate) {
				ExecAsProc(wxT("OnCopy"), source->GetValue());
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
	}

	InvalidateNames();

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

const ibSourceExplorer* ibValueRecordDataObjectRef::GetSourceExplorer() const
{
	m_sourceExplorer.Reset(
		wxT("Ref"), _("Ref"), m_metaObject->GetMetaID(), GetClassType(),
		false, false
	);

	ibValueMetaObjectAttributeBase* attribute = m_metaObject->GetAttributeForCode();

	for (const auto object : m_metaObject->GetGenericAttributeArrayObject()) {
		if (!m_metaObject->IsDataReference(object->GetMetaID())) {
			m_sourceExplorer.AppendColumn(object, object != attribute);
		}
	}

	for (const auto object : m_metaObject->GetGenericTableArrayObject()) {
		if (object != nullptr && !object->IsDeleted()) {
			ibSourceExplorer& tblNode = m_sourceExplorer.AppendTable(object->GetName(), object->GetSynonym(), object->GetMetaID(), object->GetTypeDesc());
			for (ibValueMetaObjectAttributeBase* tblCol : object->GetGenericAttributeArrayObject()) tblNode.AppendColumn(tblCol);
		}
	}

	return &m_sourceExplorer;
}

void ibValueRecordDataObjectRef::Modify(bool mod)
{
	ibBackendValueForm* const foundedForm = ibBackendValueForm::FindFormBySourceUniqueKey(m_objGuid);

	if (foundedForm != nullptr)
		foundedForm->Modify(mod);

	m_objModified = mod;
}

bool ibValueRecordDataObjectRef::Generate()
{
	if (m_newObject)
		return false;

	ibBackendValueForm* const foundedForm = ibBackendValueForm::FindFormBySourceUniqueKey(m_objGuid);
	if (foundedForm != nullptr)
		return foundedForm->GenerateForm(this);

	return false;
}

bool ibValueRecordDataObjectRef::Filling(ibValue cValue) const
{
	ibValue standartProcessing = true;
	ExecAsProc(wxT("Filling"), cValue, standartProcessing);
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
	if (m_metaObject->IsDataReference(id)) {
		pvarMetaVal = GetReference();
		return true; 
	}

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
		m_listObjectValue.insert_or_assign(object->GetMetaID(), new ibValueTabularSectionDataObjectRef(this, object));
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
		ibValueTabularSectionDataObjectRef* tableSection = new ibValueTabularSectionDataObjectRef(this, object);
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

// Hierarchy axis (Catalog / ChartOf* / Enumeration) — see commonObject.h for
// the rationale. Class identity comes from the metaobject's clsFactory via
// GetClassType(); no RTTI macro is involved (wxRTTI was dropped in Phase 3).

ibValueRecordDataObjectHierarchyRef::ibValueRecordDataObjectHierarchyRef(const ibValueMetaObjectRecordDataHierarchyMutableRef* metaObject, const ibGuid& objGuid, ibObjectMode objMode)
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

const ibSourceExplorer* ibValueRecordDataObjectHierarchyRef::GetSourceExplorer() const
{
	m_sourceExplorer.Reset(
		wxT("Ref"), _("Ref"), m_metaObject->GetMetaID(), GetClassType(),
		false, false
	);
	ibValueMetaObjectAttributeBase* attribute = m_metaObject->GetAttributeForCode();
	for (const auto object : m_metaObject->GetGenericAttributeArrayObject()) {
		ibItemMode attrUse = object->GetItemMode();
		if (m_objMode == ibObjectMode::OBJECT_ITEM) {
			if (attrUse == ibItemMode::ibItemMode_Item
				|| attrUse == ibItemMode::ibItemMode_Folder_Item) {
				if (!m_metaObject->IsDataReference(object->GetMetaID())) {
					m_sourceExplorer.AppendColumn(object, object != attribute);
				}
			}
		}
		else {
			if (attrUse == ibItemMode::ibItemMode_Folder ||
				attrUse == ibItemMode::ibItemMode_Folder_Item) {
				if (!m_metaObject->IsDataReference(object->GetMetaID())) {
					m_sourceExplorer.AppendColumn(object, object != attribute);
				}
			}
		}
	}

	for (const auto object : m_metaObject->GetGenericTableArrayObject()) {
		ibItemMode tableUse = object->GetTableUse();
		if (m_objMode == ibObjectMode::OBJECT_ITEM) {
			if (tableUse == ibItemMode::ibItemMode_Item
				|| tableUse == ibItemMode::ibItemMode_Folder_Item) {
				if (object != nullptr && !object->IsDeleted()) {
					ibSourceExplorer& tblNode = m_sourceExplorer.AppendTable(object->GetName(), object->GetSynonym(), object->GetMetaID(), object->GetTypeDesc());
					for (ibValueMetaObjectAttributeBase* tblCol : object->GetGenericAttributeArrayObject()) tblNode.AppendColumn(tblCol);
				}
			}
		}
		else {
			if (tableUse == ibItemMode::ibItemMode_Folder ||
				tableUse == ibItemMode::ibItemMode_Folder_Item) {
				if (object != nullptr && !object->IsDeleted()) {
					ibSourceExplorer& tblNode = m_sourceExplorer.AppendTable(object->GetName(), object->GetSynonym(), object->GetMetaID(), object->GetTypeDesc());
					for (ibValueMetaObjectAttributeBase* tblCol : object->GetGenericAttributeArrayObject()) tblNode.AppendColumn(tblCol);
				}
			}
		}
	}

	return &m_sourceExplorer;
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
	const ibValueMetaObjectRecordDataHierarchyMutableRef* metaFolder = GetMetaObject();
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
				m_listObjectValue.insert_or_assign(object->GetMetaID(), new ibValueTabularSectionDataObjectRef(this, object));
			}
			else {
				m_listObjectValue.insert_or_assign(object->GetMetaID(), ibValueTypes::TYPE_NULL);
			}
		}
		else {
			if (tableUse == ibItemMode::ibItemMode_Folder ||
				tableUse == ibItemMode::ibItemMode_Folder_Item) {
				m_listObjectValue.insert_or_assign(object->GetMetaID(), new ibValueTabularSectionDataObjectRef(this, object));
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
	const ibValueMetaObjectRecordDataHierarchyMutableRef* metaFolder = GetMetaObject();
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
		ibValueTabularSectionDataObjectRef* tableSection = new ibValueTabularSectionDataObjectRef(this, object);
		if (tableSection->LoadDataFromTable(source->GetTableByMetaID(object->GetMetaID())))
			m_listObjectValue.insert_or_assign(object->GetMetaID(), tableSection);
		else
			wxDELETE(tableSection);
	}
	m_objModified = true;
}


//----------------------------------------------------------------------
// Phase B — template-method Write/Delete on
// ibValueRecordDataObjectHierarchyRef. The 3 hierarchy-mutable-ref
// leaves (Catalog / ChartOfAccounts / ChartOfCharacteristicTypes)
// share byte-identical pipelines mod class-name qualification on
// IsNewObject / GenerateUniqueIdentifier / ResetUniqueIdentifier;
// virtual dispatch lets the same body serve all three.
//----------------------------------------------------------------------

bool ibValueRecordDataObjectHierarchyRef::WriteObject()
{
	ibConnectionScope scope = ibSession::Current()->OpenConnectionScope();
	if (!BeginWriteScope(scope)) return true;

	ibBackendValueForm* const valueForm = GetForm();
	const bool newObject = IsNewObject();

	// Stage-named failures — same rule as the recorder path: the message says which stage
	// stopped the write and on which object, and a script cancel reads as a cancel.
	{
		ibValue cancel = false;
		ExecAsProc(wxT("BeforeWrite"), cancel);
		if (cancel.GetBoolean()) {
			scope.SafeRollBackTransaction();
			ibBackendCoreException::Error(_("%s: writing cancelled by the BeforeWrite handler"),
				GetSourceCaption());
			return false;
		}
	}

	bool generateUniqueIdentifier = false;
	if (!IsSetUniqueIdentifier()) {
		ibValue prefix = wxEmptyString, standartProcessing = true;
		ExecAsProc(wxT("SetNewCode"), prefix, standartProcessing);
		if (standartProcessing.GetBoolean())
			generateUniqueIdentifier = GenerateUniqueIdentifier(prefix.GetString());
	}

	if (!SaveData()) {
		if (generateUniqueIdentifier) ResetUniqueIdentifier();
		scope.SafeRollBackTransaction();
		ibBackendCoreException::Error(_("%s: failed to save the object data"), GetSourceCaption());
		return false;
	}

	{
		ibValue cancel = false;
		ExecAsProc(wxT("OnWrite"), cancel);
		if (cancel.GetBoolean()) {
			if (generateUniqueIdentifier) ResetUniqueIdentifier();
			scope.SafeRollBackTransaction();
			ibBackendCoreException::Error(_("%s: writing cancelled by the OnWrite handler"),
				GetSourceCaption());
			return false;
		}
	}

	CommitWriteScope(scope, valueForm, newObject);
	return true;
}

bool ibValueRecordDataObjectHierarchyRef::DeleteObject()
{
	// Predefined-guard fires before the scope — pure policy check that
	// doesn't need a TX. DesignerMode is checked first so the predefined
	// lookup itself doesn't run during metadata editing.
	if (!appData->DesignerMode()) {
		const ibValueMetaObjectRecordDataHierarchyMutableRef* valueMetaObject = GetMetaObject();
		wxASSERT(valueMetaObject);
		const ibGuid& objGuid = GetGuid();
		if (valueMetaObject->FindPredefinedValue(objGuid) != nullptr) {
			ibBackendCoreException::Error(_("%s cannot be deleted: it is a predefined element"),
				GetSourceCaption());
			return false;
		}
	}

	ibConnectionScope scope = ibSession::Current()->OpenConnectionScope();
	if (!BeginDeleteScope(scope)) return true;

	ibBackendValueForm* const valueForm = GetForm();

	{
		ibValue cancel = false;
		ExecAsProc(wxT("BeforeDelete"), cancel);
		if (cancel.GetBoolean()) {
			scope.SafeRollBackTransaction();
			ibBackendCoreException::Error(_("%s: deletion cancelled by the BeforeDelete handler"),
				GetSourceCaption());
			return false;
		}
	}

	if (!DeleteData()) {
		scope.SafeRollBackTransaction();
		ibBackendCoreException::Error(_("%s: failed to delete the object data"), GetSourceCaption());
		return false;
	}

	{
		ibValue cancel = false;
		ExecAsProc(wxT("OnDelete"), cancel);
		if (cancel.GetBoolean()) {
			scope.SafeRollBackTransaction();
			ibBackendCoreException::Error(_("%s: deletion cancelled by the OnDelete handler"),
				GetSourceCaption());
			return false;
		}
	}

	CommitDeleteScope(scope, valueForm);
	return true;
}


//*********************************************************************************************
//*           ibValueRecordDataObjectRecorderRef::ibRecorderRegister	                      *
//*********************************************************************************************
// Per-recorder register holder. Iterates the leaf metaobject's
// RecordDescription, creates one ibValueRecordSetObject per declared
// register seeded with this recorder's reference, fans Write/Delete
// across all of them. Promoted from being nested in Document with the
// Phase B-Recorder split. Leaf-metaobject access stays generic via
// the GetRecordDescription virtual hook on RecorderRef.


void ibValueRecordDataObjectRecorderRef::ibRecorderRegister::CreateRecordSet()
{
	const ibMetaDescription* metaDesc = m_recorder->GetRecordDescription();
	if (metaDesc == nullptr) return;   // recorder without static description — no cascade
	const ibMetaData* metaData = m_recorder->GetMetaObject()->GetMetaData();
	wxASSERT(metaData);

	ibRecorderRegister::ClearRecordSet();

	for (unsigned int idx = 0; idx < metaDesc->GetTypeCount(); idx++) {
		const ibValueMetaObjectRegisterData* metaObject = metaData->FindAnyObjectByFilter<ibValueMetaObjectRegisterData>(metaDesc->GetByIdx(idx));
		if (metaObject == nullptr || !metaObject->IsAllowed())
			continue;
		ibValueMetaObjectAttributePredefined* registerRecord = metaObject->GetRegisterRecorder();
		wxASSERT(registerRecord);
		ibValuePtr<ibValueRecordSetObject> recordSet(metaObject->CreateRecordSetObjectValue());
		recordSet->SetKeyValue(registerRecord->GetMetaID(), m_recorder->GetReference());
		m_records.insert_or_assign(metaObject->GetMetaID(), recordSet);
	}

	InvalidateNames();
}

bool ibValueRecordDataObjectRecorderRef::ibRecorderRegister::WriteRecordSet()
{
	for (auto& pair : m_records) {
		ibValueRecordSetObject* record = pair.second;
		wxASSERT(record);
		// The register's OWN exception is the informative one — it names the line, the required
		// attribute, the lock conflict, the access deny. It travels UP intact instead of being
		// flattened into a bare false that the recorder above can only report as "failed to post":
		// the whole point of this pass is that the user learns WHICH register refused and WHY.
		// The write scope's dtor rolls back the unmatched Begin, so leaving through a throw is safe.
		if (!record->WriteRecordSet())
			ibBackendCoreException::Error(_("Failed to write the movements of register '%s'"),
				record->GetMetaObject()->GetSynonym());
	}
	return true;
}

bool ibValueRecordDataObjectRecorderRef::ibRecorderRegister::DeleteRecordSet()
{
	for (auto& pair : m_records) {
		ibValueRecordSetObject* record = pair.second;
		wxASSERT(record);
		// Same as WriteRecordSet — the register speaks for itself; this only names the one that
		// returned a plain false without saying anything.
		if (!record->DeleteRecordSet())
			ibBackendCoreException::Error(_("Failed to clear the movements of register '%s'"),
				record->GetMetaObject()->GetSynonym());
	}
	return true;
}

void ibValueRecordDataObjectRecorderRef::ibRecorderRegister::ClearRecordSet()
{
	m_records.clear();
}

void ibValueRecordDataObjectRecorderRef::ibRecorderRegister::RefreshRecordSet()
{
	for (auto& pair : m_records) {
		ibValueRecordSetObject* record = pair.second;
		wxASSERT(record);
		const ibValueMetaObjectRegisterData* object = record->GetMetaObject();
		wxASSERT(object);
		ibBackendValueForm* backendFrame = ibBackendValueForm::FindFormBySourceUniqueKey(object->GetGuid());
		if (backendFrame != nullptr) backendFrame->UpdateForm();
	}
}

ibValueRecordDataObjectRecorderRef::ibRecorderRegister::ibRecorderRegister(ibValueRecordDataObjectRecorderRef* recorder) :
	ibValueDynamicMembers(ibValueTypes::TYPE_VALUE), m_recorder(recorder)
{
	m_members.Bind(this, &ibRecorderRegister::FillMembers);
	ibRecorderRegister::CreateRecordSet();
}

ibValueRecordDataObjectRecorderRef::ibRecorderRegister::~ibRecorderRegister()
{
	ibRecorderRegister::ClearRecordSet();
}

namespace { enum { enWriteRegister = 0 }; }

void ibValueRecordDataObjectRecorderRef::ibRecorderRegister::FillMembers(ibMemberTable& helper) const
{
	helper.AppendFunc(wxT("Write"), wxT("Write()"));
	for (auto& pair : m_records) {
		ibValueRecordSetObject* record = pair.second;
		wxASSERT(record);
		const ibValueMetaObjectRegisterData* metaObject = record->GetMetaObject();
		wxASSERT(metaObject);
		helper.AppendProp(metaObject->GetName(), true, false, pair.first);
	}
}

bool ibValueRecordDataObjectRecorderRef::ibRecorderRegister::SetPropVal(const long /*lPropNum*/, const ibValue& /*varPropVal*/)
{
	return false;
}

bool ibValueRecordDataObjectRecorderRef::ibRecorderRegister::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	auto it = m_records.find(m_members.GetPropData(lPropNum));
	if (it != m_records.end()) {
		pvarPropVal = it->second;
		return true;
	}
	return false;
}

bool ibValueRecordDataObjectRecorderRef::ibRecorderRegister::CallAsFunc(const long lMethodNum, ibValue& /*pvarRetValue*/, ibValue** /*paParams*/, const long /*lSizeArray*/)
{
	switch (lMethodNum) {
	case enWriteRegister:
		WriteRecordSet();
		return true;
	}
	return false;
}

//*********************************************************************************************
//*               ibValueRecordDataObjectRecorderRef — ctors + scaffold                       *
//*********************************************************************************************

ibValueRecordDataObjectRecorderRef::ibValueRecordDataObjectRecorderRef(
	const ibValueMetaObjectRecordDataMutableRef* metaObject, const ibGuid& objGuid) :
	ibValueRecordDataObjectRef(metaObject, objGuid)
{
	// m_registerRecords is intentionally NOT initialized here — the
	// leaf ctor calls InitRegisterRecords() once its own vtable is
	// active so ibRecorderRegister::CreateRecordSet's virtual call
	// to GetRecordDescription dispatches to the leaf override. See
	// the header for the full rationale.
}

ibValueRecordDataObjectRecorderRef::ibValueRecordDataObjectRecorderRef(
	const ibValueRecordDataObjectRecorderRef& src) :
	ibValueRecordDataObjectRef(src)
{
	// Same as the primary ctor — leaf does InitRegisterRecords.
}

ibValueRecordDataObjectRecorderRef::~ibValueRecordDataObjectRecorderRef() = default;

void ibValueRecordDataObjectRecorderRef::InitRegisterRecords()
{
	wxASSERT(m_registerRecords == nullptr);
	m_registerRecords = new ibRecorderRegister(this);
}

// RegisterRecords — EXPORTED context variable, bound BEFORE the base compiles so
// the module resolves it. SetParent first so the lazily-built compile module gets
// the scope chain; the base re-SetParents (idempotent), binds ThisObject and
// compiles. The bind is the single source for both designer (compile module only —
// m_binder null) and runtime (binder). Shared by all recorder/document-like objects.
bool ibValueRecordDataObjectRecorderRef::InitializeObject(const ibGuid& copyGuid)
{
	ibRuntimeModuleDataObject::SetParent(ibSession::EditModuleManagerFor(m_metaObject->GetMetaData()));
	ibRecorderRegister* recordSet = m_registerRecords;
	BindExportVariable(wxT("RegisterRecords"), recordSet);
	return ibValueRecordDataObjectRef::InitializeObject(copyGuid);
}

bool ibValueRecordDataObjectRecorderRef::InitializeObject(ibValueRecordDataObjectRef* source, bool generate)
{
	ibRuntimeModuleDataObject::SetParent(ibSession::EditModuleManagerFor(m_metaObject->GetMetaData()));
	ibRecorderRegister* recordSet = m_registerRecords;
	BindExportVariable(wxT("RegisterRecords"), recordSet);
	return ibValueRecordDataObjectRef::InitializeObject(source, generate);
}

bool ibValueRecordDataObjectRecorderRef::WriteObject(ibDocumentWriteMode writeMode, ibDocumentPostingMode postingMode)
{
	// Posting pre-guard: leaf-specific check (Document's DeletionMark
	// blocks posting). Default hook returns true (ok to proceed).
	if (!appData->DesignerMode()
	    && writeMode == ibDocumentWriteMode::ibDocumentWriteMode_Posting
	    && !CheckDeletionMarkOnPosting(writeMode))
	{
		ibBackendCoreException::Error(_("%s cannot be posted: it is marked for deletion"),
			GetSourceCaption());
		return false;
	}

	// Scaffold via Phase A Begin/CommitWriteScope. Per-recorder middle:
	// BeforeWrite(wm, pm) + ApplyPostedAttributeOnWrite hook + SetNew
	// Number codegen + FillDefaultDateForNew hook + SaveData +
	// register cascade (CreateRecordSet for new, Posting/UndoPosting
	// scripts + WriteRecordSet/DeleteRecordSet) + OnWrite.
	ibConnectionScope scope = ibSession::Current()->OpenConnectionScope();
	if (!BeginWriteScope(scope)) return true;

	ibBackendValueForm* const valueForm = GetForm();
	const bool newObject = IsNewObject();

	// Every failure below says WHICH STAGE refused and on WHICH OBJECT. A posting run walks a long
	// chain — handler, row, movements per register, handler again — and "failed to write object in
	// db!" for all of them tells the user nothing about where to look. A cancel raised by script is
	// also reported as a cancel, not as a database failure: nothing went wrong in the DB there.
	{
		ibValue cancel = false;
		ExecAsProc(wxT("BeforeWrite"), cancel,
			ibValue::CreateEnumObject<ibValueEnumDocumentWriteMode>(writeMode),
			ibValue::CreateEnumObject<ibValueEnumDocumentPostingMode>(postingMode)
		);
		if (cancel.GetBoolean()) {
			scope.SafeRollBackTransaction();
			ibBackendCoreException::Error(_("%s: writing cancelled by the BeforeWrite handler"),
				GetSourceCaption());
			return false;
		}
		ApplyPostedAttributeOnWrite(writeMode);
	}

	bool generateUniqueIdentifier = false;
	if (!IsSetUniqueIdentifier()) {
		ibValue prefix = wxEmptyString, standartProcessing = true;
		ExecAsProc(wxT("SetNewNumber"), prefix, standartProcessing);
		if (standartProcessing.GetBoolean())
			generateUniqueIdentifier = GenerateUniqueIdentifier(prefix.GetString());
	}

	if (newObject)
		FillDefaultDateForNew();

	if (!SaveData()) {
		if (generateUniqueIdentifier) ResetUniqueIdentifier();
		scope.SafeRollBackTransaction();
		ibBackendCoreException::Error(_("%s: failed to save the object data"), GetSourceCaption());
		return false;
	}

	if (newObject)
		m_registerRecords->CreateRecordSet();

	// Posting / UndoPosting cascade — scripts then the matching
	// register set Write/Delete. The cascade rides under this recorder's
	// row-lock from BeginWriteScope, so concurrent re-posts on the same
	// recorder serialise here.
	if (writeMode == ibDocumentWriteMode::ibDocumentWriteMode_Posting) {
		ibValue cancel = false;
		ExecAsProc(wxT("Posting"), cancel,
			ibValue::CreateEnumObject<ibValueEnumDocumentPostingMode>(postingMode));
		if (cancel.GetBoolean()) {
			if (generateUniqueIdentifier) ResetUniqueIdentifier();
			scope.SafeRollBackTransaction();
			ibBackendCoreException::Error(_("%s: posting cancelled by the Posting handler"),
				GetSourceCaption());
			return false;
		}
		// The cascade names the failing register itself (and lets its own exception through);
		// this only covers a silent false from the fan-out.
		if (!m_registerRecords->WriteRecordSet()) {
			if (generateUniqueIdentifier) ResetUniqueIdentifier();
			scope.SafeRollBackTransaction();
			ibBackendCoreException::Error(_("%s: failed to write the register movements"),
				GetSourceCaption());
			return false;
		}
	}
	else if (writeMode == ibDocumentWriteMode::ibDocumentWriteMode_UndoPosting) {
		ibValue cancel = false;
		ExecAsProc(wxT("UndoPosting"), cancel);
		if (cancel.GetBoolean()) {
			if (generateUniqueIdentifier) ResetUniqueIdentifier();
			scope.SafeRollBackTransaction();
			ibBackendCoreException::Error(_("%s: undo posting cancelled by the UndoPosting handler"),
				GetSourceCaption());
			return false;
		}
		if (!m_registerRecords->DeleteRecordSet()) {
			if (generateUniqueIdentifier) ResetUniqueIdentifier();
			scope.SafeRollBackTransaction();
			ibBackendCoreException::Error(_("%s: failed to clear the register movements"),
				GetSourceCaption());
			return false;
		}
	}

	{
		ibValue cancel = false;
		ExecAsProc(wxT("OnWrite"), cancel);
		if (cancel.GetBoolean()) {
			if (generateUniqueIdentifier) ResetUniqueIdentifier();
			scope.SafeRollBackTransaction();
			ibBackendCoreException::Error(_("%s: writing cancelled by the OnWrite handler"),
				GetSourceCaption());
			return false;
		}
	}

	// Posting / UndoPosting audit. Layered on top of the generic
	// record.saved that CommitWriteScope emits — admin sees BOTH the
	// row change and the posting state change as distinct events.
	// Plain ibDocumentWriteMode_Write skips this — the generic
	// record.* audit covers it.
	if (ibLog && ibLog->IsEnabled(ibLogLevel::Audit)
	    && writeMode != ibDocumentWriteMode::ibDocumentWriteMode_Write)
	{
		const wxString refGuid = m_reference_impl
			? ibGuid(m_reference_impl->m_guid).str() : wxString();
		const int refMetaId = m_metaObject != nullptr
			? static_cast<int>(m_metaObject->GetMetaID()) : 0;   // type from the metaObject, not the key bytes
		const wxString evt = (writeMode == ibDocumentWriteMode::ibDocumentWriteMode_Posting)
			? wxT("posted") : wxT("unposted");
		ibLog->Audit(wxT("document"), evt, GetSourceCaption(), refGuid, refMetaId);
	}

	CommitWriteScope(scope, valueForm, newObject);
	m_registerRecords->RefreshRecordSet();
	return true;
}

void ibValueRecordDataObjectRecorderRef::SetDeletionMark(bool deletionMark)
{
	// Recorder-flavour of the deletion-mark algorithm: same as the
	// catalog/charts path (set the flag + SaveModify) but with an
	// up-front un-post so the row's movements clear before the mark
	// lands. UndoPosting is a no-op for non-posted recorders via the
	// IsPosted / ApplyPostedAttributeOnWrite hooks.
	if (m_newObject)
		return;
	WriteObject(ibDocumentWriteMode::ibDocumentWriteMode_UndoPosting,
	             ibDocumentPostingMode::ibDocumentPostingMode_Regular);
	ibValueRecordDataObjectRef::SetDeletionMark(deletionMark);
}

bool ibValueRecordDataObjectRecorderRef::DeleteObject()
{
	// Scaffold via Phase A Begin/CommitDeleteScope. Per-recorder middle:
	// BeforeDelete + register-set DeleteRecordSet (cascading off this
	// recorder) + OnDelete + DeleteData. The register clear runs
	// BEFORE OnDelete + DeleteData so the recorder row exists for any
	// script side-effects and so the cascade uses recorder-level
	// row-locks for serialization.
	ibConnectionScope scope = ibSession::Current()->OpenConnectionScope();
	if (!BeginDeleteScope(scope)) return true;

	ibBackendValueForm* const valueForm = GetForm();

	// Stage-named failures, as on the write path: a delete that stops has a reason and a place.
	{
		ibValue cancel = false;
		ExecAsProc(wxT("BeforeDelete"), cancel);
		if (cancel.GetBoolean()) {
			scope.SafeRollBackTransaction();
			ibBackendCoreException::Error(_("%s: deletion cancelled by the BeforeDelete handler"),
				GetSourceCaption());
			return false;
		}
	}

	if (!m_registerRecords->DeleteRecordSet()) {
		scope.SafeRollBackTransaction();
		ibBackendCoreException::Error(_("%s: failed to clear the register movements"),
			GetSourceCaption());
		return false;
	}

	{
		ibValue cancel = false;
		ExecAsProc(wxT("OnDelete"), cancel);
		if (cancel.GetBoolean()) {
			scope.SafeRollBackTransaction();
			ibBackendCoreException::Error(_("%s: deletion cancelled by the OnDelete handler"),
				GetSourceCaption());
			return false;
		}
	}

	if (!DeleteData()) {
		scope.SafeRollBackTransaction();
		ibBackendCoreException::Error(_("%s: failed to delete the object data"), GetSourceCaption());
		return false;
	}

	CommitDeleteScope(scope, valueForm);
	m_registerRecords->RefreshRecordSet();
	return true;
}

//***********************************************************************
//*						     metaData									*
//***********************************************************************




//***********************************************************************
//*                      Record key & set								*
//***********************************************************************

//////////////////////////////////////////////////////////////////////
//						  ibValueRecordKeyObject							//
//////////////////////////////////////////////////////////////////////

ibValueRecordKeyObject::ibValueRecordKeyObject(const ibValueMetaObjectRegisterData* metaObject) : ibValueDynamicMembers(ibValueTypes::TYPE_VALUE),
m_metaObject(metaObject)
{
	m_members.Bind(this, &ibValueRecordKeyObject::FillMembers);
}

ibValueRecordKeyObject::ibValueRecordKeyObject(const ibValueMetaObjectRegisterData* metaObject, const ibRowMetaValues& keyValues) : ibValueDynamicMembers(ibValueTypes::TYPE_VALUE),
m_metaObject(metaObject)
{
	m_members.Bind(this, &ibValueRecordKeyObject::FillMembers);
	// VERIFY completeness against the register's OWN dimensions: take each from the supplied row values, and FILL
	// a missing one with its typed-empty default (mirrors CreateUniqueKeyPair). So the caller can hand a WHOLE row
	// map — the key keeps only its dimensions and is always COMPLETE, whatever the row carried.
	if (metaObject != nullptr) {
		for (const auto* attr : metaObject->GetGenericDimensionArrayObject()) {
			const auto it = keyValues.find(attr->GetMetaID());
			m_keyValues.insert_or_assign(attr->GetMetaID(), it != keyValues.end() ? it->second : attr->CreateValue());
		}
	}
}

ibValueRecordKeyObject::~ibValueRecordKeyObject()
{
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

	InvalidateNames();

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

	InvalidateNames();

	//is Ok
	return true;
}

ibValueRecordManagerObject* ibValueRecordManagerObject::CopyRegisterValue()
{
	return m_metaObject->CreateRecordManagerObjectValue(this);
}

ibValueRecordManagerObject::ibValueRecordManagerObject(const ibValueMetaObjectRegisterData* metaObject, const ibUniqueKeyPair& uniqueKey) : ibValueDynamicMembers(ibValueTypes::TYPE_VALUE),
m_objGuid(uniqueKey),
m_metaObject(metaObject),
m_recordSet(m_metaObject->CreateRecordSetObjectValue(uniqueKey, false)), m_recordLine(nullptr)
{
}

ibValueRecordManagerObject::ibValueRecordManagerObject(const ibValueRecordManagerObject& source) : ibValueDynamicMembers(ibValueTypes::TYPE_VALUE),
m_objGuid(source.m_metaObject->CreateUniqueKeyPair()),
m_metaObject(source.m_metaObject),
m_recordSet(m_metaObject->CreateRecordSetObjectValue(source.m_recordSet, false)), m_recordLine(nullptr)
{
}

ibValueRecordManagerObject::~ibValueRecordManagerObject()
{
}

ibBackendValueForm* ibValueRecordManagerObject::GetForm() const
{
	if (!m_objGuid.isValid())
		return nullptr;
	if (m_recordSet->m_selected)
		return ibBackendValueForm::FindFormBySourceUniqueKey(m_objGuid);
	return nullptr;
}

bool ibValueRecordManagerObject::IsEmpty() const
{
	return m_recordSet->IsEmpty();
}

const ibSourceExplorer* ibValueRecordManagerObject::GetSourceExplorer() const
{
	m_sourceExplorer.Reset(
		wxT("Ref"), _("Ref"), m_metaObject->GetMetaID(), GetClassType(), false, false
	);

	for (const auto object : m_metaObject->GetGenericAttributeArrayObject()) {
		m_sourceExplorer.AppendColumn(object);
	}

	return &m_sourceExplorer;
}

void ibValueRecordManagerObject::Modify(bool mod)
{
	ibBackendValueForm* const foundedForm = ibBackendValueForm::FindFormBySourceUniqueKey(m_objGuid);
	if (foundedForm != nullptr)
		foundedForm->Modify(mod);
	m_recordSet->Modify(mod);
}

bool ibValueRecordManagerObject::IsModified() const
{
	return m_recordSet->IsModified();
}

// ibSourceDataObject hop gate — reads the id, then filters the pin by the field's LIVE declared type
// (FindAnyAttributeObjectByFilter -> GetTypeDesc): a composite field's UNDEFINED resolves to the pinned twin,
// a field retyped away from the pin does not. Empty type (attribute not found) skips the check.
bool ibValueRecordManagerObject::GetValueBySourceHop(const ibSourceHop& hop, ibValue& out) const
{
	const bool got = GetValueByMetaID(hop.m_id, out);
	const ibValueMetaObjectAttributeBase* attribute = GetMetaObject()->FindAnyAttributeObjectByFilter(hop.m_id);
	return ibValueReferenceDataObject::CoerceHopType(hop, out, attribute != nullptr ? attribute->GetTypeDesc() : ibTypeDescription(), GetSourceMetaData()) || got;
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
		m_recordLine = new ibValueRecordSetObject::ibValueRecordSetObjectRegisterReturnLine(
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
	for (const auto object : m_metaObject->GetGenericDimensionArrayObject()) {
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
		ibBackendAccessException::Error(wxString::Format(_("reading register '%s'"), m_metaObject->GetSynonym()));
		return false;
	}

	ibValueModuleManager* moduleManager = ibSession::EditModuleManagerFor(m_metaObject->GetMetaData());
	wxASSERT(moduleManager);

	ibRuntimeModuleDataObject::SetParent(moduleManager);
	BindContextVariable(thisObject, this);                   // contextual
	BindExportVariable(wxT("Filter"), m_recordSetKeyValue);  // exported — register filter/key

	try {
		Compile();
	}
	catch (const ibBackendException&) {
		if (!appData->DesignerMode())
			throw;
		return false;
	};

	if (source != nullptr) {
		for (long row = 0; row < source->GetRowCount(); row++) {
			ibComposerNode* node = source->GetViewData<ibComposerNode>(source->GetItem(row));
			wxASSERT(node);
			ibValueModelStorage::Append(new ibComposerNode(*node), false);
		}
	}

	if (!appData->DesignerMode()) {
		if (!newRecord) ReadData();
	}

	if (!appData->DesignerMode()) {
		wxASSERT(m_procUnit == nullptr);
		InitializeRuntime();
		// Descriptor parent cascades both compile and procUnit parents.
		ibRuntimeModuleDataObject::SetParent(moduleManager);
		Execute();
	}

	InvalidateNames();

	//is Ok
	return true;
}

///////////////////////////////////////////////////////////////////////////////////

ibValueRecordSetObject* ibValueRecordSetObject::CopyRegisterValue()
{
	return m_metaObject->CreateRecordSetObjectValue(this);
}

///////////////////////////////////////////////////////////////////////////////////

ibValueRecordSetObject::ibValueRecordSetObject(const ibValueMetaObjectRegisterData* metaObject, const ibUniqueKeyPair& uniqueKey) : ibValueModelStorage(),
ibRuntimeModuleDataObject(m_members, this),
m_objModified(false), m_selected(false),
m_keyValues(uniqueKey.IsOk() ? uniqueKey : metaObject->CreateUniqueKeyPair()), m_metaObject(metaObject),
m_recordColumnCollection(new ibValueRecordSetObjectRegisterColumnCollection(this)), m_recordSetKeyValue(new ibValueRecordSetObjectRegisterKeyValue(this))
{
}

ibValueRecordSetObject::ibValueRecordSetObject(const ibValueRecordSetObject& source) : ibValueModelStorage(),
ibRuntimeModuleDataObject(m_members, this),
m_objModified(true), m_selected(false),
m_keyValues(source.m_keyValues), m_metaObject(source.m_metaObject),
m_recordColumnCollection(new ibValueRecordSetObjectRegisterColumnCollection(this)), m_recordSetKeyValue(new ibValueRecordSetObjectRegisterKeyValue(this))
{
	for (long row = 0; row < source.GetRowCount(); row++) {
		ibComposerNode* node = source.GetViewData<ibComposerNode>(source.GetItem(row));
		wxASSERT(node);
		ibValueModelStorage::Append(new ibComposerNode(*node), false);
	}
}

ibValueRecordSetObject::~ibValueRecordSetObject()
{
}


//----------------------------------------------------------------------
// Phase B template-method Write/Delete for register-set leaves.
// Accumulation / Accounting / Information are byte-identical mod
// SaveData / DeleteData (virtual). The base owns this scaffold;
// subclasses inherit it verbatim and override only SaveData /
// DeleteData with their per-type UPSERT / DELETE SQL.
//----------------------------------------------------------------------

bool ibValueRecordSetObject::WriteRecordSet(bool replace, bool clearTable)
{
	ibConnectionScope scope = ibSession::Current()->OpenConnectionScope();
	if (!BeginRecordSetWriteScope(scope)) return true;

	{
		ibValue cancel = false;
		ExecAsProc(wxT("BeforeWrite"), cancel);
		if (cancel.GetBoolean()) {
			scope.SafeRollBackTransaction();
			ibBackendCoreException::Error(_("Register '%s': writing cancelled by the BeforeWrite handler"),
				m_metaObject->GetSynonym());
			return false;
		}
	}

	// SaveData already reports a failed fill check per line ("The %s is required on line %i");
	// this names the register whose rows could not be stored.
	if (!SaveData(replace, clearTable)) {
		scope.SafeRollBackTransaction();
		ibBackendCoreException::Error(_("Register '%s': failed to store the records"),
			m_metaObject->GetSynonym());
		return false;
	}

	{
		ibValue cancel = false;
		ExecAsProc(wxT("OnWrite"), cancel);
		if (cancel.GetBoolean()) {
			scope.SafeRollBackTransaction();
			ibBackendCoreException::Error(_("Register '%s': writing cancelled by the OnWrite handler"),
				m_metaObject->GetSynonym());
			return false;
		}
	}

	CommitRecordSetScope(scope);
	return true;
}

bool ibValueRecordSetObject::DeleteRecordSet()
{
	ibConnectionScope scope = ibSession::Current()->OpenConnectionScope();
	if (!BeginRecordSetDeleteScope(scope)) return true;

	{
		ibValue cancel = false;
		ExecAsProc(wxT("BeforeDelete"), cancel);
		if (cancel.GetBoolean()) {
			scope.SafeRollBackTransaction();
			ibBackendCoreException::Error(_("Register '%s': deletion cancelled by the BeforeDelete handler"),
				m_metaObject->GetSynonym());
			return false;
		}
	}

	if (!DeleteData()) {
		scope.SafeRollBackTransaction();
		ibBackendCoreException::Error(_("Register '%s': failed to delete the records"),
			m_metaObject->GetSynonym());
		return false;
	}

	{
		ibValue cancel = false;
		ExecAsProc(wxT("OnDelete"), cancel);
		if (cancel.GetBoolean()) {
			scope.SafeRollBackTransaction();
			ibBackendCoreException::Error(_("Register '%s': deletion cancelled by the OnDelete handler"),
				m_metaObject->GetSynonym());
			return false;
		}
	}

	CommitRecordSetScope(scope);
	return true;
}

bool ibValueRecordSetObject::GetAt(const ibValue& varKeyValue, ibValue& pvarValue)
{
	long index = varKeyValue.GetUInteger();
	if (index >= GetRowCount() && !appData->DesignerMode()) {
		ibBackendCoreException::Error(_("Array index out of bounds"));
		return false;
	}
	pvarValue = new ibValueRecordSetObjectRegisterReturnLine(this, GetItem(index));
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

bool ibValueRecordSetObject::LoadDataFromTable(ibValueModel* srcTable)
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

ibValueModel* ibValueRecordSetObject::SaveDataToTable() const
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
	valueTable->InvalidateNames();
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
		ibComposerNode* node = GetViewData<ibComposerNode>(item);
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

	ibComposerNode* node = GetViewData<ibComposerNode>(item);
	if (node == nullptr)
		return false;
	return node->GetValue(id, pvarMetaVal);
}

//////////////////////////////////////////////////////////////////////
//					ibValueRecordSetObjectRegisterColumnCollection				//
//////////////////////////////////////////////////////////////////////


ibValueRecordSetObject::ibValueRecordSetObjectRegisterColumnCollection::ibValueRecordSetObjectRegisterColumnCollection() :
	ibValueModelColumnCollection(),
	m_ownerTable(nullptr)
{
}

ibValueRecordSetObject::ibValueRecordSetObjectRegisterColumnCollection::ibValueRecordSetObjectRegisterColumnCollection(ibValueRecordSetObject* ownerTable) :
	ibValueModelColumnCollection(),
	m_ownerTable(ownerTable)
{
	const ibValueMetaObjectGenericData* metaObject = m_ownerTable->GetMetaObject();
	wxASSERT(metaObject);

	for (const auto object : metaObject->GetGenericAttributeArrayObject()) {
		m_listColumnInfo.insert_or_assign(object->GetMetaID(),
			new ibValueRecordSetRegisterColumnInfo(object));
	}
}

ibValueRecordSetObject::ibValueRecordSetObjectRegisterColumnCollection::~ibValueRecordSetObjectRegisterColumnCollection()
{
}

bool ibValueRecordSetObject::ibValueRecordSetObjectRegisterColumnCollection::SetAt(const ibValue& varKeyValue, const ibValue& varValue)// array index starts at 0
{
	return false;
}

bool ibValueRecordSetObject::ibValueRecordSetObjectRegisterColumnCollection::GetAt(const ibValue& varKeyValue, ibValue& pvarValue) // array index starts at 0
{
	unsigned int index = varKeyValue.GetUInteger();
	// `index` is unsigned, so `index < 0` was dead code, and && binds tighter than ||
	// — the condition already meant "out of range AND not in the designer". Spelled out;
	// the designer-mode exemption is preserved, not introduced (see docs/portability.md).
	if (index >= m_listColumnInfo.size() && !appData->DesignerMode()) {
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


ibValueRecordSetObject::ibValueRecordSetObjectRegisterReturnLine::ibValueRecordSetObjectRegisterReturnLine(ibValueRecordSetObject* ownerTable, const ibDataViewItem& line)
	: ibValueModelReturnLine(line), m_ownerTable(ownerTable)
{
	m_members.Bind(this, &ibValueRecordSetObjectRegisterReturnLine::FillMembers);
}

ibValueRecordSetObject::ibValueRecordSetObjectRegisterReturnLine::~ibValueRecordSetObjectRegisterReturnLine()
{
}

void ibValueRecordSetObject::ibValueRecordSetObjectRegisterReturnLine::FillMembers(ibMemberTable& helper) const
{
	const ibValueMetaObjectGenericData* metaObject = m_ownerTable->GetMetaObject();
	if (metaObject != nullptr) {
		wxString objectName;
		for (const auto object : metaObject->GetGenericAttributeArrayObject()) {
			if (object->IsDeleted())
				continue;
			if (!object->GetObjectNameAsString(objectName))
				continue;
			helper.AppendProp(
				objectName,
				object->GetMetaID()
			);
		}
	}
}

//////////////////////////////////////////////////////////////////////
//				       ibValueRecordSetObjectRegisterKeyValue					//
//////////////////////////////////////////////////////////////////////

ibValueRecordSetObject::ibValueRecordSetObjectRegisterKeyValue::ibValueRecordSetObjectRegisterKeyValue(ibValueRecordSetObject* recordSet) : ibValueDynamicMembers(ibValueTypes::TYPE_VALUE, true),
m_recordSet(recordSet)
{
	m_members.Bind(this, &ibValueRecordSetObjectRegisterKeyValue::FillMembers);
}

ibValueRecordSetObject::ibValueRecordSetObjectRegisterKeyValue::~ibValueRecordSetObjectRegisterKeyValue()
{
}

//////////////////////////////////////////////////////////////////////
//						ibValueRecordSetObjectRegisterKeyDescriptionValue		//
//////////////////////////////////////////////////////////////////////

ibValueRecordSetObject::ibValueRecordSetObjectRegisterKeyValue::ibValueRecordSetObjectRegisterKeyDescriptionValue::ibValueRecordSetObjectRegisterKeyDescriptionValue(ibValueRecordSetObject* recordSet, const ibMetaID& id) : ibValueDynamicMembers(ibValueTypes::TYPE_VALUE),
m_metaId(id), m_recordSet(recordSet)
{
	m_members.Bind(this, &ibValueRecordSetObjectRegisterKeyDescriptionValue::FillMembers);
}

ibValueRecordSetObject::ibValueRecordSetObjectRegisterKeyValue::ibValueRecordSetObjectRegisterKeyDescriptionValue::~ibValueRecordSetObjectRegisterKeyDescriptionValue()
{
}

//////////////////////////////////////////////////////////////////////////////////////

long ibValueRecordSetObject::AppendRow(unsigned int before)
{
	ibComposerNode* rowData = new ibComposerNode();

	const ibValueMetaObjectRegisterData* metaObject = GetMetaObject();
	wxASSERT(metaObject);
	const ibMetaData* metaData = metaObject->GetMetaData();
	for (const auto object : metaObject->GetGenericAttributeArrayObject()) {
		rowData->AppendTableValue(object->GetMetaID(), object->CreateValue());
	}

	if (before > 0)
		return ibValueModelStorage::Insert(rowData, before, !ibBackendException::IsEvalMode());

	return ibValueModelStorage::Append(rowData, !ibBackendException::IsEvalMode());
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
	const ibMetaID& id = m_members.GetPropData(lPropNum);
	if (id != wxNOT_FOUND)
		return SetValueByMetaID(id, varPropVal);
	return false;
}

bool ibValueRecordSetObject::ibValueRecordSetObjectRegisterReturnLine::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	const ibMetaID& id = m_members.GetPropData(lPropNum);
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
	const ibMetaID& id = m_members.GetPropData(lPropNum);
	if (id != wxNOT_FOUND) {
		pvarPropVal = new ibValueRecordSetObjectRegisterKeyDescriptionValue(m_recordSet, id);
		return true;
	}
	return false;
}

//****************************************************************************
//*                              Support methods                             *
//****************************************************************************

void ibValueRecordKeyObject::FillMembers(ibMemberTable& helper) const
{
	helper.AppendFunc(wxT("IsEmpty"), wxT("IsEmpty()"));
	helper.AppendFunc(wxT("Metadata"), wxT("Metadata()"));

	wxString objectName;

	//fill custom attributes
	for (const auto object : m_metaObject->GetGenericDimensionArrayObject()) {
		if (object->IsDeleted())
			continue;
		if (!object->GetObjectNameAsString(objectName))
			continue;
		helper.AppendProp(
			objectName,
			object->GetMetaID()
		);
	}
}

//////////////////////////////////////////////////////////////

void ibValueRecordSetObject::ibValueRecordSetObjectRegisterKeyValue::FillMembers(ibMemberTable& helper) const
{
	const ibValueMetaObjectRegisterData* metaObject = m_recordSet->GetMetaObject();
	if (metaObject != nullptr) {
		wxString objectName;
		for (const auto object : metaObject->GetGenericDimensionArrayObject()) {
			if (object->IsDeleted())
				continue;
			if (!object->GetObjectNameAsString(objectName))
				continue;
			helper.AppendProp(
				objectName,
				object->GetMetaID()
			);
		}
	}
}

//////////////////////////////////////////////////////////////

void ibValueRecordSetObject::ibValueRecordSetObjectRegisterKeyValue::ibValueRecordSetObjectRegisterKeyDescriptionValue::FillMembers(ibMemberTable& helper) const
{
	helper.AppendFunc(wxT("Set"), 1, wxT("Set(value: any)"));

	helper.AppendProp(wxT("Value"), m_metaId);
	helper.AppendProp(wxT("Use"));
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

SYSTEM_TYPE_REGISTER(ibValueRecordDataObjectRecorderRef::ibRecorderRegister, "RecordRegister", system_to_clsid("VL_RECR"));

SYSTEM_TYPE_REGISTER(ibValueRecordSetObject::ibValueRecordSetObjectRegisterColumnCollection, "RecordSetRegisterColumn", system_to_clsid("VL_RSCL"));
SYSTEM_TYPE_REGISTER(ibValueRecordSetObject::ibValueRecordSetObjectRegisterColumnCollection::ibValueRecordSetRegisterColumnInfo, "RecordSetRegisterColumnInfo", system_to_clsid("VL_RSCI"));

SYSTEM_TYPE_REGISTER(ibValueRecordSetObject::ibValueRecordSetObjectRegisterKeyValue, "RecordSetRegisterKey", system_to_clsid("VL_RSCK"));
SYSTEM_TYPE_REGISTER(ibValueRecordSetObject::ibValueRecordSetObjectRegisterKeyValue::ibValueRecordSetObjectRegisterKeyDescriptionValue, "RecordSetRegisterKeyDescription", system_to_clsid("VL_RDVL"));

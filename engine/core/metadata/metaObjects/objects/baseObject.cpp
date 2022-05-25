////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : base classes for catalogs, docs etc..  
////////////////////////////////////////////////////////////////////////////

#include "baseObject.h"
#include "metadata/metadata.h"
#include "common/srcExplorer.h"
#include "compiler/systemObjects.h"
#include "metadata/singleMetaTypes.h"
#include "utils/stringUtils.h"

//***********************************************************************
//*								 metadata                               * 
//***********************************************************************

wxIMPLEMENT_ABSTRACT_CLASS(IMetaObjectWrapperData, IMetaObject);
wxIMPLEMENT_ABSTRACT_CLASS(IMetaObjectRegisterData, IMetaObjectWrapperData);

wxIMPLEMENT_ABSTRACT_CLASS(IMetaObjectRecordData, IMetaObjectWrapperData);
wxIMPLEMENT_ABSTRACT_CLASS(IMetaObjectRecordDataRef, IMetaObjectRecordData);
wxIMPLEMENT_ABSTRACT_CLASS(IMetaObjectRecordDataMutableRef, IMetaObjectRecordDataRef);

//***********************************************************************
//*							IMetaObjectWrapperData				        *
//***********************************************************************

IMetaTypeObjectValueSingle* IMetaObjectWrapperData::GetTypeObject(eMetaObjectType refType)
{
	return m_metaData->GetTypeObject(this, refType);
}

IMetaObject* IMetaObjectWrapperData::FindMetaObjectByID(const meta_identifier_t& id)
{
	return m_metaData->GetMetaObject(id);
}

IMetaAttributeObject* IMetaObjectWrapperData::FindGenericAttribute(const meta_identifier_t& id) const
{
	for (auto metaObject : GetGenericAttributes()) {
		if (metaObject->GetMetaID() == id) {
			return dynamic_cast<IMetaAttributeObject*>(metaObject);
		}
	}

	return NULL;
}

CValueForm* IMetaObjectWrapperData::GetGenericForm(const wxString& formName, IValueFrame* ownerControl, const CUniqueKey& formGuid)
{
	if (!formName.IsEmpty()) {
		for (auto metaForm : GetGenericForms()) {
			if (StringUtils::CompareString(formName, metaForm->GetName())) {
				return metaForm->GenerateFormAndRun(ownerControl,
					NULL, formGuid
				);
			}
		}
	}

	CSystemObjects::Raise(_("Сommon form not found '") + formName + "'");
	return NULL;
}

CValueForm* IMetaObjectWrapperData::GetGenericForm(const meta_identifier_t& id, IValueFrame* ownerControl, const CUniqueKey& formGuid)
{
	CMetaFormObject* defList = NULL;

	for (auto metaForm : GetGenericForms()) {
		if (id == metaForm->GetMetaID()) {
			defList = metaForm; break;
		}
	}

	if (defList == NULL) {
		return NULL;
	}

	return GetGenericForm(defList->GetName(),
		ownerControl, formGuid
	);
}

//***********************************************************************
//*                           IMetaObjectRecordData					    *
//***********************************************************************
#include "compiler/methods.h"
#include "utils/fs/fs.h"

IMetaObjectRecordData::IMetaObjectRecordData() :IMetaObjectWrapperData() {}

IMetaObjectRecordData::~IMetaObjectRecordData() {}

bool IMetaObjectRecordData::OnLoadMetaObject(IMetadata* metaData)
{
	return IMetaObject::OnLoadMetaObject(metaData);
}

bool IMetaObjectRecordData::OnSaveMetaObject()
{
	return IMetaObject::OnSaveMetaObject();
}

bool IMetaObjectRecordData::OnDeleteMetaObject()
{
	return IMetaObject::OnDeleteMetaObject();
}

bool IMetaObjectRecordData::OnBeforeRunMetaObject(int flags)
{
	if (m_metaClsid != g_metaEnumerationCLSID) {
		registerObject();
	}

	registerManager();
	return IMetaObject::OnBeforeRunMetaObject(flags);
}

bool IMetaObjectRecordData::OnAfterCloseMetaObject()
{
	if (m_metaClsid != g_metaEnumerationCLSID) {
		unregisterObject();
	}

	unregisterManager();
	return IMetaObject::OnAfterCloseMetaObject();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////

CValueForm* IMetaObjectRecordData::GetObjectForm(const meta_identifier_t& id, IValueFrame* ownerControl, const CUniqueKey& formGuid)
{
	CMetaFormObject* defList = NULL;

	for (auto metaForm : GetObjectForms()) {
		if (id == metaForm->GetMetaID()) {
			defList = metaForm; break;
		}
	}

	return GetObjectForm(defList ? defList->GetName() : wxEmptyString,
		ownerControl, formGuid
	);
}

//***********************************************************************
//*								 ARRAY									*
//***********************************************************************

std::vector<IMetaAttributeObject*> IMetaObjectRecordData::GetGenericAttributes() const
{
	std::vector<IMetaAttributeObject*> attributes;

	for (auto attribute : GetObjectAttributes()) {
		attributes.push_back(attribute);
	}

	return attributes;
}

std::vector<CMetaFormObject*> IMetaObjectRecordData::GetGenericForms() const
{
	return GetObjectForms();
}

std::vector<IMetaAttributeObject*> IMetaObjectRecordData::GetObjectAttributes() const
{
	std::vector<IMetaAttributeObject*> attributes;
	for (auto metaObject : m_aMetaObjects) {
		if (metaObject->GetClsid() == g_metaAttributeCLSID) {
			attributes.push_back(dynamic_cast<CMetaAttributeObject*>(metaObject));
		}
	}
	return attributes;
}

std::vector<CMetaFormObject*> IMetaObjectRecordData::GetObjectForms() const
{
	std::vector<CMetaFormObject*> forms;
	for (auto metaObject : m_aMetaObjects) {
		if (metaObject->GetClsid() == g_metaFormCLSID) {
			forms.push_back(dynamic_cast<CMetaFormObject*>(metaObject));
		}
	}
	return forms;
}

std::vector<CMetaGridObject*> IMetaObjectRecordData::GetObjectTemplates() const
{
	std::vector<CMetaGridObject*> templates;
	for (auto metaObject : m_aMetaObjects) {
		if (metaObject->GetClsid() == g_metaTemplateCLSID) {
			templates.push_back(dynamic_cast<CMetaGridObject*>(metaObject));
		}
	}
	return templates;
}

std::vector<CMetaTableObject*> IMetaObjectRecordData::GetObjectTables() const
{
	std::vector<CMetaTableObject*> tables;
	for (auto metaObject : m_aMetaObjects) {
		if (metaObject->GetClsid() == g_metaTableCLSID) {
			tables.push_back(dynamic_cast<CMetaTableObject*>(metaObject));
		}
	}
	return tables;
}

#include "frontend/visualView/controls/form.h"

//***********************************************************************
//*						IMetaObjectRecordDataRef					    *
//***********************************************************************

IMetaObjectRecordDataRef::IMetaObjectRecordDataRef() : IMetaObjectRecordData()
{
	m_attributeReference = CMetaDefaultAttributeObject::CreateSpecialType("reference", _("Reference"), wxEmptyString, CValue::GetIDByVT(eValueTypes::TYPE_EMPTY));
	m_attributeReference->SetClsid(g_metaDefaultAttributeCLSID);

	//set child/parent
	m_attributeReference->SetParent(this);
	AddChild(m_attributeReference);
}

IMetaObjectRecordDataRef::~IMetaObjectRecordDataRef()
{
	wxDELETE(m_attributeReference);
}

//***************************************************************************
//*                       Save & load metadata                              *
//***************************************************************************

bool IMetaObjectRecordDataRef::LoadData(CMemoryReader& dataReader)
{
	//load default attributes:
	m_attributeReference->LoadMeta(dataReader);

	return IMetaObjectRecordData::LoadData(dataReader);
}

bool IMetaObjectRecordDataRef::SaveData(CMemoryWriter& dataWritter)
{
	//save default attributes:
	m_attributeReference->SaveMeta(dataWritter);

	//create or update table:
	return IMetaObjectRecordData::SaveData(dataWritter);
}

//***********************************************************************
//*                           read & save events                        *
//***********************************************************************

bool IMetaObjectRecordDataRef::OnCreateMetaObject(IMetadata* metaData)
{
	if (!IMetaObjectRecordData::OnCreateMetaObject(metaData))
		return false;

	return m_attributeReference->OnCreateMetaObject(metaData);
}

bool IMetaObjectRecordDataRef::OnLoadMetaObject(IMetadata* metaData)
{
	if (!m_attributeReference->OnLoadMetaObject(metaData))
		return false;

	return IMetaObjectRecordData::OnLoadMetaObject(metaData);
}

bool IMetaObjectRecordDataRef::OnSaveMetaObject()
{
	if (!m_attributeReference->OnSaveMetaObject())
		return false;

	return IMetaObjectRecordData::OnSaveMetaObject();
}

bool IMetaObjectRecordDataRef::OnDeleteMetaObject()
{
	if (!m_attributeReference->OnDeleteMetaObject())
		return false;

	return IMetaObjectRecordData::OnDeleteMetaObject();
}

bool IMetaObjectRecordDataRef::OnBeforeRunMetaObject(int flags)
{
	if (!m_attributeReference->OnBeforeRunMetaObject(flags))
		return false;

	registerReference();
	registerRefList();

	if (!IMetaObjectRecordData::OnBeforeRunMetaObject(flags))
		return false;

	IMetaTypeObjectValueSingle* singleObject = m_metaData->GetTypeObject(this, eMetaObjectType::enReference);

	if (singleObject != NULL) {
		m_attributeReference->SetDefaultMetatype(singleObject->GetClassType());
	}

	return true;
}

bool IMetaObjectRecordDataRef::OnAfterRunMetaObject(int flags)
{
	return IMetaObjectRecordData::OnAfterRunMetaObject(flags);
}

bool IMetaObjectRecordDataRef::OnBeforeCloseMetaObject()
{
	return IMetaObjectRecordData::OnBeforeCloseMetaObject();
}

bool IMetaObjectRecordDataRef::OnAfterCloseMetaObject()
{
	if (!m_attributeReference->OnAfterCloseMetaObject())
		return false;

	if (m_attributeReference != NULL) {
		m_attributeReference->SetDefaultMetatype(CValue::GetIDByVT(eValueTypes::TYPE_EMPTY));
	}

	unregisterReference();
	unregisteRefList();

	return IMetaObjectRecordData::OnAfterCloseMetaObject();
}

//***********************************************************************
//*                             form events                             *
//***********************************************************************

//process choice 
bool IMetaObjectRecordDataRef::ProcessChoice(IValueFrame* ownerValue, const meta_identifier_t& id) 
{
	CValueForm* selectForm = IMetaObjectRecordDataRef::GetSelectForm(id, ownerValue);

	if (selectForm == NULL)
		return false;

	selectForm->ShowForm();
	return true;
}

bool IMetaObjectRecordDataRef::ProcessListChoice(IValueFrame* ownerValue, const meta_identifier_t& id)
{
	return true;
}

IRecordDataObject* IMetaObjectRecordDataRef::CreateObjectValue()
{
	return CreateObjectRefValue();
}

CReferenceDataObject* IMetaObjectRecordDataRef::FindObjectValue(const Guid& guid)
{
	return CReferenceDataObject::Create(this, guid);
}

CValueForm* IMetaObjectRecordDataRef::GetListForm(const meta_identifier_t& id, IValueFrame* ownerControl, const CUniqueKey& formGuid)
{
	CMetaFormObject* defList = NULL;

	for (auto metaForm : GetObjectForms()) {
		if (id == metaForm->GetMetaID()) {
			defList = metaForm; break;
		}
	}

	return GetListForm(defList ? defList->GetName() : wxEmptyString,
		ownerControl, formGuid
	);
}

CValueForm* IMetaObjectRecordDataRef::GetSelectForm(const meta_identifier_t& id, IValueFrame* ownerControl, const CUniqueKey& formGuid)
{
	CMetaFormObject* defList = NULL;

	for (auto metaForm : GetObjectForms()) {
		if (id == metaForm->GetMetaID()) {
			defList = metaForm; break;
		}
	}

	return GetSelectForm(defList ? defList->GetName() : wxEmptyString,
		ownerControl, formGuid
	);
}

//***********************************************************************
//*                            ARRAY									*
//***********************************************************************

std::vector<IMetaAttributeObject*> IMetaObjectRecordDataRef::GetGenericAttributes() const
{
	std::vector<IMetaAttributeObject*> attributes;

	for (auto attribute : GetDefaultAttributes()) {
		attributes.push_back(attribute);
	}

	for (auto attribute : GetObjectAttributes()) {
		attributes.push_back(attribute);
	}

	return attributes;
}

std::vector<CMetaFormObject*> IMetaObjectRecordDataRef::GetGenericForms() const
{
	return GetObjectForms();
}

std::vector<CMetaEnumerationObject*> IMetaObjectRecordDataRef::GetObjectEnums() const
{
	std::vector<CMetaEnumerationObject*> enumerations;
	for (auto metaObject : m_aMetaObjects) {
		if (metaObject->GetClsid() == g_metaEnumCLSID) {
			enumerations.push_back(dynamic_cast<CMetaEnumerationObject*>(metaObject));
		}
	}
	return enumerations;
}

//***********************************************************************
//*						IMetaObjectRecordDataMutableRef					*
//***********************************************************************

IMetaObjectRecordDataMutableRef::IMetaObjectRecordDataMutableRef() : IMetaObjectRecordDataRef()
{
	m_attributeDeletionMark = CMetaDefaultAttributeObject::CreateBoolean("deletionMark", _("DeletionMark"), wxEmptyString);
	m_attributeDeletionMark->SetClsid(g_metaDefaultAttributeCLSID);

	//set child/parent
	m_attributeDeletionMark->SetParent(this);
	AddChild(m_attributeDeletionMark);
}

IMetaObjectRecordDataMutableRef::~IMetaObjectRecordDataMutableRef()
{
	wxDELETE(m_attributeDeletionMark);
}

//***************************************************************************
//*                       Save & load metadata                              *
//***************************************************************************

bool IMetaObjectRecordDataMutableRef::LoadData(CMemoryReader& dataReader)
{
	//load default attributes:
	m_attributeDeletionMark->LoadMeta(dataReader);

	return IMetaObjectRecordDataRef::LoadData(dataReader);
}

bool IMetaObjectRecordDataMutableRef::SaveData(CMemoryWriter& dataWritter)
{
	//save default attributes:
	m_attributeDeletionMark->SaveMeta(dataWritter);

	//create or update table:
	return IMetaObjectRecordDataRef::SaveData(dataWritter);
}

//***********************************************************************
//*                           read & save events                        *
//***********************************************************************

#include "appData.h"

bool IMetaObjectRecordDataMutableRef::OnCreateMetaObject(IMetadata* metaData)
{
	if (!IMetaObjectRecordDataRef::OnCreateMetaObject(metaData))
		return false;

	return m_attributeDeletionMark->OnCreateMetaObject(metaData);
}

bool IMetaObjectRecordDataMutableRef::OnLoadMetaObject(IMetadata* metaData)
{
	if (!m_attributeDeletionMark->OnLoadMetaObject(metaData))
		return false;

	return IMetaObjectRecordDataRef::OnLoadMetaObject(metaData);
}

bool IMetaObjectRecordDataMutableRef::OnSaveMetaObject()
{
	if (!m_attributeDeletionMark->OnSaveMetaObject())
		return false;

	return IMetaObjectRecordDataRef::OnSaveMetaObject();
}

bool IMetaObjectRecordDataMutableRef::OnDeleteMetaObject()
{
	if (!m_attributeDeletionMark->OnDeleteMetaObject())
		return false;

	return IMetaObjectRecordDataRef::OnDeleteMetaObject();
}

bool IMetaObjectRecordDataMutableRef::OnBeforeRunMetaObject(int flags)
{
	if (!m_attributeDeletionMark->OnBeforeRunMetaObject(flags))
		return false;

	return IMetaObjectRecordDataRef::OnBeforeRunMetaObject(flags);
}

bool IMetaObjectRecordDataMutableRef::OnAfterRunMetaObject(int flags)
{
	return IMetaObjectRecordDataRef::OnAfterRunMetaObject(flags);
}

bool IMetaObjectRecordDataMutableRef::OnBeforeCloseMetaObject()
{
	return IMetaObjectRecordDataRef::OnBeforeCloseMetaObject();
}

bool IMetaObjectRecordDataMutableRef::OnAfterCloseMetaObject()
{
	if (!m_attributeDeletionMark->OnAfterCloseMetaObject())
		return false;

	return IMetaObjectRecordDataRef::OnAfterCloseMetaObject();
}

//***********************************************************************
//*                      IMetaObjectRegisterData						*
//***********************************************************************

IMetaObjectRegisterData::IMetaObjectRegisterData() : IMetaObjectWrapperData()
{
	//create default attributes
	m_attributeLineActive = CMetaDefaultAttributeObject::CreateBoolean(wxT("active"), _("Active"), wxEmptyString, false, true);
	m_attributeLineActive->SetClsid(g_metaDefaultAttributeCLSID);
	m_attributePeriod = CMetaDefaultAttributeObject::CreateDate(wxT("period"), _("Period"), wxEmptyString, eDateFractions::eDateFractions_DateTime, true);
	m_attributePeriod->SetClsid(g_metaDefaultAttributeCLSID);
	m_attributeRecorder = CMetaDefaultAttributeObject::CreateEmptyType(wxT("recorder"), _("Recorder"), wxEmptyString);
	m_attributeRecorder->SetClsid(g_metaDefaultAttributeCLSID);
	m_attributeLineNumber = CMetaDefaultAttributeObject::CreateNumber(wxT("lineNumber"), _("Line number"), wxEmptyString, 15, 0);
	m_attributeLineNumber->SetClsid(g_metaDefaultAttributeCLSID);

	//set child/parent
	m_attributeLineActive->SetParent(this);
	AddChild(m_attributeLineActive);
	m_attributePeriod->SetParent(this);
	AddChild(m_attributePeriod);
	m_attributeRecorder->SetParent(this);
	AddChild(m_attributeRecorder);
	m_attributeLineNumber->SetParent(this);
	AddChild(m_attributeLineNumber);
}

IMetaObjectRegisterData::~IMetaObjectRegisterData()
{
	wxDELETE(m_attributeLineActive);
	wxDELETE(m_attributePeriod);
	wxDELETE(m_attributeRecorder);
	wxDELETE(m_attributeLineNumber);
}

//***************************************************************************
//*                       Save & load metadata                              *
//*************23121994qwe@M_	**************************************************************

bool IMetaObjectRegisterData::LoadData(CMemoryReader& dataReader)
{
	//load default attributes:
	m_attributeLineActive->LoadMeta(dataReader);
	m_attributePeriod->LoadMeta(dataReader);
	m_attributeRecorder->LoadMeta(dataReader);
	m_attributeLineNumber->LoadMeta(dataReader);

	return IMetaObjectWrapperData::LoadData(dataReader);
}

bool IMetaObjectRegisterData::SaveData(CMemoryWriter& dataWritter)
{
	//save default attributes:
	m_attributeLineActive->SaveMeta(dataWritter);
	m_attributePeriod->SaveMeta(dataWritter);
	m_attributeRecorder->SaveMeta(dataWritter);
	m_attributeLineNumber->SaveMeta(dataWritter);

	//create or update table:
	return IMetaObjectWrapperData::SaveData(dataWritter);
}

//***********************************************************************
//*                           read & save events                        *
//***********************************************************************

bool IMetaObjectRegisterData::OnCreateMetaObject(IMetadata* metaData)
{
	if (!IMetaObject::OnCreateMetaObject(metaData))
		return false;

	return m_attributeLineActive->OnCreateMetaObject(metaData) &&
		m_attributePeriod->OnCreateMetaObject(metaData) &&
		m_attributeRecorder->OnCreateMetaObject(metaData) &&
		m_attributeLineNumber->OnCreateMetaObject(metaData);
}

bool IMetaObjectRegisterData::OnLoadMetaObject(IMetadata* metaData)
{
	if (!m_attributeLineActive->OnLoadMetaObject(metaData))
		return false;

	if (!m_attributePeriod->OnLoadMetaObject(metaData))
		return false;

	if (!m_attributeRecorder->OnLoadMetaObject(metaData))
		return false;

	if (!m_attributeLineNumber->OnLoadMetaObject(metaData))
		return false;

	return IMetaObject::OnLoadMetaObject(metaData);
}

bool IMetaObjectRegisterData::OnSaveMetaObject()
{
	if (!m_attributeLineActive->OnSaveMetaObject())
		return false;

	if (!m_attributePeriod->OnSaveMetaObject())
		return false;

	if (!m_attributeRecorder->OnSaveMetaObject())
		return false;

	if (!m_attributeLineNumber->OnSaveMetaObject())
		return false;

	return IMetaObject::OnSaveMetaObject();
}

bool IMetaObjectRegisterData::OnDeleteMetaObject()
{
	if (!m_attributeLineActive->OnDeleteMetaObject())
		return false;

	if (!m_attributePeriod->OnDeleteMetaObject())
		return false;

	if (!m_attributeRecorder->OnDeleteMetaObject())
		return false;

	if (!m_attributeLineNumber->OnDeleteMetaObject())
		return false;

	return IMetaObject::OnDeleteMetaObject();
}

#include "metadata/singleMetaTypes.h"

bool IMetaObjectRegisterData::OnBeforeRunMetaObject(int flags)
{
	if (!m_attributeLineActive->OnBeforeRunMetaObject(flags))
		return false;

	if (!m_attributePeriod->OnBeforeRunMetaObject(flags))
		return false;

	if (!m_attributeRecorder->OnBeforeRunMetaObject(flags))
		return false;

	if (!m_attributeLineNumber->OnBeforeRunMetaObject(flags))
		return false;

	registerManager();
	registerRegList();
	registerRecordKey();
	registerRecordSet();

	if (HasRecordManager()) {
		registerRecordManager();
	}

	return IMetaObject::OnBeforeRunMetaObject(flags);
}

bool IMetaObjectRegisterData::OnAfterCloseMetaObject()
{
	if (!m_attributeLineActive->OnAfterCloseMetaObject())
		return false;

	if (!m_attributePeriod->OnAfterCloseMetaObject())
		return false;

	if (!m_attributeRecorder->OnAfterCloseMetaObject())
		return false;

	if (!m_attributeLineNumber->OnAfterCloseMetaObject())
		return false;

	unregisterManager();
	unregisterRegList();
	unregisterRecordKey();
	unregisterRecordSet();

	if (HasRecordManager()) {
		unregisterRecordManager();
	}

	return IMetaObject::OnAfterCloseMetaObject();
}

CValueForm* IMetaObjectRegisterData::GetListForm(const meta_identifier_t& id, IValueFrame* ownerControl, const CUniqueKey& formGuid)
{
	CMetaFormObject* defList = NULL;

	for (auto metaForm : GetObjectForms()) {
		if (id == metaForm->GetMetaID()) {
			defList = metaForm; break;
		}
	}

	return GetListForm(defList ? defList->GetName() : wxEmptyString,
		ownerControl, formGuid
	);
}

//***********************************************************************
//*								ARRAY									*
//***********************************************************************

std::vector<IMetaAttributeObject*> IMetaObjectRegisterData::GetGenericAttributes() const
{
	std::vector<IMetaAttributeObject*> attributes;

	for (auto attribute : GetDefaultAttributes()) {
		attributes.push_back(attribute);
	}

	for (auto dimension : GetObjectDimensions()) {
		attributes.push_back(dimension);
	}

	for (auto resource : GetObjectResources()) {
		attributes.push_back(resource);
	}

	for (auto attribute : GetObjectAttributes()) {
		attributes.push_back(attribute);
	}

	return attributes;
}

std::vector<CMetaFormObject*> IMetaObjectRegisterData::GetGenericForms() const
{
	return GetObjectForms();
}

std::vector<IMetaAttributeObject*> IMetaObjectRegisterData::GetObjectDimensions() const
{
	std::vector<IMetaAttributeObject*> attributes;
	for (auto metaObject : m_aMetaObjects) {
		if (metaObject->GetClsid() == g_metaDimensionCLSID) {
			attributes.push_back(dynamic_cast<IMetaAttributeObject*>(metaObject));
		}
	}
	return attributes;
}

std::vector<IMetaAttributeObject*> IMetaObjectRegisterData::GetObjectResources() const
{
	std::vector<IMetaAttributeObject*> resources;
	for (auto metaObject : m_aMetaObjects) {
		if (metaObject->GetClsid() == g_metaResourceCLSID) {
			resources.push_back(dynamic_cast<IMetaAttributeObject*>(metaObject));
		}
	}
	return resources;
}

std::vector<IMetaAttributeObject*> IMetaObjectRegisterData::GetObjectAttributes() const
{
	std::vector<IMetaAttributeObject*> attributes;
	for (auto metaObject : m_aMetaObjects) {
		if (metaObject->GetClsid() == g_metaAttributeCLSID) {
			attributes.push_back(dynamic_cast<IMetaAttributeObject*>(metaObject));
		}
	}
	return attributes;
}

std::vector<CMetaFormObject*> IMetaObjectRegisterData::GetObjectForms() const
{
	std::vector<CMetaFormObject*> forms;
	for (auto metaObject : m_aMetaObjects) {
		if (metaObject->GetClsid() == g_metaFormCLSID) {
			forms.push_back(dynamic_cast<CMetaFormObject*>(metaObject));
		}
	}
	return forms;
}

std::vector<CMetaGridObject*> IMetaObjectRegisterData::GetObjectTemplates() const
{
	std::vector<CMetaGridObject*> templates;
	for (auto metaObject : m_aMetaObjects) {
		if (metaObject->GetClsid() == g_metaTemplateCLSID) {
			templates.push_back(dynamic_cast<CMetaGridObject*>(metaObject));
		}
	}
	return templates;
}

///////////////////////////////////////////////////////////////////////

IMetaAttributeObject* IMetaObjectRegisterData::FindAttribute(const meta_identifier_t& id) const
{
	for (auto metaObject : m_aMetaObjects) {
		if (
			metaObject->GetClsid() == g_metaDefaultAttributeCLSID ||
			metaObject->GetClsid() == g_metaAttributeCLSID ||
			metaObject->GetClsid() == g_metaDimensionCLSID ||
			metaObject->GetClsid() == g_metaResourceCLSID ||

			metaObject->GetMetaID() == id) {
			return dynamic_cast<IMetaAttributeObject*>(metaObject);
		}
	}

	return NULL;
}

//***********************************************************************
//*                        ISourceDataObject							*
//***********************************************************************

#include "frontend/visualView/controls/form.h"

//***********************************************************************
//*                        IRecordDataObject							*
//***********************************************************************

wxIMPLEMENT_ABSTRACT_CLASS(IRecordDataObject, CValue);

IRecordDataObject::IRecordDataObject(const Guid& guid, bool newObject) : CValue(eValueTypes::TYPE_VALUE),
ISourceDataObject(), IObjectValueInfo(guid, newObject), IModuleInfo(),
m_methods(new CMethods())
{
}

IRecordDataObject::IRecordDataObject(const IRecordDataObject& source) : CValue(eValueTypes::TYPE_VALUE),
ISourceDataObject(source), IObjectValueInfo(Guid::newGuid(), true), IModuleInfo(),
m_methods(new CMethods())
{
}

IRecordDataObject::~IRecordDataObject()
{
	wxDELETE(m_methods);
}

CValueForm* IRecordDataObject::GetForm() const
{
	return CValueForm::FindFormByGuid(m_objGuid);
}

CLASS_ID IRecordDataObject::GetClassType() const
{
	IMetaObjectRecordData* metaObject = GetMetaObject();
	wxASSERT(metaObject);
	IMetaTypeObjectValueSingle* clsFactory =
		metaObject->GetTypeObject(eMetaObjectType::enObject);
	wxASSERT(clsFactory);
	return clsFactory->GetClassType();
}

wxString IRecordDataObject::GetTypeString() const
{
	IMetaObjectRecordData* metaObject = GetMetaObject();
	wxASSERT(metaObject);
	IMetaTypeObjectValueSingle* clsFactory =
		metaObject->GetTypeObject(eMetaObjectType::enObject);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

wxString IRecordDataObject::GetString() const
{
	IMetaObjectRecordData* metaObject = GetMetaObject();
	wxASSERT(metaObject);
	IMetaTypeObjectValueSingle* clsFactory =
		metaObject->GetTypeObject(eMetaObjectType::enObject);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

CSourceExplorer IRecordDataObject::GetSourceExplorer() const
{
	IMetaObjectRecordData* metaObject = GetMetaObject();

	CSourceExplorer srcHelper(
		metaObject, GetClassType(),
		false
	);

	for (auto attribute : metaObject->GetGenericAttributes()) {
		srcHelper.AppendSource(attribute);
	}

	for (auto table : metaObject->GetObjectTables()) {
		srcHelper.AppendSource(table);
	}

	return srcHelper;
}

#include "metadata/metaObjects/objects/tabularSection/tabularSection.h"

bool IRecordDataObject::GetTable(IValueTable*& tableValue, const meta_identifier_t& id)
{
	auto foundedIt = std::find_if(m_aObjectTables.begin(), m_aObjectTables.end(), [id](ITabularSectionDataObject* vts) {
		CMetaTableObject* metaObject = vts->GetMetaObject();
		wxASSERT(metaObject);
		return id == metaObject->GetMetaID();
		});

	if (foundedIt == m_aObjectTables.end())
		return false;

	tableValue = *foundedIt;
	return true;
}

#include "compiler/valueTypeDescription.h"

void IRecordDataObject::SetValueByMetaID(const meta_identifier_t& id, const CValue& cVal)
{
	auto foundedIt = m_aObjectValues.find(id);
	wxASSERT(foundedIt != m_aObjectValues.end());
	if (foundedIt != m_aObjectValues.end()) {
		IMetaObjectRecordData* metaObjectValue = GetMetaObject();
		wxASSERT(metaObjectValue);
		IMetaAttributeObject* metaAttribute = wxDynamicCast(
			metaObjectValue->FindMetaObjectByID(id), IMetaAttributeObject
		);
		wxASSERT(metaAttribute);
		foundedIt->second = metaAttribute->AdjustValue(cVal);
	}
}

CValue IRecordDataObject::GetValueByMetaID(const meta_identifier_t& id) const
{
	auto foundedIt = m_aObjectValues.find(id);
	wxASSERT(foundedIt != m_aObjectValues.end());
	if (foundedIt != m_aObjectValues.end()) {
		return foundedIt->second;
	}

	return CValue();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////

void IRecordDataObject::PrepareEmptyObject()
{
	IMetaObjectRecordData* metaObject = GetMetaObject();
	wxASSERT(metaObject);
	//attrbutes can refValue 
	for (auto attribute : metaObject->GetGenericAttributes()) {
		m_aObjectValues[attribute->GetMetaID()] = attribute->CreateValue();
	}

	// table is collection values 
	for (auto table : metaObject->GetObjectTables()) {
		CTabularSectionDataObject* tableSection = new CTabularSectionDataObject(this, table);
		m_aObjectValues[table->GetMetaID()] = tableSection;
		m_aObjectTables.push_back(tableSection);
	}
}

//***********************************************************************
//*                        IRecordDataObjectRef							*           
//***********************************************************************

wxIMPLEMENT_ABSTRACT_CLASS(IRecordDataObjectRef, IRecordDataObject);

IRecordDataObjectRef::IRecordDataObjectRef() :
	IRecordDataObject(Guid::newGuid()), m_metaObject(NULL), m_reference_impl(NULL), m_codeGenerator(NULL), m_objModified(false)
{
}

IRecordDataObjectRef::IRecordDataObjectRef(IMetaObjectRecordDataRef* metaObject) :
	IRecordDataObject(Guid::newGuid(), true), m_metaObject(metaObject), m_reference_impl(NULL), m_codeGenerator(NULL), m_objModified(false)
{
	IMetaAttributeObject* attributeCode = m_metaObject->GetAttributeForCode();
	if (attributeCode != NULL) {
		m_codeGenerator = new CCodeGenerator(
			m_metaObject, attributeCode
		);
	}
	m_reference_impl = new reference_t(m_metaObject->GetMetaID(), m_objGuid);
}

IRecordDataObjectRef::IRecordDataObjectRef(IMetaObjectRecordDataRef* metaObject, const Guid& objGuid) :
	IRecordDataObject(objGuid, false), m_metaObject(metaObject), m_reference_impl(NULL), m_codeGenerator(NULL), m_objModified(false)
{
	IMetaAttributeObject* attributeCode = m_metaObject->GetAttributeForCode();
	if (attributeCode != NULL) {
		m_codeGenerator = new CCodeGenerator(
			m_metaObject, attributeCode
		);
	}
	m_reference_impl = new reference_t(m_metaObject->GetMetaID(), m_objGuid);
}

IRecordDataObjectRef::IRecordDataObjectRef(const IRecordDataObjectRef& source) :
	IRecordDataObject(source), m_metaObject(source.m_metaObject), m_reference_impl(NULL), m_codeGenerator(NULL), m_objModified(false)
{
	IMetaAttributeObject* attributeCode = m_metaObject->GetAttributeForCode();
	if (attributeCode != NULL) {
		m_codeGenerator = new CCodeGenerator(
			m_metaObject, attributeCode
		);
	}
	m_reference_impl = new reference_t(m_metaObject->GetMetaID(), m_objGuid);
}

IRecordDataObjectRef::~IRecordDataObjectRef()
{
	wxDELETE(m_codeGenerator);
	wxDELETE(m_reference_impl);
}

#include "appData.h"

bool IRecordDataObjectRef::InitializeObject(const IRecordDataObject* source)
{
	IMetadata* metaData = m_metaObject->GetMetadata();
	wxASSERT(metaData);
	IModuleManager* moduleManager = metaData->GetModuleManager();
	wxASSERT(moduleManager);

	if (m_compileModule == NULL) {
		m_compileModule = new CCompileModule(m_metaObject->GetModuleObject());
		m_compileModule->SetParent(moduleManager->GetCompileModule());
		m_compileModule->AddContextVariable(thisObject, this);
	}

	if (appData->EnterpriseMode()) {
		try
		{
			m_compileModule->Compile();
		}
		catch (const CTranslateError* err)
		{
			if (appData->EnterpriseMode()) {
				CSystemObjects::Raise(err->what());
			}

			return false;
		};
	}

	m_aObjectValues.clear();
	m_aObjectTables.clear();

	if (appData->DesignerMode() ||
		(appData->EnterpriseMode() && !ReadData())) {
		if (source != NULL) {
			PrepareEmptyObject(source);
		}
		else {
			PrepareEmptyObject();
		}
	}

	if (appData->EnterpriseMode()) {

		wxASSERT(m_procUnit == NULL);

		m_procUnit = new CProcUnit();
		m_procUnit->SetParent(moduleManager->GetProcUnit());

		m_procUnit->Execute(m_compileModule->m_cByteCode, true);

		if (m_newObject && source != NULL) {
			m_procUnit->CallFunction("OnCopy", CValue((CValue*)source));
		}

		if (m_newObject) {
			m_procUnit->CallFunction("Filling");
		}
	}

	//is Ok
	return true;
}

CLASS_ID IRecordDataObjectRef::GetClassType() const
{
	return IRecordDataObject::GetClassType();
}

wxString IRecordDataObjectRef::GetTypeString() const
{
	return IRecordDataObject::GetTypeString();
}

wxString IRecordDataObjectRef::GetString() const
{
	return m_metaObject->GetDescription(this);
}

CSourceExplorer IRecordDataObjectRef::GetSourceExplorer() const
{
	CSourceExplorer srcHelper(
		m_metaObject, GetClassType(),
		false
	);

	IMetaAttributeObject* metaAttribute = m_metaObject->GetAttributeForCode();

	for (auto attribute : m_metaObject->GetGenericAttributes()) {
		if (!m_metaObject->IsDataReference(attribute->GetMetaID())) {
			srcHelper.AppendSource(attribute, attribute != metaAttribute);
		}
	}

	for (auto table : m_metaObject->GetObjectTables()) {
		srcHelper.AppendSource(table);
	}

	return srcHelper;
}

bool IRecordDataObjectRef::GetTable(IValueTable*& tableValue, const meta_identifier_t& id)
{
	auto foundedIt = std::find_if(m_aObjectTables.begin(), m_aObjectTables.end(), [id](ITabularSectionDataObject* vts) {
		CMetaTableObject* metaObject = vts->GetMetaObject();
		wxASSERT(metaObject);
		return id == metaObject->GetMetaID();
		});

	if (foundedIt == m_aObjectTables.end())
		return false;

	tableValue = *foundedIt;
	return true;
}

void IRecordDataObjectRef::Modify(bool mod)
{
	CValueForm* foundedForm = CValueForm::FindFormByGuid(m_objGuid);

	if (foundedForm != NULL) {
		foundedForm->Modify(mod);
	}

	m_objModified = mod;
}

void IRecordDataObjectRef::SetValueByMetaID(const meta_identifier_t& id, const CValue& cVal)
{
	IRecordDataObject::SetValueByMetaID(id, cVal);
	IRecordDataObjectRef::Modify(true);
}

CValue IRecordDataObjectRef::GetValueByMetaID(const meta_identifier_t& id) const
{
	return IRecordDataObject::GetValueByMetaID(id);
}

void IRecordDataObjectRef::PrepareEmptyObject()
{
	//attrbutes can refValue 
	for (auto attribute : m_metaObject->GetGenericAttributes()) {
		m_aObjectValues[attribute->GetMetaID()] = attribute->CreateValue();
	}

	// table is collection values 
	for (auto table : m_metaObject->GetObjectTables()) {
		CTabularSectionDataObjectRef* tableSection = new CTabularSectionDataObjectRef(this, table);
		m_aObjectValues[table->GetMetaID()] = tableSection;
		m_aObjectTables.push_back(tableSection);
	}

	m_objModified = true;
}

void IRecordDataObjectRef::PrepareEmptyObject(const IRecordDataObject* source)
{
	IMetaAttributeObject* codeAttribute = m_metaObject->GetAttributeForCode();
	wxASSERT(codeAttribute);
	m_aObjectValues[codeAttribute->GetMetaID()] = codeAttribute->CreateValue();

	//attributes can refValue 
	for (auto attribute : m_metaObject->GetGenericAttributes()) {
		if (attribute != codeAttribute) {
			m_aObjectValues[attribute->GetMetaID()] = source->GetValueByMetaID(attribute->GetMetaID());
		}
	}

	// table is collection values 
	for (auto table : m_metaObject->GetObjectTables()) {
		CTabularSectionDataObjectRef* tableSection = new CTabularSectionDataObjectRef(this, table);
		tableSection->LoadDataFromTable(source->GetTableByMetaID(table->GetMetaID()));
		m_aObjectValues[table->GetMetaID()] = tableSection;
		m_aObjectTables.push_back(tableSection);
	}

	m_objModified = true;
}

CReferenceDataObject* IRecordDataObjectRef::GetReference() const
{
	if (m_newObject) {
		return CReferenceDataObject::Create(m_metaObject);
	}

	return CReferenceDataObject::Create(m_metaObject, m_objGuid);
}

//***********************************************************************
//*						     metadata									* 
//***********************************************************************

wxIMPLEMENT_ABSTRACT_CLASS(IRecordSetObject, IValueTable);
wxIMPLEMENT_ABSTRACT_CLASS(IRecordManagerObject, CValue);

wxIMPLEMENT_ABSTRACT_CLASS(CRecordKeyObject, CValue);

wxIMPLEMENT_DYNAMIC_CLASS(IRecordSetObject::CRecordSetRegisterKeyValue, CValue);
wxIMPLEMENT_DYNAMIC_CLASS(IRecordSetObject::CRecordSetRegisterKeyValue::CRecordSetRegisterKeyDescriptionValue, CValue);

//***********************************************************************
//*                      Record key & set								*
//***********************************************************************

//////////////////////////////////////////////////////////////////////
//						  CRecordKeyObject							//
//////////////////////////////////////////////////////////////////////

CRecordKeyObject::CRecordKeyObject(IMetaObjectRegisterData* metaObject) : CValue(eValueTypes::TYPE_VALUE),
m_metaObject(metaObject), m_methods(new CMethods())
{
}

CRecordKeyObject::~CRecordKeyObject()
{
	wxDELETE(m_methods);
}

bool CRecordKeyObject::IsEmpty() const
{
	for (auto value : m_aKeyValues) {
		const CValue& cValue = value.second;
		if (!cValue.IsEmpty())
			return false;
	}

	return true;
}

CLASS_ID CRecordKeyObject::GetClassType() const
{
	IMetaTypeObjectValueSingle* clsFactory =
		m_metaObject->GetTypeObject(eMetaObjectType::enRecordKey);
	wxASSERT(clsFactory);
	return clsFactory->GetClassType();
}

wxString CRecordKeyObject::GetTypeString() const
{
	IMetaTypeObjectValueSingle* clsFactory =
		m_metaObject->GetTypeObject(eMetaObjectType::enRecordKey);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

wxString CRecordKeyObject::GetString() const
{
	IMetaTypeObjectValueSingle* clsFactory =
		m_metaObject->GetTypeObject(eMetaObjectType::enRecordKey);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

//////////////////////////////////////////////////////////////////////
//						  IRecordManagerObject						//
//////////////////////////////////////////////////////////////////////

void IRecordManagerObject::CreateEmptyKey()
{
	m_recordSet->CreateEmptyKey();
}

bool IRecordManagerObject::InitializeObject(const IRecordManagerObject* source, bool newRecord)
{
	if (!appData->DesignerMode()) {
		if (!newRecord && !ReadData()) {
			PrepareEmptyObject(source);
		}
		else if (newRecord) {
			PrepareEmptyObject(source);
		}
	}
	else {
		PrepareEmptyObject(source);
	}

	//is Ok
	return true;
}

IRecordManagerObject::IRecordManagerObject(IMetaObjectRegisterData* metaObject) : CValue(eValueTypes::TYPE_VALUE),
m_metaObject(metaObject), m_methods(new CMethods()), m_recordSet(m_metaObject->CreateRecordSet()), m_recordLine(NULL),
m_objGuid(metaObject)
{
	InitializeObject(NULL, true);
}

IRecordManagerObject::IRecordManagerObject(IMetaObjectRegisterData* metaObject, const CUniquePairKey& uniqueKey) : CValue(eValueTypes::TYPE_VALUE),
m_metaObject(metaObject), m_methods(new CMethods()), m_recordSet(m_metaObject->CreateRecordSet(uniqueKey)), m_recordLine(NULL),
m_objGuid(uniqueKey)
{
	InitializeObject(NULL, false);
}

IRecordManagerObject::IRecordManagerObject(const IRecordManagerObject& source) : CValue(eValueTypes::TYPE_VALUE),
m_metaObject(source.m_metaObject), m_methods(new CMethods()), m_recordSet(source.m_recordSet->CopyRegisterValue()), m_recordLine(NULL),
m_objGuid(source.m_metaObject)
{
	InitializeObject(&source, true);
}

IRecordManagerObject::~IRecordManagerObject()
{
	wxDELETE(m_recordSet);
	wxDELETE(m_methods);
}

CValueForm* IRecordManagerObject::GetForm() const
{
	return CValueForm::FindFormByGuid(m_objGuid);
}

bool IRecordManagerObject::IsEmpty() const
{
	return m_recordSet->IsEmpty();
}

CSourceExplorer IRecordManagerObject::GetSourceExplorer() const
{
	CSourceExplorer srcHelper(
		m_metaObject, GetClassType(),
		false
	);

	for (auto attribute : m_metaObject->GetGenericAttributes()) {
		srcHelper.AppendSource(attribute);
	}

	return srcHelper;
}

bool IRecordManagerObject::GetTable(IValueTable*& tableValue, const meta_identifier_t& id)
{
	return false;
}

void IRecordManagerObject::Modify(bool mod)
{
	CValueForm* foundedForm = CValueForm::FindFormByGuid(m_objGuid);

	if (foundedForm != NULL) {
		foundedForm->Modify(mod);
	}

	m_recordSet->Modify(mod);
}

bool IRecordManagerObject::IsModified() const
{
	return m_recordSet->IsModified();
}

void IRecordManagerObject::SetValueByMetaID(const meta_identifier_t& id, const CValue& cVal)
{
	m_recordLine->SetValueByMetaID(id, cVal);
	IRecordManagerObject::Modify(true);
}

CValue IRecordManagerObject::GetValueByMetaID(const meta_identifier_t& id) const
{
	return m_recordLine->GetValueByMetaID(id);
}

CLASS_ID IRecordManagerObject::GetClassType() const
{
	IMetaTypeObjectValueSingle* clsFactory =
		m_metaObject->GetTypeObject(eMetaObjectType::enRecordManager);
	wxASSERT(clsFactory);
	return clsFactory->GetClassType();
}

wxString IRecordManagerObject::GetTypeString() const
{
	IMetaTypeObjectValueSingle* clsFactory =
		m_metaObject->GetTypeObject(eMetaObjectType::enRecordManager);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

wxString IRecordManagerObject::GetString() const
{
	IMetaTypeObjectValueSingle* clsFactory =
		m_metaObject->GetTypeObject(eMetaObjectType::enRecordManager);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

/////////////////////////////////////////////////////////////////////

void IRecordManagerObject::PrepareEmptyObject(const IRecordManagerObject* source)
{
	if (source == NULL) {
		m_recordLine = new IRecordSetObject::CRecordSetRegisterReturnLine(
			m_recordSet,
			m_recordSet->AppenRow()
		);
	}
	else if (source != NULL) {
		m_recordLine = m_recordSet->GetRowAt(0);
	}

	m_recordSet->Modify(true);
}

//////////////////////////////////////////////////////////////////////
//						  IRecordSetObject							//
//////////////////////////////////////////////////////////////////////

#include "metadata/singleMetaTypes.h"

void IRecordSetObject::CreateEmptyKey()
{
	m_aKeyValues.clear();

	for (auto attributes : m_metaObject->GetGenericDimensions()) {
		m_aKeyValues.insert_or_assign(attributes->GetMetaID(), attributes->CreateValue());
	}
}

bool IRecordSetObject::InitializeObject(const IRecordSetObject* source, bool newRecord)
{
	IMetadata* metaData = m_metaObject->GetMetadata();
	wxASSERT(metaData);
	IModuleManager* moduleManager = metaData->GetModuleManager();
	wxASSERT(moduleManager);

	if (m_compileModule == NULL) {
		m_compileModule = new CCompileModule(m_metaObject->GetModuleObject());
		m_compileModule->SetParent(moduleManager->GetCompileModule());
		m_compileModule->AddContextVariable(thisObject, this);
	}

	if (appData->EnterpriseMode()) {
		try
		{
			m_compileModule->Compile();
		}
		catch (const CTranslateError* err)
		{
			if (appData->EnterpriseMode()) {
				CSystemObjects::Raise(err->what());
			}

			return false;
		};
	}

	if (source == NULL) {
		m_aObjectValues.clear();
	}

	if (!appData->DesignerMode()) {
		if (!newRecord) {
			ReadData();
		}
	}

	if (appData->EnterpriseMode()) {

		wxASSERT(m_procUnit == NULL);

		m_procUnit = new CProcUnit();
		m_procUnit->SetParent(moduleManager->GetProcUnit());

		m_procUnit->Execute(m_compileModule->m_cByteCode, true);
	}

	//is Ok
	return true;
}

///////////////////////////////////////////////////////////////////////////////////

bool IRecordSetObject::FindKeyValue(const meta_identifier_t& id) const
{
	return m_aKeyValues.find(id) != m_aKeyValues.end();
}

void IRecordSetObject::SetKeyValue(const meta_identifier_t& id, const CValue& cValue)
{
	IMetaAttributeObject* metaAttribute =
		m_metaObject->FindGenericAttribute(id);
	wxASSERT(metaAttribute);
	m_aKeyValues.insert_or_assign(
		id, metaAttribute ? metaAttribute->AdjustValue(cValue) : cValue
	);
}

CValue IRecordSetObject::GetKeyValue(const meta_identifier_t& id) const
{
	return m_aKeyValues.at(id);
}

void IRecordSetObject::EraseKeyValue(const meta_identifier_t& id)
{
	m_aKeyValues.erase(id);
}

///////////////////////////////////////////////////////////////////////////////////

IRecordSetObject::IRecordSetObject(IMetaObjectRegisterData* metaObject) : IValueTable(),
m_methods(new CMethods()),
m_metaObject(metaObject), m_aKeyValues(CUniquePairKey(metaObject)), m_objModified(true), m_selected(false)
{
	InitializeObject(NULL, true);

	m_dataColumnCollection = new CRecordSetRegisterColumnCollection(this);
	m_dataColumnCollection->IncrRef();
	m_recordSetKeyValue = new CRecordSetRegisterKeyValue(this);
	m_recordSetKeyValue->IncrRef();
}

IRecordSetObject::IRecordSetObject(IMetaObjectRegisterData* metaObject, const CUniquePairKey& uniqueKey) : IValueTable(),
m_methods(new CMethods()),
m_metaObject(metaObject), m_aKeyValues(uniqueKey), m_objModified(false), m_selected(false)
{
	InitializeObject(NULL, false);

	m_dataColumnCollection = new CRecordSetRegisterColumnCollection(this);
	m_dataColumnCollection->IncrRef();
	m_recordSetKeyValue = new CRecordSetRegisterKeyValue(this);
	m_recordSetKeyValue->IncrRef();
}

IRecordSetObject::IRecordSetObject(const IRecordSetObject& source) : IValueTable(),
m_methods(new CMethods()),
m_metaObject(source.m_metaObject), m_aKeyValues(source.m_aKeyValues), m_aObjectValues(source.m_aObjectValues), m_objModified(true), m_selected(false)
{
	InitializeObject(&source, true);

	m_dataColumnCollection = new CRecordSetRegisterColumnCollection(this);
	m_dataColumnCollection->IncrRef();
	m_recordSetKeyValue = new CRecordSetRegisterKeyValue(this);
	m_recordSetKeyValue->IncrRef();
}

IRecordSetObject::~IRecordSetObject()
{
	wxDELETE(m_methods);

	if (m_dataColumnCollection != NULL) {
		m_dataColumnCollection->DecrRef();
	}

	if (m_recordSetKeyValue != NULL) {
		m_recordSetKeyValue->DecrRef();
	}
}

CValue IRecordSetObject::GetAt(const CValue& cKey)
{
	unsigned int index = cKey.ToUInt();

	if (index >= m_aObjectValues.size() && !appData->DesignerMode())
		CTranslateError::Error(_("Index outside array bounds"));

	return new CRecordSetRegisterReturnLine(this, index);
}

CLASS_ID IRecordSetObject::GetClassType() const
{
	IMetaTypeObjectValueSingle* clsFactory =
		m_metaObject->GetTypeObject(eMetaObjectType::enRecordSet);
	wxASSERT(clsFactory);
	return clsFactory->GetClassType();
}

wxString IRecordSetObject::GetTypeString() const
{
	IMetaTypeObjectValueSingle* clsFactory =
		m_metaObject->GetTypeObject(eMetaObjectType::enRecordSet);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

wxString IRecordSetObject::GetString() const
{
	IMetaTypeObjectValueSingle* clsFactory =
		m_metaObject->GetTypeObject(eMetaObjectType::enRecordSet);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

#include "compiler/valueTable.h"

bool IRecordSetObject::LoadDataFromTable(IValueTable* srcTable)
{
	IValueTableColumnCollection* colData = srcTable->GetColumns();

	if (colData == NULL)
		return false;

	wxArrayString columnsName;

	for (unsigned int idx = 0; idx < colData->GetColumnCount() - 1; idx++) {
		IValueTableColumnCollection::IValueTableColumnInfo* colInfo = colData->GetColumnInfo(idx);
		wxASSERT(colInfo);
		if (m_dataColumnCollection->GetColumnByName(colInfo->GetColumnName()) != NULL) {
			columnsName.push_back(colInfo->GetColumnName());
		}
	}

	unsigned int rowsCount = srcTable->GetCount();

	for (unsigned int row = 0; row < rowsCount; row++) {
		IValueTableReturnLine* retLine = srcTable->GetRowAt(row);
		IValueTableReturnLine* newRetLine = new CRecordSetRegisterReturnLine(this, AppenRow());
		newRetLine->PrepareNames();
		for (auto colName : columnsName) {
			newRetLine->SetAttribute(colName, retLine->GetAttribute(colName));
		}
		wxDELETE(newRetLine);
	}

	return true;
}

IValueTable* IRecordSetObject::SaveDataToTable() const
{
	CValueTable* valueTable = new CValueTable;
	IValueTableColumnCollection* colData = valueTable->GetColumns();

	for (unsigned int idx = 0; idx < m_dataColumnCollection->GetColumnCount() - 1; idx++) {
		IValueTableColumnCollection::IValueTableColumnInfo* colInfo = m_dataColumnCollection->GetColumnInfo(idx);
		wxASSERT(colInfo);
		IValueTableColumnCollection::IValueTableColumnInfo* newColInfo = colData->AddColumn(
			colInfo->GetColumnName(), colInfo->GetColumnTypes(), colInfo->GetColumnCaption(), colInfo->GetColumnWidth()
		);
		newColInfo->SetColumnID(colInfo->GetColumnID());
	}

	valueTable->PrepareNames();

	for (auto row : m_aObjectValues) {
		CValueTable::CValueTableReturnLine* retLine = valueTable->AddRow();
		wxASSERT(retLine);
		for (auto cols : row) {
			retLine->SetValueByMetaID(cols.first, cols.second);
		}
	}

	return valueTable;
}

bool IRecordSetObject::IsEmpty() const
{
	return m_selected;
}

void IRecordSetObject::Modify(bool mod)
{
	m_objModified = mod;
}

bool IRecordSetObject::IsModified() const
{
	return m_objModified;
}

void IRecordSetObject::SetValueByMetaID(long line, const meta_identifier_t& id, const CValue& cVal)
{
	IMetaAttributeObject* metaAttribute = m_metaObject->FindGenericAttribute(id);
	wxASSERT(metaAttribute);

	m_aObjectValues[line][id] = metaAttribute->AdjustValue(cVal);
	IRecordSetObject::Cleared();
}

CValue IRecordSetObject::GetValueByMetaID(long line, const meta_identifier_t& id) const
{
	IMetaAttributeObject* metaAttribute = m_metaObject->FindAttribute(id);
	wxASSERT(metaAttribute);

	if (appData->DesignerMode()) {
		return metaAttribute->CreateValue();
	}

	return m_aObjectValues[line].at(id);
}

//////////////////////////////////////////////////////////////////////
//					CRecordSetRegisterColumnCollection				//
//////////////////////////////////////////////////////////////////////

#include "compiler/methods.h"

wxIMPLEMENT_DYNAMIC_CLASS(IRecordSetObject::CRecordSetRegisterColumnCollection, IValueTable::IValueTableColumnCollection);

IRecordSetObject::CRecordSetRegisterColumnCollection::CRecordSetRegisterColumnCollection() : IValueTableColumnCollection(), m_methods(NULL), m_ownerTable(NULL)
{
}

IRecordSetObject::CRecordSetRegisterColumnCollection::CRecordSetRegisterColumnCollection(IRecordSetObject* ownerTable) : IValueTableColumnCollection(), m_methods(new CMethods()), m_ownerTable(ownerTable)
{
	IMetaObjectWrapperData* metaObject = m_ownerTable->GetMetaObject();
	wxASSERT(metaObject);

	for (auto attributes : metaObject->GetGenericAttributes()) {
		CValueRecordSetRegisterColumnInfo* columnInfo = new CValueRecordSetRegisterColumnInfo(attributes);
		m_aColumnInfo.insert_or_assign(attributes->GetMetaID(), columnInfo);
		columnInfo->IncrRef();
	}
}

IRecordSetObject::CRecordSetRegisterColumnCollection::~CRecordSetRegisterColumnCollection()
{
	for (auto& colInfo : m_aColumnInfo) {
		CValueRecordSetRegisterColumnInfo* columnInfo = colInfo.second;
		wxASSERT(columnInfo);
		columnInfo->DecrRef();
	}

	wxDELETE(m_methods);
}

void IRecordSetObject::CRecordSetRegisterColumnCollection::SetAt(const CValue& cKey, CValue& cVal)//индекс массива должен начинаться с 0
{
}

CValue IRecordSetObject::CRecordSetRegisterColumnCollection::GetAt(const CValue& cKey) //индекс массива должен начинаться с 0
{
	unsigned int index = cKey.ToUInt();

	if ((index < 0 || index >= m_aColumnInfo.size() && !appData->DesignerMode()))
		CTranslateError::Error(_("Index goes beyond array"));

	auto itFounded = m_aColumnInfo.begin();
	std::advance(itFounded, index);
	return itFounded->second;
}

//////////////////////////////////////////////////////////////////////
//					CValueRecordSetRegisterColumnInfo               //
//////////////////////////////////////////////////////////////////////

wxIMPLEMENT_DYNAMIC_CLASS(IRecordSetObject::CRecordSetRegisterColumnCollection::CValueRecordSetRegisterColumnInfo, IValueTable::IValueTableColumnCollection::IValueTableColumnInfo);

IRecordSetObject::CRecordSetRegisterColumnCollection::CValueRecordSetRegisterColumnInfo::CValueRecordSetRegisterColumnInfo() : IValueTableColumnInfo(), m_metaAttribute(NULL) {}

IRecordSetObject::CRecordSetRegisterColumnCollection::CValueRecordSetRegisterColumnInfo::CValueRecordSetRegisterColumnInfo(IMetaAttributeObject* metaAttribute) : IValueTableColumnInfo(), m_metaAttribute(metaAttribute) {}

IRecordSetObject::CRecordSetRegisterColumnCollection::CValueRecordSetRegisterColumnInfo::~CValueRecordSetRegisterColumnInfo() {  }

//////////////////////////////////////////////////////////////////////
//					 CRecordSetRegisterReturnLine					//
//////////////////////////////////////////////////////////////////////

wxIMPLEMENT_DYNAMIC_CLASS(IRecordSetObject::CRecordSetRegisterReturnLine, IValueTable::IValueTableReturnLine);

IRecordSetObject::CRecordSetRegisterReturnLine::CRecordSetRegisterReturnLine()
	: IValueTableReturnLine(), m_methods(new CMethods())
{
}

IRecordSetObject::CRecordSetRegisterReturnLine::CRecordSetRegisterReturnLine(IRecordSetObject* ownerTable, int line)
	: IValueTableReturnLine(), m_ownerTable(ownerTable), m_lineTable(line), m_methods(new CMethods())
{
}

IRecordSetObject::CRecordSetRegisterReturnLine::~CRecordSetRegisterReturnLine()
{
	wxDELETE(m_methods);
}

void IRecordSetObject::CRecordSetRegisterReturnLine::PrepareNames() const
{
	IMetaObjectWrapperData* metaObject = m_ownerTable->GetMetaObject();

	std::vector<SEng> aAttributes;

	for (auto attribute : metaObject->GetGenericAttributes())
	{
		SEng aAttribute;
		aAttribute.sName = attribute->GetName();
		aAttribute.iName = attribute->GetMetaID();
		aAttribute.sSynonym = wxT("default");
		aAttributes.push_back(aAttribute);
	}

	m_methods->PrepareAttributes(aAttributes.data(), aAttributes.size());
}

void IRecordSetObject::CRecordSetRegisterReturnLine::SetValueByMetaID(const meta_identifier_t& id, const CValue& cVal)
{
	m_ownerTable->SetValueByMetaID(m_lineTable, id, cVal);
}

CValue IRecordSetObject::CRecordSetRegisterReturnLine::GetValueByMetaID(const meta_identifier_t& id) const
{
	return m_ownerTable->GetValueByMetaID(m_lineTable, id);
}

//////////////////////////////////////////////////////////////////////
//				       CRecordSetRegisterKeyValue					//
//////////////////////////////////////////////////////////////////////

IRecordSetObject::CRecordSetRegisterKeyValue::CRecordSetRegisterKeyValue(IRecordSetObject* recordSet) : CValue(eValueTypes::TYPE_VALUE, true),
m_methods(new CMethods()), m_recordSet(recordSet)
{
}

IRecordSetObject::CRecordSetRegisterKeyValue::~CRecordSetRegisterKeyValue()
{
	wxDELETE(m_methods);
}

//////////////////////////////////////////////////////////////////////
//						CRecordSetRegisterKeyDescriptionValue		//
//////////////////////////////////////////////////////////////////////

IRecordSetObject::CRecordSetRegisterKeyValue::CRecordSetRegisterKeyDescriptionValue::CRecordSetRegisterKeyDescriptionValue(IRecordSetObject* recordSet, const meta_identifier_t& id) : CValue(eValueTypes::TYPE_VALUE),
m_methods(new CMethods()), m_recordSet(recordSet), m_metaId(id)
{
}

IRecordSetObject::CRecordSetRegisterKeyValue::CRecordSetRegisterKeyDescriptionValue::~CRecordSetRegisterKeyDescriptionValue()
{
	wxDELETE(m_methods);
}

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

SO_VALUE_REGISTER(IRecordSetObject::CRecordSetRegisterColumnCollection, "recordSetRegisterColumn", CRecordSetRegisterColumnCollection, TEXT2CLSID("VL_RSCL"));
SO_VALUE_REGISTER(IRecordSetObject::CRecordSetRegisterColumnCollection::CValueRecordSetRegisterColumnInfo, "recordSetRegisterColumnInfo", CValueRecordSetRegisterColumnInfo, TEXT2CLSID("VL_RSCI"));
SO_VALUE_REGISTER(IRecordSetObject::CRecordSetRegisterReturnLine, "recordSetRegisterRow", CRecordSetRegisterReturnLine, TEXT2CLSID("VL_RSCR"));

SO_VALUE_REGISTER(IRecordSetObject::CRecordSetRegisterKeyValue, "recordSetRegisterKey", CRecordSetRegisterKeyValue, TEXT2CLSID("VL_RSCK"));
SO_VALUE_REGISTER(IRecordSetObject::CRecordSetRegisterKeyValue::CRecordSetRegisterKeyDescriptionValue, "recordSetRegisterKeyDescription", CRecordSetRegisterKeyDescriptionValue, TEXT2CLSID("VL_RDVL"));
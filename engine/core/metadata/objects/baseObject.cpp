////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : base classes for catalogs, docs etc..  
////////////////////////////////////////////////////////////////////////////

#include "baseObject.h"
#include "metadata/metadata.h"

//***********************************************************************
//*                         metadata                                    * 
//***********************************************************************

wxIMPLEMENT_ABSTRACT_CLASS(IMetaObjectValue, IMetaObject)

//*******************************************************************
//*                           MetaObjectValue                       *
//*******************************************************************
#include "compiler/methods.h"
#include "utils/fs/fs.h"

IMetaTypeObjectValueSingle *IMetaObjectValue::GetTypeObject(eMetaObjectType refType)
{
	return m_metaData->GetTypeObject(this, refType);
}

IMetaObjectValue::IMetaObjectValue() :IMetaObject() {}

IMetaObjectValue::~IMetaObjectValue() {}

IMetaObject *IMetaObjectValue::FindMetaObjectByID(meta_identifier_t meta_id)
{
	return m_metaData->GetMetaObject(meta_id);
}

bool IMetaObjectValue::OnLoadMetaObject(IMetadata *metaData)
{
	return IMetaObject::OnLoadMetaObject(metaData);
}

bool IMetaObjectValue::OnSaveMetaObject()
{
	return IMetaObject::OnSaveMetaObject();
}

bool IMetaObjectValue::OnDeleteMetaObject()
{
	return IMetaObject::OnDeleteMetaObject();
}

#include "metadata/singleMetaTypes.h"

bool IMetaObjectValue::OnRunMetaObject(int flags)
{
	registerObject();
	registerManager();
	return IMetaObject::OnRunMetaObject(flags);
}

bool IMetaObjectValue::OnCloseMetaObject()
{
	unregisterObject();
	unregisterManager();
	return IMetaObject::OnCloseMetaObject();
}

#include "compiler/systemObjects.h"
#include "utils/stringUtils.h"

CValueForm *IMetaObjectValue::GetGenericForm(const wxString &formName, IValueFrame *ownerControl, const Guid &formGuid)
{
	if (!formName.IsEmpty()) {
		for (auto metaForm : GetObjectForms()) {
			if (StringUtils::CompareString(formName, metaForm->GetName())) {
				return metaForm->GenerateFormAndRun(ownerControl,
					NULL, formGuid
				);
			}
		}
	}

	CSystemObjects::Raise("Ñommon form not found '" + formName + "'");
	return NULL;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////

CValueForm *IMetaObjectValue::GetObjectForm(meta_identifier_t id, IValueFrame *ownerControl, const Guid &formGuid)
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

CValueForm *IMetaObjectValue::GetGenericForm(meta_identifier_t id, IValueFrame *ownerControl, const Guid &formGuid)
{
	CMetaFormObject* defList = NULL;

	for (auto metaForm : GetObjectForms()) {
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

//*******************************************************************
//*                            ARRAY                                *
//*******************************************************************

std::vector<IMetaAttributeObject *> IMetaObjectValue::GetObjectAttributes() const
{
	std::vector<IMetaAttributeObject *> attributes;
	for (auto metaObject : m_aMetaObjects) {
		if (metaObject->GetClsid() == g_metaAttributeCLSID) {
			attributes.push_back(dynamic_cast<CMetaAttributeObject *>(metaObject));
		}
	}
	return attributes;
}

std::vector<CMetaFormObject *> IMetaObjectValue::GetObjectForms() const
{
	std::vector<CMetaFormObject *> forms;
	for (auto metaObject : m_aMetaObjects) {
		if (metaObject->GetClsid() == g_metaFormCLSID) {
			forms.push_back(dynamic_cast<CMetaFormObject *>(metaObject));
		}
	}
	return forms;
}

std::vector<CMetaGridObject *> IMetaObjectValue::GetObjectTemplates() const
{
	std::vector<CMetaGridObject *> templates;
	for (auto metaObject : m_aMetaObjects) {
		if (metaObject->GetClsid() == g_metaTemplateCLSID) {
			templates.push_back(dynamic_cast<CMetaGridObject *>(metaObject));
		}
	}
	return templates;
}

std::vector<CMetaTableObject *> IMetaObjectValue::GetObjectTables() const
{
	std::vector<CMetaTableObject *> tables;
	for (auto metaObject : m_aMetaObjects) {
		if (metaObject->GetClsid() == g_metaTableCLSID) {
			tables.push_back(dynamic_cast<CMetaTableObject *>(metaObject));
		}
	}
	return tables;
}

#include "frontend/visualView/controls/form.h"

//*******************************************************************
//*                           IMetaObjectRefValue                   *
//*******************************************************************

wxIMPLEMENT_ABSTRACT_CLASS(IMetaObjectRefValue, IMetaObjectValue)

bool IMetaObjectRefValue::OnLoadMetaObject(IMetadata *metaData)
{
	return IMetaObjectValue::OnLoadMetaObject(metaData);
}

bool IMetaObjectRefValue::OnDeleteMetaObject()
{
	return IMetaObjectValue::OnDeleteMetaObject();
}

bool IMetaObjectRefValue::OnRunMetaObject(int flags)
{
	registerReference();
	registerList();
	return IMetaObjectValue::OnRunMetaObject(flags);
}

bool IMetaObjectRefValue::OnCloseMetaObject()
{
	unregisterReference();
	unregisterList();
	return IMetaObjectValue::OnCloseMetaObject();
}

bool IMetaObjectRefValue::ProcessChoice(IValueFrame *ownerValue, meta_identifier_t id)
{
	CValueForm *selectForm = IMetaObjectRefValue::GetSelectForm(id, ownerValue);

	if (!selectForm) {
		return false;
	}

	selectForm->ShowForm();
	return true;
}

IMetaObject *IMetaObjectRefValue::FindMetaObjectByID(meta_identifier_t id)
{
	for (auto metaObject : GetObjects())
	{
		if (id == metaObject->GetMetaID())
			return metaObject;
	}

	return NULL;
}

IDataObjectValue *IMetaObjectRefValue::CreateObjectValue()
{
	return CreateObjectRefValue();
}

CValueReference *IMetaObjectRefValue::FindObjectValue(const Guid &guid)
{
	return new CValueReference(this, guid);
}

CValueForm *IMetaObjectRefValue::GetListForm(meta_identifier_t id, IValueFrame* ownerControl, const Guid & formGuid)
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

CValueForm *IMetaObjectRefValue::GetSelectForm(meta_identifier_t id, IValueFrame *ownerControl, const Guid &formGuid)
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

//*******************************************************************
//*                            ARRAY                                *
//*******************************************************************

std::vector<CMetaEnumerationObject *> IMetaObjectRefValue::GetObjectEnums() const
{
	std::vector<CMetaEnumerationObject *> enumerations;
	for (auto metaObject : m_aMetaObjects) {
		if (metaObject->GetClsid() == g_metaEnumCLSID) {
			enumerations.push_back(dynamic_cast<CMetaEnumerationObject *>(metaObject));
		}
	}
	return enumerations;
}

//*******************************************************************
//*                           IDataObjectSource                     *
//*******************************************************************

#include "frontend/visualView/controls/form.h"

//*******************************************************************
//*                           IDataObjectValue                      *
//*******************************************************************

wxIMPLEMENT_ABSTRACT_CLASS(IDataObjectValue, CValue);

IDataObjectValue::IDataObjectValue(const Guid &guid, bool newObject) : CValue(eValueTypes::TYPE_VALUE),
IDataObjectSource(), IObjectValueInfo(guid, newObject), IModuleInfo(),
m_methods(new CMethods())
{
}

IDataObjectValue::IDataObjectValue(const IDataObjectValue &source) : CValue(eValueTypes::TYPE_VALUE),
IDataObjectSource(source), IObjectValueInfo(Guid::newGuid(), true), IModuleInfo(),
m_methods(new CMethods())
{
}

IDataObjectValue::~IDataObjectValue()
{
	wxDELETE(m_methods);
}

CValueForm *IDataObjectValue::GetFrame() const
{
	return CValueForm::FindFormByGuid(m_objGuid);
}

CLASS_ID IDataObjectValue::GetTypeID() const
{
	IMetaObjectValue *metaObject = GetMetaObject();
	wxASSERT(metaObject);
	IMetaTypeObjectValueSingle *clsFactory =
		metaObject->GetTypeObject(eMetaObjectType::enObject);
	wxASSERT(clsFactory);
	return clsFactory->GetTypeID();
}

wxString IDataObjectValue::GetTypeString() const
{
	IMetaObjectValue *metaObject = GetMetaObject();
	wxASSERT(metaObject);
	IMetaTypeObjectValueSingle *clsFactory =
		metaObject->GetTypeObject(eMetaObjectType::enObject);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

wxString IDataObjectValue::GetString() const
{
	IMetaObjectValue *metaObject = GetMetaObject();
	wxASSERT(metaObject);
	IMetaTypeObjectValueSingle *clsFactory =
		metaObject->GetTypeObject(eMetaObjectType::enObject);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

CSourceExplorer IDataObjectValue::GetSourceExplorer() const
{
	IMetaObjectValue *metaObject = GetMetaObject();
	CSourceExplorer srcHelper(metaObject, false);

	for (auto attribute : metaObject->GetObjectAttributes()) {
		srcHelper.AppendSource(attribute);
	}

	for (auto table : metaObject->GetObjectTables()) {
		srcHelper.AppendSource(table);
	}

	return srcHelper;
}

#include "metadata/objects/tabularSection/tabularSection.h"

bool IDataObjectValue::GetTable(IValueTable *&tableValue, meta_identifier_t id)
{
	auto foundedIt = std::find_if(m_aObjectTables.begin(), m_aObjectTables.end(), [id](IValueTabularSection *vts) {
		CMetaTableObject *metaObject = vts->GetMetaObject();
		wxASSERT(metaObject);
		return id == metaObject->GetMetaID();
	});

	if (foundedIt == m_aObjectTables.end())
		return false;

	tableValue = *foundedIt;
	return true;
}

#include "compiler/valueTypeDescription.h"

void IDataObjectValue::SetValueByMetaID(meta_identifier_t id, const CValue &cVal)
{
	auto foundedIt = m_aObjectValues.find(id);
	wxASSERT(foundedIt != m_aObjectValues.end());
	if (foundedIt != m_aObjectValues.end()) {
		IMetaObjectValue *metaObjectValue = GetMetaObject();
		wxASSERT(metaObjectValue);
		IMetaAttributeObject *metaAttribute = wxDynamicCast(
			metaObjectValue->FindMetaObjectByID(id), IMetaAttributeObject
		);
		wxASSERT(metaAttribute);
		CValueTypeDescription *td = new CValueTypeDescription(metaAttribute->GetClassTypeObject(),
			metaAttribute->GetNumberQualifier(),
			metaAttribute->GetDateQualifier(),
			metaAttribute->GetStringQualifier()
		);

		foundedIt->second = td->AdjustValue(cVal);
		delete td;
	}
}

CValue IDataObjectValue::GetValueByMetaID(meta_identifier_t id) const
{
	auto foundedIt = m_aObjectValues.find(id);
	wxASSERT(foundedIt != m_aObjectValues.end());
	if (foundedIt != m_aObjectValues.end()) {
		return foundedIt->second;
	}

	return CValue();
}

//**************************************************************************************************
//*                                  IDataObjectRefValue                                           *           
//**************************************************************************************************

wxIMPLEMENT_ABSTRACT_CLASS(IDataObjectRefValue, IDataObjectValue);

IDataObjectRefValue::IDataObjectRefValue() :
	IDataObjectValue(Guid::newGuid()), m_metaObject(NULL), m_reference_impl(NULL), m_codeGenerator(NULL), m_objModified(false)
{
}

IDataObjectRefValue::IDataObjectRefValue(IMetaObjectRefValue *metaObject) :
	IDataObjectValue(Guid::newGuid(), true), m_metaObject(metaObject), m_reference_impl(NULL), m_codeGenerator(NULL), m_objModified(false)
{
	IMetaAttributeObject *attributeCode = m_metaObject->GetAttributeForCode();
	if (attributeCode != NULL) {
		m_codeGenerator = new CCodeGenerator(
			m_metaObject, attributeCode
		);
	}
	m_reference_impl = new reference_t(m_metaObject->GetMetaID(), m_objGuid);
}

IDataObjectRefValue::IDataObjectRefValue(IMetaObjectRefValue *metaObject, const Guid &objGuid) :
	IDataObjectValue(objGuid, false), m_metaObject(metaObject), m_reference_impl(NULL), m_codeGenerator(NULL), m_objModified(false)
{
	IMetaAttributeObject *attributeCode = m_metaObject->GetAttributeForCode();
	if (attributeCode != NULL) {
		m_codeGenerator = new CCodeGenerator(
			m_metaObject, attributeCode
		);
	}
	m_reference_impl = new reference_t(m_metaObject->GetMetaID(), m_objGuid);
}

IDataObjectRefValue::IDataObjectRefValue(const IDataObjectRefValue &source) :
	IDataObjectValue(source), m_metaObject(source.m_metaObject), m_reference_impl(NULL), m_codeGenerator(NULL), m_objModified(false)
{
	IMetaAttributeObject *attributeCode = m_metaObject->GetAttributeForCode();
	if (attributeCode != NULL) {
		m_codeGenerator = new CCodeGenerator(
			m_metaObject, attributeCode
		);
	}
	m_reference_impl = new reference_t(m_metaObject->GetMetaID(), m_objGuid);
}

IDataObjectRefValue::~IDataObjectRefValue()
{
	wxDELETE(m_codeGenerator);
	wxDELETE(m_reference_impl);
}

#include "appData.h"

bool IDataObjectRefValue::InitializeObject(const IDataObjectValue *source)
{
	IMetadata *metaData = m_metaObject->GetMetadata();
	wxASSERT(metaData);
	IModuleManager *moduleManager = metaData->GetModuleManager();
	wxASSERT(moduleManager);

	if (!m_compileModule) {
		m_compileModule = new CCompileModule(m_metaObject->GetModuleObject());
		m_compileModule->SetParent(moduleManager->GetCompileModule());
		m_compileModule->AddContextVariable(thisObject, this);
	}

	if (appData->EnterpriseMode()) {
		try
		{
			m_compileModule->Compile();
		}
		catch (const CTranslateError *err)
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
		(appData->EnterpriseMode() && !ReadInDB())) {
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

		if (m_bNewObject && source != NULL) {
			m_procUnit->CallFunction("OnCopy", CValue((CValue *)source));
		}

		if (m_bNewObject) {
			m_procUnit->CallFunction("Filling");
		}
	}

	//is Ok
	return true;
}


CLASS_ID IDataObjectRefValue::GetTypeID() const
{
	return IDataObjectValue::GetTypeID();
}

wxString IDataObjectRefValue::GetTypeString() const
{
	return IDataObjectValue::GetTypeString();
}

wxString IDataObjectRefValue::GetString() const
{
	return m_metaObject->GetDescription(this);
}

CSourceExplorer IDataObjectRefValue::GetSourceExplorer() const
{
	CSourceExplorer srcHelper(m_metaObject, false);

	IMetaAttributeObject *metaAttribute = m_metaObject->GetAttributeForCode();
	for (auto attribute : m_metaObject->GetObjectAttributes()) {
		srcHelper.AppendSource(attribute, attribute != metaAttribute);
	}

	for (auto table : m_metaObject->GetObjectTables()) {
		srcHelper.AppendSource(table);
	}

	return srcHelper;
}

bool IDataObjectRefValue::GetTable(IValueTable *&tableValue, meta_identifier_t id)
{
	auto foundedIt = std::find_if(m_aObjectTables.begin(), m_aObjectTables.end(), [id](IValueTabularSection *vts) {
		CMetaTableObject *metaObject = vts->GetMetaObject();
		wxASSERT(metaObject);
		return id == metaObject->GetMetaID();
	});

	if (foundedIt == m_aObjectTables.end())
		return false;

	tableValue = *foundedIt;
	return true;
}

void IDataObjectRefValue::Modify(bool mod)
{
	CValueForm *foundedForm = CValueForm::FindFormByGuid(m_objGuid);

	if (foundedForm) {
		foundedForm->Modify(mod);
	}

	m_objModified = mod;
}

void IDataObjectRefValue::SetValueByMetaID(meta_identifier_t id, const CValue & cVal)
{
	IDataObjectValue::SetValueByMetaID(id, cVal);
	IDataObjectRefValue::Modify(true);
}

CValue IDataObjectRefValue::GetValueByMetaID(meta_identifier_t id) const
{
	return IDataObjectValue::GetValueByMetaID(id);
}

void IDataObjectRefValue::PrepareEmptyObject()
{
	//attrbutes can refValue 
	for (auto attribute : m_metaObject->GetObjectAttributes()) {
		switch (attribute->GetTypeObject())
		{
		case eValueTypes::TYPE_BOOLEAN: m_aObjectValues[attribute->GetMetaID()] = eValueTypes::TYPE_BOOLEAN; break;
		case eValueTypes::TYPE_NUMBER: m_aObjectValues[attribute->GetMetaID()] = eValueTypes::TYPE_NUMBER; break;
		case eValueTypes::TYPE_DATE: m_aObjectValues[attribute->GetMetaID()] = eValueTypes::TYPE_DATE; break;
		case eValueTypes::TYPE_STRING: m_aObjectValues[attribute->GetMetaID()] = eValueTypes::TYPE_STRING; break;
		default: m_aObjectValues[attribute->GetMetaID()] = new CValueReference(
			m_metaObject->GetMetadata(), attribute->GetTypeObject()
		); break;
		}
	}

	// table is collection values 
	for (auto table : m_metaObject->GetObjectTables()) {
		CValueTabularRefSection *tableSection = new CValueTabularRefSection(this, table);
		m_aObjectValues[table->GetMetaID()] = tableSection;
		m_aObjectTables.push_back(tableSection);
	}

	m_objModified = true;
}

void IDataObjectRefValue::PrepareEmptyObject(const IDataObjectValue *source)
{
	IMetaAttributeObject *codeAttribute = m_metaObject->GetAttributeForCode();

	switch (codeAttribute->GetTypeObject())
	{
	case eValueTypes::TYPE_BOOLEAN: m_aObjectValues[codeAttribute->GetMetaID()] = eValueTypes::TYPE_BOOLEAN; break;
	case eValueTypes::TYPE_NUMBER: m_aObjectValues[codeAttribute->GetMetaID()] = eValueTypes::TYPE_NUMBER; break;
	case eValueTypes::TYPE_DATE: m_aObjectValues[codeAttribute->GetMetaID()] = eValueTypes::TYPE_DATE; break;
	case eValueTypes::TYPE_STRING: m_aObjectValues[codeAttribute->GetMetaID()] = eValueTypes::TYPE_STRING; break;
	default: m_aObjectValues[codeAttribute->GetMetaID()] = new CValueReference(
		m_metaObject->GetMetadata(), codeAttribute->GetTypeObject()
	); break;
	}

	//attributes can refValue 
	for (auto attribute : m_metaObject->GetObjectAttributes()) {
		if (attribute != codeAttribute) {
			m_aObjectValues[attribute->GetMetaID()] = source->GetValueByMetaID(attribute->GetMetaID());
		}
	}

	// table is collection values 
	for (auto table : m_metaObject->GetObjectTables()) {
		CValueTabularRefSection *tableSection = new CValueTabularRefSection(this, table);
		tableSection->LoadDataFromTable(source->GetTableByMetaID(table->GetMetaID()));
		m_aObjectValues[table->GetMetaID()] = tableSection;
		m_aObjectTables.push_back(tableSection);
	}

	m_objModified = true;
}

CValueReference *IDataObjectRefValue::GetReference()
{
	if (m_bNewObject) {
		return new CValueReference(m_metaObject);
	}
	else {
		return new CValueReference(m_metaObject, m_objGuid);
	}
}
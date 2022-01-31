////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : report - object
////////////////////////////////////////////////////////////////////////////

#include "dataReport.h"
#include "metadata/metadata.h"

#include "appData.h"
#include "reference/reference.h"
#include "frontend/visualView/controls/form.h"
#include "databaseLayer/databaseLayer.h"
#include "compiler/systemObjects.h"

#include "metadata/objects/tabularSection/tabularSection.h"

#include "utils/fs/fs.h"
#include "utils/stringUtils.h"

//*********************************************************************************************
//*                                  ObjectCatalogValue                                       *
//*********************************************************************************************

CObjectReportValue::CObjectReportValue(const CObjectReportValue &source) : IDataObjectValue(source),
m_metaObject(source.m_metaObject)
{
	if (m_metaObject->m_objMode == METAOBJECT_NORMAL) {
		InitializeObject();
	}
}

CObjectReportValue::CObjectReportValue(CMetaObjectReportValue *metaObject) : IDataObjectValue(Guid::newGuid()),
m_metaObject(metaObject)
{
	if (m_metaObject->m_objMode == METAOBJECT_NORMAL) {
		InitializeObject();
	}
}

CObjectReportValue::~CObjectReportValue()
{
	if (appData->EnterpriseMode()
		&& m_metaObject->m_objMode == METAOBJECT_EXTERNAL) {
		delete m_metaObject->GetMetadata();
	}
}

void CObjectReportValue::PrepareEmptyObject()
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
		CValueTabularSection *tableSection = new CValueTabularSection(this, table);
		m_aObjectValues[table->GetMetaID()] = tableSection;
		m_aObjectTables.push_back(tableSection);
	}
}

bool CObjectReportValue::InitializeObject()
{
	if (m_metaObject->m_objMode == METAOBJECT_NORMAL) {
		IMetadata *m_metaData = m_metaObject->GetMetadata();
		wxASSERT(m_metaData);
		IModuleManager *moduleManager = m_metaData->GetModuleManager();
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
			
			m_procUnit = new CProcUnit();
			m_procUnit->SetParent(moduleManager->GetProcUnit());
		}
	}

	m_aObjectValues.clear();
	m_aObjectTables.clear();

	PrepareEmptyObject();

	if (m_metaObject->m_objMode == METAOBJECT_NORMAL) {
		if (appData->EnterpriseMode()) {
			m_procUnit->Execute(m_compileModule->m_cByteCode, true);
		}
	}

	//is Ok
	return true;
}

void CObjectReportValue::ShowFormValue(const wxString &formName, IValueFrame *owner)
{
	CValueForm *foundedForm = GetFrame();

	if (foundedForm && foundedForm->IsShown()) {
		foundedForm->ActivateForm();
		return;
	}

	//if form is not initialised then generate  
	CValueForm *valueForm =
		GetFormValue(formName, owner);

	valueForm->Modify(false);
	valueForm->ShowForm();
}

CValueForm *CObjectReportValue::GetFormValue(const wxString &formName, IValueFrame *ownerControl)
{
	CValueForm *foundedForm = GetFrame();

	if (foundedForm)
		return foundedForm;

	IMetaFormObject* defList = NULL;

	if (!formName.IsEmpty()) {
		for (auto metaForm : m_metaObject->GetObjectForms()) {
			if (StringUtils::CompareString(formName, metaForm->GetName())) {
				defList = metaForm; break;
			}
		}
		wxASSERT(defList);
	}
	else {
		defList = m_metaObject->GetDefaultFormByID(CMetaObjectReportValue::eFormReport);
	}

	CValueForm *valueForm = NULL;

	if (defList) {
		valueForm = defList->GenerateFormAndRun(
			ownerControl, this, m_objGuid
		);
	}
	else {
		valueForm = new CValueForm();
		valueForm->InitializeForm(ownerControl, NULL,
			this, m_objGuid
		);
		valueForm->ReadProperty();
		valueForm->BuildForm(CMetaObjectReportValue::eFormReport);
	}

	return valueForm;
}
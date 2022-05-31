////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : dataProcessor - object
////////////////////////////////////////////////////////////////////////////

#include "dataProcessor.h"
#include "metadata/metadata.h"

#include "appData.h"
#include "reference/reference.h"
#include "frontend/visualView/controls/form.h"
#include "databaseLayer/databaseLayer.h"
#include "compiler/systemObjects.h"

#include "metadata/metaObjects/objects/tabularSection/tabularSection.h"

#include "utils/fs/fs.h"
#include "utils/stringUtils.h"

//*********************************************************************************************
//*                                  ObjectCatalogValue                                       *
//*********************************************************************************************

CObjectDataProcessor::CObjectDataProcessor(const CObjectDataProcessor &source) : IRecordDataObject(source),
m_metaObject(source.m_metaObject)
{
	if (m_metaObject->m_objMode == METAOBJECT_NORMAL) {
		InitializeObject();
	}
}

CObjectDataProcessor::CObjectDataProcessor(CMetaObjectDataProcessor *metaObject) : IRecordDataObject(Guid::newGuid()),
m_metaObject(metaObject)
{
	if (m_metaObject->m_objMode == METAOBJECT_NORMAL) {
		InitializeObject();
	}
}

CObjectDataProcessor::~CObjectDataProcessor()
{
	if (appData->EnterpriseMode()
		&& m_metaObject->m_objMode == METAOBJECT_EXTERNAL) {
		delete m_metaObject->GetMetadata();
	}
}

bool CObjectDataProcessor::InitializeObject()
{
	if (m_metaObject->m_objMode == METAOBJECT_NORMAL) {
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

void CObjectDataProcessor::ShowFormValue(const wxString &formName, IValueFrame *owner)
{
	CValueForm *foundedForm = GetForm();

	if (foundedForm && foundedForm->IsShown()) {
		foundedForm->ActivateForm();
		return;
	}

	//if form is not initialised then generate  
	CValueForm *valueForm = GetFormValue(formName, owner);

	valueForm->Modify(false);
	valueForm->ShowForm();
}

CValueForm *CObjectDataProcessor::GetFormValue(const wxString &formName, IValueFrame *ownerControl)
{
	CValueForm *foundedForm = GetForm();

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
		defList = m_metaObject->GetDefaultFormByID(CMetaObjectDataProcessor::eFormDataProcessor);
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
		valueForm->BuildForm(CMetaObjectDataProcessor::eFormDataProcessor);
	}

	return valueForm;
}
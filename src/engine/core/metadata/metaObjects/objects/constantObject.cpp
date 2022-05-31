////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : constants - db
////////////////////////////////////////////////////////////////////////////

#include "constant.h"
#include "metadata/metadata.h"
#include "frontend/visualView/controls/form.h"
#include "compiler/systemObjects.h"
#include "metadata/singleMetaTypes.h"
#include "utils/stringUtils.h"
#include "appData.h"

//***********************************************************************
//*                           contant value                             *
//***********************************************************************

CConstantObject* CMetaConstantObject::CreateObjectValue()
{
	IModuleManager* moduleManager = m_metaData->GetModuleManager();
	wxASSERT(moduleManager);

	CConstantObject* pDataRef = NULL;

	if (appData->DesignerMode()) {
		if (!moduleManager->FindCompileModule(m_moduleObject, pDataRef))
			return new CConstantObject(this);
	}
	else {
		pDataRef = new CConstantObject(this);
	}

	return pDataRef;
}

//*********************************************************************************************
//*                                  CConstantObject                                     *
//*********************************************************************************************

bool CConstantObject::InitializeObject(const CConstantObject* source)
{
	IMetadata* m_metaData = m_metaObject->GetMetadata();
	wxASSERT(m_metaData);
	IModuleManager* moduleManager = m_metaData->GetModuleManager();
	wxASSERT(moduleManager);

	if (!m_compileModule) {
		m_compileModule = new CCompileModule(m_metaObject->GetModuleObject());
		m_compileModule->SetParent(moduleManager->GetCompileModule());
	}

	m_constVal = GetConstValue();

	if (appData->EnterpriseMode() ||
		appData->ServiceMode())
	{
		m_procUnit = new CProcUnit();
		m_procUnit->SetParent(moduleManager->GetProcUnit());

		try
		{
			m_compileModule->Compile();
		}
		catch (const CTranslateError* err)
		{
			CSystemObjects::Raise(err->what());
			return false;
		};

		m_procUnit->Execute(m_compileModule->m_cByteCode, true);
	}

	//is Ok
	return true;
}

CConstantObject::CConstantObject(CMetaConstantObject* metaObject)
	: m_metaObject(metaObject)
{
	InitializeObject();
}

CConstantObject::CConstantObject(const CConstantObject& source)
	: m_metaObject(source.m_metaObject)
{
	InitializeObject(&source);
}

CValueForm* CConstantObject::GetForm() const
{
	return CValueForm::FindFormByGuid(
		m_metaObject->GetGuid()
	);
}

CLASS_ID CConstantObject::GetClassType() const
{
	IMetadata* metaData = m_metaObject->GetMetadata();
	wxASSERT(metaData);
	IMetaTypeObjectValueSingle* clsFactory =
		metaData->GetTypeObject(m_metaObject, eMetaObjectType::enObject);
	wxASSERT(clsFactory);
	return clsFactory->GetClassType();
}

wxString CConstantObject::GetTypeString() const
{
	IMetadata* metaData = m_metaObject->GetMetadata();
	wxASSERT(metaData);
	IMetaTypeObjectValueSingle* clsFactory =
		metaData->GetTypeObject(m_metaObject, eMetaObjectType::enObject);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

wxString CConstantObject::GetString() const
{
	IMetadata* metaData = m_metaObject->GetMetadata();
	wxASSERT(metaData);
	IMetaTypeObjectValueSingle* clsFactory =
		metaData->GetTypeObject(m_metaObject, eMetaObjectType::enObject);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

CSourceExplorer CConstantObject::GetSourceExplorer() const
{
	CSourceExplorer srcHelper(
		m_metaObject, GetClassType(),
		false, true
	);

	srcHelper.AppendSource(m_metaObject);
	return srcHelper;
}

bool CConstantObject::GetTable(IValueTable*& tableValue, const meta_identifier_t& id)
{
	return false;
}

void CConstantObject::ShowFormValue()
{
	CValueForm* foundedForm = GetForm();

	if (foundedForm && foundedForm->IsShown()) {
		foundedForm->ActivateForm();
		return;
	}

	//if form is not initialised then generate  
	CValueForm* valueForm = GetFormValue();

	valueForm->Modify(false);
	valueForm->ShowForm();
}

CValueForm* CConstantObject::GetFormValue()
{
	CValueForm* foundedForm = GetForm();

	if (foundedForm != NULL)
		return foundedForm;

	CValueForm* valueForm = new CValueForm();
	valueForm->InitializeForm(NULL, NULL,
		this, m_metaObject->GetGuid()
	);
	valueForm->ReadProperty();
	valueForm->BuildForm(defaultFormType);
	valueForm->Modify(false);

	return valueForm;
}

void CConstantObject::SetValueByMetaID(const meta_identifier_t& id, const CValue& cVal)
{
	if (id == m_metaObject->GetMetaID()) {
		CValueForm* foundedForm = CValueForm::FindFormByGuid(m_metaObject->GetGuid());
		m_constVal = m_metaObject->AdjustValue(cVal);
		if (foundedForm != NULL) {
			foundedForm->Modify(true);
		}
	}
}

CValue CConstantObject::GetValueByMetaID(const meta_identifier_t& id) const
{
	if (id == m_metaObject->GetMetaID()) {
		return m_constVal;
	}

	return CValue();
}

#include "databaseLayer/databaseLayer.h"

CValue CConstantObject::GetConstValue() const
{
	CValue ret;

	if (appData->EnterpriseMode()) {

		wxString tableName = m_metaObject->GetTableNameDB();
		wxString fieldName = m_metaObject->GetFieldNameDB();

		if (databaseLayer->TableExists(tableName)) {

			DatabaseResultSet* resultSet =
				databaseLayer->RunQueryWithResults("SELECT FIRST 1 %s FROM %s", IMetaAttributeObject::GetSQLFieldName(m_metaObject), tableName);

			if (resultSet == NULL) {
				return ret;
			}

			if (resultSet->Next()) {
				ret = IMetaAttributeObject::GetValueAttribute(
					m_metaObject, resultSet);
				ret = m_metaObject->AdjustValue(ret);
			}
			else {
				ret = m_metaObject->CreateValue();
			}

			resultSet->Close();
		}
	}

	return ret;
}

#include "databaseLayer/databaseErrorCodes.h" 

bool CConstantObject::SetConstValue(const CValue& cValue)
{
	if (appData->EnterpriseMode()) {

		wxString tableName = m_metaObject->GetTableNameDB();
		wxString fieldName = m_metaObject->GetFieldNameDB();

		if (databaseLayer->TableExists(tableName)) {

			CValueForm* foundedForm = GetForm();

			databaseLayer->BeginTransaction();

			{
				CValue cancel = false;
				m_procUnit->CallFunction("BeforeWrite", cancel);

				if (cancel.GetBoolean()) {
					databaseLayer->RollBack(); CSystemObjects::Raise("failed to write object in db!"); return false;
				}
			}

			CValue adjustVal = m_metaObject->AdjustValue(cValue);

			//check fill attributes 
			bool fillCheck = true;

			if (m_metaObject->FillCheck()) {
				if (adjustVal.IsEmpty()) {
					wxString fillError =
						wxString::Format(_("""%s"" is a required field"), m_metaObject->GetSynonym());
					CSystemObjects::Message(fillError, eStatusMessage::eStatusMessage_Information);
					fillCheck = false;
				}
			}

			if (!fillCheck) {
				return false;
			}

			wxString sqlText = "UPDATE OR INSERT INTO %s (%s, RECORD_KEY) VALUES(";
			for (unsigned int idx = 0; idx < IMetaAttributeObject::GetSQLFieldCount(m_metaObject); idx++) {
				sqlText += "?,";
			}
			sqlText += "'6') MATCHING(RECORD_KEY);";
			PreparedStatement* statement =
				databaseLayer->PrepareStatement(sqlText, tableName, IMetaAttributeObject::GetSQLFieldName(m_metaObject));

			if (statement == NULL) {
				return false;
			}

			int position = 1;

			IMetaAttributeObject::SetValueAttribute(
				m_metaObject,
				adjustVal,
				statement,
				position
			);

			bool hasError = statement->RunQuery() == DATABASE_LAYER_QUERY_RESULT_ERROR;
			databaseLayer->CloseStatement(statement);

			{
				CValue cancel = false;
				m_procUnit->CallFunction("OnWrite", cancel);
				if (cancel.GetBoolean()) {
					databaseLayer->RollBack(); CSystemObjects::Raise("failed to write object in db!"); return false;
				}
			}

			databaseLayer->Commit();

			if (foundedForm != NULL) {
				foundedForm->Modify(false);
			}
		}
	}

	return true;
}


////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : constants - db
////////////////////////////////////////////////////////////////////////////

#include "constant.h"
#include "backend/metaData.h"
#include "backend/systemManager/systemManager.h"

#include "backend/appData.h"

//***********************************************************************
//*                           constant value                            *
//***********************************************************************

CRecordDataObjectConstant* CMetaObjectConstant::CreateObjectValue()
{
	IModuleManager* moduleManager = m_metaData->GetModuleManager();
	wxASSERT(moduleManager);

	CRecordDataObjectConstant* pDataRef = nullptr;

	if (appData->DesignerMode()) {
		if (!moduleManager->FindCompileModule(m_moduleObject, pDataRef))
			return m_metaData->CreateAndConvertObjectValueRef<CRecordDataObjectConstant>(this);
	}
	else {
		pDataRef = m_metaData->CreateAndConvertObjectValueRef<CRecordDataObjectConstant>(this);
	}

	return pDataRef;
}

//*********************************************************************************************
//*                                  CRecordDataObjectConstant                                     *
//*********************************************************************************************

bool CRecordDataObjectConstant::InitializeObject(const CRecordDataObjectConstant* source)
{
	IMetaData* metaData = m_metaObject->GetMetaData();
	wxASSERT(metaData);
	IModuleManager* moduleManager = metaData->GetModuleManager();
	wxASSERT(moduleManager);

	if (!m_compileModule) {
		m_compileModule = new CCompileModule(m_metaObject->GetModuleObject());
		m_compileModule->SetParent(moduleManager->GetCompileModule());
	}

	m_constVal = GetConstValue();

	if (!appData->DesignerMode()) {
		m_procUnit = new CProcUnit();
		m_procUnit->SetParent(moduleManager->GetProcUnit());
		try {
			m_compileModule->Compile();
		}
		catch (const CBackendException* err) {
			CSystemFunction::Raise(err->what());
			return false;
		};
		m_procUnit->Execute(m_compileModule->m_cByteCode, true);
	}

	PrepareNames();
	//is Ok
	return true;
}

CRecordDataObjectConstant::CRecordDataObjectConstant(CMetaObjectConstant* metaObject)
	: m_metaObject(metaObject)
{
	InitializeObject();
}

CRecordDataObjectConstant::CRecordDataObjectConstant(const CRecordDataObjectConstant& source)
	: m_metaObject(source.m_metaObject)
{
	InitializeObject(&source);
}

IBackendValueForm* CRecordDataObjectConstant::GetForm() const
{
	return IBackendValueForm::FindFormByUniqueKey(
		m_metaObject->GetGuid()
	);
}

#include "backend/objCtor.h"

class_identifier_t CRecordDataObjectConstant::GetClassType() const
{
	IMetaData* metaData = m_metaObject->GetMetaData();
	wxASSERT(metaData);
	IMetaValueTypeCtor* clsFactory =
		metaData->GetTypeCtor(m_metaObject, eCtorMetaType::eCtorMetaType_Object);
	wxASSERT(clsFactory);
	return clsFactory->GetClassType();
}

wxString CRecordDataObjectConstant::GetClassName() const
{
	IMetaData* metaData = m_metaObject->GetMetaData();
	wxASSERT(metaData);
	IMetaValueTypeCtor* clsFactory =
		metaData->GetTypeCtor(m_metaObject, eCtorMetaType::eCtorMetaType_Object);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

wxString CRecordDataObjectConstant::GetString() const
{
	IMetaData* metaData = m_metaObject->GetMetaData();
	wxASSERT(metaData);
	IMetaValueTypeCtor* clsFactory =
		metaData->GetTypeCtor(m_metaObject, eCtorMetaType::eCtorMetaType_Object);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

CSourceExplorer CRecordDataObjectConstant::GetSourceExplorer() const
{
	CSourceExplorer srcHelper(
		m_metaObject, GetClassType(),
		false, true
	);

	srcHelper.AppendSource(m_metaObject);
	return srcHelper;
}

bool CRecordDataObjectConstant::GetModel(IValueModel*& tableValue, const meta_identifier_t& id)
{
	return false;
}

void CRecordDataObjectConstant::ShowFormValue()
{
	IBackendValueForm* const foundedForm = GetForm();

	if (foundedForm && foundedForm->IsShown()) {
		foundedForm->ActivateForm();
		return;
	}

	//if form is not initialized then generate  
	IBackendValueForm* valueForm = GetFormValue();

	valueForm->Modify(false);
	valueForm->ShowForm();
}

IBackendValueForm* CRecordDataObjectConstant::GetFormValue()
{
	IBackendValueForm* const foundedForm = GetForm();

	if (foundedForm != nullptr)
		return foundedForm;

	IBackendValueForm* valueForm = IBackendValueForm::CreateNewForm(nullptr, nullptr,
		this, m_metaObject->GetGuid()
	);;
	valueForm->BuildForm(defaultFormType);
	valueForm->Modify(false);

	return valueForm;
}

bool CRecordDataObjectConstant::SetValueByMetaID(const meta_identifier_t& id, const CValue& varMetaVal)
{
	if (id == m_metaObject->GetMetaID()) {
		IBackendValueForm* const foundedForm = IBackendValueForm::FindFormByUniqueKey(m_metaObject->GetGuid());
		m_constVal = m_metaObject->AdjustValue(varMetaVal);
		if (foundedForm != nullptr) {
			foundedForm->Modify(true);
		}
		return true;
	}
	return false;
}

bool CRecordDataObjectConstant::GetValueByMetaID(const meta_identifier_t& id, CValue& pvarMetaVal) const
{
	if (id == m_metaObject->GetMetaID()) {
		pvarMetaVal = m_constVal;
		return true;
	}

	return false;
}

#include "backend/databaseLayer/databaseLayer.h"

CValue CRecordDataObjectConstant::GetConstValue() const
{
	CValue ret;

	if (!appData->DesignerMode()) {
		const wxString& tableName = m_metaObject->GetTableNameDB();
		const wxString& fieldName = m_metaObject->GetFieldNameDB();
		if (db_query->TableExists(tableName)) {
			IDatabaseResultSet* resultSet = nullptr;
			if (db_query->GetDatabaseLayerType() == DATABASELAYER_POSTGRESQL)
				resultSet = db_query->RunQueryWithResults("SELECT %s FROM %s LIMIT 1", IMetaObjectAttribute::GetSQLFieldName(m_metaObject), tableName);
			else
				resultSet = db_query->RunQueryWithResults("SELECT FIRST 1 %s FROM %s", IMetaObjectAttribute::GetSQLFieldName(m_metaObject), tableName);
			if (resultSet == nullptr)
				return ret;
			if (resultSet->Next()) {
				if (IMetaObjectAttribute::GetValueAttribute(m_metaObject, ret, resultSet))
					ret = m_metaObject->AdjustValue(ret);
				else
					ret = m_metaObject->CreateValue();
			}
			else {
				ret = m_metaObject->CreateValue();
			}
			resultSet->Close();
		}
	}

	return ret;
}

#include "backend/databaseLayer/databaseErrorCodes.h" 

bool CRecordDataObjectConstant::SetConstValue(const CValue& cValue)
{
	if (!appData->DesignerMode()) {
		const wxString& tableName = m_metaObject->GetTableNameDB();
		const wxString& fieldName = m_metaObject->GetFieldNameDB();
		if (db_query->TableExists(tableName)) {
			
			IBackendValueForm* const foundedForm = GetForm();

			db_query->BeginTransaction();
			{
				CValue cancel = false;
				m_procUnit->CallAsProc("BeforeWrite", cancel);

				if (cancel.GetBoolean()) {
					db_query->RollBack(); CSystemFunction::Raise("failed to write object in db!"); return false;
				}
			}

			const CValue& adjustVal = m_metaObject->AdjustValue(cValue);

			//check fill attributes 
			bool fillCheck = true;

			if (m_metaObject->FillCheck()) {
				if (adjustVal.IsEmpty()) {
					wxString fillError =
						wxString::Format(_("""%s"" is a required field"), m_metaObject->GetSynonym());
					CSystemFunction::Message(fillError, eStatusMessage::eStatusMessage_Information);
					fillCheck = false;
				}
			}

			if (!fillCheck) {
				return false;
			}

			wxString sqlText = "";

			if (db_query->GetDatabaseLayerType() == DATABASELAYER_POSTGRESQL) {
				sqlText = "INSERT INTO %s (%s, RECORD_KEY) VALUES(";
				for (unsigned int idx = 0; idx < IMetaObjectAttribute::GetSQLFieldCount(m_metaObject); idx++) {
					sqlText += "?,";
				}
				sqlText += "'6')";
				sqlText += " ON CONFLICT (RECORD_KEY) ";
				sqlText += " DO UPDATE SET " + IMetaObjectAttribute::GetExcluteSQLFieldName(m_metaObject) + ";";
			}
			else {
				sqlText = "UPDATE OR INSERT INTO %s (%s, RECORD_KEY) VALUES(";
				for (unsigned int idx = 0; idx < IMetaObjectAttribute::GetSQLFieldCount(m_metaObject); idx++) {
					sqlText += "?,";
				}
				sqlText += "'6') MATCHING(RECORD_KEY);";
			}

			IPreparedStatement* statement =
				db_query->PrepareStatement(sqlText, tableName, IMetaObjectAttribute::GetSQLFieldName(m_metaObject));

			if (statement == nullptr) {
				return false;
			}

			int position = 1;

			IMetaObjectAttribute::SetValueAttribute(
				m_metaObject,
				adjustVal,
				statement,
				position
			);

			bool hasError = statement->RunQuery() == DATABASE_LAYER_QUERY_RESULT_ERROR;
			db_query->CloseStatement(statement);

			if (hasError) {
				db_query->RollBack(); CSystemFunction::Raise("failed to write object in db!"); return false;
			}

			{
				CValue cancel = false;
				m_procUnit->CallAsProc("OnWrite", cancel);
				if (cancel.GetBoolean()) {
					db_query->RollBack(); CSystemFunction::Raise("failed to write object in db!"); return false;
				}
			}

			db_query->Commit();

			if (foundedForm != nullptr) {
				foundedForm->Modify(false);
			}
		}
	}

	return true;
}


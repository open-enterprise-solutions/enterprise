////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : constants - db
////////////////////////////////////////////////////////////////////////////

#include "constant.h"
#include "backend/metaData.h"
#include "backend/system/systemManager.h"

#include "backend/appData.h"

//***********************************************************************
//*                           constant value                            *
//***********************************************************************

ibValueRecordDataObjectConstant* ibValueMetaObjectConstant::CreateRecordDataObjectValue()
{
	ibValueModuleManager* moduleManager = m_metaData->GetModuleManager();
	wxASSERT(moduleManager);

	ibValueRecordDataObjectConstant* pDataRef = nullptr;

	if (appData->DesignerMode()) {
		if (!moduleManager->FindCompileModule(m_propertyModule->GetMetaObject(), pDataRef))
			return ibValue::CreateAndPrepareValueRef<ibValueRecordDataObjectConstant>(this);
	}
	else {
		pDataRef = ibValue::CreateAndPrepareValueRef<ibValueRecordDataObjectConstant>(this);
	}

	return pDataRef;
}

//*********************************************************************************************
//*                                  ibValueRecordDataObjectConstant                                     *
//*********************************************************************************************

bool ibValueRecordDataObjectConstant::InitializeObject(const ibValueRecordDataObjectConstant* source)
{
	ibMetaData* metaData = m_metaObject->GetMetaData();
	wxASSERT(metaData);
	ibValueModuleManager* moduleManager = metaData->GetModuleManager();
	wxASSERT(moduleManager);

	if (!m_compileModule) {
		m_compileModule = new ibCompileModule(m_metaObject->GetModuleObject());
		m_compileModule->SetParent(moduleManager->GetCompileModule());
		m_compileModule->AddContextVariable(wxT("ThisObject"), this);
	}

	try {
		m_constValue = GetConstValue();
	}
	catch (const ibBackendException* err) {
		if (!appData->DesignerMode())
			throw(err);
		return false;
	}

	if (!appData->DesignerMode()) {
		m_procUnit = new ibProcUnit();
		m_procUnit->SetParent(moduleManager->GetProcUnit());
		try {
			m_compileModule->Compile();
		}
		catch (const ibBackendException* err) {
			if (!appData->DesignerMode())
				throw(err);
			return false;
		};
		m_procUnit->Execute(m_compileModule->m_cByteCode, true);
	}

	PrepareNames();
	//is Ok
	return true;
}

ibValueRecordDataObjectConstant::ibValueRecordDataObjectConstant(ibValueMetaObjectConstant* metaObject)
	: m_metaObject(metaObject), m_objModified(false), m_methodHelper(new ibValueMethodHelper())
{
	InitializeObject();
}

ibValueRecordDataObjectConstant::ibValueRecordDataObjectConstant(const ibValueRecordDataObjectConstant& source)
	: m_metaObject(source.m_metaObject), m_objModified(false), m_methodHelper(new ibValueMethodHelper())
{
	InitializeObject(&source);
}

ibValueRecordDataObjectConstant::~ibValueRecordDataObjectConstant()
{
	wxDELETE(m_methodHelper);
}

ibBackendValueForm* ibValueRecordDataObjectConstant::GetForm() const
{
	return ibBackendValueForm::FindFormByUniqueKey(m_metaObject->GetGuid());
}

void ibValueRecordDataObjectConstant::Modify(bool mod)
{
	ibBackendValueForm* const foundedForm = GetForm();

	if (foundedForm != nullptr)
		foundedForm->Modify(mod);

	m_objModified = mod;
}

#include "backend/objCtor.h"

ibClassID ibValueRecordDataObjectConstant::GetClassType() const
{
	const ibMetaData* metaData = m_metaObject->GetMetaData();
	wxASSERT(metaData);
	const ibCtorMetaValueType* clsFactory =
		metaData->GetTypeCtor(m_metaObject, ibCtorObjectMetaType::ibCtorObjectMetaType_Object);
	wxASSERT(clsFactory);
	return clsFactory->GetClassType();
}

wxString ibValueRecordDataObjectConstant::GetClassName() const
{
	const ibMetaData* metaData = m_metaObject->GetMetaData();
	wxASSERT(metaData);
	const ibCtorMetaValueType* clsFactory =
		metaData->GetTypeCtor(m_metaObject, ibCtorObjectMetaType::ibCtorObjectMetaType_Object);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

wxString ibValueRecordDataObjectConstant::GetString() const
{
	const ibMetaData* metaData = m_metaObject->GetMetaData();
	wxASSERT(metaData);
	const ibCtorMetaValueType* clsFactory =
		metaData->GetTypeCtor(m_metaObject, ibCtorObjectMetaType::ibCtorObjectMetaType_Object);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

ibSourceExplorer ibValueRecordDataObjectConstant::GetSourceExplorer() const
{
	ibSourceExplorer srcHelper(
		m_metaObject, GetClassType(),
		false, true
	);

	srcHelper.AppendSource(m_metaObject);
	return srcHelper;
}

bool ibValueRecordDataObjectConstant::GetModel(ibValueModel*& tableValue, const ibMetaID& id)
{
	return false;
}

#pragma region _form_builder_h_
void ibValueRecordDataObjectConstant::ShowFormValue()
{
	ibBackendValueForm* const foundedForm = GetForm();

	if (foundedForm && foundedForm->IsShown()) {
		foundedForm->ActivateForm();
		return;
	}

	//if form is not initialized then generate  
	ibBackendValueForm* const valueForm =
		GetFormValue();

	if (valueForm != nullptr) {
		valueForm->Modify(false);
		valueForm->ShowForm();
	}
}

ibBackendValueForm* ibValueRecordDataObjectConstant::GetFormValue()
{
	ibBackendValueForm* const foundedForm = GetForm();

	if (foundedForm == nullptr)
		return ibValueMetaObjectFormBase::CreateAndBuildForm(nullptr, nullptr, this, m_metaObject->GetGuid());

	return foundedForm;
}
#pragma endregion

bool ibValueRecordDataObjectConstant::SetValueByMetaID(const ibMetaID& id, const ibValue& varMetaVal)
{
	if (id == m_metaObject->GetMetaID()) {
		ibBackendValueForm* const foundedForm = ibBackendValueForm::FindFormByUniqueKey(m_metaObject->GetGuid());
		m_constValue = m_metaObject->AdjustValue(varMetaVal);
		if (foundedForm != nullptr) {
			foundedForm->Modify(true);
		}
		return true;
	}
	return false;
}

bool ibValueRecordDataObjectConstant::GetValueByMetaID(const ibMetaID& id, ibValue& pvarMetaVal) const
{
	if (id == m_metaObject->GetMetaID()) {
		pvarMetaVal = m_constValue;
		return true;
	}

	return false;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////

void ibValueRecordDataObjectConstant::PrepareNames() const
{
	m_methodHelper->ClearHelper();
	m_methodHelper->AppendProp(wxT("Value"),
		true, true, eValue, eSystem
	);

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

bool ibValueRecordDataObjectConstant::SetPropVal(const long lPropNum, const ibValue& varPropVal)
{
	const long lPropAlias = m_methodHelper->GetPropAlias(lPropNum);
	if (lPropAlias == eProcUnit) {
		if (m_procUnit != nullptr) {
			return m_procUnit->SetPropVal(
				GetPropName(lPropNum), varPropVal
			);
		}
	}
	else if (lPropAlias == eSystem) {
		m_constValue = varPropVal;
		return true;
	}
	return false;
}

bool ibValueRecordDataObjectConstant::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	const long lPropAlias = m_methodHelper->GetPropAlias(lPropNum);
	if (lPropAlias == eProcUnit) {
		if (m_procUnit != nullptr) {
			return m_procUnit->GetPropVal(
				GetPropName(lPropNum), pvarPropVal
			);
		}
	}
	else if (lPropAlias == eSystem) {
		switch (m_methodHelper->GetPropData(lPropNum))
		{
		case eValue:
			pvarPropVal = m_constValue;
			return true;
		}
	}
	return false;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////

#include "backend/databaseLayer/databaseLayer.h"

ibValue ibValueRecordDataObjectConstant::GetConstValue() const
{
	ibValue ret;

	if (!appData->DesignerMode()) {

		if (db_query != nullptr && !db_query->IsOpen())
			ibBackendCoreException::Error(_("Database is not open!"));
		else if (db_query == nullptr)
			ibBackendCoreException::Error(_("Database is not open!"));

		if (!m_metaObject->AccessRight_Read()) {
			ibBackendAccessException::Error();
			return false;
		}

		const wxString& tableName = m_metaObject->GetTableNameDB();
		const wxString& fieldName = m_metaObject->GetFieldNameDB();
		if (db_query->TableExists(tableName)) {
			ibDatabaseResultSet* resultSet = nullptr;
			if (db_query->GetDatabaseLayerType() != DATABASELAYER_FIREBIRD)
				resultSet = db_query->RunQueryWithResults("SELECT %s FROM %s LIMIT 1", ibValueMetaObjectAttributeBase::GetSQLFieldName(m_metaObject), tableName);
			else
				resultSet = db_query->RunQueryWithResults("SELECT FIRST 1 %s FROM %s", ibValueMetaObjectAttributeBase::GetSQLFieldName(m_metaObject), tableName);
			if (resultSet == nullptr)
				return ret;
			if (resultSet->Next()) {
				if (ibValueMetaObjectAttributeBase::GetValueAttribute(m_metaObject, ret, resultSet))
					ret = m_metaObject->AdjustValue(ret);
				else
					ret = m_metaObject->CreateValue();
			}
			else {
				ret = m_metaObject->CreateValue();
			}
			db_query->CloseResultSet(resultSet);
		}
	}
	else {
		ret = m_metaObject->AdjustValue();
	}

	return ret;
}

#include "backend/backend_mainFrame.h"
#include "backend/databaseLayer/databaseErrorCodes.h" 

bool ibValueRecordDataObjectConstant::SetConstValue(const ibValue& cValue)
{
	if (!appData->DesignerMode()) {

		const ibValue& constValue = m_constValue;

		if (db_query != nullptr && !db_query->IsOpen())
			ibBackendCoreException::Error(_("Database is not open!"));
		else if (db_query == nullptr)
			ibBackendCoreException::Error(_("Database is not open!"));

		if (!m_metaObject->AccessRight_Write()) {
			ibBackendAccessException::Error();
			return false;
		}

		const wxString& tableName = m_metaObject->GetTableNameDB();
		const wxString& fieldName = m_metaObject->GetFieldNameDB();

		if (db_query->TableExists(tableName)) {

			ibTransactionGuard db_query_active_transaction = db_query;
			{
				ibBackendValueForm* const valueForm = GetForm();

				db_query_active_transaction.BeginTransaction();
				{
					ibValue cancel = false;

					m_procUnit->CallAsProc(wxT("BeforeWrite"), cancel);

					if (cancel.GetBoolean()) {
						db_query_active_transaction.RollBackTransaction();
						ibBackendCoreException::Error(_("failed to write object in db!"));
						return false;
					}
				}

				m_constValue = m_metaObject->AdjustValue(cValue);

				//check fill attributes 
				bool fillCheck = true;

				if (m_metaObject->FillCheck()) {
					if (m_constValue.IsEmpty()) {
						wxString fillError =
							wxString::Format(_("""%s"" is a required field"), m_metaObject->GetSynonym());
						ibValueSystemFunction::Message(fillError, ibStatusMessage::ibStatusMessage_Information);
						fillCheck = false;
					}
				}

				if (!fillCheck) {
					m_constValue = constValue;
					return false;
				}

				wxString sqlText = "";

				if (db_query->GetDatabaseLayerType() != DATABASELAYER_FIREBIRD) {
					sqlText = "INSERT INTO %s (%s, RECORD_KEY) VALUES(";
					for (unsigned int idx = 0; idx < ibValueMetaObjectAttributeBase::GetSQLFieldCount(m_metaObject); idx++) {
						sqlText += "?,";
					}
					sqlText += "'6')";
					sqlText += " ON CONFLICT (RECORD_KEY) ";
					sqlText += " DO UPDATE SET " + ibValueMetaObjectAttributeBase::GetExcludeSQLFieldName(m_metaObject) + ";";
				}
				else {
					sqlText = "UPDATE OR INSERT INTO %s (%s, RECORD_KEY) VALUES(";
					for (unsigned int idx = 0; idx < ibValueMetaObjectAttributeBase::GetSQLFieldCount(m_metaObject); idx++) {
						sqlText += "?,";
					}
					sqlText += "'6') MATCHING(RECORD_KEY);";
				}

				ibPreparedStatement* statement =
					db_query->PrepareStatement(sqlText, tableName, ibValueMetaObjectAttributeBase::GetSQLFieldName(m_metaObject));

				if (statement == nullptr) {
					m_constValue = constValue;
					return false;
				}

				int position = 1;

				ibValueMetaObjectAttributeBase::SetValueAttribute(
					m_metaObject,
					m_constValue,
					statement,
					position
				);

				bool hasError = statement->RunQuery() == DATABASE_LAYER_QUERY_RESULT_ERROR;
				db_query->CloseStatement(statement);

				if (hasError) {
					m_constValue = constValue;
					db_query_active_transaction.RollBackTransaction();
					ibBackendCoreException::Error(_("Failed to write object in db!")); return false;
				}

				{
					ibValue cancel = false;
					m_procUnit->CallAsProc(wxT("OnWrite"), cancel);
					if (cancel.GetBoolean()) {
						db_query_active_transaction.RollBackTransaction();
						ibBackendCoreException::Error(_("Failed to write object in db!")); return false;
					}
				}

				db_query_active_transaction.CommitTransaction();

				if (valueForm != nullptr) valueForm->NotifyChange(GetValue());
			}
		}
	}

	return true;
}
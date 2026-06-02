////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : constants - db
////////////////////////////////////////////////////////////////////////////

#include "constant.h"
#include "backend/metaData.h"
#include "backend/system/systemManager.h"

#include "backend/appData.h"
#include "backend/databaseLayer/connectionPool.h"
#include "backend/session/session.h"
#include "backend/lock/lockManager.h"

//***********************************************************************
//*                           constant value                            *
//***********************************************************************

ibValueRecordDataObjectConstant* ibValueMetaObjectConstant::CreateRecordDataObjectValue() const
{
	ibValueRecordDataObjectConstant* pDataRef = nullptr;
	if (auto* cc = m_metaData->GetCompileCache()) {
		if (!cc->FindCompileModule(m_propertyModule->GetMetaObject(), pDataRef))
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
	ibValueModuleManager* moduleManager = ibSession::EditModuleManagerFor(m_metaObject->GetMetaData());
	wxASSERT(moduleManager);

	// Descriptor parent first — subsequent BindVariable /
	// InitializeRuntime picks up the parent on lazy creation, no
	// after-fact cascade needed.
	ibRuntimeModuleDataObject::SetParent(moduleManager);
	BindContextVariable(wxT("ThisObject"), this);
	// Constant's Value (Значение) is the module's own writable local — the
	// binder seeds the frame slot with &m_constValue, so `Value` / `Value = …`
	// inside the constant module read/write the backing member directly. No
	// AppendProp + eSystem GetPropVal/SetPropVal round-trip. The address is
	// stable (member); the value is filled by GetConstValue() just below.
	BindLocalVariable(wxT("Value"), &m_constValue);

	try {
		m_constValue = GetConstValue();
	}
	catch (const ibBackendException&) {
		if (!appData->DesignerMode())
			throw;
		return false;
	}

	InitializeRuntime();
	try {
		Compile();
	}
	catch (const ibBackendException&) {
		if (!appData->DesignerMode())
			throw;
		return false;
	};
	Run(true);

	PrepareNames();
	//is Ok
	return true;
}

ibValueRecordDataObjectConstant::ibValueRecordDataObjectConstant(const ibValueMetaObjectConstant* metaObject)
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
	// "Value" is no longer a host prop — it's a bound LOCAL of the constant
	// module (see BindLocalVariable in InitializeObject). Only the module's
	// own exported names surface here.
	ExportNamesToHelper(m_methodHelper, eProcUnit);
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

#include "backend/databaseLayer/databaseErrorCodes.h"

bool ibValueRecordDataObjectConstant::TryAcquireFormLock(ibLockMode mode)
{
	if (m_formLockHandle.IsValid()) return true;   // already held

	// Constant is one row globally — namespace IS the key, no per-row
	// sub-identifier. ForNamespace encapsulates the empty-keyFields
	// shape so call sites stay clean.
	auto* lm = ibApplicationData::GetLockManager();
	if (lm == nullptr)
		ibBackendCoreException::Error(_("Lock manager not initialised"));
	m_formLockHandle = lm->Acquire({
		ibLockItem::ForNamespace(m_metaObject->GetDocPath(), mode)
	});
	return true;
}

bool ibValueRecordDataObjectConstant::SetConstValue(const ibValue& cValue)
{
	if (appData->DesignerMode())
		return true;

	if (!m_metaObject->AccessRight_Write()) {
		ibBackendAccessException::Error();
		return false;
	}

	const ibValue& constValue = m_constValue;

	// Session-bound RAII scope — Acquires the session's conn, scope dtor
	// rolls back any uncommitted TX on early return / exception.
	ibConnectionScope scope = ibSession::Current()->OpenConnectionScope();
	if (!scope || !scope->IsOpen())
		ibBackendCoreException::Error(_("Database is not open!"));

	const wxString& tableName = m_metaObject->GetTableNameDB();
	ibBackendValueForm* const valueForm = GetForm();

	scope.SafeBeginTransaction();

	// Layer-1 DB row-lock on this constant's singleton row. Concurrent
	// writes to different constants don't conflict (each constant has
	// its own table); concurrent writes to THIS constant serialize on
	// the RECORD_KEY='6' row via the driver's FOR UPDATE / WITH LOCK.
	// See docs/record-locks.md.
	{
		const wxString hint = scope->RowLockHint();
		const wxString lockSql = wxT("SELECT 1 FROM ") + tableName
		                       + wxT(" WHERE RECORD_KEY = '6'")
		                       + (hint.IsEmpty() ? wxString() : wxT(" ") + hint)
		                       + wxT(";");
		ibDatabaseResultSet* lockRs = scope->RunQueryWithResults(lockSql);
		if (lockRs != nullptr) {
			// Drain — only the lock side effect matters.
			while (lockRs->Next()) {}
			scope->CloseResultSet(lockRs);
		}
	}

	auto rollback = [&]() {
		m_constValue = constValue;
		scope.SafeRollBackTransaction();
	};

	{
		ibValue cancel = false;
		ExecAsProc(wxT("BeforeWrite"), cancel);
		if (cancel.GetBoolean()) {
			rollback();
			ibBackendCoreException::Error(_("Failed to write object in db!"));
			return false;
		}
	}

	m_constValue = m_metaObject->AdjustValue(cValue);

	if (m_metaObject->FillCheck() && m_constValue.IsEmpty()) {
		const wxString fillError =
			wxString::Format(_("""%s"" is a required field"), m_metaObject->GetSynonym());
		ibValueSystemFunction::Message(fillError, ibStatusMessage::ibStatusMessage_Information);
		rollback();
		return false;
	}

	wxString sqlText;
	const bool isFB = (scope->GetDatabaseLayerType() == DATABASELAYER_FIREBIRD);
	if (isFB) {
		sqlText = "UPDATE OR INSERT INTO %s (%s, RECORD_KEY) VALUES(";
		for (unsigned int i = 0; i < ibValueMetaObjectAttributeBase::GetSQLFieldCount(m_metaObject); ++i)
			sqlText += "?,";
		sqlText += "'6') MATCHING(RECORD_KEY);";
	} else {
		sqlText = "INSERT INTO %s (%s, RECORD_KEY) VALUES(";
		for (unsigned int i = 0; i < ibValueMetaObjectAttributeBase::GetSQLFieldCount(m_metaObject); ++i)
			sqlText += "?,";
		sqlText += "'6') ON CONFLICT (RECORD_KEY) DO UPDATE SET "
		         + ibValueMetaObjectAttributeBase::GetExcludeSQLFieldName(m_metaObject) + ";";
	}

	ibStatementGuard statement(scope.get(), scope->PrepareStatement(
		sqlText, tableName, ibValueMetaObjectAttributeBase::GetSQLFieldName(m_metaObject)));
	if (!statement) {
		rollback();
		return false;
	}

	int position = 1;
	ibValueMetaObjectAttributeBase::SetValueAttribute(
		m_metaObject, m_constValue, statement.get(), position);

	if (statement->RunQuery() == DATABASE_LAYER_QUERY_RESULT_ERROR) {
		rollback();
		ibBackendCoreException::Error(_("Failed to write object in db!"));
		return false;
	}

	{
		ibValue cancel = false;
		ExecAsProc(wxT("OnWrite"), cancel);
		if (cancel.GetBoolean()) {
			rollback();
			ibBackendCoreException::Error(_("Failed to write object in db!"));
			return false;
		}
	}

	scope.SafeCommitTransaction();

	if (valueForm != nullptr) valueForm->NotifyChange(GetValue());

	return true;
}
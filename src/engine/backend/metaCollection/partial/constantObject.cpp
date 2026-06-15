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

// --- vended queryable — the constant's single-row (sys_const) table navigation ---
// The constant is the queryable's only column AND its one-row table; resolution by
// name / id yields the constant itself (m_meta), the value comes via GetValueAttribute.
const ibBackendQueryColumn* ibConstantQueryable::ResolveColumnByName(const wxString& name) const { return name == m_meta->GetName() ? m_meta : nullptr; }   // the constant IS its one column
wxString ibConstantQueryable::GetQueryTableName() const { return m_meta->GetPhysicalTableName(); }
ibMetaID ibConstantQueryable::GetQueryTableId() const { return m_meta->GetMetaID(); }
const ibMetaData* ibConstantQueryable::GetMetaData() const { return m_meta->GetMetaData(); }
std::vector<ibQuerySortItem> ibConstantQueryable::GetIdentitySort() const { return {}; }   // single row — no keyset
std::vector<const ibBackendQueryColumn*> ibConstantQueryable::GetPrimaryKeyColumns() const {
	// The single-row sys_const key (UPSERT match) — a RAW column, no metadata translation.
	static const ibRawDBColumn s_recordKey(wxT("RECORD_KEY"), ibRawDBColumn::RawType::String);
	return { &s_recordKey };
}
// (value materialisation moved to ibDbTableProvider — the queryable names no attribute / L1.)

//***********************************************************************
//*                           constant value                            *
//***********************************************************************

ibValueRecordDataObjectConstant* ibValueMetaObjectConstant::CreateRecordDataObjectValue() const
{
	ibValueRecordDataObjectConstant* pDataRef = nullptr;
	if (auto* cc = m_metaData->GetCompileCache()) {
		if (!cc->FindCompileModule(m_propertyModule->GetMetaObject(), pDataRef))
			return new ibValueRecordDataObjectConstant(this);
	}
	else {
		pDataRef = new ibValueRecordDataObjectConstant(this);
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

	//is Ok
	return true;
}

ibValueRecordDataObjectConstant::ibValueRecordDataObjectConstant(const ibValueMetaObjectConstant* metaObject)
	: ibValueDynamicMembers(ibValueTypes::TYPE_EMPTY), ibRuntimeModuleDataObject(m_members, this),
	m_metaObject(metaObject), m_objModified(false)
{
	InitializeObject();
}

ibValueRecordDataObjectConstant::ibValueRecordDataObjectConstant(const ibValueRecordDataObjectConstant& source)
	: ibValueDynamicMembers(ibValueTypes::TYPE_EMPTY), ibRuntimeModuleDataObject(m_members, this),
	m_metaObject(source.m_metaObject), m_objModified(false)
{
	InitializeObject(&source);
}

ibValueRecordDataObjectConstant::~ibValueRecordDataObjectConstant()
{
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

bool ibValueRecordDataObjectConstant::SetPropVal(const long lPropNum, const ibValue& varPropVal)
{
	const long lPropAlias = m_members.GetPropAlias(lPropNum);
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
	const long lPropAlias = m_members.GetPropAlias(lPropNum);
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

#include "backend/databaseLayer/databaseQueryBuilder.h"     // L2 door — open/table-exists gate + the RECORD_KEY row-lock (ir.m_lockForUpdate); value read is L3 (ibDataQueryBuilder)
#include "backend/query/dataQueryBuilder.h"                 // SetConstValue: L3 write core (UPSERT the singleton row)

ibValue ibValueRecordDataObjectConstant::GetConstValue() const
{
	ibValue ret;

	if (!appData->DesignerMode()) {

		ibDatabaseQueryBuilder dbq;   // L2 door for the open / table-exists gate (no raw ibDatabaseLayer)
		if (!dbq.IsOpen())
			ibBackendCoreException::Error(_("Database is not open!"));

		if (!m_metaObject->AccessRight_Read()) {
			ibBackendAccessException::Error();
			return false;
		}

		const wxString& tableName = m_metaObject->GetPhysicalTableName();
		if (dbq.TableExists(tableName)) {
			// Read the single sys_const row through the L3 door — the constant IS the
			// queryable (its table) AND the column (its value). The FB FIRST / others
			// LIMIT fork and the raw field concat are gone; the value comes from the
			// L3 selection (GetValue), then AdjustValue as before.
			try {
				ibDataQueryBuilder q;
				q.From(m_metaObject->GetQueryable());
				ibReadPageRequest page;
				page.m_count = 1;
				ibDataQueryResult selection = q.Execute(page);
				if (selection.Next())
					ret = m_metaObject->AdjustValue(selection.GetValue(m_metaObject));
				else
					ret = m_metaObject->CreateValue();
			}
			catch (...) {}
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

	const wxString& tableName = m_metaObject->GetPhysicalTableName();
	ibBackendValueForm* const valueForm = GetForm();

	scope.SafeBeginTransaction();

	// DB row-lock on this constant's singleton row. Concurrent writes to different constants don't
	// conflict (each constant has its own table); concurrent writes to THIS constant serialize on the
	// RECORD_KEY='6' row via the dialect's row-lock clause (FOR UPDATE / WITH LOCK). The L2 door runs
	// it on the session's bound conn — the SAME TX as the scope above — via q(session holder).
	// See docs/record-locks.md.
	{
		ibDatabaseQueryBuilder q(ibSession::Current()->Holder());
		ibQueryIR ir(ibProject(
			ibFilter(ibScan(tableName),
				ibBinOp(ibQueryBinOp::Eq, ibCol(wxT("RECORD_KEY")), ibConst(ibValue(wxString(wxT("6")))))),
			{ { ibConst(ibValue(1)), wxEmptyString } }));   // SELECT 1 — only the row lock matters
		ir.m_lockForUpdate = true;
		ibQueryResult lockRs = q.ExecuteIR(ir);
		while (lockRs.Next()) {}   // drain — only the lock side effect matters
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

	// UPSERT the singleton row through the L3 write door. RECORD_KEY is the row-key — a RAW
	// primary string column (constant value '6', matched on); the constant metaobject is
	// itself the data attribute (a column). The FB MATCHING / PG ON CONFLICT fork and the
	// manual '?,'-counting are gone — the dialect closes the UPSERT spelling. The door runs on
	// the session holder, so it joins the TX that already holds this row's lock.
	if (!ibDataQueryBuilder()
		.From(m_metaObject->GetQueryable())
		.SetValue(ibRawDBColumn::String(wxT("RECORD_KEY")), ibValue(wxT("6")))   // raw primary -> MATCHING
		.SetValue(m_metaObject, m_constValue)                                          // data attribute
		.Upsert()) {
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
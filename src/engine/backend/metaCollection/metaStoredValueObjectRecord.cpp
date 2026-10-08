////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : constants - db
////////////////////////////////////////////////////////////////////////////

#include "metaStoredValueObject.h"
#include "backend/metaData.h"
#include "backend/databaseLayer/connectionScope.h"   // ibConnectionScope — the write door's transaction
#include "backend/query/columnLayout.h"              // ibFieldSuffix — the value column's type tag, read as it lies
#include "backend/system/systemManager.h"

#include "backend/appData.h"
#include "backend/databaseLayer/connectionPool.h"
#include "backend/session/session.h"
#include "backend/lock/lockManager.h"

// --- vended queryable — the constant's single-row (sys_const) table navigation ---
// The constant is the queryable's only column AND its one-row table; resolution by
// name / id yields the constant itself (m_meta), the value comes via GetValueAttribute.
// The constant HAS one column, and it answers to the name that column reports — `Value`. Asking the
// column itself (rather than repeating the literal here) is what keeps the resolver and the catalogue
// from ever disagreeing about what the field is called.
const ibBackendQueryColumn* ibConstantQueryable::ResolveColumnByName(const wxString& name) const
{
	const ibValueMetaObjectStoredValue::ibValueMetaObjectConstantColumn* column = m_meta->GetValueColumn();
	return column != nullptr && name.IsSameAs(column->GetName(), false) ? column->GetQueryColumn() : nullptr;
}
// The metaobject behind this source — the guid and the metaID are READ OFF IT (see
// ibBackendQueryable::GetQueryTableGuid), not answered a second time here.
const ibValueMetaObjectGenericData* ibConstantQueryable::GetSourceMetaObject() const { return m_meta; }

wxString ibConstantQueryable::GetQueryTableName() const { return m_meta->GetPhysicalTableName(); }
const ibMetaData* ibConstantQueryable::GetMetaData() const { return m_meta->GetMetaData(); }
std::vector<const ibBackendQueryColumn*> ibConstantQueryable::GetPrimaryKeyColumns() const {
	// The single-row sys_const key (UPSERT match) — a RAW column, no metadata translation.
	static const ibBackendColumnRawDB s_recordKey(wxT("RECORD_KEY"), ibBackendColumnRawDB::RawType::String);
	return { &s_recordKey };
}
// (value materialisation moved to ibDbTableProvider — the queryable names no attribute / L1.)

//***********************************************************************
//*                           constant value                            *
//***********************************************************************

ibValuePtr<ibValueRecordDataObjectConstant> ibValueMetaObjectStoredValue::CreateRecordDataObjectValue() const
{
	ibValueRecordDataObjectConstant* pDataRef = nullptr;
	if (auto* cc = m_metaData->GetCompileCache()) {
		if (cc->FindCompileModule(m_propertyModule->GetMetaObject(), pDataRef))
			return ibValuePtr<ibValueRecordDataObjectConstant>(pDataRef);   // the cache's, initialised already
	}

	// Held BEFORE its module runs, as every data object is (issue #154): the module's top level may take
	// `ThisObject` and let it go again. It used to run inside the constructor, where nothing could hold it.
	const ibValuePtr<ibValueRecordDataObjectConstant> created(new ibValueRecordDataObjectConstant(this));
	created->InitializeObject();
	return created;
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
	// Constant's Value is the module's own writable local — the
	// binder seeds the frame slot with &m_constValue, so `Value` / `Value = …`
	// inside the constant module read/write the backing member directly. No
	// AppendProp + eSystem GetPropVal/SetPropVal round-trip. The address is
	// stable (member); the value is filled by GetConstValue() just below.
	BindLocalVariable(wxT("Value"), &m_constValue);

	try {
		m_constValue = GetConstValue();
	}
	catch (const ibCoreException&) {
		if (!appData->DesignerMode())
			throw;
		return false;
	}

	InitializeRuntime();
	try {
		Compile();
	}
	catch (const ibCoreException&) {
		if (!appData->DesignerMode())
			throw;
		return false;
	};
	Run(true);

	//is Ok
	return true;
}

ibValueRecordDataObjectConstant::ibValueRecordDataObjectConstant(const ibValueMetaObjectStoredValue* metaObject)
	: ibValueDynamicMembers(ibValueTypes::TYPE_EMPTY), ibRuntimeModuleDataObject(m_members, this),
	m_objModified(false), m_metaObject(metaObject)
{
	// (No InitializeObject here — the creator runs it once the object is held; see CreateRecordDataObjectValue.)
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
	// Same as the catalog / document objects (commonObject.cpp): marking a constant modified is an
	// operation on the constant, and looking for a window to tell is an afterthought that only
	// applies where windows exist. Getting a form still raises — it is asking from HERE that was
	// wrong, so the refusal is caught here.
	ibBackendValueForm* const foundedForm = ibFormToNotify([this] { return GetForm(); });

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

ibString ibValueRecordDataObjectConstant::GetString() const
{
	const ibMetaData* metaData = m_metaObject->GetMetaData();
	wxASSERT(metaData);
	const ibCtorMetaValueType* clsFactory =
		metaData->GetTypeCtor(m_metaObject, ibCtorObjectMetaType::ibCtorObjectMetaType_Object);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

const ibSourceExplorer* ibValueRecordDataObjectConstant::GetSourceExplorer() const
{
	if (!m_sourceExplorer.Reset(m_metaObject->GetMetaData()->GetFactoryCountChanges(),
		m_metaObject->GetName(), m_metaObject->GetSynonym(), m_metaObject->GetMetaID(), GetClassType(),
		false, true))
		return &m_sourceExplorer;   // built at this version of the metadata already — read as it is

	m_sourceExplorer.AppendColumn(m_metaObject->GetValueColumn()->GetQueryColumn());
	return &m_sourceExplorer;
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
	const ibFormPtr<ibBackendValueForm> valueForm =
		GetFormValue();

	if (valueForm) {
		valueForm->Modify(false);
		valueForm->ShowForm();
	}
}

ibFormPtr<ibBackendValueForm> ibValueRecordDataObjectConstant::GetFormValue()
{
	ibBackendValueForm* const foundedForm = GetForm();

	if (foundedForm == nullptr)
		return ibValueMetaObjectFormBase::CreateAndBuildForm(ibFormRequest(wxString(), m_metaObject->GetGuid()), nullptr, nullptr, this);

	return ibFormPtr<ibBackendValueForm>(foundedForm);   // the open one — its window holds it
}
#pragma endregion

bool ibValueRecordDataObjectConstant::SetValueByMetaID(const ibMetaID& id, const ibValue& varMetaVal)
{
	if (id == m_metaObject->GetMetaID()) {
		// THE VALUE IS SET WHATEVER HAPPENS BELOW. This is modifiedness again wearing another name:
		// the assignment is the operation, telling an open window is the afterthought. Through
		// GetForm rather than the registry directly, so there is one road to catch on.
		m_constValue = m_metaObject->AdjustValue(varMetaVal);
		ibBackendValueForm* const foundedForm = ibFormToNotify([this] { return GetForm(); });
		if (foundedForm != nullptr)
			foundedForm->Modify(true);
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

		ibDatabaseQueryBuilder dbq;   // L2 door for the open gate (no raw ibDatabaseLayer)
		if (!dbq.IsOpen())
			ibBackendCoreException::Error(_("Database is not open!"));

		if (!m_metaObject->AccessRight_Read()) {
			ibBackendAccessException::Error(wxString::Format(_("reading constant '%s'"),
				m_metaObject->GetSynonym()));
			return false;
		}

		ret = m_metaObject->ReadStoredValue();
	}
	else {
		ret = m_metaObject->AdjustValue();
	}

	return ret;
}

ibValue ibValueMetaObjectStoredValue::ReadStoredValue() const
{
	ibDatabaseQueryBuilder dbq;   // L2 door for the table-exists gate (no raw ibDatabaseLayer)
	if (dbq.TableExists(GetPhysicalTableName())) {
		// Read the single sys_const row through the L3 door — the constant IS the
		// queryable (its table) AND the column (its value). The FB FIRST / others
		// LIMIT fork and the raw field concat are gone; the value comes from the
		// L3 selection (GetValue).
		try {
			ibDataQueryBuilder q;
			q.From(GetQueryable());
			ibReadPageRequest page;
			page.m_count = 1;
			ibDataQueryResult selection = q.Execute(page);

			// ⚠ NOTHING WRITTEN is told by the value column's TYPE TAG, read AS IT LIES: 0 until the first write
			// stores the value's own type (a column added after the row was defaults to 0 too). Read through the
			// column, an untagged cell comes back as its type's EMPTY (columnLayout.cpp, the tag's `default`
			// arm) — a Boolean's False — so a never-switched functional option read as switched off
			// (measured 2026-09-27: `FunctionalOptions.X.Get()` answered False with nothing ever written).
			if (selection.Next()) {
				const ibBackendColumnRawDB tag(GetValueColumn()->GetPhysicalName() + ibFieldSuffix(ibColumnRole::Discriminator),
					ibBackendColumnRawDB::RawType::Number);
				const ibValue written = selection.GetValue(tag);
				if (written.GetType() == ibValueTypes::TYPE_NUMBER && written.GetInteger() != 0)
					return AdjustValue(selection.GetValue(GetValueColumn()->GetQueryColumn()));
			}
		}
		catch (...) {}
	}

	// No table, no row, no tag: a fresh value of this kind (a constant's type's empty, an option's initial value).
	return CreateValue();
}

#include "backend/databaseLayer/databaseErrorCodes.h"

bool ibValueRecordDataObjectConstant::TryAcquireFormLock(ibLockMode mode)
{
	if (m_formLockHandle.IsValid()) return true;   // already held

	// Constant is one row globally — namespace IS the key, no per-row
	// sub-identifier. ForNamespace encapsulates the empty-keyFields
	// shape so call sites stay clean.
	auto* lm = ibApplicationInstance::GetLockManager();
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
		ibBackendAccessException::Error(wxString::Format(_("writing constant '%s'"),
			m_metaObject->GetSynonym()));
		return false;
	}

	const ibValue& constValue = m_constValue;

	// Session-bound RAII scope — Acquires the session's conn, scope dtor
	// rolls back any uncommitted TX on early return / exception.
	ibConnectionScope scope = ibSession::Current()->OpenConnectionScope();
	if (!scope || !scope->IsOpen())
		ibBackendCoreException::Error(_("Database is not open!"));

	const wxString& tableName = m_metaObject->GetPhysicalTableName();
	// Told afterwards if there is anybody to tell — see ibFormToNotify (backend_form.h).
	ibBackendValueForm* const valueForm = ibFormToNotify([this] { return GetForm(); });

	scope.SafeBeginTransaction();

	// DB row-lock on this constant's singleton row. Concurrent writes to different constants don't
	// conflict (each constant has its own table); concurrent writes to THIS constant serialize on the
	// RECORD_KEY='6' row via the dialect's row-lock clause (FOR UPDATE / WITH LOCK). The L2 door runs
	// it on the session's bound conn — the SAME TX as the scope above — via q(session holder).
	// See docs/private/record-locks.md.
	{
		ibDatabaseQueryBuilder q(ibSession::Current()->Holder());
		ibQueryIR ir(ibProject(
			ibFilter(ibScan(tableName),
				ibBinOp(ibQueryBinOp::Eq, ibCol(wxT("RECORD_KEY")), ibConst(ibValue(wxString(wxT("6")))))),
			{ { ibCol(wxT("RECORD_KEY")), wxEmptyString } }));   // project the TYPED key column, not a bare const (SELECT ? = FB -804); only the row lock matters
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
		ExecAsEvent(wxT("BeforeWrite"), cancel);
		if (cancel.GetBoolean()) {
			rollback();
			ibBackendCoreException::Error(_("Constant '%s': writing cancelled by the BeforeWrite handler"),
				m_metaObject->GetSynonym());
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
		.SetValue(ibBackendColumnRawDB::String(wxT("RECORD_KEY")), ibValue(wxT("6")))   // raw primary -> MATCHING
		.SetValue(m_metaObject->GetValueColumn()->GetQueryColumn(), m_constValue)                        // the value column
		.Upsert()) {
		rollback();
		ibBackendCoreException::Error(_("Constant '%s': failed to store the value"),
			m_metaObject->GetSynonym());
		return false;
	}

	{
		ibValue cancel = false;
		ExecAsEvent(wxT("OnWrite"), cancel);
		if (cancel.GetBoolean()) {
			rollback();
			ibBackendCoreException::Error(_("Constant '%s': writing cancelled by the OnWrite handler"),
				m_metaObject->GetSynonym());
			return false;
		}
	}

	scope.SafeCommitTransaction();

	m_metaObject->OnAfterValueWrite();

	if (valueForm != nullptr) valueForm->NotifyChange(GetValue());

	return true;
}
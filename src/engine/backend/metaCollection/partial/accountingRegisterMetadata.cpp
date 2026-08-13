////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : accounting register metaData
////////////////////////////////////////////////////////////////////////////

#include "accountingRegister.h"
#include "backend/serialize/dataBuilder.h"
#include "chartOfAccounts.h"
#include "chartOfCharacteristicTypes.h"   // the CONTOUR — a slot's value type is the chart's own composition
#include "backend/system/value/valueDynamicList.h"   // ibValueDynamicList — the standard list migrates onto the universal dynamic list
#include "backend/metadataConfiguration.h"
#include "backend/moduleManager/moduleManager.h"


ibValueMetaObjectAccountingRegister::ibValueMetaObjectAccountingRegister() : ibValueMetaObjectRegisterData()
{
	(*m_propertyObjectModule)->SetDefaultProcedure(wxT("BeforeWrite"), ibContentHelper::eProcedureHelper, { wxT("Cancel") });
	(*m_propertyObjectModule)->SetDefaultProcedure(wxT("OnWrite"), ibContentHelper::eProcedureHelper, { wxT("Cancel") });
}

ibValueMetaObjectAccountingRegister::~ibValueMetaObjectAccountingRegister()
{
}

ibValueMetaObjectFormBase* ibValueMetaObjectAccountingRegister::GetDefaultFormByID(const ibFormID& id) const
{
	if (id == eFormList && m_propertyDefFormList->GetValueAsInteger() != wxNOT_FOUND)
		return FindFormObjectByFilter(m_propertyDefFormList->GetValueAsInteger());
	return nullptr;
}

#pragma region _form_builder_h_
ibBackendValueForm* ibValueMetaObjectAccountingRegister::GetListForm(const wxString& strFormName, ibBackendControlFrame* ownerControl, const ibUniqueKey& formGuid) const
{
	return ibValueMetaObjectGenericData::CreateAndBuildForm(strFormName, eFormList, ownerControl,
		ibCreateList(GetQueryable(), GetRegisterPeriod()), formGuid);   // migrated onto the universal dynamic list
}
#pragma endregion

bool ibValueMetaObjectAccountingRegister::ReadData(const ibDataNode& node)
{
	m_propertyAttributeRecordType->ReadNodeValue(node.GetProperty(m_propertyAttributeRecordType->GetName()));
	m_propertyAttributeAccount->ReadNodeValue(node.GetProperty(m_propertyAttributeAccount->GetName()));
	// THE SLOTS COME BACK FROM THE FILE, not from a count.
	//
	// Their number is the chart of accounts' business, but their IDS are theirs alone — an id is
	// what the physical column is named after (fld<metaID>), so a slot rebuilt with a fresh one
	// would point at a column holding no data. Read until the names run out: the file itself says
	// how many were saved, and that is the only source that cannot disagree with what is stored.
	for (unsigned int no = 1; ; no++) {
		const wxString slotName = wxString::Format(wxT("AccountDimension%u"), no);
		const ibDataValue* saved = node.FindProperty(slotName);
		if (saved == nullptr)
			break;

		ibValueMetaObjectAttributePredefined* slot = CreateEmptyType(
			slotName, wxString::Format(_("Account dimension %u"), no),
			wxEmptyString, false, ibItemMode::ibItemMode_Item);

		const std::shared_ptr<ibDataNode>& child = saved->AsChild();
		if (child)
			slot->LoadNode(*child);

		// The KIND half of the same pair — its own metaobject, its own id, its own column.
		const wxString kindName = wxString::Format(wxT("AccountDimension%uKind"), no);
		ibValueMetaObjectAttributePredefined* kind = CreateEmptyType(
			kindName, wxString::Format(_("Account dimension %u kind"), no),
			wxEmptyString, false, ibItemMode::ibItemMode_Item);

		if (const ibDataValue* savedKind = node.FindProperty(kindName)) {
			const std::shared_ptr<ibDataNode>& kindChild = savedKind->AsChild();
			if (kindChild)
				kind->LoadNode(*kindChild);
		}

		m_accountDimensionKinds.push_back(kind);
		m_accountDimensionSlots.push_back(slot);
	}
	m_accountDimensionCount = static_cast<unsigned int>(m_accountDimensionSlots.size());

	// The credit side. Its presence in the file IS the record that this register was saved in
	// correspondence mode — the property says so too, and the two are written together.
	for (unsigned int no = 1; ; no++) {
		const wxString valueName = wxString::Format(wxT("AccountDimensionCr%u"), no);
		const ibDataValue* savedValue = node.FindProperty(valueName);
		if (savedValue == nullptr)
			break;

		ibValueMetaObjectAttributePredefined* value = CreateEmptyType(
			valueName, wxString::Format(_("Account dimension Cr%u"), no),
			wxEmptyString, false, ibItemMode::ibItemMode_Item);
		const std::shared_ptr<ibDataNode>& valueChild = savedValue->AsChild();
		if (valueChild)
			value->LoadNode(*valueChild);

		const wxString kindName = wxString::Format(wxT("AccountDimensionCr%uKind"), no);
		ibValueMetaObjectAttributePredefined* kind = CreateEmptyType(
			kindName, wxString::Format(_("Account dimension Cr%u kind"), no),
			wxEmptyString, false, ibItemMode::ibItemMode_Item);
		if (const ibDataValue* savedKind = node.FindProperty(kindName)) {
			const std::shared_ptr<ibDataNode>& kindChild = savedKind->AsChild();
			if (kindChild)
				kind->LoadNode(*kindChild);
		}

		m_accountDimensionKindsCr.push_back(kind);
		m_accountDimensionSlotsCr.push_back(value);
	}

	if (const ibDataValue* savedAccountCr = node.FindProperty(wxT("AccountCr"))) {
		m_accountCr = CreateEmptyType(wxT("AccountCr"), _("Credit account"),
			wxEmptyString, false, ibItemMode::ibItemMode_Item);
		const std::shared_ptr<ibDataNode>& child = savedAccountCr->AsChild();
		if (child)
			m_accountCr->LoadNode(*child);
	}

	m_propertyDefFormList->SetValue(GetIdByGuid(node.GetValue<wxString>(m_propertyDefFormList->GetName())));

	m_propertyChartOfAccounts->ReadNodeValue(node.GetProperty(m_propertyChartOfAccounts->GetName()));

	m_propertyObjectModule->ReadNodeValue(node.GetProperty(m_propertyObjectModule->GetName()));
	m_propertyManagerModule->ReadNodeValue(node.GetProperty(m_propertyManagerModule->GetName()));

	return ibValueMetaObjectRegisterData::ReadData(node);
}

bool ibValueMetaObjectAccountingRegister::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertyAttributeRecordType->GetName(), m_propertyAttributeRecordType->GetNodeValue());
	node.SetProperty(m_propertyAttributeAccount->GetName(), m_propertyAttributeAccount->GetNodeValue());
	// Only the ACTIVE slots are written — a deactivated one has no column and no business in the
	// file. Each rides its whole node, id included, keyed by its own name.
	for (unsigned int idx = 0; idx < m_accountDimensionCount; idx++) {
		auto kindChild = std::make_shared<ibDataNode>();
		m_accountDimensionKinds[idx]->SaveNode(*kindChild);
		node.SetProperty(m_accountDimensionKinds[idx]->GetName(), ibDataValue::Child(kindChild));

		auto child = std::make_shared<ibDataNode>();
		m_accountDimensionSlots[idx]->SaveNode(*child);
		node.SetProperty(m_accountDimensionSlots[idx]->GetName(), ibDataValue::Child(child));
	}

	// The credit side, when there is one. Same shape, its own names, its own ids.
	for (size_t idx = 0; idx < m_accountDimensionSlotsCr.size(); idx++) {
		auto kindChild = std::make_shared<ibDataNode>();
		m_accountDimensionKindsCr[idx]->SaveNode(*kindChild);
		node.SetProperty(m_accountDimensionKindsCr[idx]->GetName(), ibDataValue::Child(kindChild));

		auto child = std::make_shared<ibDataNode>();
		m_accountDimensionSlotsCr[idx]->SaveNode(*child);
		node.SetProperty(m_accountDimensionSlotsCr[idx]->GetName(), ibDataValue::Child(child));
	}

	if (m_accountCr != nullptr) {
		auto child = std::make_shared<ibDataNode>();
		m_accountCr->SaveNode(*child);
		node.SetProperty(m_accountCr->GetName(), ibDataValue::Child(child));
	}

	node.SetValue(m_propertyDefFormList->GetName(), GetGuidByID(m_propertyDefFormList->GetValueAsInteger()).str());

	node.SetProperty(m_propertyChartOfAccounts->GetName(), m_propertyChartOfAccounts->GetNodeValue());

	node.SetProperty(m_propertyObjectModule->GetName(), m_propertyObjectModule->GetNodeValue());
	node.SetProperty(m_propertyManagerModule->GetName(), m_propertyManagerModule->GetNodeValue());

	return ibValueMetaObjectRegisterData::WriteData(node);
}


#include "backend/appData.h"

bool ibValueMetaObjectAccountingRegister::OnCreateMetaObject(ibMetaData* metaData, int flags)
{
	if (!ibValueMetaObjectRegisterData::OnCreateMetaObject(metaData, flags)) return false;
	return (*m_propertyAttributeRecordType)->OnCreateMetaObject(metaData, flags) &&
		(*m_propertyAttributeAccount)->OnCreateMetaObject(metaData, flags) &&
		(*m_propertyManagerModule)->OnCreateMetaObject(metaData, flags) &&
		(*m_propertyObjectModule)->OnCreateMetaObject(metaData, flags);
}

bool ibValueMetaObjectAccountingRegister::OnLoadMetaObject(ibMetaData* metaData)
{
	if (!(*m_propertyAttributeRecordType)->OnLoadMetaObject(metaData)) return false;
	if (!(*m_propertyAttributeAccount)->OnLoadMetaObject(metaData)) return false;
	// Every slot the file brought back — not the active count: a slot outside it is still a live
	// metaobject that has to travel the same lifecycle as the rest.
	for (ibValueMetaObjectAttributePredefined* slot : m_accountDimensionKinds)
		if (!slot->OnLoadMetaObject(metaData)) return false;
	for (ibValueMetaObjectAttributePredefined* slot : m_accountDimensionSlots)
		if (!slot->OnLoadMetaObject(metaData)) return false;
	if (!(*m_propertyManagerModule)->OnLoadMetaObject(metaData)) return false;
	if (!(*m_propertyObjectModule)->OnLoadMetaObject(metaData)) return false;
	return ibValueMetaObjectRegisterData::OnLoadMetaObject(metaData);
}

bool ibValueMetaObjectAccountingRegister::OnSaveMetaObject(int flags)
{
	if (!(*m_propertyAttributeRecordType)->OnSaveMetaObject(flags)) return false;
	if (!(*m_propertyAttributeAccount)->OnSaveMetaObject(flags)) return false;
	for (ibValueMetaObjectAttributePredefined* slot : m_accountDimensionKinds)
		if (!slot->OnSaveMetaObject(flags)) return false;
	for (ibValueMetaObjectAttributePredefined* slot : m_accountDimensionSlots)
		if (!slot->OnSaveMetaObject(flags)) return false;
	if (!(*m_propertyManagerModule)->OnSaveMetaObject(flags)) return false;
	if (!(*m_propertyObjectModule)->OnSaveMetaObject(flags)) return false;
	return ibValueMetaObjectRegisterData::OnSaveMetaObject(flags);
}

bool ibValueMetaObjectAccountingRegister::OnDeleteMetaObject()
{
	if (!(*m_propertyAttributeRecordType)->OnDeleteMetaObject()) return false;
	if (!(*m_propertyAttributeAccount)->OnDeleteMetaObject()) return false;
	for (ibValueMetaObjectAttributePredefined* slot : m_accountDimensionKinds)
		if (!slot->OnDeleteMetaObject()) return false;
	for (ibValueMetaObjectAttributePredefined* slot : m_accountDimensionSlots)
		if (!slot->OnDeleteMetaObject()) return false;
	if (!(*m_propertyManagerModule)->OnDeleteMetaObject()) return false;
	if (!(*m_propertyObjectModule)->OnDeleteMetaObject()) return false;
	return ibValueMetaObjectRegisterData::OnDeleteMetaObject();
}

bool ibValueMetaObjectAccountingRegister::OnReloadMetaObject()
{
	if (auto* cc = m_metaData->GetCompileCache()) {
		ibValueRecordSetObjectAccountingRegister* recordSet = nullptr;
		if (cc->FindCompileModule(m_propertyObjectModule->GetMetaObject(), recordSet)) {
			if (!recordSet->InitializeObject()) return false;
		}
	}
	return true;
}

#include "backend/objCtor.h"

bool ibValueMetaObjectAccountingRegister::OnBeforeRunMetaObject(int flags)
{
	if (!(*m_propertyAttributeRecordType)->OnBeforeRunMetaObject(flags)) return false;
	if (!(*m_propertyAttributeAccount)->OnBeforeRunMetaObject(flags)) return false;
	for (ibValueMetaObjectAttributePredefined* slot : m_accountDimensionKinds)
		if (!slot->OnBeforeRunMetaObject(flags)) return false;
	for (ibValueMetaObjectAttributePredefined* slot : m_accountDimensionSlots)
		if (!slot->OnBeforeRunMetaObject(flags)) return false;
	if (!(*m_propertyManagerModule)->OnBeforeRunMetaObject(flags)) return false;
	if (!(*m_propertyObjectModule)->OnBeforeRunMetaObject(flags)) return false;
	registerSelection();
	return ibValueMetaObjectRegisterData::OnBeforeRunMetaObject(flags);
}

// BRING THE SLOT SET IN LINE WITH THE CHART OF ACCOUNTS.
//
// The count is asked for, never stored here: the chart declares it, this register builds columns
// from it. Slots are created once and REUSED — a metaID is the physical column name (fld<metaID>),
// so handing a slot a fresh id would point it at a column that does not hold its data. Lowering the
// number therefore deactivates from the TAIL (the surplus stays alive, stops being contributed, and
// its column drops at the next restructuring); raising it again finds the very same slots.
//
// More than one chart may be bound; the widest one wins, because a slot that some account never
// fills costs an empty column, while a missing slot costs an account its analytics.
void ibValueMetaObjectAccountingRegister::SyncAccountDimensionSlots()
{
	unsigned int want = 0;

	const ibMetaDescription& metaDesc = m_propertyChartOfAccounts->GetValueAsMetaDesc();
	for (unsigned int idx = 0; idx < metaDesc.GetTypeCount(); idx++) {
		const ibValueMetaObject* chartOfAccounts = m_metaData->FindAnyObjectByFilter(metaDesc.GetByIdx(idx));
		const ibValueMetaObjectChartOfAccounts* chartOfAccountsObj = nullptr;
		if (chartOfAccounts != nullptr && chartOfAccounts->ConvertToValue(chartOfAccountsObj) && chartOfAccountsObj != nullptr)
			want = std::max(want, chartOfAccountsObj->GetMaxAccountDimensionCount());
	}

	// One dimension is a PAIR of columns; the side prefix appears only where there are two sides to
	// tell apart. In a one-sided register the side is already said by RecordType, so the name
	// carries none.
	auto makePair = [&](const wxString& prefix, unsigned int no,
		std::vector<ibValueMetaObjectAttributePredefined*>& kinds,
		std::vector<ibValueMetaObjectAttributePredefined*>& values) {
		// Numbered, because a slot is a POSITION: the same slot holds an item on one account and a
		// counterparty on another, so a meaningful name would be a lie. Meaning comes from the kind
		// stored next to it — which is why a slot is created as a PAIR.
		ibValueMetaObjectAttributePredefined* kind = CreateEmptyType(
			wxString::Format(wxT("AccountDimension%s%uKind"), prefix, no),
			wxString::Format(_("Account dimension %s%u kind"), prefix, no),
			wxEmptyString, false, ibItemMode::ibItemMode_Item);

		ibValueMetaObjectAttributePredefined* value = CreateEmptyType(
			wxString::Format(wxT("AccountDimension%s%u"), prefix, no),
			wxString::Format(_("Account dimension %s%u"), prefix, no),
			wxEmptyString, false, ibItemMode::ibItemMode_Item);

		// The id is what makes each of them a column, and it is handed out by the create event — a
		// slot born after the register's own creation has to ask for one itself. GenerateNewID is
		// monotonic, so a number it gets here can never be one a dropped slot used to own.
		if (m_metaData != nullptr) {
			kind->OnCreateMetaObject(m_metaData, 0);
			value->OnCreateMetaObject(m_metaData, 0);
		}

		kinds.push_back(kind);
		values.push_back(value);
	};

	const bool correspondence = IsCorrespondence();
	const wxString debitPrefix = correspondence ? wxT("Dr") : wxEmptyString;

	// THE CREDIT ACCOUNT. In correspondence mode the line names both sides, so the inherited
	// Account becomes the debit one and this is its counterpart. Its type is the same as the debit
	// account's — both are references into the same chart — and it is set where that one is set.
	if (correspondence && m_accountCr == nullptr) {
		m_accountCr = CreateEmptyType(wxT("AccountCr"), _("Credit account"),
			wxEmptyString, false, ibItemMode::ibItemMode_Item);
		if (m_metaData != nullptr)
			m_accountCr->OnCreateMetaObject(m_metaData, 0);
	}

	while (m_accountDimensionSlots.size() < want)
		makePair(debitPrefix, static_cast<unsigned int>(m_accountDimensionSlots.size()) + 1,
			m_accountDimensionKinds, m_accountDimensionSlots);

	// The credit side exists only in correspondence mode. Switching correspondence OFF leaves the
	// credit slots alive but uncontributed, exactly as lowering the count does — their data goes
	// with their columns at the next restructuring, which is why this is a warning-worthy switch.
	if (correspondence) {
		while (m_accountDimensionSlotsCr.size() < want)
			makePair(wxT("Cr"), static_cast<unsigned int>(m_accountDimensionSlotsCr.size()) + 1,
				m_accountDimensionKindsCr, m_accountDimensionSlotsCr);
	}
	else {
		m_accountDimensionKindsCr.clear();
		m_accountDimensionSlotsCr.clear();
	}

	m_accountDimensionCount = want;
}

bool ibValueMetaObjectAccountingRegister::OnAfterRunMetaObject(int flags)
{
	if (!(*m_propertyAttributeRecordType)->OnAfterRunMetaObject(flags)) return false;
	if (!(*m_propertyAttributeAccount)->OnAfterRunMetaObject(flags)) return false;
	for (ibValueMetaObjectAttributePredefined* slot : m_accountDimensionKinds)
		if (!slot->OnAfterRunMetaObject(flags)) return false;
	for (ibValueMetaObjectAttributePredefined* slot : m_accountDimensionSlots)
		if (!slot->OnAfterRunMetaObject(flags)) return false;
	if (!(*m_propertyManagerModule)->OnAfterRunMetaObject(flags)) return false;
	if (!(*m_propertyObjectModule)->OnAfterRunMetaObject(flags)) return false;


	// Set Account field type from Chart of Accounts binding
	const ibMetaDescription& metaDesc = m_propertyChartOfAccounts->GetValueAsMetaDesc();
	ibTypeDescription typeDesc;
	for (unsigned int idx = 0; idx < metaDesc.GetTypeCount(); idx++) {
		const ibValueMetaObject* chartOfAccounts = m_metaData->FindAnyObjectByFilter(metaDesc.GetByIdx(idx));
		if (chartOfAccounts != nullptr) {
			const ibCtorMetaValueType* so = m_metaData->GetTypeCtor(chartOfAccounts, ibCtorObjectMetaType::ibCtorObjectMetaType_Reference);
			wxASSERT(so);
			typeDesc.AppendMetaType(so->GetClassType());
		}
	}
	(*m_propertyAttributeAccount)->SetDefaultMetaType(typeDesc);
	// The credit account is a reference into the SAME chart — one declaration, both sides.
	if (m_accountCr != nullptr)
		m_accountCr->GetTypeDesc().SetDefaultMetaType(typeDesc);

	if ((*m_propertyAttributeAccount)->GetClsidCount() > 0)
		(*m_propertyAttributeAccount)->ClearFlag(metaDisableFlag);
	else
		(*m_propertyAttributeAccount)->SetFlag(metaDisableFlag);

	// THE DIMENSION SLOTS — how many, and of what type. Two questions, two sources:
	// the chart of ACCOUNTS says how many slots exist, the chart of CHARACTERISTIC TYPES bound to
	// it says what a slot may hold. Neither is derivable from the other.
	SyncAccountDimensionSlots();

	for (unsigned int idx = 0; idx < metaDesc.GetTypeCount(); idx++) {
		const ibValueMetaObject* chartOfAccounts = m_metaData->FindAnyObjectByFilter(metaDesc.GetByIdx(idx));
		if (chartOfAccounts == nullptr)
			continue;

		const ibValueMetaObjectChartOfAccounts* chartOfAccountsObj = nullptr;
		if (!chartOfAccounts->ConvertToValue(chartOfAccountsObj) || chartOfAccountsObj == nullptr)
			continue;

		ibPropertyChartOfCharacteristicTypes* pvhBinding = chartOfAccountsObj->GetChartOfCharacteristicTypes();
		if (pvhBinding == nullptr)
			continue;

		// THE CONTOUR, not a reference to a kind. A slot holds the VALUE of a characteristic, and
		// what a characteristic may be is exactly what the chart declares as its composition
		// (TypesOfCharacteristics, vended by GetTypeDesc). The chart's ELEMENTS are the KINDS, and a
		// kind is stored separately, in the account's kinds table beside the value.
		//
		// This used to append a Reference-to-element type instead, which typed the value slot as the
		// kind slot — "Contractors" could be written where "OOO Romashka" belongs.
		const ibMetaDescription& pvhDesc = pvhBinding->GetValueAsMetaDesc();

		ibTypeDescription kindTypeDesc;   // reference to a characteristic — the KIND half
		ibTypeDescription valueTypeDesc;  // the chart's composition — the VALUE half

		for (unsigned int pvhIdx = 0; pvhIdx < pvhDesc.GetTypeCount(); pvhIdx++) {
			const ibValueMetaObject* pvh = m_metaData->FindAnyObjectByFilter(pvhDesc.GetByIdx(pvhIdx));
			if (pvh == nullptr)
				continue;

			// The KIND is an element of the chart — an ordinary reference, exactly as a reference
			// to a catalog item.
			const ibCtorMetaValueType* pvhCtor = m_metaData->GetTypeCtor(pvh, ibCtorObjectMetaType::ibCtorObjectMetaType_Reference);
			if (pvhCtor != nullptr)
				kindTypeDesc.AppendMetaType(pvhCtor->GetClassType());

			// The VALUE is whatever a characteristic of this chart may BE. The chart answers that
			// itself — GetTypeDesc vends TypesOfCharacteristics — so the slot asks rather than
			// rebuilds, and it cannot drift from what the chart declares.
			// ⚠ One bound chart is the supported case: the composition is taken WHOLE, so with more
			// than one bound the last wins rather than the union. Merging them needs an append over
			// a type description, which this type does not vend — worth adding only when a
			// configuration actually binds two charts to one chart of accounts.
			const ibValueMetaObjectChartOfCharacteristicTypes* pvhObj = nullptr;
			if (pvh->ConvertToValue(pvhObj) && pvhObj != nullptr)
				valueTypeDesc.SetDefaultMetaType(pvhObj->GetTypesOfCharacteristics());
		}

		// Both halves of every pair are typed identically across slots — slots differ by the kind
		// standing in them at run time, never by declaration.
		for (unsigned int slot = 0; slot < m_accountDimensionCount; slot++) {
			if (kindTypeDesc.GetClsidCount() > 0)
				m_accountDimensionKinds[slot]->GetTypeDesc().SetDefaultMetaType(kindTypeDesc);
			if (valueTypeDesc.GetClsidCount() > 0)
				m_accountDimensionSlots[slot]->GetTypeDesc().SetDefaultMetaType(valueTypeDesc);
		}
	}

	if (auto* cc = m_metaData->GetCompileCache()) {
		if (ibValueMetaObjectRegisterData::OnAfterRunMetaObject(flags)) {
			if (!cc->AddCompileModule(m_propertyObjectModule->GetMetaObject(), [this]() -> ibValue* { return CreateRecordSetObjectValue(); })) return false;
			return true;
		}
	}
	return ibValueMetaObjectRegisterData::OnAfterRunMetaObject(flags);
}

bool ibValueMetaObjectAccountingRegister::OnBeforeCloseMetaObject()
{
	if (!(*m_propertyAttributeRecordType)->OnBeforeCloseMetaObject()) return false;
	if (!(*m_propertyAttributeAccount)->OnBeforeCloseMetaObject()) return false;
	for (ibValueMetaObjectAttributePredefined* slot : m_accountDimensionKinds)
		if (!slot->OnBeforeCloseMetaObject()) return false;
	for (ibValueMetaObjectAttributePredefined* slot : m_accountDimensionSlots)
		if (!slot->OnBeforeCloseMetaObject()) return false;
	if (!(*m_propertyManagerModule)->OnBeforeCloseMetaObject()) return false;
	if (!(*m_propertyObjectModule)->OnBeforeCloseMetaObject()) return false;
	if (auto* cc = m_metaData->GetCompileCache()) {
		if (ibValueMetaObjectRegisterData::OnBeforeCloseMetaObject()) {
			cc->RemoveCompileModule(m_propertyObjectModule->GetMetaObject());
			return true;
		}
	}
	return ibValueMetaObjectRegisterData::OnBeforeCloseMetaObject();
}

bool ibValueMetaObjectAccountingRegister::OnAfterCloseMetaObject()
{
	if (!(*m_propertyAttributeRecordType)->OnAfterCloseMetaObject()) return false;
	if (!(*m_propertyAttributeAccount)->OnAfterCloseMetaObject()) return false;
	for (ibValueMetaObjectAttributePredefined* slot : m_accountDimensionKinds)
		if (!slot->OnAfterCloseMetaObject()) return false;
	for (ibValueMetaObjectAttributePredefined* slot : m_accountDimensionSlots)
		if (!slot->OnAfterCloseMetaObject()) return false;
	if (!(*m_propertyManagerModule)->OnAfterCloseMetaObject()) return false;
	if (!(*m_propertyObjectModule)->OnAfterCloseMetaObject()) return false;
	unregisterSelection();
	return ibValueMetaObjectRegisterData::OnAfterCloseMetaObject();
}

void ibValueMetaObjectAccountingRegister::OnCreateFormObject(ibValueMetaObjectFormBase* metaForm)
{
	if (metaForm->GetTypeForm() == eFormList && m_propertyDefFormList->GetValueAsInteger() == wxNOT_FOUND)
		m_propertyDefFormList->SetValue(metaForm->GetMetaID());
}

void ibValueMetaObjectAccountingRegister::OnRemoveMetaForm(ibValueMetaObjectFormBase* metaForm)
{
	if (metaForm->GetTypeForm() == eFormList && m_propertyDefFormList->GetValueAsInteger() == metaForm->GetMetaID())
		m_propertyDefFormList->SetValue(wxNOT_FOUND);
}

#include "accountingRegisterManager.h"

ibValueManagerDataObject* ibValueMetaObjectAccountingRegister::CreateManagerDataObjectValue() const
{
	return new ibValueManagerDataObjectAccountingRegister(this);
}

ibValueRecordSetObject* ibValueMetaObjectAccountingRegister::CreateRecordSetObjectRegValue(const ibUniqueKeyPair& uniqueKey) const
{
	if (auto* cc = m_metaData->GetCompileCache()) {
		ibValueRecordSetObject* pDataRef = nullptr;
		if (!cc->FindCompileModule(m_propertyObjectModule->GetMetaObject(), pDataRef))
			return new ibValueRecordSetObjectAccountingRegister(this, uniqueKey);
		return pDataRef;
	}
	return new ibValueRecordSetObjectAccountingRegister(this, uniqueKey);
}

ibSourceDataObject* ibValueMetaObjectAccountingRegister::CreateSourceObject(const ibValueMetaObjectFormBase* metaObject) const
{
	switch (metaObject->GetTypeForm()) {
	case eFormList: return ibCreateList(GetQueryable(), GetRegisterPeriod());   // migrated onto the universal dynamic list
	}
	return nullptr;
}

METADATA_TYPE_REGISTER(ibValueMetaObjectAccountingRegister, "AccountingRegister", g_metaAccountingRegisterCLSID);

////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : accounting register metaData
////////////////////////////////////////////////////////////////////////////

#include "accountingRegister.h"
#include "backend/serialize/dataBuilder.h"
#include "chartOfAccounts.h"
#include "chartOfCharacteristicTypes.h"   // the CONTOUR — a slot's value type is the chart's own composition
#include "backend/system/value/valueDynamicList.h"   // ibValueDynamicList — the standard list migrates onto the universal dynamic list
#include "backend/system/systemManager.h"            // ibValueSystemFunction::Message — the message pane, not a dialog
#include "backend/metadataConfiguration.h"
#include "backend/moduleManager/moduleManager.h"


ibValueMetaObjectAccountingRegister::ibValueMetaObjectAccountingRegister() : ibValueMetaObjectRegisterData()
{
	(*m_propertyObjectModule)->SetDefaultProcedure(wxT("BeforeWrite"), ibContentHelper::eProcedureHelper, { wxT("Cancel") });
	(*m_propertyObjectModule)->SetDefaultProcedure(wxT("OnWrite"), ibContentHelper::eProcedureHelper, { wxT("Cancel") });
	// The two totals tables are declared with their initialiser on the class itself — see
	// accountingRegister.h. Nothing to do here.
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
	// ⭐⭐ THE NAME MUST BE THE ONE THAT WAS WRITTEN — INCLUDING THE SIDE PREFIX.
	//
	// A correspondence register names its debit slots "AccountDimensionDr1", not "AccountDimension1"
	// (SyncAccountDimensionSlots: the prefix appears whenever there are two sides to tell apart). This
	// loop looked for the UNPREFIXED name only, so on a correspondence register it found nothing,
	// broke on the first iteration, and left the debit slots to be re-created from scratch.
	//
	// Re-created means NEW METAIDS — and a metaID is what the physical column is named after. Every
	// load minted a fresh set: the saved configuration ended up holding slots the working one had
	// never heard of, the id counter of each container marched on independently, and a dimension
	// added afterwards was handed a number a slot in the other container already owned. The apply
	// then tried to drop reference columns belonging to a slot nobody had created, which is the
	// "column FLDnnnn_RTRef does not exist" that closed every attempt.
	//
	// The side is not stored separately: the presence of the credit block below is what records
	// correspondence, and it is read the same way. So the prefix is DERIVED here exactly as it is
	// derived when writing — try the prefixed name first, fall back to the bare one for registers
	// saved one-sided.
	const bool savedWithCredit = (node.FindProperty(wxT("AccountDimensionCr1")) != nullptr);
	const wxString debitPrefix = savedWithCredit ? wxT("Dr") : wxEmptyString;

	for (unsigned int no = 1; ; no++) {
		const wxString slotName = wxString::Format(wxT("AccountDimension%s%u"), debitPrefix, no);
		const ibDataValue* saved = node.FindProperty(slotName);
		if (saved == nullptr)
			break;

		ibValueMetaObjectAttributePredefined* slot = CreateEmptyType(
			slotName, wxString::Format(_("Account dimension %s%u"), debitPrefix, no),
			wxEmptyString, false, ibItemMode::ibItemMode_Item);

		const std::shared_ptr<ibDataNode>& child = saved->AsChild();
		if (child)
			slot->LoadNode(*child);

		// The KIND half of the same pair — its own metaobject, its own id, its own column.
		const wxString kindName = wxString::Format(wxT("AccountDimension%s%uKind"), debitPrefix, no);
		ibValueMetaObjectAttributePredefined* kind = CreateEmptyType(
			kindName, wxString::Format(_("Account dimension %s%u kind"), debitPrefix, no),
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

	// ⭐⭐ THE TWO SWITCHES ARE READ BACK, and their absence here was the root of the whole
	// "restructuring does not apply" arc.
	//
	// Correspondence and SplitTotals were declared, edited and USED to build the schema — and never
	// written or read. So they lived for as long as the designer held the object in memory and were
	// reborn at their DEFAULTS the moment the saved configuration was re-read. The re-read produces
	// the BASELINE the next apply diffs against, which means:
	//   * turning a switch OFF applied correctly, then the baseline came back saying ON;
	//   * turning it ON again diffed ON against ON, emitted nothing, and left the physical table
	//     without the column the maintenance was about to be written for — "Column unknown T.SHARD_";
	//   * and it looked INTERMITTENT, because whether it broke depended on which way the setting
	//     happened to differ from the default that round.
	//
	// The accumulation register serialises its own SplitTotals (accumulationRegisterMetadata.cpp) and
	// therefore never showed any of this — the difference between the two registers that named the bug.
	//
	// A configuration written before this reads no property and keeps the constructor's default, which
	// is the behaviour it already had.
	m_propertyCorrespondence->ReadNodeValue(node.GetProperty(m_propertyCorrespondence->GetName()));
	m_propertySplitTotals->ReadNodeValue(node.GetProperty(m_propertySplitTotals->GetName()));

	// Absent sub-node = a configuration written before the totals tables existed. The object keeps the
	// id it was given at construction rather than being left at zero — and a zero id is what makes the
	// schema differ pour one register's columns into another's table.
	if (const ibDataNode* totals = node.FindChild(wxT("DebitTotals")))
		m_totalsDr->LoadNode(*totals);
	if (const ibDataNode* totals = node.FindChild(wxT("CreditTotals")))
		m_totalsCr->LoadNode(*totals);

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

	// The pair the reader above explains: a setting that BUILDS THE SCHEMA has to survive the save,
	// or the baseline the next apply compares against is not the configuration that was applied.
	node.SetProperty(m_propertyCorrespondence->GetName(), m_propertyCorrespondence->GetNodeValue());
	node.SetProperty(m_propertySplitTotals->GetName(), m_propertySplitTotals->GetNodeValue());

	// The two totals tables are written for their IDENTITY alone: the sub-node carries each object's
	// metaID, which is what makes the id survive a save and what the schema differ matches the physical
	// table by.
	m_totalsDr->SaveNode(node.Child(wxT("DebitTotals")));
	m_totalsCr->SaveNode(node.Child(wxT("CreditTotals")));

	node.SetProperty(m_propertyObjectModule->GetName(), m_propertyObjectModule->GetNodeValue());
	node.SetProperty(m_propertyManagerModule->GetName(), m_propertyManagerModule->GetNodeValue());

	return ibValueMetaObjectRegisterData::WriteData(node);
}


#include "backend/appData.h"

bool ibValueMetaObjectAccountingRegister::OnCreateMetaObject(ibMetaData* metaData, int flags)
{
	if (!ibValueMetaObjectRegisterData::OnCreateMetaObject(metaData, flags)) return false;
	// A register born here owns no slots yet — the chart of accounts has not been chosen, so there is
	// nothing to walk. The walk stands here anyway because it is the SAME list every other pass takes:
	// a pass that is right only while a vector happens to be empty is a pass nobody will remember to
	// add later. A slot created afterwards asks for its own id in SyncAccountDimensionSlots.
	if (!ForEachOwnAttribute([metaData, flags](ibValueMetaObjectAttributePredefined* attribute, ibOwnRole) {
			return attribute->OnCreateMetaObject(metaData, flags); }))
		return false;
	return (*m_propertyAttributeRecordType)->OnCreateMetaObject(metaData, flags) &&
		(*m_propertyAttributeAccount)->OnCreateMetaObject(metaData, flags) &&
		// The totals tables get their ids here, from the ordinary generator — the same walk that
		// guarantees no other metaobject in the tree carries them.
		m_totalsDr->OnCreateMetaObject(metaData, flags) &&
		m_totalsCr->OnCreateMetaObject(metaData, flags) &&
		(*m_propertyManagerModule)->OnCreateMetaObject(metaData, flags) &&
		(*m_propertyObjectModule)->OnCreateMetaObject(metaData, flags);
}

bool ibValueMetaObjectAccountingRegister::OnLoadMetaObject(ibMetaData* metaData)
{
	if (!(*m_propertyAttributeRecordType)->OnLoadMetaObject(metaData)) return false;
	if (!(*m_propertyAttributeAccount)->OnLoadMetaObject(metaData)) return false;
	// Every attribute the file brought back — not the active count, and not the debit side alone: a
	// slot outside the count is still a live metaobject, and the credit side is not a special case
	// that each pass gets to forget. ONE walk (accountingRegister.h) so it cannot be forgotten twice.
	if (!ForEachOwnAttribute([metaData](ibValueMetaObjectAttributePredefined* attribute, ibOwnRole) {
			return attribute->OnLoadMetaObject(metaData); }))
		return false;
	if (!m_totalsDr->OnLoadMetaObject(metaData)) return false;
	if (!m_totalsCr->OnLoadMetaObject(metaData)) return false;
	if (!(*m_propertyManagerModule)->OnLoadMetaObject(metaData)) return false;
	if (!(*m_propertyObjectModule)->OnLoadMetaObject(metaData)) return false;
	return ibValueMetaObjectRegisterData::OnLoadMetaObject(metaData);
}

bool ibValueMetaObjectAccountingRegister::OnSaveMetaObject(int flags)
{
	// ⚠ THE "ONE CHART OF ACCOUNTS" RULE IS NOT CHECKED HERE, and that is a correction rather than an
	// omission. Raising from inside the save leaves the configuration WRITE TRANSACTION open: the
	// exception unwinds past the code that would have closed it, and the next attempt to save waits on
	// the lock until it times out as a DEADLOCK on sys_config_save — an error that names neither the
	// rule nor the register. A refusal has to happen where refusing is free.
	//
	// It lives on the schema declaration instead (accountingRegisterMetadataSchema.cpp): every rule
	// runs BEFORE the first statement, states its reason into the ledger, and greys the Apply button —
	// the same road the analytics ceiling and the hierarchy rules already take.
	//
	// ⭐ WHAT *IS* CHECKED HERE IS THE ABSENCE, AND IT IS REPORTED, NOT THROWN. "Two charts" is a
	// question about a schema that can still be computed; "no chart at all" is not — the account
	// column has no type, the analytics slots have no kinds, and every column below is derived from a
	// binding that names nothing. Saving that state produces a register nothing can be written into.
	//
	// It says so the way the chart of accounts says its own missing binding: a message in the pane
	// under the editor and a refusal to save. No exception leaves this function, so the configuration
	// write transaction closes normally and the next save is not met by a deadlock — which is the
	// whole reason the rule above lives elsewhere.
	if (m_propertyChartOfAccounts->IsEmptyProperty()) {
		ibValueSystemFunction::Message(
			wxString::Format(_("%s: a chart of accounts is required - the account type, the number of analytics and their type all come from it"), GetName()),
			ibStatusMessage::ibStatusMessage_Error);
		return false;
	}

	if (!(*m_propertyAttributeRecordType)->OnSaveMetaObject(flags)) return false;
	if (!(*m_propertyAttributeAccount)->OnSaveMetaObject(flags)) return false;
	if (!ForEachOwnAttribute([flags](ibValueMetaObjectAttributePredefined* attribute, ibOwnRole) {
			return attribute->OnSaveMetaObject(flags); }))
		return false;
	if (!m_totalsDr->OnSaveMetaObject(flags)) return false;
	if (!m_totalsCr->OnSaveMetaObject(flags)) return false;
	if (!(*m_propertyManagerModule)->OnSaveMetaObject(flags)) return false;
	if (!(*m_propertyObjectModule)->OnSaveMetaObject(flags)) return false;
	return ibValueMetaObjectRegisterData::OnSaveMetaObject(flags);
}

bool ibValueMetaObjectAccountingRegister::OnDeleteMetaObject()
{
	if (!(*m_propertyAttributeRecordType)->OnDeleteMetaObject()) return false;
	if (!(*m_propertyAttributeAccount)->OnDeleteMetaObject()) return false;
	if (!ForEachOwnAttribute([](ibValueMetaObjectAttributePredefined* attribute, ibOwnRole) {
			return attribute->OnDeleteMetaObject(); }))
		return false;
	if (!m_totalsDr->OnDeleteMetaObject()) return false;
	if (!m_totalsCr->OnDeleteMetaObject()) return false;
	if (!(*m_propertyManagerModule)->OnDeleteMetaObject()) return false;
	if (!(*m_propertyObjectModule)->OnDeleteMetaObject()) return false;
	return ibValueMetaObjectRegisterData::OnDeleteMetaObject();
}

// ⭐ ONE CHART OF ACCOUNTS. Everything this register's schema is built from — the account's type, how
// many analytics slots exist, what a slot may hold — comes from that chart and the characteristic
// chart it is bound to. Two charts would answer those questions twice, and the engine would have to
// pick one silently.
const ibValueMetaObjectChartOfAccounts* ibValueMetaObjectAccountingRegister::GetChartOfAccounts() const
{
	const ibMetaDescription& metaDesc = m_propertyChartOfAccounts->GetValueAsMetaDesc();
	for (unsigned int idx = 0; idx < metaDesc.GetTypeCount(); idx++) {
		const ibValueMetaObject* found = m_metaData != nullptr
			? m_metaData->FindAnyObjectByFilter(metaDesc.GetByIdx(idx)) : nullptr;
		const ibValueMetaObjectChartOfAccounts* chart = nullptr;
		if (found != nullptr && found->ConvertToValue(chart) && chart != nullptr)
			return chart;   // the first one IS the one — the write below refuses a second
	}
	return nullptr;
}

bool ibValueMetaObjectAccountingRegister::OnReloadMetaObject()
{
	// ⭐⭐ WHICH TABLES THIS REGISTER OFFERS IS DECIDED BY A PROPERTY, AND THE PROPERTY CAN CHANGE.
	//
	// The registration on run asks whether the register keeps correspondence — but it asks ONCE. Turn
	// correspondence off afterwards and `.DrCrTurnovers` stays registered: the catalogue goes on
	// offering a matrix the register can no longer answer, and the property panel and the query
	// constructor say different things about the same object.
	//
	// So the answer is re-taken on reload, which is what a designer edit ends in. Unregistering is
	// unconditional (removing what is not there is a no-op) and registering follows the mode as it is
	// NOW — the same rule, asked again rather than remembered.
	if (m_metaData != nullptr) {
		m_metaData->UnregisterSource(&m_drCrTurnover);
		if (IsCorrespondence())
			m_metaData->RegisterSource(&m_drCrTurnover);
	}

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
	if (!ForEachOwnAttribute([flags](ibValueMetaObjectAttributePredefined* attribute, ibOwnRole) {
			return attribute->OnBeforeRunMetaObject(flags); }))
		return false;
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
			AccountDimensionKindColumnName(prefix, no),
			wxString::Format(_("Account dimension %s%u kind"), prefix, no),
			wxEmptyString, false, ibItemMode::ibItemMode_Item);

		ibValueMetaObjectAttributePredefined* value = CreateEmptyType(
			AccountDimensionColumnName(prefix, no),
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
	const wxString debitPrefix = GetDebitSidePrefix();

	// ⭐⭐ THE SETTING RENAMES THE DEBIT SIDE — it does not only decide what gets written.
	//
	// Turning correspondence ON makes a line name both accounts, and from that moment the inherited
	// `Account` IS the debit one. It kept its bare name anyway, so the pair read `Account` /
	// `AccountCr` while its own dimensions right beside it read `AccountDimensionDr1` /
	// `AccountDimensionCr1` — the same fact spelled two ways in one member list. Turning the setting
	// back off has to undo it just as plainly: with one side, the side is said by RecordType and the
	// prefix would be noise.
	//
	// SAFE TO RENAME: a metaID is what names the physical column (fld<metaID>), and the id is
	// untouched here — this changes what the SCRIPT calls the field, which is exactly what the
	// setting means. Applied on every Sync, so the names follow the setting in both directions,
	// including for slots created under the previous one.
	auto applySideName = [](ibValueMetaObjectAttributePredefined* attribute,
		const wxString& name, const wxString& synonym) {
		if (attribute == nullptr || attribute->GetName() == name)
			return;
		attribute->SetName(name);
		attribute->SetOwnerSynonym(synonym);
	};

	applySideName(GetRegisterAccount(),
		AccountColumnName(debitPrefix), AccountColumnSynonym(debitPrefix));

	for (size_t idx = 0; idx < m_accountDimensionSlots.size(); idx++) {
		const unsigned int no = static_cast<unsigned int>(idx) + 1;
		applySideName(m_accountDimensionSlots[idx], AccountDimensionColumnName(debitPrefix, no),
			wxString::Format(_("Account dimension %s%u"), debitPrefix, no));
		if (idx < m_accountDimensionKinds.size())
			applySideName(m_accountDimensionKinds[idx], AccountDimensionKindColumnName(debitPrefix, no),
				wxString::Format(_("Account dimension %s%u kind"), debitPrefix, no));
	}

	// THE CREDIT ACCOUNT. In correspondence mode the line names both sides, so the inherited
	// Account becomes the debit one and this is its counterpart. Its type is the same as the debit
	// account's — both are references into the same chart — and it is set where that one is set.
	// ⭐ CREATED ALWAYS, not only in correspondence mode. It is a predefined attribute like every other
	// one here, and the setting decides whether anything is WRITTEN into it — not whether it exists.
	// While its creation was conditional, the credit totals table had to be keyed by the debit account
	// whenever correspondence happened to be off, and the maintenance built later keyed it by the
	// credit one: two shapes for the same table, and "Column unknown T.FLDnnnn_RRRef" when they met.
	if (m_accountCr == nullptr) {
		m_accountCr = CreateEmptyType(wxT("AccountCr"), _("Credit account"),
			wxEmptyString, false, ibItemMode::ibItemMode_Item);
		if (m_metaData != nullptr)
			m_accountCr->OnCreateMetaObject(m_metaData, 0);
	}

	while (m_accountDimensionSlots.size() < want)
		makePair(debitPrefix, static_cast<unsigned int>(m_accountDimensionSlots.size()) + 1,
			m_accountDimensionKinds, m_accountDimensionSlots);

	// The credit side is only GROWN in correspondence mode — a one-sided register needs no second
	// breakdown, so nothing is created for it. What already exists is kept (see below).
	if (correspondence) {
		while (m_accountDimensionSlotsCr.size() < want)
			makePair(wxT("Cr"), static_cast<unsigned int>(m_accountDimensionSlotsCr.size()) + 1,
				m_accountDimensionKindsCr, m_accountDimensionSlotsCr);
	}
	// ⭐⭐ AND THEY ARE NOT THROWN AWAY WHEN CORRESPONDENCE GOES OFF.
	//
	// Clearing the vectors removed the credit slots from every walk, so their COLUMNS were dropped at
	// the next apply — with whatever had been filed in them. Switching the setting back produced the
	// slots again, empty, under fresh ids, and the two snapshots disagreed about attributes that had
	// never left the configuration. That is the same failure the accounts had, one level down.
	//
	// So the slots stay. A one-sided register simply never writes into them, exactly as it never
	// writes into AccountCr, and the columns stand empty — which costs storage and nothing else.

	// ⭐ AND THE SIDE THAT IS NOT THERE IS MARKED AS SUCH — the columns stay, the FIELDS do not.
	//
	// The paragraph above is about storage; this is about what a line offers a script. A one-sided
	// register showed `AccountCr` and a whole credit breakdown beside its single account, all of them
	// writable and none of them ever written — the reader could not tell which of the two accounts
	// was the real one. metaDisableFlag is the mark the platform already uses for exactly this (a
	// catalog with no owner, an independent information register's Recorder), and the member walk
	// obeys it, so the mark alone is the whole change.
	const auto markSide = [](ibValueMetaObjectAttributePredefined* attribute, bool active) {
		if (attribute == nullptr)
			return;
		if (active) attribute->ClearFlag(metaDisableFlag);
		else        attribute->SetFlag(metaDisableFlag);
	};

	markSide(m_accountCr, correspondence);
	for (ibValueMetaObjectAttributePredefined* slot : m_accountDimensionSlotsCr) markSide(slot, correspondence);
	for (ibValueMetaObjectAttributePredefined* slot : m_accountDimensionKindsCr) markSide(slot, correspondence);

	m_accountDimensionCount = want;
}

// ⭐⭐ THE SLOTS' TYPES, AS A STEP OF ITS OWN — because two different moments need it.
//
// It used to live inside OnAfterRunMetaObject only, i.e. in the RUN phase. Editing does not run a
// configuration: picking a chart of accounts creates the slots (SyncAccountDimensionSlots) and left
// them UNTYPED until something happened to run the configuration again. Save in that window and the
// schema is built from typeless slots — each gets its discriminator column and nothing else — while
// the configuration written moments later carries the types the run then supplied.
//
// From there the two never agree again: the next apply sees the slots typed on one side of the diff
// and typeless on the other, and tries to drop reference columns that were never created. "column
// FLDnnnn_RTRef does not exist", three edits away from the cause.
//
// So the typing is a step both callers take: the run, and the moment the binding changes.
void ibValueMetaObjectAccountingRegister::ApplyAccountDimensionSlotTypes()
{
	const ibMetaDescription& metaDesc = m_propertyChartOfAccounts->GetValueAsMetaDesc();

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
		//
		// ⚠ BOTH SIDES, through the same walk the lifecycle uses. This counted to the ACTIVE COUNT over
		// the debit vectors, so in a correspondence register the credit slots were created, saved,
		// reloaded and given columns — and never typed. A composite column that admits nothing accepts
		// nothing: the credit breakdown was a set of columns no value could enter.
		ForEachOwnAttribute([&kindTypeDesc, &valueTypeDesc](ibValueMetaObjectAttributePredefined* attribute, ibOwnRole role) {
			switch (role) {
			case ibOwnRole::DimensionKind:
				if (kindTypeDesc.GetClsidCount() > 0)
					attribute->GetTypeDesc().SetDefaultMetaType(kindTypeDesc);
				break;
			case ibOwnRole::DimensionValue:
				if (valueTypeDesc.GetClsidCount() > 0)
					attribute->GetTypeDesc().SetDefaultMetaType(valueTypeDesc);
				break;
			case ibOwnRole::AccountCr:
				break;   // typed with the debit account above — one chart, one declaration for both sides
			}
			return true;
		});
	}
}

bool ibValueMetaObjectAccountingRegister::OnAfterRunMetaObject(int flags)
{
	if (!(*m_propertyAttributeRecordType)->OnAfterRunMetaObject(flags)) return false;
	if (!(*m_propertyAttributeAccount)->OnAfterRunMetaObject(flags)) return false;
	if (!ForEachOwnAttribute([flags](ibValueMetaObjectAttributePredefined* attribute, ibOwnRole) {
			return attribute->OnAfterRunMetaObject(flags); }))
		return false;
	if (!(*m_propertyManagerModule)->OnAfterRunMetaObject(flags)) return false;
	if (!(*m_propertyObjectModule)->OnAfterRunMetaObject(flags)) return false;

	// ⭐ THE SLOT SET COMES FIRST, AND THE TYPING AFTER IT.
	//
	// How many slots there are, and whether there is a credit account at all, is decided here — so a
	// run that types what exists BEFORE this call types the previous state. The credit account is the
	// case that bit: turning correspondence on creates it in this very call, and the typing above it
	// had already run, so the new attribute stood in the table for a whole configuration run with no
	// type. Create, then declare — in that order, in one place.
	//
	// THE DIMENSION SLOTS — how many, and of what type. Two questions, two sources: the chart of
	// ACCOUNTS says how many slots exist, the chart of CHARACTERISTIC TYPES bound to it says what a
	// slot may hold. Neither is derivable from the other.
	SyncAccountDimensionSlots();

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

	ApplyAccountDimensionSlotTypes();

	// ⭐ THE VIRTUAL TABLES. The base records descriptor (the movements table itself) is registered by
	// ibValueMetaObjectRegisterData below; these five are the register's own readings, reached as
	// AccountingRegister.<Name>.Balance / .Turnovers / .BalanceAndTurnovers /
	// .RecordsWithAccountDimensions — and .DrCrTurnovers ONLY in correspondence mode.
	//
	// ⚠ WHAT A REGISTER DOES NOT HAVE, IT DOES NOT OFFER. A one-sided line discards which debit
	// answered which credit at write time, so the correspondence matrix has no answer to give; a
	// source a person can pick, join, and select a wrong number from is worse than an absent one.
	m_metaData->RegisterSource(&m_balance);
	m_metaData->RegisterSource(&m_turnover);
	m_metaData->RegisterSource(&m_balanceAndTurnover);
	m_metaData->RegisterSource(&m_records);
	if (IsCorrespondence())
		m_metaData->RegisterSource(&m_drCrTurnover);

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
	// ⚠ ALL FIVE, UNCONDITIONALLY — deliberately not mirroring the registration's condition. The
	// correspondence switch can be flipped while the configuration is open, and a close that asked the
	// CURRENT mode would leave behind whatever was registered under the previous one. Unregistering
	// something that was never registered is a no-op, so the asymmetry costs nothing and removes a
	// whole class of dangling descriptor.
	m_metaData->UnregisterSource(&m_balance);
	m_metaData->UnregisterSource(&m_turnover);
	m_metaData->UnregisterSource(&m_drCrTurnover);
	m_metaData->UnregisterSource(&m_balanceAndTurnover);
	m_metaData->UnregisterSource(&m_records);

	if (!(*m_propertyAttributeRecordType)->OnBeforeCloseMetaObject()) return false;
	if (!(*m_propertyAttributeAccount)->OnBeforeCloseMetaObject()) return false;
	if (!ForEachOwnAttribute([](ibValueMetaObjectAttributePredefined* attribute, ibOwnRole) {
			return attribute->OnBeforeCloseMetaObject(); }))
		return false;
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
	if (!ForEachOwnAttribute([](ibValueMetaObjectAttributePredefined* attribute, ibOwnRole) {
			return attribute->OnAfterCloseMetaObject(); }))
		return false;
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

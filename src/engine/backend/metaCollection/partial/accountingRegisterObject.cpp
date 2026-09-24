////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : accounting register object (record set) - posting, by kind
////////////////////////////////////////////////////////////////////////////

#include "accountingRegister.h"
#include "chartOfAccounts.h"                   // the account's own OffBalance — the balance separator
#include "chartOfCharacteristicTypes.h"        // the kind's own Type — what a value is adjusted to
#include "reference/reference.h"               // ibValueReferenceDataObject::GetValueByMetaID — reading the kind
#include "backend/metaCollection/resource/metaResourceObject.h"   // IsBalanceResource — which figure balances
#include "backend/system/value/valueMap.h"      // ibValueContainer — a breakdown handed over as a whole map
#include "backend/system/value/valueType.h"    // ibValueTypeDescription::AdjustValue

#include "backend/appData.h"

#include <unordered_map>   // value-keyed caches — see ibValueHash (value.h)
#include "backend/session/session.h"
#include "backend/databaseLayer/connectionPool.h"
#include "backend/system/systemManager.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

// WriteRecordSet / DeleteRecordSet inherited from ibValueRecordSetObject
// (Phase B template-method) — the scaffold is in commonObject.cpp; the
// Begin/Commit + LockByKeys helpers it calls live in commonObjectRecordSetQuery.cpp.


// 🛑 THIS ORDER IS THE CALL NUMBER, AND IT MUST MATCH FillMembers EXACTLY. A method is invoked by
// its INDEX in the member table, so an enumerator out of step silently runs a different verb:
// `Write` landed on Load, `Load` on Unload and `Unload` on Write. Posting any document crashed
// (Write handed its bool to Load, which casts it to a table) and an Unload would have WRITTEN the
// set. Found 2026-09-03 by posting a goods receipt from the sandbox.
enum func
{
	eAdd = 0,
	eAddDebit,
	eAddCredit,
	eCount,
	eClear,
	eWriteRecordSet,
	eLoad,
	eUnload,
	eModifiedRecordSet,
	eReadRecordSet,
	eSelectedRecordSet,
	eGetMetadataRecordSet,
};

//****************************************************************************
//*                    The dimensions of a line, BY KIND                     *
//****************************************************************************

ibValueRecordSetObjectAccountingRegister::ibValueAccountDimensions::ibValueAccountDimensions(
	ibValueRecordSetObjectAccountingRegister* recordSet, const ibDataViewItem& line, bool creditSide)
	: ibValueDynamicMembers(ibValueTypes::TYPE_VALUE, true),
	  m_recordSet(recordSet), m_line(line), m_creditSide(creditSide)
{
	m_members.Bind(this, &ibValueAccountDimensions::FillMembers);
}

ibValueRecordSetObjectAccountingRegister::ibValueAccountDimensions::~ibValueAccountDimensions()
{
}

namespace {

enum dimensionFunc
{
	eDimensionCount = 0,
	eDimensionKind,
	eDimensionClear,
};

} // namespace

void ibValueRecordSetObjectAccountingRegister::ibValueAccountDimensions::FillMembers(ibMemberTable& helper) const
{
	helper.AppendFunc(wxT("Count"), wxT("Count()"));
	helper.AppendFunc(wxT("Kind"), 1, wxT("Kind(number)"));
	helper.AppendFunc(wxT("Clear"), wxT("Clear()"));
}

bool ibValueRecordSetObjectAccountingRegister::ibValueAccountDimensions::CallAsFunc(
	const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	const ibValueMetaObjectAccountingRegister* meta =
		m_recordSet != nullptr ? m_recordSet->GetAccountingMetaObject() : nullptr;
	if (meta == nullptr)
		return false;

	switch (lMethodNum) {
	case dimensionFunc::eDimensionCount:
		pvarRetValue = meta->GetAccountDimensionCount();
		return true;

	// WHICH KIND stands in slot N — the one place a position is legitimately asked about, because a
	// reader of the raw movements has nothing else to ask with.
	case dimensionFunc::eDimensionKind: {
		const unsigned int no = lSizeArray > 0 ? paParams[0]->GetUInteger() : 0;
		const ibValueMetaObjectAttributeBase* kindSlot =
			no > 0 ? meta->GetAccountDimensionKindSlot(m_creditSide, no - 1) : nullptr;
		if (kindSlot == nullptr)
			return false;
		return m_recordSet->GetValueByMetaID(m_line, kindSlot->GetMetaID(), pvarRetValue);
	}

	case dimensionFunc::eDimensionClear:
		Clear();
		return true;
	}

	return false;
}

// ⭐⭐ WRITING BY KIND — the pair is written TOGETHER, in the slot the ACCOUNT gives the kind.
//
// The kind lands in its own column beside the value. That is what makes a stored movement
// self-describing: an old row still says what its value was a kind OF, so re-ordering an account's kinds
// later cannot silently change the meaning of data already written, and a reading needs no join per slot
// per row.
//
// ⭐⭐ THE SLOT IS THE KIND'S POSITION ON THE ACCOUNT — the order of the account's own kinds table: its
// first kind in slot 1, its second in slot 2. The author names the kind; the position is the account's.
// It used to be "the slot already holding this kind, or the first free one", so the slot a kind landed in
// depended on which kind a posting happened to write first — one account's counterparty in slot 1 on one
// line and slot 2 on the next, against what the class declares (accountingRegister.h) and what every
// reading by position assumes (2026-09-15).
//
// ⚠ AND SAID, NOT SWALLOWED, when there is no such position: the row names no account yet (the account
// comes first — it is what decides), or the account does not keep that kind. Filing a value under a kind
// the account does not keep would be analytics nobody can read back by that account.
bool ibValueRecordSetObjectAccountingRegister::ibValueAccountDimensions::SetAt(
	const ibValue& varKeyValue, const ibValue& varValue)
{
	const ibValueMetaObjectAccountingRegister* meta =
		m_recordSet != nullptr ? m_recordSet->GetAccountingMetaObject() : nullptr;
	if (meta == nullptr)
		return false;

	if (varKeyValue.IsEmpty())
		ibBackendCoreException::Error(_("an account dimension is addressed by its KIND, and none was given"));

	const ibValue account = LineAccount();
	if (account.IsEmpty())
		ibBackendCoreException::Error(_("the line names no account yet: set the account first - it decides which slot the kind \"%s\" goes to"),
			varKeyValue.GetString());

	const std::vector<std::pair<wxString, ibValue>> kinds = DeclaredKinds();
	long target = wxNOT_FOUND;
	for (size_t idx = 0; idx < kinds.size(); idx++)
		if (kinds[idx].second == varKeyValue) { target = static_cast<long>(idx); break; }

	if (target == wxNOT_FOUND)
		ibBackendCoreException::Error(_("account %s keeps no analytics by \"%s\" - its kinds table does not list it"),
			account.GetString(), varKeyValue.GetString());

	if (target >= static_cast<long>(meta->GetAccountDimensionCount())) {
		// The number of slots is declared by the chart of accounts, so "no room" is a configuration
		// statement the author can act on.
		ibBackendCoreException::Error(_("the kind \"%s\" is number %ld on account %s, and the chart of accounts declares %u account dimension slots"),
			varKeyValue.GetString(), target + 1, account.GetString(), meta->GetAccountDimensionCount());
		return false;
	}

	// A slot that held this kind before (the account was changed after the analytics were written) gives
	// it up — one kind, one slot.
	for (unsigned int idx = 0; idx < meta->GetAccountDimensionCount(); idx++) {
		if (static_cast<long>(idx) == target)
			continue;
		const ibValueMetaObjectAttributeBase* kindSlot = meta->GetAccountDimensionKindSlot(m_creditSide, idx);
		const ibValueMetaObjectAttributeBase* slot     = meta->GetAccountDimensionSlot(m_creditSide, idx);
		if (kindSlot == nullptr || slot == nullptr)
			continue;
		ibValue current;
		m_recordSet->GetValueByMetaID(m_line, kindSlot->GetMetaID(), current);
		if (!current.IsEmpty() && current == varKeyValue) {
			m_recordSet->SetValueByMetaID(m_line, kindSlot->GetMetaID(), ibValue());
			m_recordSet->SetValueByMetaID(m_line, slot->GetMetaID(), ibValue());
		}
	}

	const ibValueMetaObjectAttributeBase* kindSlot = meta->GetAccountDimensionKindSlot(m_creditSide, target);
	const ibValueMetaObjectAttributeBase* slot     = meta->GetAccountDimensionSlot(m_creditSide, target);
	if (kindSlot == nullptr || slot == nullptr)
		return false;

	m_recordSet->SetValueByMetaID(m_line, kindSlot->GetMetaID(), varKeyValue);

	// ⭐⭐ THE KIND BRINGS THE VALUE, AND IT IS ASKED TO — one verb, the same one a link by type uses
	// (ibValue::AdjustValue). This used to read the kind's `Type` here: two casts to find out what
	// stood in the value, a third to read the description out of it, and the id of an attribute this
	// file had no business knowing. All of that lives in the chart now, which is where the knowledge
	// is; the posting says what it wants — "bound this by that" — and the chart does it, qualifiers
	// and all. A kind that was told no type narrows nothing and says so in its own place.
	//
	// What comes back is a value of what the kind allows either way: the same one when it fits — the
	// reference itself, nothing rebuilt — or the empty value of the type the kind declares when it does
	// not. So the column is NOT adjusted on top of it: after the kind has spoken there is one option
	// left, and a second pass would either change nothing or undo what the kind just decided, at the
	// cost of another copy per dimension of every line (Max, 2026-09-24).
	ibValue narrowed;
	varKeyValue.AdjustOutValue(varValue, narrowed);
	m_recordSet->SetValueByMetaID(m_line, slot->GetMetaID(), narrowed);
	return true;
}

bool ibValueRecordSetObjectAccountingRegister::ibValueAccountDimensions::GetAt(
	const ibValue& varKeyValue, ibValue& pvarValue)
{
	const ibValueMetaObjectAccountingRegister* meta =
		m_recordSet != nullptr ? m_recordSet->GetAccountingMetaObject() : nullptr;
	if (meta == nullptr)
		return false;

	for (unsigned int idx = 0; idx < meta->GetAccountDimensionCount(); idx++) {
		const ibValueMetaObjectAttributeBase* kindSlot = meta->GetAccountDimensionKindSlot(m_creditSide, idx);
		const ibValueMetaObjectAttributeBase* slot     = meta->GetAccountDimensionSlot(m_creditSide, idx);
		if (kindSlot == nullptr || slot == nullptr)
			continue;

		ibValue current;
		m_recordSet->GetValueByMetaID(m_line, kindSlot->GetMetaID(), current);
		if (!current.IsEmpty() && current == varKeyValue)
			return m_recordSet->GetValueByMetaID(m_line, slot->GetMetaID(), pvarValue);
	}

	// A kind this line does not carry reads as EMPTY, not as an error: asking a posting whether it has
	// a counterparty breakdown is a legitimate question with a legitimate negative answer.
	pvarValue = ibValue();
	return true;
}

// ⭐ EMPTYING A SIDE IS ONE ACT, and it takes the KINDS out with the values. Leaving the kind columns
// filled would describe a breakdown the row no longer carries — a movement that claims to be filed
// under a counterparty and files nothing.
void ibValueRecordSetObjectAccountingRegister::ibValueAccountDimensions::Clear()
{
	const ibValueMetaObjectAccountingRegister* meta =
		m_recordSet != nullptr ? m_recordSet->GetAccountingMetaObject() : nullptr;
	if (meta == nullptr)
		return;

	for (unsigned int idx = 0; idx < meta->GetAccountDimensionCount(); idx++) {
		const ibValueMetaObjectAttributeBase* kindSlot = meta->GetAccountDimensionKindSlot(m_creditSide, idx);
		const ibValueMetaObjectAttributeBase* slot     = meta->GetAccountDimensionSlot(m_creditSide, idx);
		if (kindSlot != nullptr) m_recordSet->SetValueByMetaID(m_line, kindSlot->GetMetaID(), ibValue());
		if (slot     != nullptr) m_recordSet->SetValueByMetaID(m_line, slot->GetMetaID(),     ibValue());
	}
}

wxString ibValueRecordSetObjectAccountingRegister::ibValueAccountDimensions::GetString() const
{
	return m_creditSide ? wxT("AccountDimensionCr") : wxT("AccountDimension");
}

//****************************************************************************
//*        …and by NAME — the kinds THIS ROW'S ACCOUNT declares              *
//****************************************************************************

namespace {

// The kinds an account declares, in the order of its own table, each with the name a script writes.
//
// ⭐ THE NAMES ARE DATA. They are the characteristics' own descriptions — nothing in the metadata
// declares them, and two accounts have different ones. That is why they are resolved per call rather
// than built into a member table: `row.AccountDimensionDr.Contractor` means whatever the account in
// THIS row says it means.
std::vector<std::pair<wxString, ibValue>> KindsOfAccount(const ibValue& account)
{
	std::vector<std::pair<wxString, ibValue>> kinds;

	ibValueReferenceDataObject* reference = nullptr;
	if (!account.ConvertToValue(reference) || reference == nullptr)
		return kinds;

	const ibValueMetaObjectChartOfAccounts* chart = nullptr;
	if (!reference->GetMetaObject()->ConvertToValue(chart) || chart == nullptr)
		return kinds;

	const ibValueMetaObjectAccountDimensionKindsTable* table = chart->GetAccountDimensionKindsTable();
	if (table == nullptr || table->GetAccountDimensionKind() == nullptr)
		return kinds;

	// Held in an ibValue so the loaded object is released with it — GetObject CREATES one.
	ibValue holder = reference->GetObject();
	ibValueRecordDataObjectRef* object = nullptr;
	if (!holder.ConvertToValue(object) || object == nullptr)
		return kinds;

	ibValueModel* rows = object->GetTableByMetaID(table->GetMetaID());
	if (rows == nullptr)
		return kinds;

	for (long row = 0; row < rows->GetRowCount(); row++) {
		ibValue kind;
		rows->GetValueByMetaID(rows->GetItem(row), table->GetAccountDimensionKind()->GetMetaID(), kind);
		if (kind.IsEmpty())
			continue;

		// The characteristic's own description is the name. Asked of the element, not spelled here:
		// a kind is an ordinary record of the chart of characteristic types.
		wxString name;
		ibValueReferenceDataObject* kindRef = nullptr;
		if (kind.ConvertToValue(kindRef) && kindRef != nullptr) {
			const ibValueMetaObjectChartOfCharacteristicTypes* kindChart = nullptr;
			if (kindRef->GetMetaObject()->ConvertToValue(kindChart) && kindChart != nullptr
				&& kindChart->GetDataDescription() != nullptr) {
				ibValue description;
				if (kindRef->GetValueByMetaID(kindChart->GetDataDescription()->GetMetaID(), description))
					name = description.GetString();
			}
		}
		if (name.IsEmpty())
			name = kind.GetString();

		kinds.push_back({ name, kind });
	}

	return kinds;
}

} // namespace

const std::vector<std::pair<wxString, ibValue>>&
ibValueRecordSetObjectAccountingRegister::AccountKinds(const ibValue& account) const
{
	auto found = m_accountKinds.find(account);
	if (found == m_accountKinds.end())
		found = m_accountKinds.emplace(account, KindsOfAccount(account)).first;
	return found->second;
}

ibValue ibValueRecordSetObjectAccountingRegister::ibValueAccountDimensions::LineAccount() const
{
	const ibValueMetaObjectAccountingRegister* meta =
		m_recordSet != nullptr ? m_recordSet->GetAccountingMetaObject() : nullptr;
	if (meta == nullptr)
		return ibValue();

	// Whose analytics these are: the debit account on the debit side, the credit one on the credit
	// side — and in a one-sided register there is only the one account, whichever side the row is.
	const ibValueMetaObjectAttributeBase* accountAttribute =
		(m_creditSide && meta->IsCorrespondence() && meta->GetRegisterAccountCr() != nullptr)
			? meta->GetRegisterAccountCr() : meta->GetRegisterAccount();
	if (accountAttribute == nullptr)
		return ibValue();

	ibValue account;
	m_recordSet->GetValueByMetaID(m_line, accountAttribute->GetMetaID(), account);
	return account;
}

std::vector<std::pair<wxString, ibValue>>
ibValueRecordSetObjectAccountingRegister::ibValueAccountDimensions::DeclaredKinds() const
{
	const ibValue account = LineAccount();
	if (account.IsEmpty())
		return {};
	return m_recordSet->AccountKinds(account);
}

long ibValueRecordSetObjectAccountingRegister::ibValueAccountDimensions::FindProp(const wxString& strPropName) const
{
	const std::vector<std::pair<wxString, ibValue>> kinds = DeclaredKinds();
	for (size_t idx = 0; idx < kinds.size(); idx++)
		if (stringUtils::CompareString(kinds[idx].first, strPropName))
			return static_cast<long>(idx);
	return wxNOT_FOUND;
}

long ibValueRecordSetObjectAccountingRegister::ibValueAccountDimensions::GetNProps() const
{
	return static_cast<long>(DeclaredKinds().size());
}

wxString ibValueRecordSetObjectAccountingRegister::ibValueAccountDimensions::GetPropName(const long lPropNum) const
{
	const std::vector<std::pair<wxString, ibValue>> kinds = DeclaredKinds();
	return lPropNum >= 0 && lPropNum < static_cast<long>(kinds.size()) ? kinds[lPropNum].first : wxString();
}

bool ibValueRecordSetObjectAccountingRegister::ibValueAccountDimensions::SetPropVal(const long lPropNum, const ibValue& varPropVal)
{
	const std::vector<std::pair<wxString, ibValue>> kinds = DeclaredKinds();
	if (lPropNum < 0 || lPropNum >= static_cast<long>(kinds.size()))
		return false;
	// ONE ROAD for every write: by name, by key, or a whole map — all of them end in SetAt, which
	// writes the pair and adjusts the value to the kind's own type.
	return SetAt(kinds[lPropNum].second, varPropVal);
}

bool ibValueRecordSetObjectAccountingRegister::ibValueAccountDimensions::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	const std::vector<std::pair<wxString, ibValue>> kinds = DeclaredKinds();
	if (lPropNum < 0 || lPropNum >= static_cast<long>(kinds.size()))
		return false;
	return GetAt(kinds[lPropNum].second, pvarPropVal);
}

//****************************************************************************
//*                       A line of an accounting register                   *
//****************************************************************************

ibValueRecordSetObjectAccountingRegister::ibValueAccountingLine::ibValueAccountingLine(
	ibValueRecordSetObjectAccountingRegister* ownerTable, const ibDataViewItem& line)
	: ibValueRecordSetObjectRegisterReturnLine(ownerTable, line), m_ownerSet(ownerTable)
{
}

ibValueRecordSetObjectAccountingRegister::ibValueAccountingLine::~ibValueAccountingLine()
{
}

void ibValueRecordSetObjectAccountingRegister::DescribeReturnLine(ibMemberTable& helper) const
{
	// The attributes, as any register line has them; then the two views over the slot pairs. In a one-sided
	// register there is one collection and it needs no side in its name — the side is said by RecordType; in a
	// correspondence register there are two, because the line names both accounts and each has its own
	// analytical breakdown.
	ibValueRecordSetObject::DescribeReturnLine(helper);
	const ibValueMetaObjectAccountingRegister* meta = GetAccountingMetaObject();
	if (meta == nullptr)
		return;

	if (meta->IsCorrespondence()) {
		helper.AppendProp(wxT("AccountDimensionDr"), ibValueAccountingLine::ePropAccountDimensionDr);
		helper.AppendProp(wxT("AccountDimensionCr"), ibValueAccountingLine::ePropAccountDimensionCr);
	}
	else {
		helper.AppendProp(wxT("AccountDimension"), ibValueAccountingLine::ePropAccountDimension);
	}
}

bool ibValueRecordSetObjectAccountingRegister::ibValueAccountingLine::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	const ibMetaID& id = m_ownerSet->m_methodHelperReturnLine.GetPropData(lPropNum);
	switch (id) {
	case ePropAccountDimension:
	case ePropAccountDimensionDr:
		pvarPropVal = new ibValueAccountDimensions(m_ownerSet, GetLineItem(), /*creditSide*/ false);
		return true;
	case ePropAccountDimensionCr:
		pvarPropVal = new ibValueAccountDimensions(m_ownerSet, GetLineItem(), /*creditSide*/ true);
		return true;
	}
	return ibValueRecordSetObjectRegisterReturnLine::GetPropVal(lPropNum, pvarPropVal);
}

bool ibValueRecordSetObjectAccountingRegister::ibValueAccountingLine::SetPropVal(const long lPropNum, const ibValue& varPropVal)
{
	const ibMetaID& id = m_ownerSet->m_methodHelperReturnLine.GetPropData(lPropNum);

	// ⭐⭐ THE BREAKDOWN IS A MAP, AND A WHOLE MAP MAY BE HANDED OVER.
	//
	// What a posting carries is *(kind -> value)* pairs, so the natural way to write one is to build a
	// map and assign it: `row.AccountDimensionDr = New Map(); …`. Element-by-element assignment
	// (`[kind] = value`) is the same thing said one pair at a time, and both end in the same place —
	// the pair is written to the row, kind beside value.
	//
	// A POSITION is imitated the way it should be: the kind is taken from the ACCOUNT (its kinds table
	// says which kind is first, second, third) and the value is filed under that kind. Nothing here
	// takes a number, because a number means a different breakdown on a different account.
	if (id == ePropAccountDimension || id == ePropAccountDimensionDr || id == ePropAccountDimensionCr) {
		ibValueAccountDimensions target(m_ownerSet, GetLineItem(), id == ePropAccountDimensionCr);

		// ⭐ ASSIGNING NOTHING EMPTIES THE SIDE. "This posting has no analytics after all" is a thing an
		// author says, and saying it should not require remembering a method name.
		if (varPropVal.IsEmpty()) {
			target.Clear();
			return true;
		}

		ibValueContainer* pairs = nullptr;
		if (!varPropVal.ConvertToValue(pairs) || pairs == nullptr)
			return false;   // anything that is neither a map nor emptiness is not a breakdown
		for (const std::pair<ibValue, ibValue>& pair : pairs->Entries())
			if (!target.SetAt(pair.first, pair.second))
				return false;
		return true;
	}

	return ibValueRecordSetObjectRegisterReturnLine::SetPropVal(lPropNum, varPropVal);
}

//****************************************************************************
//*                              Support methods                             *
//****************************************************************************

void ibValueRecordSetObjectAccountingRegister::FillMembers(ibMemberTable& helper) const
{
	helper.AppendFunc(wxT("Add"), wxT("Add()"));
	// ⭐ THE VERB SETS THE SIDE, so the author never writes the flag by hand and it cannot be forgotten
	// or contradicted by the account assigned next to it. They are ordinary verbs of the collection,
	// not a second mode — and a correspondence register needs them too: an OFF-BALANCE account has no
	// counterpart by definition, so a one-sided row there is the record's meaning rather than an
	// incomplete entry.
	//
	// ⚠ The accumulation register has the same IDEA in its own words (AddReceipt / AddExpense) and the
	// two sets do NOT meet: receipt/expense belongs there, debit/credit here, and neither vocabulary is
	// a special case of the other. Similar in shape, separate in meaning — no shared verb.
	helper.AppendFunc(wxT("AddDebit"), wxT("AddDebit()"));
	helper.AppendFunc(wxT("AddCredit"), wxT("AddCredit()"));
	helper.AppendFunc(wxT("Count"), wxT("Count()"));
	helper.AppendFunc(wxT("Clear"), wxT("Clear()"));
	helper.AppendFunc(wxT("Write"), 1, wxT("Write(replace : boolean)"));
	helper.AppendFunc(wxT("Load"), 1, wxT("Load(value: table)"));
	helper.AppendFunc(wxT("Unload"), wxT("Unload()"));
	helper.AppendFunc(wxT("Modified"), wxT("Modified()"));
	helper.AppendFunc(wxT("Read"), wxT("Read()"));
	helper.AppendFunc(wxT("Selected"), wxT("Selected()"));
	helper.AppendFunc(wxT("GetMetadata"), wxT("GetMetadata()"));

	// `Filter` is NOT declared here — see informationRegisterObject.cpp: it is an export variable of
	// the set, bound in InitializeObject, and that reaches this table on its own.
}

bool ibValueRecordSetObjectAccountingRegister::SetPropVal(const long lPropNum, const ibValue& varPropVal)
{
	return false;
}

bool ibValueRecordSetObjectAccountingRegister::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	// The set's own properties (Filter) live on the base — asked here first because a register may add
	// its own later, and answered by the base when it has none of its own. Returning false outright is
	// what kept `Filter` unreachable from outside while it was declared in this very file's member
	// table (2026-09-05).
	return ibValueRecordSetObject::GetPropVal(lPropNum, pvarPropVal);
}

namespace {

// A new line, with its side already stated where the register has one to state. In a correspondence
// register the side is said by WHICH ACCOUNT is filled, so the verb adds an ordinary row and the
// author names the account — the verb still exists because an off-balance posting fills one side only
// and the intent is worth writing down.
ibValue AppendPostingLine(ibValueRecordSetObjectAccountingRegister* recordSet,
                          const ibValueMetaObjectAccountingRegister* meta,
                          bool sideGiven, bool credit)
{
	ibValueRecordSetObjectAccountingRegister::ibValueAccountingLine* line =
		new ibValueRecordSetObjectAccountingRegister::ibValueAccountingLine(
			recordSet, recordSet->GetItem(recordSet->AppendRow()));

	if (sideGiven && meta != nullptr && !meta->IsCorrespondence() && meta->GetRegisterRecordType() != nullptr)
		line->SetValueByMetaID(meta->GetRegisterRecordType()->GetMetaID(),
			ibValue::CreateEnumObject<ibValueEnumAccountingRegisterRecordType>(
				credit ? ibAccountingRecordType::eCredit : ibAccountingRecordType::eDebit));

	return line;
}

} // namespace

//****************************************************************************
//*                  Double entry — checked where it becomes data            *
//****************************************************************************

namespace {

// Is this account kept OFF the balance? Read from the account itself, cached per reference: a posting
// names few distinct accounts and this would otherwise be one read per line per resource.
bool IsOffBalanceAccount(const ibValue& account, std::unordered_map<ibValue, bool, ibValueHash, ibValueEqual>& cache)
{
	if (account.IsEmpty())
		return false;

	// Cached by the ACCOUNT VALUE — a reference compares by guid there (ibValueHash, value.h),
	// which is what keying by its GetHashKey text used to buy.
	const auto found = cache.find(account);
	if (found != cache.end())
		return found->second;

	bool offBalance = false;

	ibValueReferenceDataObject* reference = nullptr;
	if (account.ConvertToValue(reference) && reference != nullptr) {
		const ibValueMetaObjectChartOfAccounts* chart = nullptr;
		if (reference->GetMetaObject()->ConvertToValue(chart) && chart != nullptr
			&& chart->GetOffBalance() != nullptr) {
			ibValue flag;
			if (reference->GetValueByMetaID(chart->GetOffBalance()->GetMetaID(), flag))
				offBalance = flag.GetBoolean();
		}
	}

	cache[account] = offBalance;
	return offBalance;
}

} // namespace

// DOES THIS ACCOUNT KEEP THAT KIND OF ACCOUNTING? The kinds a chart declares are boolean fields of the
// account, so the question goes to the account itself — the same shape, and the same memory, as the
// off-balance answer above. An empty account keeps nothing; an account whose chart never declared the
// kind answers no, which is the honest answer for a figure that has nothing to belong to.
bool ibValueMetaObjectAccountingRegister::IsAccountingKindKept(const ibValue& account, const ibMetaID& kind,
	std::unordered_map<ibValue, bool, ibValueHash, ibValueEqual>& cache)
{
	if (account.IsEmpty())
		return false;

	const auto found = cache.find(account);
	if (found != cache.end())
		return found->second;

	bool keeps = false;

	ibValueReferenceDataObject* reference = nullptr;
	if (account.ConvertToValue(reference) && reference != nullptr) {
		ibValue flag;
		if (reference->GetValueByMetaID(kind, flag))
			keeps = flag.GetBoolean();
	}

	cache[account] = keeps;
	return keeps;
}

void ibValueRecordSetObjectAccountingRegister::CheckDoubleEntry() const
{
	const ibValueMetaObjectAccountingRegister* meta = GetAccountingMetaObject();
	if (meta == nullptr)
		return;

	const ibValueMetaObjectAttributeBase* account = meta->GetRegisterAccount();
	if (account == nullptr)
		return;

	std::unordered_map<ibValue, bool, ibValueHash, ibValueEqual> offBalanceCache;

	// ⭐⭐ A CORRESPONDENCE LINE BALANCES IN ITS AMOUNT BY CONSTRUCTION — one figure, both accounts — so
	// what has to be checked there is different: that the line NAMES both sides. An ordinary posting
	// must say where the value came from and where it went; an OFF-BALANCE one need not, because that
	// circuit is kept beside the books rather than inside them and has nothing to be paired with.
	//
	// That is the whole meaning of the flag: a field on the account by which off-balance entries are
	// excluded — from this requirement, and from the balance.
	if (meta->IsCorrespondence()) {
		const ibValueMetaObjectAttributeBase* accountCr = meta->GetRegisterAccountCr();
		if (accountCr == nullptr)
			return;

		for (long row = 0; row < GetRowCount(); row++) {
			const ibDataViewItem item = GetItem(row);

			ibValue debitAccount, creditAccount;
			GetValueByMetaID(item, account->GetMetaID(),   debitAccount);
			GetValueByMetaID(item, accountCr->GetMetaID(), creditAccount);

			if (!debitAccount.IsEmpty() && !creditAccount.IsEmpty())
				continue;   // both sides named — an ordinary entry

			// One side named, and legitimate only if THAT side is off-balance.
			const ibValue named = debitAccount.IsEmpty() ? creditAccount : debitAccount;
			if (named.IsEmpty())
				ibBackendCoreException::Error(_("a posting names no account at line %ld"), row + 1);
			if (!IsOffBalanceAccount(named, offBalanceCache))
				ibBackendCoreException::Error(
					_("an entry on '%s' names only one side: an ordinary account requires both debit and credit"),
					named.GetString());
		}
		return;
	}

	const ibValueMetaObjectAttributeBase* recordType = meta->GetRegisterRecordType();
	if (recordType == nullptr)
		return;

	const ibValue debitSide = ibValue::CreateEnumObject<ibValueEnumAccountingRegisterRecordType>(
		ibAccountingRecordType::eDebit);

	std::map<ibMetaID, ibNumber> debit, credit;

	std::vector<const ibValueMetaObjectResource*> balancing;
	for (const auto resource : meta->GetResourceArrayObject())
		if (resource != nullptr && resource->IsBalanceResource())
			balancing.push_back(resource);
	if (balancing.empty())
		return;   // nothing declared as balance-bearing: the register does not claim double entry

	// ⭐⭐ AN INACTIVE LINE IS THERE AND IS NOT IN FORCE. The totals delta is guarded by Active — an
	// entry written but not in effect moves no figure — so a check that sums it is asking a different
	// question from the one the stored data answers: a posting whose inactive lines happen not to
	// balance would be refused although nothing it contributes is unbalanced, and one deliberately
	// balanced BY its inactive lines would be accepted while the register books it lopsided.
	const ibValueMetaObjectAttributeBase* active = meta->GetRegisterActive();

	for (long row = 0; row < GetRowCount(); row++) {
		const ibDataViewItem item = GetItem(row);

		if (active != nullptr) {
			ibValue inForce;
			GetValueByMetaID(item, active->GetMetaID(), inForce);
			if (!inForce.GetBoolean())
				continue;
		}

		ibValue accountValue;
		GetValueByMetaID(item, account->GetMetaID(), accountValue);
		if (IsOffBalanceAccount(accountValue, offBalanceCache))
			continue;   // a separate circuit — it is not summed into the balance and needs no counterpart

		ibValue side;
		GetValueByMetaID(item, recordType->GetMetaID(), side);
		const bool isDebit = (side == debitSide);

		for (const ibValueMetaObjectResource* resource : balancing) {
			ibValue figure;
			GetValueByMetaID(item, resource->GetMetaID(), figure);
			// ALGEBRAIC. A negative amount is a reversal and lowers its own side, which a plain sum
			// already does — nothing is normalised into an entry on the other side.
			(isDebit ? debit : credit)[resource->GetMetaID()] += figure.GetNumber();
		}
	}

	for (const ibValueMetaObjectResource* resource : balancing) {
		const ibNumber dr = debit[resource->GetMetaID()];
		const ibNumber cr = credit[resource->GetMetaID()];
		if (dr == cr)
			continue;

		// ⚠ NAMED, NOT "the posting is wrong". The author needs the resource and the difference to find
		// the line that is off; a message that only says something is unbalanced is a message that
		// costs an hour.
		ibBackendCoreException::Error(
			_("the entry does not balance in '%s': debit %s, credit %s, difference %s"),
			resource->GetName(), dr.ToString(), cr.ToString(), (dr - cr).ToString());
	}
}

// ⭐⭐ WHAT THE ACCOUNT DOES NOT KEEP IS EMPTIED — see the note beside the declaration. The kinds of
// accounting a chart declares are boolean fields OF THE ACCOUNT, so the account itself is asked, and
// the answer is remembered per kind: a posting names the same few accounts over and over.
//
// ⚠ WITH A CORRESPONDENCE LINE, EITHER SIDE KEEPS IT. One line, two accounts and one set of figures:
// a currency amount is real if EITHER the debit or the credit account is a currency account, and
// blanking it because the other side is not would throw away the half that is meant. Telling the two
// sides apart is what a NON-BALANCE dimension is for — a value of its own per side — and until those
// halves exist there is one value, so one value is what is judged.
void ibValueRecordSetObjectAccountingRegister::ApplyAccountingKinds()
{
	const ibValueMetaObjectAccountingRegister* meta = GetAccountingMetaObject();
	if (meta == nullptr)
		return;

	const ibValueMetaObjectAttributeBase* account = meta->GetRegisterAccount();
	if (account == nullptr)
		return;

	// WHICH FIELDS NAME A KIND — asked once for the whole set, not per row. A field also says WHICH
	// SIDE judges it: a non-balance dimension holds a value per side, so its debit half answers to the
	// debit account and its credit half to the credit one. Everything else is one value for the line,
	// and one value is kept if either side keeps the kind.
	struct ibFigureOfKind { ibMetaID m_field; ibMetaID m_kind; ibAcctJudgedBy m_by; };
	std::vector<ibFigureOfKind> figures;
	const auto judge = [&figures, meta](const ibValueMetaObjectAttributeBase* field, const ibMetaDescription& kind) {
		const ibValueMetaObjectAttributeBase* debit  = meta->GetFieldSide(/*creditSide*/ false, field);
		const ibValueMetaObjectAttributeBase* credit = meta->GetFieldSide(/*creditSide*/ true, field);
		if (debit == nullptr || credit == nullptr) {
			figures.push_back({ field->GetMetaID(), kind.GetByIdx(0), ibAcctJudgedBy::EitherAccount });
			return;
		}
		figures.push_back({ debit->GetMetaID(),  kind.GetByIdx(0), ibAcctJudgedBy::Account });
		figures.push_back({ credit->GetMetaID(), kind.GetByIdx(0), ibAcctJudgedBy::CreditAccount });
	};

	for (const ibValueMetaObjectDimension* dimension : meta->GetDimensionArrayObject()) {
		if (dimension == nullptr) continue;
		const ibMetaDescription& kind = dimension->GetAccountingKind();
		if (!kind.IsOk()) continue;

		// ⭐ ONE ENTRY PER ATTRIBUTE THE LINE HOLDS IT IN — the field itself, judged by either side; or,
		// kept per side, its two side attributes, each judged by its own account.
		judge(dimension, kind);
	}
	// A FIGURE SPLITS FOR THE SAME REASON A DIMENSION DOES, and is judged the same way: a quantity or
	// an amount in currency is held per side, so the debit half answers to the debit account and the
	// credit half to the credit one — the goods account keeps a quantity, the supplier account does
	// not, and one entry between them fills exactly one of the two.
	for (const ibValueMetaObjectResource* resource : meta->GetResourceArrayObject()) {
		if (resource == nullptr) continue;
		const ibMetaDescription& kind = resource->GetAccountingKind();
		if (!kind.IsOk()) continue;

		judge(resource, kind);
	}

	if (figures.empty())
		return;   // nothing is kept conditionally — every figure belongs to every account

	const ibValueMetaObjectAttributeBase* accountCr =
		meta->IsCorrespondence() ? meta->GetRegisterAccountCr() : nullptr;

	// One memory per kind: account -> does it keep this kind.
	std::map<ibMetaID, std::unordered_map<ibValue, bool, ibValueHash, ibValueEqual>> kept;

	for (long row = 0; row < GetRowCount(); row++) {
		const ibDataViewItem item = GetItem(row);

		ibValue debitAccount, creditAccount;
		GetValueByMetaID(item, account->GetMetaID(), debitAccount);
		if (accountCr != nullptr)
			GetValueByMetaID(item, accountCr->GetMetaID(), creditAccount);

		for (const ibFigureOfKind& figure : figures) {
			auto& memory = kept[figure.m_kind];

			bool keeps = false;
			switch (figure.m_by) {
			case ibAcctJudgedBy::Account:
				keeps = meta->IsAccountingKindKept(debitAccount, figure.m_kind, memory);
				break;
			case ibAcctJudgedBy::CreditAccount:
				// One-sided register: the line is a debit OR a credit and its account is the one read
				// above, so the credit half has no account of its own to answer for it.
				keeps = accountCr != nullptr && meta->IsAccountingKindKept(creditAccount, figure.m_kind, memory);
				break;
			case ibAcctJudgedBy::EitherAccount:
				keeps = meta->IsAccountingKindKept(debitAccount, figure.m_kind, memory)
					|| (accountCr != nullptr && meta->IsAccountingKindKept(creditAccount, figure.m_kind, memory));
				break;
			case ibAcctJudgedBy::CorrAccount:
				break;   // a line has no correspondent of its own — that is a reading's cut
			}

			if (!keeps)
				SetValueByMetaID(item, figure.m_field, ibValue());
		}
	}
}

bool ibValueRecordSetObjectAccountingRegister::WriteRecordSet(bool replace, bool clearTable)
{
	// THE KINDS ARE APPLIED FIRST — the balance below must not weigh a figure this account does not
	// keep, and the base must not store one.
	ApplyAccountingKinds();
	// BEFORE the write, not inside it: a set that does not balance is not a posting, and the cheapest
	// place to say so is before a transaction has been opened for it.
	CheckDoubleEntry();
	return ibValueRecordSetObject::WriteRecordSet(replace, clearTable);
}

bool ibValueRecordSetObjectAccountingRegister::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	switch (lMethodNum)
	{
	case func::eAdd:
		pvarRetValue = AppendPostingLine(this, GetAccountingMetaObject(), /*sideGiven*/ false, false);
		return true;
	case func::eAddDebit:
		pvarRetValue = AppendPostingLine(this, GetAccountingMetaObject(), /*sideGiven*/ true, /*credit*/ false);
		return true;
	case func::eAddCredit:
		pvarRetValue = AppendPostingLine(this, GetAccountingMetaObject(), /*sideGiven*/ true, /*credit*/ true);
		return true;
	case func::eCount:
		pvarRetValue = (unsigned int)GetRowCount();
		return true;
	case func::eClear:
		ibValueModelStorage::Clear();
		// ⭐ CLEARING IS A CHANGE. Emptying the set is how a handler says "no movements" - and the
		// document's final write skips a set that is not modified, so an unmarked Clear would leave
		// yesterday's movements standing.
		Modify(true);
		return true;
	case func::eLoad:
		LoadDataFromTable(paParams[0]->ConvertToType<ibValueModel>());
		return true;
	case func::eUnload:
		pvarRetValue = SaveDataToTable();
		return true;
	case func::eWriteRecordSet:
		WriteRecordSet(
			lSizeArray > 0 ?
			paParams[0]->GetBoolean() : true
		);
		return true;
	case func::eModifiedRecordSet:
		pvarRetValue = m_objModified;
		return true;
	case func::eReadRecordSet:
		Read();
		return true;
	case func::eSelectedRecordSet:
		pvarRetValue = Selected();
		return true;
	case func::eGetMetadataRecordSet:
		pvarRetValue = GetMetaObject();
		return true;
	}

	return false;
}

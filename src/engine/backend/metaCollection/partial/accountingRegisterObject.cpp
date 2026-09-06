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

// ⭐⭐ WRITING BY KIND — the pair is written TOGETHER.
//
// The kind lands in its own column beside the value, rather than being inferred from the account's
// kinds table by position. That is what makes a stored movement self-describing: an old row still says
// what its value was a kind OF, so re-ordering an account's kinds later cannot silently change the
// meaning of data already written, and a reading needs no join per slot per row.
//
// Which slot is used is decided here and is nobody's business upstream: the one already holding this
// kind, or the first free one.
bool ibValueRecordSetObjectAccountingRegister::ibValueAccountDimensions::SetAt(
	const ibValue& varKeyValue, const ibValue& varValue)
{
	const ibValueMetaObjectAccountingRegister* meta =
		m_recordSet != nullptr ? m_recordSet->GetAccountingMetaObject() : nullptr;
	if (meta == nullptr)
		return false;

	if (varKeyValue.IsEmpty())
		ibBackendCoreException::Error(_("an account dimension is addressed by its KIND, and none was given"));

	long target = wxNOT_FOUND;
	long firstFree = wxNOT_FOUND;

	for (unsigned int idx = 0; idx < meta->GetAccountDimensionCount(); idx++) {
		const ibValueMetaObjectAttributeBase* kindSlot = meta->GetAccountDimensionKindSlot(m_creditSide, idx);
		if (kindSlot == nullptr)
			continue;

		ibValue current;
		m_recordSet->GetValueByMetaID(m_line, kindSlot->GetMetaID(), current);
		if (!current.IsEmpty() && current == varKeyValue) { target = idx; break; }
		if (current.IsEmpty() && firstFree == wxNOT_FOUND) firstFree = idx;
	}

	if (target == wxNOT_FOUND)
		target = firstFree;

	if (target == wxNOT_FOUND) {
		// ⚠ SAID, NOT SWALLOWED. The number of slots is declared by the chart of accounts, so "no room"
		// is a configuration statement the author can act on — and a posting that quietly dropped one of
		// its analytics would be found months later, in a report that does not add up.
		ibBackendCoreException::Error(_("this register has no free account dimension slot: the chart of accounts declares %u"),
			meta->GetAccountDimensionCount());
		return false;
	}

	const ibValueMetaObjectAttributeBase* kindSlot = meta->GetAccountDimensionKindSlot(m_creditSide, target);
	const ibValueMetaObjectAttributeBase* slot     = meta->GetAccountDimensionSlot(m_creditSide, target);
	if (kindSlot == nullptr || slot == nullptr)
		return false;

	m_recordSet->SetValueByMetaID(m_line, kindSlot->GetMetaID(), varKeyValue);

	// The kind LIMITS: its own `Type` attribute IS a description of types, read straight off the
	// reference by the id the chart of characteristic types declares for it.
	ibValue kindType;
	const ibValueMetaObjectChartOfCharacteristicTypes* chart = nullptr;
	ibValueReferenceDataObject* kindRef = nullptr;
	if (varKeyValue.ConvertToValue(kindRef) && kindRef != nullptr &&
		kindRef->GetMetaObject()->ConvertToValue(chart) && chart != nullptr)
		kindRef->GetValueByMetaID(chart->GetDataType()->GetMetaID(), kindType);

	ibValueTypeDescription* limit = nullptr;
	const bool limited = kindType.ConvertToValue(limit) && limit != nullptr;

	// LOUD, because there is nothing to deduce from silence here: a kind that carries no type
	// description means the value went in bounded by the column alone, and the kind's own rule was
	// never applied. In a release build the posting still goes through — refusing would turn a
	// metadata problem into lost data.
	wxASSERT_MSG(limited, "account dimension: the kind carries no Type description, the limit is unknown");

	m_recordSet->SetValueByMetaID(m_line, slot->GetMetaID(),
		limited ? slot->AdjustValue(varValue, limit->m_typeDesc) : slot->AdjustValue(varValue));
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

std::vector<std::pair<wxString, ibValue>>
ibValueRecordSetObjectAccountingRegister::ibValueAccountDimensions::DeclaredKinds() const
{
	const ibValueMetaObjectAccountingRegister* meta =
		m_recordSet != nullptr ? m_recordSet->GetAccountingMetaObject() : nullptr;
	if (meta == nullptr)
		return {};

	// Whose analytics these are: the debit account on the debit side, the credit one on the credit
	// side — and in a one-sided register there is only the one account, whichever side the row is.
	const ibValueMetaObjectAttributeBase* accountAttribute =
		(m_creditSide && meta->IsCorrespondence() && meta->GetRegisterAccountCr() != nullptr)
			? meta->GetRegisterAccountCr() : meta->GetRegisterAccount();
	if (accountAttribute == nullptr)
		return {};

	ibValue account;
	m_recordSet->GetValueByMetaID(m_line, accountAttribute->GetMetaID(), account);
	return KindsOfAccount(account);
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
	m_members.Bind(this, &ibValueAccountingLine::FillMembers);
}

ibValueRecordSetObjectAccountingRegister::ibValueAccountingLine::~ibValueAccountingLine()
{
}

void ibValueRecordSetObjectAccountingRegister::ibValueAccountingLine::FillMembers(ibMemberTable& helper) const
{
	// The attributes come from the base contributor (bound in its own ctor); these are the two views
	// over the slot pairs. In a one-sided register there is one collection and it needs no side in its
	// name — the side is said by RecordType; in a correspondence register there are two, because the
	// line names both accounts and each has its own analytical breakdown.
	const ibValueMetaObjectAccountingRegister* meta =
		m_ownerSet != nullptr ? m_ownerSet->GetAccountingMetaObject() : nullptr;
	if (meta == nullptr)
		return;

	if (meta->IsCorrespondence()) {
		helper.AppendProp(wxT("AccountDimensionDr"), ePropAccountDimensionDr);
		helper.AppendProp(wxT("AccountDimensionCr"), ePropAccountDimensionCr);
	}
	else {
		helper.AppendProp(wxT("AccountDimension"), ePropAccountDimension);
	}
}

bool ibValueRecordSetObjectAccountingRegister::ibValueAccountingLine::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	const ibMetaID& id = m_members.GetPropData(lPropNum);
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
	const ibMetaID& id = m_members.GetPropData(lPropNum);

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

	// ⭐ THE FILTER, AS A PROPERTY. It is also bound as a module export (InitializeObject), which is
	// what lets the set's own module say `Filter.X` — but an export is invisible from outside and to
	// the editor's completion. Declared here, a caller can address the set before writing it, which
	// is the difference between "replace these records" and "replace the table".
	helper.AppendProp(wxT("Filter"), ibValueRecordSetObject::enPropFilter);
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

bool ibValueRecordSetObjectAccountingRegister::WriteRecordSet(bool replace, bool clearTable)
{
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

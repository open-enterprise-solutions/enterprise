////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : value structure and containers
////////////////////////////////////////////////////////////////////////////

#include "valueMap.h"
#include "backend/backend_exception.h"
#include "backend/appData.h"

#include <algorithm>  // lexicographical_compare / equal — the entry walk
#include <cwctype>    // towupper — the non-ASCII half of the case fold


namespace {
// Upper-fold ONE character. Every name a script writes, and every digit an
// integer key prints, is ASCII — a compare and a subtract, inlined. std::towupper
// is locale-aware and does not inline, and it was being paid per CHARACTER of
// every key on every hash AND every comparison; the non-ASCII tail still goes to
// it, so what counts as equal is unchanged.
inline wchar_t FoldChar(const wchar_t c)
{
	if (c < 128)
		return (c >= L'a' && c <= L'z') ? (wchar_t)(c - (L'a' - L'A')) : c;
	return (wchar_t)std::towupper((wint_t)c);
}
}

// WHICH RULE A KEY FOLLOWS — decided by what the key is, with nothing rendered to text on the way.
//
//   a STRING key in a STRUCTURE is a field NAME and folds case, because a script reaches a field
//   through a dot and does not care how it was typed (`s.Name` and `s.name` are one field). The fold
//   runs over the text ONCE per lookup, here, and not once per candidate.
//
//   a STRING key in a CONTAINER is a value, and hashes as the value it is: "fr" and "FR" are two keys,
//   as `"fr" = "FR"` is False. Until 2026-09-23 it folded like a field name, which made a Container
//   answer a question about its keys differently from the language's own `=`.
//
//   anything else compares AS A VALUE — ibValue's own ordering, so a reference matches by its guid
//   and a number by its magnitude. This is what replaced GetHashKey(): the container used to render
//   every non-string key to text and compare the text, which made `1` and "1" the same key. They are
//   different keys now, and deliberately: the language's own comparison says so everywhere else.
size_t ibValueContainer::HashOf(const ibValue& key) const
{
	if (m_keyKind == ibKeyKind::Name && key.GetType() == ibValueTypes::TYPE_STRING) {
		const ibString text = key.GetString();        // the key's text shared, not copied
		std::uint64_t h = kIbHashBasis;
		for (const wchar_t* p = text.wc_str(); *p != L'\0'; ++p)
			h = ibHashCombine(h, FoldChar(*p));
		return (size_t)h;                                    // folded once, per lookup — not per candidate
	}
	return key.GetValueHash();                               // the value's own hash, agreeing with its order
}

// A STRUCTURE'S BUCKET WALK, with the two shortcuts the string comparison in stringUtils earned:
// LENGTH FIRST (two names of different length are never the same field, and that decides most
// candidates without reading a character), then FOLD ONLY WHAT DIFFERS (characters that already
// match need no case conversion — and in a structure the field being looked up usually matches
// exactly). Applied to the live buffers, so no string is built to compare two.
static bool FoldedEquals(const ibString& a, const ibString& b)
{
	if (a.Len() != b.Len())
		return false;
	const wchar_t* pa = a.wc_str();
	const wchar_t* pb = b.wc_str();
	for (; *pa != L'\0'; ++pa, ++pb) {
		if (*pa == *pb)
			continue;
		if (FoldChar(*pa) != FoldChar(*pb))
			return false;
	}
	return true;
}

// Entries in one bucket share a hash, not a key, so each candidate is settled against the entry
// itself — a field name against a field name folded (Structure), text against text exactly
// (Container), value against value otherwise. Text is kept apart from every other kind in both:
// the value ordering puts an enumeration beside the text of its presentation, and a key is not that.
bool ibValueContainer::KeyMatches(const ibValue& candidate, const ibValue& key) const
{
	const bool isText = (key.GetType() == ibValueTypes::TYPE_STRING);
	if (isText != (candidate.GetType() == ibValueTypes::TYPE_STRING))
		return false;                                        // text never matches a non-text key
	if (isText) {
		const ibString candText = candidate.GetString();     // shared, not copied
		const ibString keyText = key.GetString();
		return m_keyKind == ibKeyKind::Name ? FoldedEquals(candText, keyText) : candText == keyText;
	}
	return candidate.CompareValueLS(key) == 0;
}

long ibValueContainer::FindWithHash(const ibValue& key, const size_t hash) const
{
	const auto range = m_index.equal_range(hash);
	for (auto it = range.first; it != range.second; ++it)
		if (KeyMatches(m_entries[it->second].first, key))
			return (long)it->second;
	return wxNOT_FOUND;
}

// A container with no index (fewer than kIndexMin entries) is walked: the same rule of equality, no hash.
long ibValueContainer::FindByWalk(const ibValue& key) const
{
	for (size_t at = 0; at < m_entries.size(); ++at)
		if (KeyMatches(m_entries[at].first, key))
			return (long)at;
	return wxNOT_FOUND;
}

long ibValueContainer::IndexOf(const ibValue& key) const
{
	return Indexed() ? FindWithHash(key, HashOf(key)) : FindByWalk(key);
}

void ibValueContainer::IndexNewEntry(const bool wasIndexed, const size_t hash)
{
	if (wasIndexed) {
		m_index.emplace(hash, m_entries.size() - 1);
		return;
	}
	if (!Indexed())
		return;
	// The count just reached kIndexMin: every entry is indexed now, the new one with the rest.
	m_index.reserve(m_entries.size());
	for (size_t at = 0; at < m_entries.size(); ++at)
		m_index.emplace(HashOf(m_entries[at].first), at);
}

// The other side as a container, or nullptr. dynamic_cast for the reason
// measured in valueArray.cpp (a class-id compare is ×7.6 SLOWER here, not
// faster) — read that note before changing this one.
//
// ⚠ A cast to the BASE also accepts the derived Structure, where a class-id
// compare would have separated them. That is deliberate and it matches the
// store: a Structure IS a Container whose keys must be strings, and two of them
// holding the same pairs hold the same data. If the two are ever required to
// compare unequal, that is a rule about the TYPES and belongs in an override on
// ibValueStructure, not in a cheaper-looking cast here.
const ibValueContainer* ibValueContainer::AsContainer(const ibValue& cParam) const
{
	return dynamic_cast<const ibValueContainer*>(cParam.GetRef());
}

// An entry is a PAIR, so it orders as one: key first, value only as the
// tiebreak. Same element walk as the array — the vector's own comparison, handed
// the comparator for what it holds.
int ibValueContainer::CompareValueLS(const ibValue& cParam) const
{
	const ibValueContainer* rhs = AsContainer(cParam);
	if (rhs == nullptr)
		return ibValue::CompareValueLS(cParam);   // not a container — the base places it by KIND


	using Entry = std::pair<ibValue, ibValue>;
	const auto less = [](const Entry& a, const Entry& b) {
		const int c = a.first.CompareValueLS(b.first);
		return c != 0 ? c < 0 : a.second.CompareValueLS(b.second) < 0;
	};
	if (std::lexicographical_compare(m_entries.begin(), m_entries.end(),
	                                 rhs->m_entries.begin(), rhs->m_entries.end(), less))
		return -1;
	if (std::lexicographical_compare(rhs->m_entries.begin(), rhs->m_entries.end(),
	                                 m_entries.begin(), m_entries.end(), less))
		return 1;
	return 0;
}

// Entry order is insertion order and the comparison walks it, so the hash walks
// it too: same pairs in the same sequence, same value. Both halves of a pair go
// in — {a:1} and {a:2} are different containers.
size_t ibValueContainer::GetValueHash() const
{
	std::uint64_t h = ibHashCombine(kIbHashBasis, m_entries.size());
	for (const auto& entry : m_entries) {
		h = ibHashCombine(h, entry.first.GetValueHash());
		h = ibHashCombine(h, entry.second.GetValueHash());
	}
	return (size_t)h;
}

// Equality asks the entries for EQUALITY — the type-strict rule — rather than
// for order-equal, exactly as the array does.
bool ibValueContainer::CompareValueEQ(const ibValue& cParam) const
{
	const ibValueContainer* rhs = AsContainer(cParam);
	if (rhs == nullptr)
		return false;

	using Entry = std::pair<ibValue, ibValue>;
	return m_entries.size() == rhs->m_entries.size()
		&& std::equal(m_entries.begin(), m_entries.end(), rhs->m_entries.begin(),
		              [](const Entry& a, const Entry& b) {
			              return a.first.CompareValueEQ(b.first)
			                  && a.second.CompareValueEQ(b.second);
		              });
}

//**********************************************************************
//*                          ibValueReturnMap                           *
//**********************************************************************

void ibValueContainer::ibValueReturnContainer::BindNames(ibMemberTable& helper, const ibValue* /*ctx*/)
{
	helper.AppendProp(wxT("Key"));
	helper.AppendProp(wxT("Value"));
}

bool ibValueContainer::ibValueReturnContainer::SetPropVal(const long lPropNum, const ibValue& cValue)
{
	return false;
}

bool ibValueContainer::ibValueReturnContainer::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	switch (lPropNum)
	{
	case enKey:
		pvarPropVal = m_key;
		return true;
	case enValue:
		pvarPropVal = m_value;
		return true;
	}

	return false;
}

//**********************************************************************
//*                            ibValueContainer                         *
//**********************************************************************

ibValueContainer::ibValueContainer() : ibValue(ibValueTypes::TYPE_VALUE), m_keyKind(ibKeyKind::Value) {
}

ibValueContainer::ibValueContainer(const std::map<ibValue, ibValue>& containerValues) : ibValue(ibValueTypes::TYPE_VALUE, true), m_keyKind(ibKeyKind::Value) {
	// SetAt, not Insert: should the source map hold two keys this store calls one, a
	// build keeps the last rather than throwing.
	for (const auto& cntVal : containerValues)
		ibValueContainer::SetAt(cntVal.first, cntVal.second);
}

ibValueContainer::ibValueContainer(bool readOnly) : ibValue(ibValueTypes::TYPE_VALUE, readOnly), m_keyKind(ibKeyKind::Value) {
}

ibValueContainer::ibValueContainer(bool readOnly, ibKeyKind keyKind) : ibValue(ibValueTypes::TYPE_VALUE, readOnly), m_keyKind(keyKind) {
}

ibValueContainer::~ibValueContainer() {
}

namespace {
void BindWritableContainerNames(ibValue::ibMemberTable& helper, const ibValue* /*ctx*/) { ibValueContainer::BindContainerNames(helper, false); }
void BindReadOnlyContainerNames(ibValue::ibMemberTable& helper, const ibValue* /*ctx*/) { ibValueContainer::BindContainerNames(helper, true); }
}

ibValue::ibMemberTable* ibValueContainer::DoGetPMethods() const
{
	return m_bReadOnly ? ibMemberTable::Shared<&BindReadOnlyContainerNames>()
	                   : ibMemberTable::Shared<&BindWritableContainerNames>();
}

// The FIXED method surface — methods only. It does not publish the keys, so it is
// type-invariant given the read-only flag, and shared (DoGetPMethods). The keys are
// the store's job (FindProp / GetPropName).
void ibValueContainer::BindContainerNames(ibMemberTable& helper, const bool readOnly)
{
	helper.AppendFunc(wxT("Count"), wxT("Count()"));
	helper.AppendFunc(wxT("Property"), 2, wxT("Property(key : any, valueFound : any)"));
	// READ WITHOUT ASKING FIRST. Property answers whether a key is there and hands the value
	// back through its second argument; [key] answers the value and raises when the key is not
	// there. Get is the third question, the one the reference system's Map answers: the value,
	// or Undefined -- which is what "nothing is bound to this key" looks like everywhere else
	// in the language. A Structure gets it too: the member table is one table, a position in it
	// is a method number, and a field that is not there is the same question.
	helper.AppendFunc(wxT("Get"), 1, wxT("Get(key : any)"));

	if (!readOnly) {
		helper.AppendFunc(wxT("Clear"), wxT("Clear()"));
		helper.AppendFunc(wxT("Delete"), 1, wxT("Delete(key : any)"));
		helper.AppendFunc(wxT("Insert"), 2, wxT("Insert(key : any, value : any)"));
	}
}

// ---- the key surface, straight off the store --------------------------------
// FindProp is the `container.key` resolver: a probe of the store (hash or walk,
// IndexOf), not a member-table scan. GetNProps / GetPropName / Get / SetPropVal
// are the index side of the same store, used by the interpreter after FindProp
// and by introspection.

long ibValueContainer::FindProp(const ibString& strPropName) const
{
	return IndexOf(ibValue(strPropName));
}

const ibString& ibValueContainer::GetPropName(const long lPropNum) const
{
	static const ibString s_absent;
	if (lPropNum < 0 || lPropNum >= (long)m_entries.size())
		return s_absent;
	// A string key lends its own text. A key of another kind — a number, a date — is NAMED by its text as well:
	// a LINQ projection over containers names its columns so (procUnitLINQ.cpp), and a key 42 came out as a
	// column with no name. That text is nowhere to be lent from, so it is made into this thread's one slot and
	// lives until the next such name here; every caller copies it on the spot. (FindProp still cannot reach
	// such a key by name — a text is not a number.)
	const ibValue& key = m_entries[lPropNum].first;
	if (key.m_typeClass == ibValueTypes::TYPE_STRING)
		return key.m_sData;
	static thread_local ibString s_made;
	s_made = key.GetString();
	return s_made;
}

bool ibValueContainer::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	if (lPropNum < 0 || lPropNum >= (long)m_entries.size())
		return false;
	pvarPropVal = m_entries[lPropNum].second;
	return true;
}

bool ibValueContainer::SetPropVal(const long lPropNum, const ibValue& varPropVal)
{
	if (lPropNum < 0 || lPropNum >= (long)m_entries.size())
		return false;
	m_entries[lPropNum].second = varPropVal;
	return true;
}

bool ibValueContainer::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	switch (lMethodNum)
	{
	case enClear:
		Clear();
		return true;
	case enCount:
		pvarRetValue = Count();
		return true;
	case enDelete:
		Delete(*paParams[0]);
		return true;
	case enInsert:
		Insert(*paParams[0], *paParams[1]);
		return true;
	case enProperty:
	{
		ibValue defaultVal;
		pvarRetValue = Property(*paParams[0], lSizeArray > 1 ? *paParams[1] : defaultVal);
	}
		return true;
	case enGet:
	{
		// A FORGOTTEN ARGUMENT IS REFUSED BY NAME. The arity check catches only a call with TOO
		// MANY arguments; a slot the method could have read is made empty and handed over, so
		// `c.Get()` would answer Undefined - which is this method's word for "the key is not
		// there" and would say it about a key nobody asked about.
		if (lSizeArray < 1 || paParams == nullptr)
			ibBackendCoreException::Error(_("Get: the key to look for is not given"));
		// Through Property, which is the lookup that does not raise -- and which a Structure
		// overrides to refuse a key that is not a field name, so Get refuses it there too.
		// A key that is not there leaves the value untouched, and it starts empty: Undefined.
		ibValue valueFound;
		Property(*paParams[0], valueFound);
		pvarRetValue = valueFound;
	}
		return true;
	}

	return false;
}

void ibValueContainer::Delete(const ibValue& varKeyValue)
{
	const long idx = IndexOf(varKeyValue);
	if (idx < 0)
		return;
	// Erase keeps insertion order, so every entry after the hole moves down one slot — and so does its index.
	// The index is shifted IN PLACE, not rebuilt: rebuilt, it hashed every remaining key again, and a script
	// replacing values one key at a time (Delete, then Insert) paid a hash of the whole container per value —
	// 192 s for the 36 000 employees of one payroll (MEASURED 2026-09-14, Debug). It is still a pass over the
	// index; `[key] = value` replaces a value in place and is the put.
	const size_t hole = static_cast<size_t>(idx);
	m_entries.erase(m_entries.begin() + idx);
	if (!Indexed()) {
		m_index.clear();   // back below kIndexMin: walked again, as Indexed() says
		return;
	}
	for (auto it = m_index.begin(); it != m_index.end();) {
		if (it->second == hole) {
			it = m_index.erase(it);
			continue;
		}
		if (it->second > hole)
			--it->second;
		++it;
	}
}

void ibValueContainer::Insert(const ibValue& varKeyValue, const ibValue& cValue)
{
	// ONE HASH OF THE KEY, and none while the container is walked. The hash is a fold
	// over the key's whole text, so on a Structure — where keys are field names —
	// walking it twice was a visible share of an insert. Computed here, then handed to
	// both the duplicate check and the index.
	const bool indexed = Indexed();
	const size_t hash = indexed ? HashOf(varKeyValue) : 0;
	if ((indexed ? FindWithHash(varKeyValue, hash) : FindByWalk(varKeyValue)) >= 0) {
		if (!appData->DesignerMode())
			ibBackendCoreException::Error(_("Key '%s' is already using!"), varKeyValue.GetString());
		return;
	}
	m_entries.emplace_back(varKeyValue, cValue);
	IndexNewEntry(indexed, hash);
}

bool ibValueContainer::Property(const ibValue& varKeyValue, ibValue& cValueFound)
{
	const long idx = IndexOf(varKeyValue);
	if (idx < 0)
		return false;
	cValueFound = m_entries[idx].second;
	return true;
}

std::shared_ptr<ibValueIteratorState> ibValueContainer::CreateIterator()
{
	using EntriesT = std::decay_t<decltype(m_entries)>;
	class State : public ibValueIteratorState {
	public:
		explicit State(const EntriesT& e) : m_entries(e) {}
		bool MoveNext(ibValue& current) override {
			if (m_started) ++m_pos; else m_started = true;
			if (m_pos >= m_entries.size()) return false;
			ibValue valueCopy = m_entries[m_pos].second;
			current = ibValue(static_cast<ibValue*>(
				new ibValueReturnContainer(m_entries[m_pos].first, valueCopy)));
			return true;
		}
		void Reset() override { m_pos = 0; m_started = false; }
		bool PeekSample(ibValue& current) const override {
			current = ibValue(static_cast<ibValue*>(new ibValueReturnContainer()));
			return true;
		}
	private:
		const EntriesT& m_entries;
		size_t m_pos = 0;
		bool m_started = false;
	};
	return std::make_shared<State>(m_entries);
}

bool ibValueContainer::SetAt(const ibValue& varKeyValue, const ibValue& varValue)
{
	// Assign by key: overwrite an existing entry, create it otherwise. (Insert,
	// the script verb, still refuses a duplicate; `[key] = v` is the put.)
	const bool indexed = Indexed();
	const size_t hash = indexed ? HashOf(varKeyValue) : 0;      // one hash for both branches, none when walked
	const long idx = indexed ? FindWithHash(varKeyValue, hash) : FindByWalk(varKeyValue);
	if (idx >= 0)
		m_entries[idx].second = varValue;
	else {
		m_entries.emplace_back(varKeyValue, varValue);
		IndexNewEntry(indexed, hash);
	}
	return true;
}

bool ibValueContainer::GetAt(const ibValue& varKeyValue, ibValue& pvarValue)
{
	const long idx = IndexOf(varKeyValue);
	if (idx >= 0) {
		pvarValue = m_entries[idx].second;
		return true;
	}
	if (!appData->DesignerMode())
		ibBackendCoreException::Error(_("Key '%s' not found!"), varKeyValue.GetString());
	return false;
}

//**********************************************************************
//*                            ibValueStructure                         *
//**********************************************************************

#define st_error_conversion _("Error conversion value. Must be string!")

bool ibValueStructure::Init(ibValue** paParams, const long lSizeArray)
{
	// No args → empty Structure ready for Insert later.
	if (lSizeArray == 0 || paParams == nullptr)
		return true;

	// First arg must be a string with comma-separated field names.
	const ibValue* fieldsArg = paParams[0];
	if (fieldsArg == nullptr || fieldsArg->GetType() != ibValueTypes::TYPE_STRING) {
		ibBackendCoreException::Error(
			_("Structure ctor: first argument must be a comma-separated field name string"));
		return false;
	}

	const wxString fieldsStr = fieldsArg->GetString();

	// Single-pass scan: split on ',' and trim whitespace. wxStringTokenizer
	// would also work but the manual form keeps trimming inline + avoids
	// the include. Empty tokens (`,,`) are skipped.
	size_t cursor = 0;
	long valueIdx = 1;   // index into paParams for the value of the next field
	while (cursor <= fieldsStr.size()) {
		size_t comma = fieldsStr.find(wxT(','), cursor);
		if (comma == wxString::npos) comma = fieldsStr.size();

		// Trim leading whitespace.
		size_t start = cursor;
		while (start < comma
			&& (fieldsStr[start] == wxT(' ') || fieldsStr[start] == wxT('\t')))
			++start;

		// Trim trailing whitespace.
		size_t end = comma;
		while (end > start
			&& (fieldsStr[end - 1] == wxT(' ') || fieldsStr[end - 1] == wxT('\t')))
			--end;

		if (end > start) {
			const wxString fieldName = fieldsStr.Mid(start, end - start);
			ibValue value;
			if (valueIdx < lSizeArray && paParams[valueIdx] != nullptr)
				value = *paParams[valueIdx];
			ibValueStructure::Insert(fieldName, value);
			++valueIdx;
		}

		if (comma >= fieldsStr.size()) break;
		cursor = comma + 1;
	}

	return true;
}

bool ibValueStructure::GetAt(const ibValue& varKeyValue, ibValue& pvarValue)
{
	if (varKeyValue.GetType() != ibValueTypes::TYPE_STRING) {
		if (!appData->DesignerMode())
			ibBackendCoreException::Error(st_error_conversion);
		return false;
	}
	return ibValueContainer::GetAt(varKeyValue, pvarValue);
}

bool ibValueStructure::SetAt(const ibValue& varKeyValue, const ibValue& cValue)
{
	if (varKeyValue.GetType() != ibValueTypes::TYPE_STRING) {
		if (!appData->DesignerMode()) {
			ibBackendCoreException::Error(st_error_conversion);
		} return false;
	}

	return ibValueContainer::SetAt(varKeyValue, cValue);
}

void ibValueStructure::Delete(const ibValue& varKeyValue)
{
	if (varKeyValue.GetType() != ibValueTypes::TYPE_STRING) {
		if (!appData->DesignerMode()) {
			ibBackendCoreException::Error(st_error_conversion);
		} return;
	}

	ibValueContainer::Delete(varKeyValue);
}

void ibValueStructure::Insert(const ibValue& varKeyValue, const ibValue& cValue)
{
	if (varKeyValue.GetType() != ibValueTypes::TYPE_STRING) {
		if (!appData->DesignerMode()) {
			ibBackendCoreException::Error(st_error_conversion);
		} return;
	}

	ibValueContainer::Insert(varKeyValue, cValue);
}

bool ibValueStructure::Property(const ibValue& varKeyValue, ibValue& cValueFound)
{
	if (varKeyValue.GetType() != ibValueTypes::TYPE_STRING) {
		if (!appData->DesignerMode()) {
			ibBackendCoreException::Error(st_error_conversion);
		}
		return false;
	}

	return ibValueContainer::Property(varKeyValue, cValueFound);
}



////////////////////////////////////////////////////////////////////////////
// Serialization — the container packs its pairs, each side packs itself
////////////////////////////////////////////////////////////////////////////
//
// Header from the base; contents here. A pair is two child nodes, and both are
// asked the same question the container was — so nesting needs no special case.
//
// A STRUCTURE inherits this unchanged: it differs in what it accepts as a KEY,
// not in how it is written, and the header already says which of the two it was.

#include "backend/serialize/dataBuilder.h"


bool ibValueContainer::DoSerialize(ibDataNode& node) const
{
	node.SetValue(wxT("n"), (s32)m_entries.size());

	for (const auto& entry : m_entries) {
		ibDataNode& keyNode = node.AddChild(entry.first.GetClassType(), 0);
		if (!entry.first.Serialize(keyNode))
			return false;   // one unpackable side voids the container
		ibDataNode& valueNode = node.AddChild(entry.second.GetClassType(), 0);
		if (!entry.second.Serialize(valueNode))
			return false;
	}

	return true;
}

bool ibValueContainer::DoDeserialize(const ibDataNode& node)
{
	Clear();

	const std::vector<ibDataNode>& children = node.Children();
	// Pairs, so an ODD number of children means the blob was cut between a key
	// and its value — refuse rather than drop the dangling one.
	if ((children.size() % 2) != 0)
		return false;

	for (std::size_t i = 0; i + 1 < children.size(); i += 2) {
		const ibValue key = ibValue::FromNode(children[i]);
		const ibValue value = ibValue::FromNode(children[i + 1]);
		Insert(key, value);
	}

	return (s32)m_entries.size() == node.GetValue<s32>(wxT("n"));
}

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

VALUE_TYPE_REGISTER(ibValueContainer, "Container", g_valueContainerCLSID);
VALUE_TYPE_REGISTER(ibValueStructure, "Structure", g_valueStructureCLSID);

SYSTEM_TYPE_REGISTER(ibValueContainer::ibValueReturnContainer, "KeyValue", g_valueKeyValueCLSID);

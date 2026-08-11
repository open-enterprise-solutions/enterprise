////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : value structure and containers
////////////////////////////////////////////////////////////////////////////

#include "valueMap.h"
#include "backend/backend_exception.h"
#include "backend/appData.h"

#include <cwctype>   // towupper — inline case fold, no allocation


// The key's identity as a wide C-string. A STRING value (the overwhelmingly
// common case) is read ZERO-COPY through its own buffer; a number / reference is
// materialised into `scratch` from its hash identity (text / guid — NOT the
// display string of a reference). The pointer is valid for the key's lifetime
// (string) or `scratch`'s (materialised), which is the duration of the caller.
const wchar_t* ibValueContainer::Identity(const ibValue& key, ibString& scratch)
{
	if (key.GetType() == ibValueTypes::TYPE_STRING)
		return key.GetString(scratch).wc_str();
	scratch = key.GetHashKey();   // wxString -> ibString (rare path)
	return scratch.wc_str();
}

// FNV-1a over the upper-folded identity — case-insensitive, allocation-free for a
// string key. Shares Identity with KeyEq so equal keys always hash equal.
size_t ibValueContainer::KeyHash::operator()(const ibValue& k) const
{
	ibString scratch;
	const wchar_t* p = Identity(k, scratch);
	size_t h = 1469598103934665603ULL;            // FNV-1a offset basis (64-bit)
	for (; *p != L'\0'; ++p) {
		h ^= (size_t)std::towupper((wint_t)*p);
		h *= 1099511628211ULL;                    // FNV-1a prime
	}
	return h;
}

// Case-insensitive comparison of the two identities, in place — no string built.
bool ibValueContainer::KeyEq::operator()(const ibValue& a, const ibValue& b) const
{
	ibString sa, sb;
	const wchar_t* pa = Identity(a, sa);
	const wchar_t* pb = Identity(b, sb);
	for (; *pa != L'\0' && *pb != L'\0'; ++pa, ++pb)
		if (std::towupper((wint_t)*pa) != std::towupper((wint_t)*pb))
			return false;
	return *pa == *pb;   // equal only if both ended together
}

long ibValueContainer::IndexOf(const ibValue& key) const
{
	const auto it = m_index.find(key);
	return it != m_index.end() ? (long)it->second : wxNOT_FOUND;
}

//**********************************************************************
//*                          ibValueReturnMap                           *
//**********************************************************************

void ibValueContainer::ibValueReturnContainer::FillMembers(ibMemberTable& helper) const
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

ibValueContainer::ibValueContainer() : ibValueDynamicMembers(ibValueTypes::TYPE_VALUE) {
	m_members.Bind(&BindContainerNames, this);
}

ibValueContainer::ibValueContainer(const std::map<ibValue, ibValue>& containerValues) : ibValueDynamicMembers(ibValueTypes::TYPE_VALUE, true) {
	m_members.Bind(&BindContainerNames, this);
	// SetAt, not Insert: the source map may hold keys this container folds together
	// (its keys are case-insensitive), and a build should keep the last, not throw.
	for (const auto& cntVal : containerValues)
		ibValueContainer::SetAt(cntVal.first, cntVal.second);
}

ibValueContainer::ibValueContainer(bool readOnly) : ibValueDynamicMembers(ibValueTypes::TYPE_VALUE, readOnly) {
	m_members.Bind(&BindContainerNames, this);
}

ibValueContainer::~ibValueContainer() {
}

// The FIXED method surface — methods only. It no longer publishes the keys, so
// it is type-invariant (given the read-only flag), built ONCE, and never rebuilt
// on a data mutation. The keys are the store's job (FindProp / GetPropName).
void ibValueContainer::BindContainerNames(ibMemberTable& helper, const ibValue* ctx)
{
	const ibValueContainer* self = static_cast<const ibValueContainer*>(ctx);

	helper.AppendFunc(wxT("Count"), wxT("Count()"));
	helper.AppendFunc(wxT("Property"), 2, wxT("Property(key : any, valueFound : any)"));

	if (!self->m_bReadOnly) {
		helper.AppendFunc(wxT("Clear"), wxT("Clear()"));
		helper.AppendFunc(wxT("Delete"), 1, wxT("Delete(key : any)"));
		helper.AppendFunc(wxT("Insert"), 2, wxT("Insert(key : any, value : any)"));
	}
}

// ---- the key surface, straight off the store --------------------------------
// FindProp is the `container.key` resolver: a hash probe, not a member-table
// scan. GetNProps / GetPropName / Get / SetPropVal are the index side of the
// same store, used by the interpreter after FindProp and by introspection.

long ibValueContainer::FindProp(const wxString& strPropName) const
{
	return IndexOf(ibValue(strPropName));
}

wxString ibValueContainer::GetPropName(const long lPropNum) const
{
	if (lPropNum < 0 || lPropNum >= (long)m_entries.size())
		return wxEmptyString;
	return m_entries[lPropNum].first.GetString();
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
	}

	return false;
}

void ibValueContainer::Delete(const ibValue& varKeyValue)
{
	const long idx = IndexOf(varKeyValue);
	if (idx < 0)
		return;
	// Erase keeps insertion order, so every entry after the hole moves down one
	// slot — and so does its index. Rebuilding the map for the tail is simpler
	// (and less error-prone) than patching each shifted entry in place. Delete is
	// the rare operation; the common build / read paths stay O(1).
	m_entries.erase(m_entries.begin() + idx);
	m_index.clear();
	for (size_t i = 0; i < m_entries.size(); ++i)
		m_index.emplace(m_entries[i].first, i);
}

void ibValueContainer::Insert(const ibValue& varKeyValue, const ibValue& cValue)
{
	if (m_index.find(varKeyValue) != m_index.end()) {
		if (!appData->DesignerMode())
			ibBackendCoreException::Error(_("Key '%s' is already using!"), varKeyValue.GetString());
		return;
	}
	m_index.emplace(varKeyValue, m_entries.size());
	m_entries.emplace_back(varKeyValue, cValue);
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
	const long idx = IndexOf(varKeyValue);
	if (idx >= 0)
		m_entries[idx].second = varValue;
	else {
		m_index.emplace(varKeyValue, m_entries.size());
		m_entries.emplace_back(varKeyValue, varValue);
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

VALUE_TYPE_REGISTER(ibValueContainer, "Container", value_to_clsid("VL_CONTR"));
VALUE_TYPE_REGISTER(ibValueStructure, "Structure", value_to_clsid("VL_STRUT"));

SYSTEM_TYPE_REGISTER(ibValueContainer::ibValueReturnContainer, "KeyValue", system_to_clsid("VL_KEVAL"));

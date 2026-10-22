#include "protocolValue.h"

#include <algorithm>
#include <climits>
#include <cwchar>

#include "core/serialize/jsonProvider.h"   // ibJsonProvider::kMaxNesting — the backend's ceiling, the client's too
#include "core/serialize/jsonText.h"       // ibJsonText — how a string is spelled in JSON, written and read

namespace {

using ibKind = ibProtocolValue::ibKind;

// A whole number's digits, from the stack.
void AppendInteger(ibString& text, long long number)
{
	wchar_t digits[24];
	wchar_t* at = digits + 24;
	unsigned long long magnitude = number < 0 ? 0ULL - static_cast<unsigned long long>(number)
		: static_cast<unsigned long long>(number);
	do {
		*--at = static_cast<wchar_t>(L'0' + magnitude % 10);
		magnitude /= 10;
	} while (magnitude != 0);
	if (number < 0)
		*--at = L'-';
	text.append(at, static_cast<size_t>(digits + 24 - at));
}

// THE ANSWER READ — a recursive descent over the characters themselves: a text with no escape in it is taken whole
// where it lies, a whole number is read in place. Past ibJsonProvider::kMaxNesting the text is refused: every level is
// a frame of the descent.
class ibValueReader {
public:
	ibValueReader(const wchar_t* text, size_t length) : m_p(text), m_end(text + length) {}

	// One value, and nothing after it but space.
	bool ReadDocument(ibProtocolValue& value)
	{
		if (!ReadValue(value, 0))
			return false;
		SkipSpace();
		return m_p == m_end;
	}

private:
	const wchar_t* m_p;
	const wchar_t* m_end;

	void SkipSpace()
	{
		while (m_p != m_end && (*m_p == L' ' || *m_p == L'\t' || *m_p == L'\n' || *m_p == L'\r'))
			++m_p;
	}
	bool Accept(wchar_t c)
	{
		SkipSpace();
		if (m_p == m_end || *m_p != c)
			return false;
		++m_p;
		return true;
	}
	bool Literal(const wchar_t* word, size_t length)
	{
		if (static_cast<size_t>(m_end - m_p) < length || std::wmemcmp(m_p, word, length) != 0)
			return false;
		m_p += length;
		return true;
	}

	bool ReadValue(ibProtocolValue& value, int depth);
	bool ReadBody(ibProtocolBody& body, int depth);
	bool ReadNumber(ibProtocolValue& value);
};

bool ibValueReader::ReadValue(ibProtocolValue& value, int depth)
{
	SkipSpace();
	if (m_p == m_end)
		return false;
	switch (*m_p) {
	case L'{':
	case L'[':
		if (depth >= ibJsonProvider::kMaxNesting)
			return false;
		value = *m_p == L'[' ? ibProtocolValue::List() : ibProtocolValue::Node();
		++m_p;
		return ReadBody(*value.GetBody(), depth + 1);
	case L'"': {
		ibString text;
		if (!ibJsonText::ReadQuoted(m_p, m_end, text))
			return false;
		value = ibProtocolValue(text);
		return true;
	}
	case L't':
		value = ibProtocolValue(true);
		return Literal(L"true", 4);
	case L'f':
		value = ibProtocolValue(false);
		return Literal(L"false", 5);
	case L'n':
		value = ibProtocolValue();
		return Literal(L"null", 4);
	default:
		return ReadNumber(value);
	}
}

// What a bracket opened, up to the one that closes it — the entries of a node or the items of a list, appended as they
// come: a name said twice is found by its last (ibProtocolBody::Find).
bool ibValueReader::ReadBody(ibProtocolBody& body, int depth)
{
	const wchar_t close = body.m_list ? L']' : L'}';
	if (Accept(close))
		return true;
	for (;;) {
		ibProtocolEntry& entry = body.m_entries.emplace_back();
		if (!body.m_list) {
			SkipSpace();
			if (!ibJsonText::ReadQuoted(m_p, m_end, entry.m_name) || !Accept(L':'))
				return false;
		}
		if (!ReadValue(entry.m_value, depth))
			return false;
		if (Accept(L','))
			continue;
		return Accept(close);
	}
}

// A whole number that fits — an id, a count, a flag of the protocol's — is read where it lies; a fraction, an exponent
// or one past sixty-four bits is kept by its digits.
bool ibValueReader::ReadNumber(ibProtocolValue& value)
{
	const wchar_t* const start = m_p;
	const bool negative = m_p != m_end && *m_p == L'-';
	if (negative)
		++m_p;
	const wchar_t* const digits = m_p;
	while (m_p != m_end && *m_p >= L'0' && *m_p <= L'9')
		++m_p;
	if (m_p == digits)
		return false;
	const wchar_t* const wholeEnd = m_p;
	if (m_p != m_end && *m_p == L'.') {
		const wchar_t* const fraction = ++m_p;
		while (m_p != m_end && *m_p >= L'0' && *m_p <= L'9')
			++m_p;
		if (m_p == fraction)
			return false;
	}
	if (m_p != m_end && (*m_p == L'e' || *m_p == L'E')) {
		++m_p;
		if (m_p != m_end && (*m_p == L'-' || *m_p == L'+'))
			++m_p;
		const wchar_t* const exponent = m_p;
		while (m_p != m_end && *m_p >= L'0' && *m_p <= L'9')
			++m_p;
		if (m_p == exponent)
			return false;
	}

	if (wholeEnd == m_p) {
		unsigned long long magnitude = 0;
		bool fits = true;
		for (const wchar_t* d = digits; d != wholeEnd; ++d) {
			const unsigned digit = static_cast<unsigned>(*d - L'0');
			if (magnitude > (ULLONG_MAX - digit) / 10) {
				fits = false;
				break;
			}
			magnitude = magnitude * 10 + digit;
		}
		if (fits && magnitude <= (negative ? 0ULL - static_cast<unsigned long long>(LLONG_MIN)
			: static_cast<unsigned long long>(LLONG_MAX))) {
			value = ibProtocolValue(negative ? static_cast<long long>(0ULL - magnitude) : static_cast<long long>(magnitude));
			return true;
		}
	}
	value = ibProtocolValue::Number(ibString(start, static_cast<size_t>(m_p - start)));
	return true;
}

// A node's entry by its name — the last written under it. Walked by pointer: a reverse iterator's wrapping was most of a
// look-up in the Debug build every one runs (2026-10-22: 110 of its 129 samples).
template<class Entries, class Name>
auto FindIn(Entries& entries, const Name& name) -> decltype(&entries.front().m_value)
{
	auto* const first = entries.data();
	for (auto* entry = first + entries.size(); entry != first;) {
		--entry;
		if (entry->m_name == name)
			return &entry->m_value;
	}
	return nullptr;
}

template<class Name>
ibProtocolValue& SetIn(std::vector<ibProtocolEntry>& entries, const Name& name)
{
	if (ibProtocolValue* const found = FindIn(entries, name))
		return *found;
	ibProtocolEntry& entry = entries.emplace_back();
	entry.m_name = ibString(name);
	return entry.m_value;
}

template<class Name>
void RemoveIn(std::vector<ibProtocolEntry>& entries, const Name& name)
{
	entries.erase(std::remove_if(entries.begin(), entries.end(),
		[&name](const ibProtocolEntry& entry) { return entry.m_name == name; }), entries.end());
}

} // namespace

ibProtocolValue ibProtocolValue::Number(const ibString& digits)
{
	ibProtocolValue number;
	number.m_kind = ibKind::Number;
	number.m_text = digits;
	return number;
}

ibProtocolValue ibProtocolValue::Node()
{
	ibProtocolValue node;
	node.m_body = std::make_unique<ibProtocolBody>();
	return node;
}

ibProtocolValue ibProtocolValue::List()
{
	ibProtocolValue list;
	list.m_body = std::make_unique<ibProtocolBody>();
	list.m_body->m_list = true;
	return list;
}

ibProtocolValue::ibProtocolValue(const ibProtocolValue& other)
	: m_kind(other.m_kind), m_number(other.m_number), m_text(other.m_text),
	m_body(other.m_body != nullptr ? std::make_unique<ibProtocolBody>(*other.m_body) : nullptr)
{
}

ibProtocolValue& ibProtocolValue::operator=(const ibProtocolValue& other)
{
	if (this != &other)
		*this = ibProtocolValue(other);   // copied first: it may lie inside what this one holds
	return *this;
}

bool ibProtocolValue::Read(const wchar_t* text, size_t length, ibProtocolValue& value)
{
	ibValueReader reader(text, length);
	return reader.ReadDocument(value);
}

void ibProtocolValue::Write(ibString& text) const
{
	switch (GetKind()) {
	case ibKind::Null:   text.append(L"null", 4); break;
	case ibKind::Bool:   m_number != 0 ? text.append(L"true", 4) : text.append(L"false", 5); break;
	case ibKind::Int:    AppendInteger(text, m_number); break;
	case ibKind::Number: text += m_text; break;
	case ibKind::Text:   ibJsonText::AppendQuoted(text, m_text.wc_str(), m_text.length()); break;
	case ibKind::Node:
	case ibKind::List:   m_body->Write(text); break;
	}
}

bool ibProtocolValue::operator==(const ibProtocolValue& other) const
{
	const ibKind kind = GetKind();
	if (kind != other.GetKind())
		return false;
	switch (kind) {
	case ibKind::Null:   return true;
	case ibKind::Bool:
	case ibKind::Int:    return m_number == other.m_number;
	case ibKind::Text:
	case ibKind::Number: return m_text == other.m_text;
	case ibKind::Node:
	case ibKind::List:   return *m_body == *other.m_body;
	}
	return false;
}

ibProtocolValue*       ibProtocolBody::Find(const char* name)           { return FindIn(m_entries, name); }
const ibProtocolValue* ibProtocolBody::Find(const char* name) const     { return FindIn(m_entries, name); }
ibProtocolValue*       ibProtocolBody::Find(const ibString& name)       { return FindIn(m_entries, name); }
const ibProtocolValue* ibProtocolBody::Find(const ibString& name) const { return FindIn(m_entries, name); }

ibProtocolValue& ibProtocolBody::Set(const char* name)     { return SetIn(m_entries, name); }
ibProtocolValue& ibProtocolBody::Set(const ibString& name) { return SetIn(m_entries, name); }

void ibProtocolBody::Remove(const char* name)     { RemoveIn(m_entries, name); }
void ibProtocolBody::Remove(const ibString& name) { RemoveIn(m_entries, name); }

ibProtocolValue& ibProtocolBody::Add()
{
	return m_entries.emplace_back().m_value;
}

void ibProtocolBody::Write(ibString& text) const
{
	text.push_back(m_list ? L'[' : L'{');
	for (size_t i = 0; i < m_entries.size(); ++i) {
		if (i != 0)
			text.push_back(L',');
		if (!m_list) {
			ibJsonText::AppendQuoted(text, m_entries[i].m_name.wc_str(), m_entries[i].m_name.length());
			text.push_back(L':');
		}
		m_entries[i].m_value.Write(text);
	}
	text.push_back(m_list ? L']' : L'}');
}

bool ibProtocolBody::operator==(const ibProtocolBody& other) const
{
	if (m_list != other.m_list || m_entries.size() != other.m_entries.size())
		return false;
	for (size_t i = 0; i < m_entries.size(); ++i) {
		// A node's entry by its name — the one in the same place first: two nodes of one writer have them in its order,
		// and each looked up by name was a walk of the other's entries for every one (2026-10-22).
		const ibProtocolEntry& there = other.m_entries[i];
		const ibProtocolValue* const held = m_list || there.m_name == m_entries[i].m_name ? &there.m_value
			: other.Find(m_entries[i].m_name);
		if (held == nullptr || *held != m_entries[i].m_value)
			return false;
	}
	return true;
}

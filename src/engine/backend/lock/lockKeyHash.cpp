#include "backend/lock/lockKeyHash.h"

#include "backend/backend_exception.h"
#include "core/crypto/sha256.hpp"
#include "core/hash.h"   // FieldsHash — the mixer every value hash folds through

#include <algorithm>
#include <vector>
#include <cstdint>

namespace {

// Type tags for the canonical byte serialization. Stable across
// releases — adding a new type appends a new tag, never reorders.
// (5 was a reference's: a lock key is written by its values' hashes now
// — KeyText — and nothing else hashes a reference here.)
enum class FieldTag : std::uint8_t {
	Null    = 0,
	String  = 1,
	Number  = 2,
	Boolean = 3,
	Date    = 4,
};

// Sorted copy of the keyFields (case-insensitive on name to match the
// existing OES metadata-identifier convention). std::stable_sort so a
// caller that intentionally repeats a name keeps order — though
// duplicate names in keyFields is itself questionable, we don't
// reject it here.
std::vector<std::pair<ibString, ibValue>>
SortByName(const std::vector<std::pair<ibString, ibValue>>& keyFields)
{
	std::vector<std::pair<ibString, ibValue>> sorted = keyFields;
	std::stable_sort(sorted.begin(), sorted.end(),
		[](const auto& a, const auto& b) {
			return a.first.CmpNoCase(b.first) < 0;
		});
	return sorted;
}

// Append little-endian uint32 length prefix + raw UTF-8 bytes of the
// string. Empty strings encode as 4 zero bytes.
void AppendString(std::vector<std::uint8_t>& buf, const ibString& s)
{
	const auto utf8 = s.utf8_str();
	const std::uint32_t len = static_cast<std::uint32_t>(utf8.length());
	for (int i = 0; i < 4; ++i)
		buf.push_back(static_cast<std::uint8_t>((len >> (i * 8)) & 0xFF));
	const std::uint8_t* p = reinterpret_cast<const std::uint8_t*>(utf8.data());
	buf.insert(buf.end(), p, p + len);
}

// Encode a single key field's value into the buffer. Throws on
// unsupported types — extend as new key value types appear.
void EncodeValue(std::vector<std::uint8_t>& buf, const ibValue& v)
{
	const auto t = v.GetType();
	if (v.IsEmpty()) {
		buf.push_back(static_cast<std::uint8_t>(FieldTag::Null));
		return;
	}
	switch (t) {
	case ibValueTypes::TYPE_STRING:
		buf.push_back(static_cast<std::uint8_t>(FieldTag::String));
		AppendString(buf, v.GetString());
		break;
	case ibValueTypes::TYPE_NUMBER:
		// ibNumber::ToString() yields a canonical decimal form
		// (no trailing zero noise) — identical for equal numbers
		// regardless of how they were constructed.
		buf.push_back(static_cast<std::uint8_t>(FieldTag::Number));
		AppendString(buf, v.GetNumber().ToString());
		break;
	case ibValueTypes::TYPE_BOOLEAN:
		buf.push_back(static_cast<std::uint8_t>(FieldTag::Boolean));
		buf.push_back(v.GetBoolean() ? 1 : 0);
		break;
	case ibValueTypes::TYPE_DATE:
		buf.push_back(static_cast<std::uint8_t>(FieldTag::Date));
		// "YYYY-MM-DDTHH:MM:SS" off the date's own parts (fdatetime.h) — locale-independent,
		// sortable, exact, and the same text for the same date on every machine.
		{
			ibDateTimeParts p;
			v.GetDate().ToParts(p);
			AppendString(buf, ibString::Format(ibT("%04d-%02u-%02uT%02u:%02u:%02u"), p.m_year, p.m_month, p.m_day, p.m_hour, p.m_minute, p.m_second));
		}
		break;
	default:
		ibBackendCoreException::Error(
			_("Lock key contains an unsupported value type"));
	}
}

// Hex-encode the 32-byte SHA-256 digest into 64 lowercase chars.
ibString ToHex(const std::uint8_t digest[ibSHA256::DIGEST_SIZE])
{
	static const char kHex[17] = "0123456789abcdef";
	ibString out;
	out.reserve(ibSHA256::DIGEST_SIZE * 2);
	for (size_t i = 0; i < ibSHA256::DIGEST_SIZE; ++i) {
		out += kHex[(digest[i] >> 4) & 0x0F];
		out += kHex[ digest[i]       & 0x0F];
	}
	return out;
}

// ⭐⭐ A FIELD'S VALUE AS THE KEY WRITES IT — ITS HASH, the value's own (GetValueHash), because conflicts are read
// from this text (Overlap) and the hash is what two equal values share: a reference hashes by its guid, a number by
// its value (1 and 1.0 one key, 1.5 and 1.7 two), a text by its characters. It was GetString(), the value as a person reads it: a reference wrote
// its presentation, which two items may share, and a date the locale's format.
//
// Equal values write one hash; two values that share one meet — a lock wider than asked, never narrower, which is
// the contract GetValueHash is written to ("coarser is safe"). The low 32 bits: the hash is computed in 64 and handed
// back as size_t, so a 32-bit and a 64-bit process on one base agree on these.
ibString KeyText(const ibValue& v)
{
	return ibString::Format(ibT("%08x"), static_cast<unsigned int>(static_cast<std::uint32_t>(v.GetValueHash())));
}

// The fields of a canonical key back: (name, value hash) in the order Canonicalize wrote them.
std::vector<std::pair<ibString, ibString>> KeyFieldsOf(const ibString& keyData)
{
	std::vector<std::pair<ibString, ibString>> fields;
	std::size_t from = 0;
	while (from < keyData.length()) {
		std::size_t to = keyData.find(ibT(';'), from);
		if (to == ibString::npos)
			to = keyData.length();
		const ibString part = keyData.substr(from, to - from);
		const std::size_t eq = part.find(ibT('='));
		if (eq == ibString::npos)
			fields.emplace_back(part, ibString());
		else
			fields.emplace_back(part.substr(0, eq), part.substr(eq + 1));
		from = to + 1;
	}
	return fields;
}

} // namespace

namespace ibLockKey {

ibString Hash(const std::vector<std::pair<ibString, ibValue>>& keyFields)
{
	// Empty key → empty namespace lock — represented as a single
	// well-known sentinel hash so all "whole namespace" acquires
	// collide. SHA-256 of empty input would also work but the
	// constant makes the intent explicit in DB inspection.
	if (keyFields.empty())
		return ibString(ibT("0000000000000000000000000000000000000000000000000000000000000000"));

	const auto sorted = SortByName(keyFields);

	std::vector<std::uint8_t> buf;
	buf.reserve(64 * sorted.size());   // rough — name + value + overhead

	for (const auto& kv : sorted) {
		AppendString(buf, kv.first);
		EncodeValue(buf, kv.second);
	}

	std::uint8_t digest[ibSHA256::DIGEST_SIZE];
	ibSHA256::Hash(buf.data(), buf.size(), digest);
	return ToHex(digest);
}

ibString Canonicalize(const std::vector<std::pair<ibString, ibValue>>& keyFields)
{
	if (keyFields.empty())
		return ibString();

	const auto sorted = SortByName(keyFields);

	ibString out;
	for (const auto& kv : sorted) {
		if (!out.IsEmpty())
			out += ibT(";");
		out += kv.first;
		out += ibT("=");
		out += KeyText(kv.second);
	}
	return out;
}

bool Overlap(const ibString& keyData, const ibString& otherKeyData)
{
	const std::vector<std::pair<ibString, ibString>> mine   = KeyFieldsOf(keyData);
	const std::vector<std::pair<ibString, ibString>> theirs = KeyFieldsOf(otherKeyData);

	// Both lists are sorted by name as Canonicalize wrote them: walk them together, and compare the fields
	// both name. A field only one of them names is any value to the other.
	std::size_t i = 0, j = 0;
	while (i < mine.size() && j < theirs.size()) {
		const int byName = mine[i].first.CmpNoCase(theirs[j].first);
		if (byName < 0)
			++i;
		else if (byName > 0)
			++j;
		else {
			if (mine[i].second != theirs[j].second)
				return false;
			++i;
			++j;
		}
	}
	return true;
}

std::uint64_t FieldsHash(const ibString& keyData)
{
	std::uint64_t hash = kIbHashBasis;
	bool inName = true;   // a name runs to its '=', its value to the next ';'
	for (const ibChar c : keyData) {
		if (c == ibT(';')) {
			hash = ibHashCombine(hash, static_cast<std::uint64_t>(';'));
			inName = true;
		}
		else if (c == ibT('='))
			inName = false;
		else if (inName)
			hash = ibHashCombine(hash, static_cast<std::uint64_t>(c >= ibT('A') && c <= ibT('Z') ? c - ibT('A') + ibT('a') : c));
	}
	return hash != 0 ? hash : 1;
}

} // namespace ibLockKey

//**********************************************************************
//*           ibLockItem — hash / canonical accessors                  *
//**********************************************************************
// Delegates to the free functions above so the byte-layout discipline
// stays in one translation unit; made from keyFields as they are at the
// call, never kept (lockTypes.h says why).

// The key's hash is its TEXT's — the values' own hashes, the same ones the conflict reads — so a key of any value
// hashes, and no table is asked what a reference is. The whole table (no field) is Hash's sentinel.
ibString ibLockItem::KeyHash() const
{
	const ibString text = KeyData();
	if (text.IsEmpty())
		return ibLockKey::Hash({});
	const auto utf8 = text.utf8_str();
	std::uint8_t digest[ibSHA256::DIGEST_SIZE];
	ibSHA256::Hash(reinterpret_cast<const std::uint8_t*>(utf8.data()), utf8.length(), digest);
	return ToHex(digest);
}

ibString ibLockItem::KeyData() const
{
	return ibLockKey::Canonicalize(keyFields);
}

#include "backend/lock/lockKeyHash.h"

#include "backend/backend_exception.h"
#include "core/crypto/sha256.hpp"
#include "backend/metaCollection/partial/reference/reference.h"
#include "backend/metaCollection/partial/commonObject.h"   // ibValueMetaObjectRecordDataRef::GetDocPath()

#include <algorithm>
#include <vector>
#include <cstdint>

namespace {

// Type tags for the canonical byte serialization. Stable across
// releases — adding a new type appends a new tag, never reorders.
enum class FieldTag : std::uint8_t {
	Null    = 0,
	String  = 1,
	Number  = 2,
	Boolean = 3,
	Date    = 4,
	Ref     = 5,   // ibValueReferenceDataObject — guid + meta-id
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
	case ibValueTypes::TYPE_REFFER: {
		// The dominant case for record-locking — a ref to a Catalog
		// item / Document. Two-part identity (metaobject + guid).
		auto* refData = dynamic_cast<ibValueReferenceDataObject*>(v.GetRef());
		if (refData == nullptr) {
			ibBackendCoreException::Error(
				_("Lock key contains an unsupported reference value"));
		}
		buf.push_back(static_cast<std::uint8_t>(FieldTag::Ref));
		const auto* meta = refData->GetMetaObjectRef();
		AppendString(buf, meta != nullptr ? meta->GetDocPath() : ibString());
		AppendString(buf, static_cast<ibString>(refData->GetGuid()));
		break;
	}
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
		// Re-use GetString() for the human-readable side — slightly
		// different from EncodeValue's strict bytes (e.g. Date emits
		// platform-default format) but acceptable for a diagnostic /
		// conflict-message payload.
		out += kv.second.GetString();
	}
	return out;
}

} // namespace ibLockKey

//**********************************************************************
//*           ibLockItem — lazy hash / canonical accessors             *
//**********************************************************************
// Delegates to the free functions above so the byte-layout discipline
// stays in one translation unit; ibLockItem just owns the per-instance
// cache so call sites and lockManager don't recompute on every Acquire.

const ibString& ibLockItem::KeyHash() const
{
	if (m_keyHashCached.IsEmpty())
		m_keyHashCached = ibLockKey::Hash(keyFields);
	return m_keyHashCached;
}

const ibString& ibLockItem::KeyData() const
{
	if (!m_keyDataValid) {
		m_keyDataCached = ibLockKey::Canonicalize(keyFields);
		m_keyDataValid = true;
	}
	return m_keyDataCached;
}

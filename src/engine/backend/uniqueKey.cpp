#include "uniqueKey.h"

ibUniqueKey::ibUniqueKey() = default;

ibUniqueKey::ibUniqueKey(const ibGuid& guid) : m_objGuid(guid) {}

ibUniqueKey::~ibUniqueKey() = default;

bool ibUniqueKey::isValid() const
{
	return m_objGuid.isValid();
}

void ibUniqueKey::reset()
{
	m_objGuid.reset();
}

bool ibUniqueKey::IsOk() const
{
	return m_objGuid.isValid() || !m_keyValues.empty();
}

bool ibUniqueKey::FindKey(const ibMetaID& id) const
{
	return m_keyValues.find(id) != m_keyValues.end();
}

ibValue ibUniqueKey::GetKey(const ibMetaID& id) const
{
	const auto it = m_keyValues.find(id);
	return (it != m_keyValues.end()) ? it->second : ibValue();
}

bool ibUniqueKey::operator<(const ibUniqueKey& other) const
{
	return m_objGuid < other.m_objGuid;
}

bool ibUniqueKey::operator>(const ibUniqueKey& other) const
{
	return m_objGuid > other.m_objGuid;
}

bool ibUniqueKey::operator<=(const ibUniqueKey& other) const
{
	return m_objGuid <= other.m_objGuid;
}

bool ibUniqueKey::operator>=(const ibUniqueKey& other) const
{
	return m_objGuid >= other.m_objGuid;
}

bool ibUniqueKey::operator==(const ibUniqueKey& other) const
{
	return EqualsImpl(other);
}

bool ibUniqueKey::operator!=(const ibUniqueKey& other) const
{
	return !EqualsImpl(other);
}

bool ibUniqueKey::operator==(const ibGuid& other) const
{
	return m_objGuid == other;
}

bool ibUniqueKey::operator!=(const ibGuid& other) const
{
	return m_objGuid != other;
}

bool ibUniqueKey::EqualsImpl(const ibUniqueKey& other) const
{
	// Composite when either side carries one (a reference key's composite is empty → won't match a real one),
	// else the guid. Subsumes the old Pair-only compare — the composite now lives in the base.
	if (!m_keyValues.empty() || !other.m_keyValues.empty())
		return m_keyValues == other.m_keyValues;
	return m_objGuid == other.m_objGuid;
}

//////////////////////////////////////////////////////////////////////////////

// A register key: a fresh per-instance guid (the stable form-instance identity) + the composite dimensions.
// The data + accessors are the base's; the ctors only seed them.
ibUniqueKeyPair::ibUniqueKeyPair() : ibUniqueKey(wxNewUniqueGuid)
{
}

ibUniqueKeyPair::ibUniqueKeyPair(const ibRowMetaValues& keyValues)
	: ibUniqueKey(wxNewUniqueGuid)
{
	SetKeyValues(keyValues);
}

ibUniqueKeyPair::~ibUniqueKeyPair() = default;

bool ibUniqueKeyPair::IsOk() const
{
	return !GetKeyValues().empty();
}

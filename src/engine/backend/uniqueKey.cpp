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
	return m_objGuid.isValid();
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
	return m_objGuid == other.m_objGuid;
}

//////////////////////////////////////////////////////////////////////////////

ibUniqueKeyPair::ibUniqueKeyPair() : ibUniqueKey(wxNewUniqueGuid)
{
}

ibUniqueKeyPair::ibUniqueKeyPair(const ibRowMetaValues& keyValues)
	: ibUniqueKey(wxNewUniqueGuid)
	, m_keyValues(keyValues)
{
}

ibUniqueKeyPair::~ibUniqueKeyPair() = default;

bool ibUniqueKeyPair::IsOk() const
{
	return !m_keyValues.empty();
}

bool ibUniqueKeyPair::FindKey(const ibMetaID& id) const
{
	return m_keyValues.find(id) != m_keyValues.end();
}

ibValue ibUniqueKeyPair::GetKey(const ibMetaID& id) const
{
	const auto it = m_keyValues.find(id);
	return (it != m_keyValues.end()) ? it->second : ibValue();
}

bool ibUniqueKeyPair::EqualsImpl(const ibUniqueKey& other) const
{
	if (const auto* o = dynamic_cast<const ibUniqueKeyPair*>(&other))
		return m_keyValues == o->m_keyValues;
	return ibUniqueKey::EqualsImpl(other);
}

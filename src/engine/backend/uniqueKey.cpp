#include "uniqueKey.h"
#include "metaCollection/partial/object.h"

bool CUniqueKey::isValid() const
{
	return m_objGuid.isValid();
}

void CUniqueKey::reset()
{
	m_objGuid.reset();
}

bool CUniqueKey::operator>(const CUniqueKey& other) const
{
	return m_objGuid > other.m_objGuid;
}

bool CUniqueKey::operator>=(const CUniqueKey& other) const
{
	return m_objGuid >= other.m_objGuid;
}

bool CUniqueKey::operator<(const CUniqueKey& other) const
{
	return m_objGuid < other.m_objGuid;
}

bool CUniqueKey::operator<=(const CUniqueKey& other) const
{
	return m_objGuid <= other.m_objGuid;
}

bool CUniqueKey::operator==(const CUniqueKey& other) const
{
	if (m_uniqueData == enUniqueData::enUniqueGuid) {
		return m_objGuid == other.m_objGuid;
	}
	else if (m_uniqueData == enUniqueData::enUniqueKey) {
		if (other.m_uniqueData == enUniqueData::enUniqueKey) {
			return m_metaObject == other.m_metaObject
				&& m_keyValues == other.m_keyValues;
		}
		else if (m_uniqueData == enUniqueData::enUniqueGuid) {
			return m_objGuid == other.m_objGuid;
		}
	}
	return false;
}

bool CUniqueKey::operator!=(const CUniqueKey& other) const
{
	if (m_uniqueData == enUniqueData::enUniqueGuid) {
		return m_objGuid != other.m_objGuid;
	}
	else if (m_uniqueData == enUniqueData::enUniqueKey) {
		if (other.m_uniqueData == enUniqueData::enUniqueKey) {
			return m_metaObject != other.m_metaObject
				|| m_keyValues != other.m_keyValues;
		}
		else if (m_uniqueData == enUniqueData::enUniqueGuid) {
			return m_objGuid != other.m_objGuid;
		}
	}
	return false;
}

bool CUniqueKey::operator==(const Guid& other) const
{
	return m_objGuid == other;
}

bool CUniqueKey::operator!=(const Guid& other) const
{
	return m_objGuid != other;
}

CUniqueKey::CUniqueKey() : CUniqueKey(enUniqueData::enUniqueGuid)
{
	m_objGuid = wxNullGuid;
	m_metaObject = nullptr;
	m_keyValues = {};
}

CUniqueKey::CUniqueKey(const Guid& guid) : CUniqueKey(enUniqueData::enUniqueGuid)
{
	m_objGuid = guid;
	m_metaObject = nullptr;
	m_keyValues = {};
}

CUniquePairKey::CUniquePairKey(IMetaObjectRegisterData* metaObject) : CUniqueKey(enUniqueData::enUniqueKey)
{
	m_objGuid = wxNewUniqueGuid;
	m_metaObject = metaObject;
	m_keyValues = {};

	if (metaObject != nullptr) {
		for (auto& obj : metaObject->GetGenericDimensions()) {
			m_keyValues.insert_or_assign(
				obj->GetMetaID(),
				obj->CreateValue()
			);
		}
	}
}

CUniquePairKey::CUniquePairKey(IMetaObjectRegisterData* metaObject, const valueArray_t& keyValues) : CUniqueKey(enUniqueData::enUniqueKey)
{
	m_objGuid = wxNewUniqueGuid;
	m_metaObject = metaObject;
	m_keyValues = {};

	if (metaObject != nullptr) {
		for (auto& obj : metaObject->GetGenericDimensions()) {
			auto& it = keyValues.find(obj->GetMetaID());
			if (it != keyValues.end()) {
				m_keyValues.insert_or_assign(
					obj->GetMetaID(),
					keyValues.at(obj->GetMetaID())
				);
			}
		}
	}
}
#ifndef __UNIQUE_KEY_H__
#define __UNIQUE_KEY_H__

#include "guid/guid.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

class IMetaObjectRegisterData;

////////////////////////////////////////////////////////////////////////////////////////////////////

//class keys 
class CUniqueKey {
protected:
	enum class enUniqueData {
		enUniqueKey = 10,
		enUniqueGuid
	} m_uniqueData;

public:

	bool isValid() const;
	void reset();

	Guid GetGuid() const {
		return m_objGuid;
	}

	CUniqueKey();
	CUniqueKey(const Guid& guid);

	virtual bool IsOk() const {
		return m_metaObject != NULL;
	}

	bool operator > (const CUniqueKey& other) const;
	bool operator >= (const CUniqueKey& other) const;
	bool operator < (const CUniqueKey& other) const;
	bool operator <= (const CUniqueKey& other) const;

	// overload equality and inequality operator
	virtual bool operator==(const CUniqueKey& other) const;
	virtual bool operator!=(const CUniqueKey& other) const;

	virtual bool operator==(const Guid& other) const;
	virtual bool operator!=(const Guid& other) const;

	operator wxString() const {
		return GetGuid().str();
	}

	operator Guid() const {
		return GetGuid();
	}

	operator guid_t() const {
		return GetGuid();
	}

protected:

	CUniqueKey(enUniqueData uniqueData) : m_uniqueData(uniqueData) {}

protected:

	Guid m_objGuid;
	IMetaObjectRegisterData* m_metaObject;
	std::map<meta_identifier_t, CValue> m_keyValues;
};

class CUniquePairKey : public CUniqueKey {
public:

	CUniquePairKey(IMetaObjectRegisterData* metaObject = NULL);
	CUniquePairKey(IMetaObjectRegisterData* metaObject, const std::map<meta_identifier_t, CValue>& keyValues);

	void SetKeyPair(IMetaObjectRegisterData* metaObject,
		std::map<meta_identifier_t, CValue>& keys) {
		m_metaObject = metaObject; m_keyValues = keys;
	}

	operator std::map<meta_identifier_t, CValue>() const {
		return m_keyValues;
	}
};

#define wxNullUniqueKey CUniqueKey()
#define wxNullUniquePairKey CUniquePairKey()

#endif // !_UNIQUE_KEY_H__
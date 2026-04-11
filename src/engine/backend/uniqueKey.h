#ifndef __UNIQUE_KEY_H__
#define __UNIQUE_KEY_H__

#include "backend/compiler/value.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

class BACKEND_API ibValueMetaObjectRegisterData;

////////////////////////////////////////////////////////////////////////////////////////////////////

//class keys 
class BACKEND_API ibUniqueKey {
protected:

	enum class ibUniqueData {
		enUniqueKey = 10,
		enUniqueGuid
	} m_uniqueData;

public:

	bool isValid() const;
	void reset();

	ibGuid GetGuid() const { return m_objGuid; }

	ibUniqueKey();
	ibUniqueKey(const ibGuid& guid);

	virtual bool IsOk() const {
		return m_metaObject != nullptr;
	}

	bool operator > (const ibUniqueKey& other) const;
	bool operator >= (const ibUniqueKey& other) const;
	bool operator < (const ibUniqueKey& other) const;
	bool operator <= (const ibUniqueKey& other) const;

	// overload equality and inequality operator
	virtual bool operator==(const ibUniqueKey& other) const;
	virtual bool operator!=(const ibUniqueKey& other) const;

	virtual bool operator==(const ibGuid& other) const;
	virtual bool operator!=(const ibGuid& other) const;

	operator wxString() const { return GetGuid().str(); }
	operator ibGuid() const { return GetGuid(); }
	operator ibGuidImpl() const { return GetGuid(); }

protected:

	ibUniqueKey(ibUniqueData uniqueData) : m_uniqueData(uniqueData) {}

protected:

	ibGuid m_objGuid;
	const ibValueMetaObjectRegisterData* m_metaObject;
	ibMetaValueArray m_keyValues;
};

class BACKEND_API ibUniqueKeyPair : public ibUniqueKey {
public:

	ibUniqueKeyPair(const ibValueMetaObjectRegisterData* metaObject = nullptr);
	ibUniqueKeyPair(const ibValueMetaObjectRegisterData* metaObject, const ibMetaValueArray& keyValues);

	bool IsOk() const {
		return m_metaObject != nullptr && m_keyValues.size() > 0;
	}

	void SetKeyPair(const ibValueMetaObjectRegisterData* metaObject,
		ibMetaValueArray& keys) {
		m_metaObject = metaObject; m_keyValues = keys;
	}

	bool FindKey(const ibMetaID& id) const {
		const auto it = m_keyValues.find(id);
		return it != m_keyValues.end();
	}

	ibValue GetKey(const ibMetaID& id) const {
		const auto it = m_keyValues.find(id);
		if (it != m_keyValues.end())
			return it->second;
		return ibValue();
	}

	operator ibMetaValueArray() const { return m_keyValues; }
};

#define wxNullUniqueKey ibUniqueKey()
#define wxNullUniquePairKey ibUniqueKeyPair()

#endif // !_UNIQUE_KEY_H__
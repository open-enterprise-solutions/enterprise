#ifndef __UNIQUE_KEY_H__
#define __UNIQUE_KEY_H__

#include "backend/compiler/value.h"

////////////////////////////////////////////////////////////////////////////////////////////////
//
// ibUniqueKey      — the ONE key object.  It holds EITHER a reference (a
//                    single m_objGuid — Catalogs, Documents, Charts, Enums,
//                    and the FORM-INSTANCE identity of any open form) OR a
//                    composite (m_keyValues, a metaID → ibValue map — a
//                    register's recorder+line / period+dimensions).  A
//                    model's GetItemKey(item) fills whichever fits its kind;
//                    the caller reads GetGuid() or GetKeyValues() as needed.
//                    Because the composite lives in the base, a GetItemKey
//                    that returns ibUniqueKey BY VALUE carries it intact.
//
// ibUniqueKeyPair  — a thin convenience subclass: a ibUniqueKey seeded with
//                    a fresh per-instance m_objGuid (the STABLE form-instance
//                    identity used by FindFormBySourceUniqueKey /
//                    UpdateFormUniqueKey after a save mutates m_keyValues)
//                    plus the composite.  All state + accessors are the
//                    base's; the subclass adds only the register-key ctors
//                    and the stricter composite IsOk.  Seeded by helpers like
//                    ibValueMetaObjectRegisterData::CreateUniqueKeyPair.
//
// Equality is virtual via the EqualsImpl hook — the base compares m_keyValues
// when either side carries a composite, else m_objGuid.  Ordering operators
// stay GUID-based.
//
////////////////////////////////////////////////////////////////////////////////////////////////

class BACKEND_API ibUniqueKey {
public:

	ibUniqueKey();
	ibUniqueKey(const ibGuid& guid);
	virtual ~ibUniqueKey();

	bool isValid() const;
	void reset();

	ibGuid GetGuid() const { return m_objGuid; }

	// Composite key-values (register recorder+line / period+dimensions). Empty for a plain reference (guid) key.
	// A ibUniqueKey holds EITHER a reference (guid) OR dimensions (composite) — GetItemKey fills whichever fits
	// the model's kind. Accessors live in the base so both key shapes share them.
	const ibRowMetaValues& GetKeyValues() const { return m_keyValues; }
	void SetKeyValues(const ibRowMetaValues& keys) { m_keyValues = keys; }
	bool FindKey(const ibMetaID& id) const;
	ibValue GetKey(const ibMetaID& id) const;

	// Valid when the guid is non-zero OR the composite is populated. Pair overrides to require the composite.
	virtual bool IsOk() const;

	bool operator < (const ibUniqueKey& other) const;
	bool operator > (const ibUniqueKey& other) const;
	bool operator <=(const ibUniqueKey& other) const;
	bool operator >=(const ibUniqueKey& other) const;

	bool operator==(const ibUniqueKey& other) const;
	bool operator!=(const ibUniqueKey& other) const;
	bool operator==(const ibGuid& other) const;
	bool operator!=(const ibGuid& other) const;

	operator wxString()      const { return m_objGuid.str(); }
	operator ibGuid()        const { return m_objGuid; }
	operator ibGuidImpl()    const { return m_objGuid; }
	operator ibRowMetaValues() const { return m_keyValues; }

protected:

	// Equality hook.  The public operator== / != funnel through this single virtual.  The base compares the
	// composite when either side carries one, else the guid — subsuming the old Pair-only composite compare.
	virtual bool EqualsImpl(const ibUniqueKey& other) const;

	ibGuid          m_objGuid;
	ibRowMetaValues m_keyValues;   // register dimensions; empty for a reference key. In the BASE so a by-value key carries it.
};

class BACKEND_API ibUniqueKeyPair : public ibUniqueKey {
public:

	ibUniqueKeyPair();
	explicit ibUniqueKeyPair(const ibRowMetaValues& keyValues);
	virtual ~ibUniqueKeyPair();

	// A register key is valid only with populated dimensions (the base also accepts a bare guid).
	bool IsOk() const override;
};

#define wxNullUniqueKey     ibUniqueKey()
#define wxNullUniquePairKey ibUniqueKeyPair()

#endif // !_UNIQUE_KEY_H__

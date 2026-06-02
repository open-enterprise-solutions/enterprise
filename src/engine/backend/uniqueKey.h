#ifndef __UNIQUE_KEY_H__
#define __UNIQUE_KEY_H__

#include "backend/compiler/value.h"

////////////////////////////////////////////////////////////////////////////////////////////////
//
// ibUniqueKey      — GUID-based identity.  One field (m_objGuid) covers
//                    Catalogs, Documents, Charts, Enums and any other
//                    object whose row is keyed by a single GUID.
//                    Also serves as the FORM-INSTANCE identity for any
//                    open form (each form holds exactly one).
//
// ibUniqueKeyPair  — composite-key identity.  State is `m_keyValues`
//                    (a metaID → ibValue map); the schema is implicit
//                    in the metaIDs used as map keys.  This class is
//                    metadata-agnostic by design — the keys can be
//                    constructed by any source.  Metadata-side helpers
//                    (e.g. ibValueMetaObjectRegisterData::CreateUniqueKeyPair)
//                    seed the map from a register's dimension list and
//                    hand the resulting Pair to the caller.
//
//                    Inherited m_objGuid is set to wxNewUniqueGuid per
//                    Pair instance and is used by FindFormBySourceUniqueKey
//                    / UpdateFormUniqueKey as the STABLE form-instance
//                    identity (so the form can be located after a save
//                    mutates m_keyValues).
//
// Equality is virtual via the EqualsImpl hook — base compares m_objGuid;
// Pair extends to compare m_keyValues when both sides are Pair, and falls
// back to the GUID compare otherwise.  Ordering operators stay GUID-based.
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

	// Default validity check is "GUID is non-zero".  Pair overrides to
	// require a populated composite key instead.
	virtual bool IsOk() const;

	bool operator < (const ibUniqueKey& other) const;
	bool operator > (const ibUniqueKey& other) const;
	bool operator <=(const ibUniqueKey& other) const;
	bool operator >=(const ibUniqueKey& other) const;

	bool operator==(const ibUniqueKey& other) const;
	bool operator!=(const ibUniqueKey& other) const;
	bool operator==(const ibGuid& other) const;
	bool operator!=(const ibGuid& other) const;

	operator wxString()   const { return m_objGuid.str(); }
	operator ibGuid()     const { return m_objGuid; }
	operator ibGuidImpl() const { return m_objGuid; }

protected:

	// Equality hook.  Override in subclasses to extend semantics; the
	// public operator== / != funnel through this single virtual.
	virtual bool EqualsImpl(const ibUniqueKey& other) const;

	ibGuid m_objGuid;
};

class BACKEND_API ibUniqueKeyPair : public ibUniqueKey {
public:

	ibUniqueKeyPair();
	explicit ibUniqueKeyPair(const ibRowMetaValues& keyValues);
	virtual ~ibUniqueKeyPair();

	bool IsOk() const override;

	const ibRowMetaValues& GetKeyValues() const { return m_keyValues; }
	void SetKeyValues(const ibRowMetaValues& keys) { m_keyValues = keys; }

	bool FindKey(const ibMetaID& id) const;
	ibValue GetKey(const ibMetaID& id) const;

	operator ibRowMetaValues() const { return m_keyValues; }

protected:

	// Composite-key compare when both sides are Pair; fall through to
	// base GUID compare otherwise.  Note: comparing a Pair against a
	// plain GUID-key almost never matches because Pair's m_objGuid is a
	// fresh wxNewUniqueGuid per instance — that's intentional, mixing
	// the two is a programming error.
	bool EqualsImpl(const ibUniqueKey& other) const override;

private:

	ibRowMetaValues m_keyValues;
};

#define wxNullUniqueKey     ibUniqueKey()
#define wxNullUniquePairKey ibUniqueKeyPair()

#endif // !_UNIQUE_KEY_H__

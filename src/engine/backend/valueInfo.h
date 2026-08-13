#ifndef __VALUE_INFO_H__
#define __VALUE_INFO_H__

#include "backend/uniqueKey.h"

//reference data — the _RRRef storage key is a PURE 16-byte object guid: identity only, NO type. The type
//lives in the SEPARATE _RTRef (clsid) column beside it (query/columnLayout.cpp) and, at runtime, in the
//reference's metaObject. So an empty reference is simply an all-zero guid — no special-casing, full 128-bit
//uniqueness, and a retype touches only _RTRef, never this blob. (Earlier this baked the metaID into Data1,
//which duplicated _RTRef and made an unset key look non-empty; that stamping is gone.) sizeof == 16.
struct ibReference {

	ibReference(const ibGuidImpl& guid) : m_guid(guid) {}

	ibGuidImpl m_guid;   // pure object identity; the type is read from the _RTRef column, not from here
};

///////////////////

class ibValueDataObject {
public:

	ibValueDataObject(const ibGuid& objGuid = wxNullGuid, bool newObject = true) : m_newObject(newObject), m_objGuid(objGuid) {}

	// The class is polymorphic (GetMetaObject below is pure virtual) and three classes derive
	// it, so the destructor has to be virtual: deleting a derived object through this type
	// would otherwise be undefined behaviour. Nothing does that today — every delete goes
	// through the exact type — but the class shape invites it, and GCC flags the risk. The
	// vtable already exists, so this costs neither a word of storage nor a layout change.
	virtual ~ibValueDataObject() = default;

	//support source set/get data
	virtual bool SetValueByMetaID(const ibMetaID& id, const ibValue& varMetaVal) { return false; }
	virtual bool GetValueByMetaID(const ibMetaID& id, ibValue& pvarMetaVal) const { return false; }


	//get unique identifier 
	virtual ibUniqueKey GetGuid() const { return m_objGuid; }

	//is new object?
	virtual bool IsNewObject() const { return m_newObject; }

	//get metaData from object 
	virtual const class ibValueMetaObjectRecordData* GetMetaObject() const = 0;

	//set modify 
	virtual void Modify(bool mod) {}

protected:
	bool m_newObject;
	ibRowMetaValues m_listObjectValue;
	ibGuid m_objGuid;
};

#define reference_size_t int(sizeof(ibReference))

#endif
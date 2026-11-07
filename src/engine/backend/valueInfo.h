#ifndef __VALUE_INFO_H__
#define __VALUE_INFO_H__

#include "backend/uniqueKey.h"
#include "backend/valuePart.h"

#include <map>

class BACKEND_API ibValueTabularSectionDataObjectBase;

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

class BACKEND_API ibValueDataObject {
public:

	ibValueDataObject(const ibGuid& objGuid = wxNullGuid, bool newObject = true) : m_newObject(newObject), m_objGuid(objGuid) {}

	// The class is polymorphic (GetMetaObject below is pure virtual) and three classes derive
	// it, so the destructor has to be virtual: deleting a derived object through this type
	// would otherwise be undefined behaviour. Nothing does that today — every delete goes
	// through the exact type — but the class shape invites it, and GCC flags the risk. The
	// vtable already exists, so this costs neither a word of storage nor a layout change.
	//
	// Out of line (tabularSection.cpp): it lets go of the sections, a type this header only names.
	virtual ~ibValueDataObject();

	// THE VALUE THIS OBJECT IS. The class is a mixin under three values (a record object, a reference, a
	// selector's row), not a value itself; a tabular section holds its object through this (#645).
	// Null for an object that is no value (the quick-choice comparator) — it makes no section.
	virtual ibValue* GetDataValue() = 0;

	//support source set/get data
	virtual bool SetValueByMetaID(const ibMetaID& id, const ibValue& varMetaVal) { return false; }
	virtual bool GetValueByMetaID(const ibMetaID& id, ibValue& pvarMetaVal) const { return false; }


	//get unique identifier — the key as it lies; its raw guid is GetGuid().GetGuid()
	virtual const ibUniqueKey& GetGuid() const { return m_objGuid; }

	//is new object?
	virtual bool IsNewObject() const { return m_newObject; }

	//get metaData from object 
	virtual const class ibValueMetaObjectRecordData* GetMetaObject() const = 0;

	//set modify 
	virtual void Modify(bool mod) {}

protected:

	// A tabular section of this object, by its id: true when the id is a section of this object — `out` is
	// then the section (holding it holds this object) or NULL for a section this kind does not use (a
	// catalog folder's item-only section); false when the id is not a section here.
	bool GetTableValue(const ibMetaID& id, ibValue& out) const;
	// …the section itself, uncounted: alive while this object is. Null for an unused or unknown section.
	ibValueTabularSectionDataObjectBase* FindTable(const ibMetaID& id) const;
	// …and put in place, letting go of the one that stood there (whoever else holds it keeps it, and this
	// object with it). Null puts "not used by this kind" in place.
	void SetTable(const ibMetaID& id, ibValueTabularSectionDataObjectBase* table);

	bool m_newObject;
	// The fields of the object. Never a tabular section: an ibValue counts what it holds, and a section's
	// count is its object's too, so the object would hold itself and never end.
	ibRowMetaValues m_listObjectValue;
	ibUniqueKey m_objGuid;
	// The tabular sections, each held as a PART of this object (valuePart.h): the hold counts the section
	// alone. An empty hold is a section this kind does not use.
	std::map<ibMetaID, ibPartHold<ibValueTabularSectionDataObjectBase>> m_listTableValue;
};

#define reference_size_t int(sizeof(ibReference))

#endif
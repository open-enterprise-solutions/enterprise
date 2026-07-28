#ifndef __VALUE_INFO_H__
#define __VALUE_INFO_H__

#include "backend/uniqueKey.h"

//reference data — a self-describing 16-byte key: the metaID rides in the guid's Data1 (see the reference
//key encoder, ibValueReferenceDataObject::MakeNewGuid). No separate metaID field — the ctor stamps Data1
//from `id`, so a real key (already branded → no-op) and a metaID-only sentinel (empty guid + id) both land
//right; GetMetaID() reads it back. sizeof == 16 (was 20).
struct ibReference {

	ibReference(const ibMetaID& id, const ibGuidImpl& guid) : m_guid(guid) { m_guid.m_data1 = (uint32_t)id; }

	ibMetaID GetMetaID() const { return (ibMetaID)m_guid.m_data1; }   // Data1 carries the metaID

	ibGuidImpl m_guid;
};

///////////////////

class ibValueDataObject {
public:

	ibValueDataObject(const ibGuid& objGuid = wxNullGuid, bool newObject = true) : m_newObject(newObject), m_objGuid(objGuid) {}

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
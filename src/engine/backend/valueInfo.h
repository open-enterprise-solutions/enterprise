#ifndef __VALUE_INFO_H__
#define __VALUE_INFO_H__

#include "backend/uniqueKey.h"

//reference data
struct ibReference {

	ibReference(const ibMetaID& id, const ibGuidImpl& guid) : m_guid(guid), m_id(id) {}

	ibGuidImpl m_guid;
	ibMetaID m_id; // id of metadata 
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
	virtual class ibValueMetaObjectRecordData* GetMetaObject() const = 0;

	//set modify 
	virtual void Modify(bool mod) {}

protected:
	bool m_newObject;
	ibMetaValueArray m_listObjectValue;
	ibGuid m_objGuid;
};

#define reference_size_t int(sizeof(ibReference))

#endif
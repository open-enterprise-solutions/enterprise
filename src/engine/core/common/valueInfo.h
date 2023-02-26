#ifndef _VALUE_INFO_H__
#define _VALUE_INFO_H__

#include "uniqueKey.h"

class IObjectValueInfo {
public:

	IObjectValueInfo(const Guid& objGuid = wxNullGuid,
		bool newObject = true) : m_objGuid(objGuid), m_newObject(newObject) {
	}

	//support source set/get data 
	virtual bool SetValueByMetaID(const meta_identifier_t& id, const CValue& varMetaVal) { return false; }
	virtual bool GetValueByMetaID(const meta_identifier_t& id, CValue& pvarMetaVal) const {
		return false;
	}

	//get unique identifier 
	virtual CUniqueKey GetGuid() const {
		return m_objGuid;
	}

	//is new object?
	virtual bool IsNewObject() const {
		return m_newObject;
	}

	//get metadata from object 
	virtual IMetaObjectRecordData* GetMetaObject() const = 0;

	//set modify 
	virtual void Modify(bool mod) {}

protected:
	bool m_newObject;
	valueArray_t m_objectValues;
	Guid m_objGuid;
};

#endif
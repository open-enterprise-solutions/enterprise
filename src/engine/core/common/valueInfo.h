#ifndef _VALUE_INFO_H__
#define _VALUE_INFO_H__

class ITabularSectionDataObject;

#include "uniqueKey.h"

class IObjectValueInfo {
public:

	IObjectValueInfo(const Guid& objGuid = wxNullGuid,
		bool newObject = true) : m_objGuid(objGuid), m_newObject(newObject) {
	}

	//support source set/get data 
	virtual void SetValueByMetaID(const meta_identifier_t& id, const CValue& cVal);
	virtual CValue GetValueByMetaID(const meta_identifier_t& id) const;

	//support tabular section 
	virtual ITabularSectionDataObject* GetTableByMetaID(const meta_identifier_t& id) const;

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

	void PrepareValues();

protected:
	
	Guid m_objGuid;
	
	std::map<meta_identifier_t, CValue> m_objectValues;
	std::vector<ITabularSectionDataObject*> m_aObjectTables;
	
	bool m_newObject;
};

#endif
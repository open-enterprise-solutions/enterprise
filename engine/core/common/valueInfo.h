#ifndef _VALUE_INFO_H__
#define _VALUE_INFO_H__

class IValueTabularSection;

#include "guid/guid.h"

class IObjectValueInfo {
public:
	IObjectValueInfo(const Guid &guid = Guid(),
		bool newObject = true) : m_objGuid(guid), m_bNewObject(newObject)
	{
	}
	//support source set/get data 
	virtual void SetValueByMetaID(meta_identifier_t id, const CValue &cVal);
	virtual CValue GetValueByMetaID(meta_identifier_t id) const;
	
	//support tabular section 
	virtual IValueTabularSection *GetTableByMetaID(meta_identifier_t id) const;
	
	//get unique identifier 
	virtual Guid GetGuid() const { return m_objGuid; };
	
	//is new object?
	virtual bool IsNewObject() const { return m_bNewObject; }
	
	//get metadata from object 
	virtual IMetaObjectValue *GetMetaObject() const = 0;
	
	//set modify 
	virtual void Modify(bool mod) {}

protected:

	void PrepareValues();

protected:
	Guid m_objGuid;
	std::map<meta_identifier_t, CValue> m_aObjectValues;
	std::vector<IValueTabularSection *> m_aObjectTables;
	bool m_bNewObject;
};

#endif
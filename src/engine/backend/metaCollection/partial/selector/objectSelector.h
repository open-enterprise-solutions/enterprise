#ifndef _SELECTOR_H__
#define _SELECTOR_H__

#include "backend/metaCollection/partial/commonObject.h"

class BACKEND_API ibValueSelectorDataObject : public ibValueDynamicMembers {
	public:

	ibValueSelectorDataObject();
	virtual ~ibValueSelectorDataObject();

	virtual bool Next() = 0;

	//is empty
	virtual bool IsEmpty() const {
		return false;
	}

	//get metaData from object 
	virtual const ibValueMetaObjectGenericData* GetMetaObject() const = 0;

	//Get ref class 
	virtual ibClassID GetClassType() const;

	//types 
	virtual wxString GetClassName() const;
	virtual wxString GetString() const;

protected:

	virtual void Reset() = 0;
	virtual bool Read() = 0;
};

class BACKEND_API ibValueSelectorRecordDataObject : public ibValueSelectorDataObject,
	public ibValueDataObject {
	public:

	ibValueSelectorRecordDataObject(const ibValueMetaObjectRecordDataMutableRef* metaObject);

	virtual bool Next();
	virtual ibValueRecordDataObjectRef* GetObject(const ibGuid& guid) const;

	//get metaData from object 
	virtual const ibValueMetaObjectRecordData* GetMetaObject() const {
		return m_metaObject;
	}

	void FillMembers(ibMemberTable& helper) const;   // bound in ctor (was PrepareNames)
	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray);//method call

	//attribute
	virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal);        //setting attribute
	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal);                   //attribute value

protected:

	virtual void Reset();
	virtual bool Read();

protected:

	const ibValueMetaObjectRecordDataMutableRef* m_metaObject;
	std::vector<ibGuid> m_currentValues;
};

/////////////////////////////////////////////////////////////////////////////

class BACKEND_API ibValueSelectorRegisterDataObject :
	public ibValueSelectorDataObject {
	public:
	ibValueSelectorRegisterDataObject(const ibValueMetaObjectRegisterData* metaObject);

	virtual bool Next();
	virtual ibValueRecordManagerObject* GetRecordManager(const ibRowMetaValues& keyValues) const;

	//get metaData from object 
	virtual const ibValueMetaObjectRegisterData* GetMetaObject() const {
		return m_metaObject;
	}

	void FillMembers(ibMemberTable& helper) const;   // bound in ctor (was PrepareNames)
	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray);//method call

	//attribute
	virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal);        //setting attribute
	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal);                   //attribute value

protected:

	virtual void Reset();
	virtual bool Read();

protected:

	const ibValueMetaObjectRegisterData* m_metaObject;

	ibRowMetaValues m_keyValues;

	std::vector <ibRowMetaValues> m_currentValues;
	std::map<
		ibRowMetaValues,
		ibRowMetaValues
	> m_listObjectValue;
};

#endif
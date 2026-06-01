#ifndef _REFERENCE_H__
#define _REFERENCE_H__

#include "backend/compiler/value.h"
#include "backend/valueInfo.h"

//********************************************************************************************
//*                                     Defines                                              *
//********************************************************************************************

class BACKEND_API ibMetaData;

//********************************************************************************************

class BACKEND_API ibValueMetaObjectRecordDataRef;
class BACKEND_API ibValueRecordDataObjectRef;

//********************************************************************************************

class BACKEND_API ibValueReferenceDataObject : public ibValue,
	public ibValueDataObject {
	public:
private:
	enum helperAlias {
		eProperty,
		eTable
	};
private:
	ibValueReferenceDataObject() : ibValue(ibValueTypes::TYPE_VALUE, true), m_initializedRef(false) {}
	ibValueReferenceDataObject(const ibValueMetaObjectRecordDataRef* metaObject, const ibGuid& objGuid = wxNullGuid);
public:

	ibReference* GetReferenceData() const {
		return m_reference_impl;
	}

	void PrepareRef(bool createData = true);

	virtual ~ibValueReferenceDataObject();

	static ibValueReferenceDataObject* Create(ibMetaData* metaData, const ibMetaID& id, const ibGuid& objGuid = wxNullGuid);
	static ibValueReferenceDataObject* Create(const ibValueMetaObjectRecordDataRef* metaObject, const ibGuid& objGuid = wxNullGuid);

	static ibValueReferenceDataObject* Create(ibMetaData* metaData, void* ptr);
	static ibValueReferenceDataObject* CreateFromPtr(ibMetaData* metaData, void* ptr);
	static ibValueReferenceDataObject* CreateFromResultSet(class ibDatabaseResultSet *rs, const ibValueMetaObjectRecordDataRef* metaObject, const ibGuid& refGuid);

	//operator '>'
	virtual bool CompareValueGT(const ibValue& cParam) const {
		ibValueReferenceDataObject* rhs = dynamic_cast<ibValueReferenceDataObject*>(cParam.GetRef());
		if (rhs != nullptr)
			return m_metaObject == rhs->m_metaObject && m_objGuid > rhs->m_objGuid;
		return false;
	}
	
	//operator '>='
	virtual bool CompareValueGE(const ibValue& cParam) const {
		ibValueReferenceDataObject* rhs = dynamic_cast<ibValueReferenceDataObject*>(cParam.GetRef());
		if (rhs != nullptr)
			return m_metaObject == rhs->m_metaObject && m_objGuid >= rhs->m_objGuid;
		return false;
	}

	//operator '<'
	virtual bool CompareValueLS(const ibValue& cParam) const {
		ibValueReferenceDataObject* rhs = dynamic_cast<ibValueReferenceDataObject*>(cParam.GetRef());
		if (rhs != nullptr)
			return m_metaObject == rhs->m_metaObject && m_objGuid < rhs->m_objGuid;
		return false;
	}

	//operator '<='
	virtual bool CompareValueLE(const ibValue& cParam) const {
		ibValueReferenceDataObject* rhs = dynamic_cast<ibValueReferenceDataObject*>(cParam.GetRef());
		if (rhs != nullptr)
			return m_metaObject == rhs->m_metaObject && m_objGuid <= rhs->m_objGuid;
		return false;
	}

	//operator '=='
	virtual bool CompareValueEQ(const ibValue& cParam) const {
		ibValueReferenceDataObject* rhs = dynamic_cast<ibValueReferenceDataObject*>(cParam.GetRef());
		if (rhs != nullptr)
			return m_metaObject == rhs->m_metaObject && m_objGuid == rhs->m_objGuid;
		return false;
	}

	//operator '!='
	virtual bool CompareValueNE(const ibValue& cParam) const {
		ibValueReferenceDataObject* rhs = dynamic_cast<ibValueReferenceDataObject*>(cParam.GetRef());
		if (rhs != nullptr)
			return m_metaObject != rhs->m_metaObject || m_objGuid != rhs->m_objGuid;
		return true;
	}

	virtual bool FindValue(const wxString& findData, std::vector<ibValue>& listValue) const;

	//support source set/get data 
	virtual bool SetValueByMetaID(const ibMetaID& id, const ibValue& varMetaVal);
	virtual bool GetValueByMetaID(const ibMetaID& id, ibValue& pvarMetaVal) const;

	//get metaData from object
	virtual const ibValueMetaObjectRecordData* GetMetaObject() const {
		return (const ibValueMetaObjectRecordData *)m_metaObject;
	}

	//support show 
	virtual void ShowValue();

	//check is empty
	virtual bool IsEmpty() const {
		return !m_objGuid.isValid();
	}

	//****************************************************************************
	//*                              Reference methods                           *
	//****************************************************************************

	bool IsEmptyRef() const {
		return IsEmpty();
	}

	const ibValueMetaObjectRecordDataRef* GetMetaObjectRef() const {
		return m_metaObject;
	}

	ibValueRecordDataObjectRef* GetObject() const;

	//****************************************************************************
	//*                              Support methods                             *
	//****************************************************************************

	virtual ibValueMethodHelper* GetPMethods() const { // get a reference to the class helper for parsing attribute and method names
		//PrepareNames(); 
		return m_methodHelper;
	}
	virtual void PrepareNames() const;

	//****************************************************************************
	//*                              Override attribute                          *
	//****************************************************************************
	virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal);       
	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal);                   

	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray);

	//****************************************************************************
	//*                              Override type                               *
	//****************************************************************************

	//Get ref class 
	virtual ibClassID GetClassType() const;

	virtual wxString GetString() const;
	virtual wxString GetClassName() const;

	friend class ibValue;

private:

	bool ReadData(bool createData = true);

protected:

	bool m_initializedRef;

	ibValueMethodHelper* m_methodHelper;
	const ibValueMetaObjectRecordDataRef* m_metaObject;
	ibReference* m_reference_impl;

	bool m_foundedRef;
};

#endif 
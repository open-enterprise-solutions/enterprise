#ifndef _REFERENCE_H__
#define _REFERENCE_H__

#include "core/compiler/value.h"
#include "core/common/valueInfo.h"

struct reference_t;

class CReferenceDataObject : public CValue,
	public IObjectValueInfo {
	wxDECLARE_DYNAMIC_CLASS(CReferenceDataObject);
private:
	enum helperAlias {
		eProperty,
		eTable
	};
private:
	CReferenceDataObject() : CValue(eValueTypes::TYPE_VALUE, true), m_initializedRef(false) {}
	CReferenceDataObject(IMetaObjectRecordDataRef* metaObject, const Guid& objGuid = wxNullGuid);
public:

	reference_t* GetReferenceData() const {
		return m_reference_impl;
	}

	void PrepareRef(bool createData = true);

	virtual ~CReferenceDataObject();

	static CReferenceDataObject* Create(IMetadata* metaData, const meta_identifier_t& id, const Guid& objGuid = wxNullGuid);
	static CReferenceDataObject* Create(IMetaObjectRecordDataRef* metaObject, const Guid& objGuid = wxNullGuid);

	static CReferenceDataObject* Create(IMetadata* metaData, void* ptr);
	static CReferenceDataObject* CreateFromPtr(IMetadata* metaData, void* ptr);

	//operator '>'
	virtual inline bool CompareValueGT(const CValue& cParam) const {
		CReferenceDataObject* rhs = dynamic_cast<CReferenceDataObject*>(cParam.GetRef());
		if (rhs != NULL)
			return m_metaObject == rhs->m_metaObject && m_objGuid > rhs->m_objGuid;
		return false;
	}
	
	//operator '>='
	virtual inline bool CompareValueGE(const CValue& cParam) const {
		CReferenceDataObject* rhs = dynamic_cast<CReferenceDataObject*>(cParam.GetRef());
		if (rhs != NULL)
			return m_metaObject == rhs->m_metaObject && m_objGuid >= rhs->m_objGuid;
		return false;
	}

	//operator '<'
	virtual inline bool CompareValueLS(const CValue& cParam) const {
		CReferenceDataObject* rhs = dynamic_cast<CReferenceDataObject*>(cParam.GetRef());
		if (rhs != NULL)
			return m_metaObject == rhs->m_metaObject && m_objGuid < rhs->m_objGuid;
		return false;
	}

	//operator '<='
	virtual inline bool CompareValueLE(const CValue& cParam) const {
		CReferenceDataObject* rhs = dynamic_cast<CReferenceDataObject*>(cParam.GetRef());
		if (rhs != NULL)
			return m_metaObject == rhs->m_metaObject && m_objGuid <= rhs->m_objGuid;
		return false;
	}

	//operator '=='
	virtual inline bool CompareValueEQ(const CValue& cParam) const {
		CReferenceDataObject* rhs = dynamic_cast<CReferenceDataObject*>(cParam.GetRef());
		if (rhs != NULL)
			return m_metaObject == rhs->m_metaObject && m_objGuid == rhs->m_objGuid;
		return false;
	}

	//operator '!='
	virtual inline bool CompareValueNE(const CValue& cParam) const {
		CReferenceDataObject* rhs = dynamic_cast<CReferenceDataObject*>(cParam.GetRef());
		if (rhs != NULL)
			return m_metaObject != rhs->m_metaObject || m_objGuid != rhs->m_objGuid;
		return false;
	}

	virtual bool FindValue(const wxString& findData, std::vector<CValue>& foundedObjects) const;

	//support source set/get data 
	virtual bool SetValueByMetaID(const meta_identifier_t& id, const CValue& varMetaVal);
	virtual bool GetValueByMetaID(const meta_identifier_t& id, CValue& pvarMetaVal) const;

	//fill values from array 
	virtual void FillFromModel(const valueArray_t& arr);

	//get metadata from object 
	virtual IMetaObjectRecordData* GetMetaObject() const {
		return (IMetaObjectRecordData *)m_metaObject;
	}

	//support show 
	virtual void ShowValue();

	//check is empty
	virtual inline bool IsEmpty() const override {
		return !m_objGuid.isValid();
	}

	//****************************************************************************
	//*                              Reference methods                           *
	//****************************************************************************

	bool IsEmptyRef() const {
		return IsEmpty();
	}

	IMetaObjectRecordDataRef* GetMetadata() const {
		return m_metaObject;
	}

	IRecordDataObjectRef* GetObject() const;

	//****************************************************************************
	//*                              Support methods                             *
	//****************************************************************************

	virtual CMethodHelper* GetPMethods() const {
		CReferenceDataObject::PrepareNames();
		return m_methodHelper;
	};

	virtual void PrepareNames() const;

	//****************************************************************************
	//*                              Override attribute                          *
	//****************************************************************************
	virtual bool SetPropVal(const long lPropNum, const CValue& varPropVal);       
	virtual bool GetPropVal(const long lPropNum, CValue& pvarPropVal);                   

	virtual bool CallAsFunc(const long lMethodNum, CValue& pvarRetValue, CValue** paParams, const long lSizeArray);

	//****************************************************************************
	//*                              Override type                               *
	//****************************************************************************

	//Get ref class 
	virtual CLASS_ID GetTypeClass() const;

	virtual wxString GetString() const;
	virtual wxString GetTypeString() const;

	friend class CMetaTypeRefObjectValueSingle;
	friend class IRecordDataObjectRef;

private:

	bool ReadData(bool createData = true);

protected:

	bool m_initializedRef;

	CMethodHelper* m_methodHelper;
	IMetaObjectRecordDataRef* m_metaObject;
	reference_t* m_reference_impl;

	bool m_foundedRef;
};

#endif 
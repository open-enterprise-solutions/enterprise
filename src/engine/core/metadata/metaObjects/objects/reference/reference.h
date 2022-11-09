#ifndef _REFERENCE_H__
#define _REFERENCE_H__

#include "compiler/value.h"
#include "common/valueInfo.h"

struct reference_t;

class CReferenceDataObject : public CValue,
	public IObjectValueInfo {
	wxDECLARE_DYNAMIC_CLASS(CReferenceDataObject);
private:

	CReferenceDataObject() : CValue(eValueTypes::TYPE_VALUE, true), m_initializedRef(false)  {}
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

	//operator '=='
	virtual inline bool CompareValueEQ(const CValue& cParam) const
	{
		CReferenceDataObject* reference = dynamic_cast<CReferenceDataObject*>(cParam.GetRef());
		if (reference != NULL)
			return m_objGuid == reference->m_objGuid && m_metaObject == reference->m_metaObject;
		return false;
	}

	//operator '!='
	virtual inline bool CompareValueNE(const CValue& cParam) const
	{
		CReferenceDataObject* reference = dynamic_cast<CReferenceDataObject*>(cParam.GetRef());
		if (reference != NULL)
			return m_objGuid != reference->m_objGuid || m_metaObject != reference->m_metaObject;
		return false;
	}

	virtual bool FindValue(const wxString& findData, std::vector<CValue>& foundedObjects);

	//support source set/get data 
	virtual void SetValueByMetaID(const meta_identifier_t& id, const CValue& cVal);
	virtual CValue GetValueByMetaID(const meta_identifier_t& id) const;

	//get metadata from object 
	virtual IMetaObjectRecordData* GetMetaObject() const;

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

	virtual CMethods* GetPMethods() const;                          // получить ссылку на класс помощник разбора имен атрибутов и методов
	virtual void PrepareNames() const;                             // этот метод автоматически вызывается для инициализации имен атрибутов и методов
	virtual CValue Method(methodArg_t& aParams);       // вызов метода

	//****************************************************************************
	//*                              Override attribute                          *
	//****************************************************************************
	virtual void SetAttribute(attributeArg_t& aParams, CValue& cVal);        //установка атрибута
	virtual CValue GetAttribute(attributeArg_t& aParams);                   //значение атрибута

	//****************************************************************************
	//*                              Override type                               *
	//****************************************************************************

	//Get ref class 
	virtual CLASS_ID GetClassType() const;

	virtual wxString GetString() const;
	virtual wxString GetTypeString() const;

	friend class CMetaTypeRefObjectValueSingle; 
	friend class IRecordDataObjectRef;

private:

	bool ReadData(bool createData = true);

protected:

	bool m_initializedRef; 

	CMethods* m_methods;
	IMetaObjectRecordDataRef* m_metaObject;
	reference_t* m_reference_impl;

	bool m_foundedRef;
};

#endif 
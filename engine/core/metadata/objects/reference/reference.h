#ifndef _REFERENCE_H__
#define _REFERENCE_H__

#include "compiler/value.h"
#include "common/valueInfo.h"

class CValueTabularRefSection;

class CValueReference : public CValue,
	public IObjectValueInfo
{
	wxDECLARE_DYNAMIC_CLASS(CValueReference);
private:
	CValueReference() : CValue() {}
public:

	CValueReference(IMetadata *metaData, meta_identifier_t metaID, const Guid &objGuid = Guid());
	CValueReference(IMetaObjectRefValue *metaObject, const Guid &objGuid = Guid());
	virtual ~CValueReference();

	static CValue CreateFromPtr(IMetadata *metaData, void *ptr);

	//operator '=='
	virtual inline bool CompareValueEQ(const CValue &cParam) const
	{
		CValueReference *m_reference = dynamic_cast<CValueReference *>(cParam.GetRef());
		if (m_reference) return m_objGuid == m_reference->m_objGuid && m_metaObject == m_reference->m_metaObject;
		return false;
	}

	//operator '!='
	virtual inline bool CompareValueNE(const CValue &cParam) const
	{
		CValueReference *m_reference = dynamic_cast<CValueReference *>(cParam.GetRef());
		if (m_reference) return m_objGuid != m_reference->m_objGuid || m_metaObject != m_reference->m_metaObject;
		return false;
	}

	virtual bool FindValue(const wxString &findData, std::vector<CValue> &foundedObjects);

	//override buffer
	virtual void SetBinaryData(int nPosition, PreparedStatement *preparedStatment);
	virtual void GetBinaryData(int nPosition, DatabaseResultSet *databaseResultSet);

	//support source set/get data 
	virtual void SetValueByMetaID(meta_identifier_t id, const CValue &cVal);
	virtual CValue GetValueByMetaID(meta_identifier_t id) const;

	//get metadata from object 
	virtual IMetaObjectValue *GetMetaObject() const;

	//support show 
	virtual void ShowValue();

	//check is empty
	virtual inline bool IsEmpty() const override { return !m_objGuid.isValid(); }

	//****************************************************************************
	//*                              Reference methods                           *
	//****************************************************************************

	CValue IsEmptyRef();
	CValue GetMetadata();
	CValue GetObject();

	//****************************************************************************
	//*                              Support methods                             *
	//****************************************************************************

	virtual CMethods* GetPMethods() const;                          // получить ссылку на класс помощник разбора имен атрибутов и методов
	virtual void PrepareNames() const;                             // этот метод автоматически вызывается для инициализации имен атрибутов и методов
	virtual CValue Method(methodArg_t &aParams);       // вызов метода

	//****************************************************************************
	//*                              Override attribute                          *
	//****************************************************************************
	virtual void SetAttribute(attributeArg_t &aParams, CValue &cVal);        //установка атрибута
	virtual CValue GetAttribute(attributeArg_t &aParams);                   //значение атрибута

	//****************************************************************************
	//*                              Override type                               *
	//****************************************************************************

	//Get ref class 
	virtual CLASS_ID GetTypeID() const;

	virtual wxString GetString() const;
	virtual wxString GetTypeString() const;

	friend class IDataObjectRefValue;

private:

	void PrepareReference();
	bool ReadReferenceInDB();

protected:
	CMethods *m_methods;
	IMetaObjectRefValue *m_metaObject;
	reference_t *m_reference_impl;

	bool m_bFoundedRef;
};

#endif 
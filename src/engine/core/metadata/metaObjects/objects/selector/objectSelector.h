#ifndef _SELECTOR_H__
#define _SELECTOR_H__

#include "core/metadata/metaObjects/objects/object.h"

class ISelectorObject : public CValue {
public:

	ISelectorObject();
	virtual ~ISelectorObject();

	virtual bool Next() = 0;

	//is empty
	virtual inline bool IsEmpty() const {
		return false;
	}

	//get metadata from object 
	virtual IMetaObjectWrapperData* GetMetaObject() const = 0;

	//Get ref class 
	virtual CLASS_ID GetTypeClass() const;

	//types 
	virtual wxString GetTypeString() const;
	virtual wxString GetString() const;

protected:

	virtual void Reset() = 0;
	virtual bool Read() = 0;

protected:
	CMethodHelper* m_methodHelper;
};

class CSelectorDataObject : public ISelectorObject,
	public IObjectValueInfo {
public:

	CSelectorDataObject(IMetaObjectRecordDataMutableRef* metaObject);

	virtual bool Next();
	virtual IRecordDataObjectRef* GetObject(const Guid& guid) const;

	//get metadata from object 
	virtual IMetaObjectRecordData* GetMetaObject() const {
		return m_metaObject;
	}

	virtual CMethodHelper* GetPMethods() const { //получить ссылку на класс помощник разбора имен атрибутов и методов
		PrepareNames();
		return m_methodHelper;
	}

	virtual void PrepareNames() const;                         //этот метод автоматически вызывается для инициализации имен атрибутов и методов
	virtual bool CallAsFunc(const long lMethodNum, CValue& pvarRetValue, CValue** paParams, const long lSizeArray);//вызов метода

	//attribute
	virtual bool SetPropVal(const long lPropNum, const CValue& varPropVal);        //установка атрибута
	virtual bool GetPropVal(const long lPropNum, CValue& pvarPropVal);                   //значение атрибута

protected:

	virtual void Reset();
	virtual bool Read();

protected:

	IMetaObjectRecordDataMutableRef* m_metaObject;
	std::vector<Guid> m_currentValues;
};

/////////////////////////////////////////////////////////////////////////////

class CSelectorRegisterObject :
	public ISelectorObject {
public:
	CSelectorRegisterObject(IMetaObjectRegisterData* metaObject);

	virtual bool Next();
	virtual IRecordManagerObject* GetRecordManager(const valueArray_t& keyValues) const;

	//get metadata from object 
	virtual IMetaObjectRegisterData* GetMetaObject() const {
		return m_metaObject;
	}

	virtual CMethodHelper* GetPMethods() const { //получить ссылку на класс помощник разбора имен атрибутов и методов
		PrepareNames();
		return m_methodHelper;
	}

	virtual void PrepareNames() const;                         //этот метод автоматически вызывается для инициализации имен атрибутов и методов
	virtual bool CallAsFunc(const long lMethodNum, CValue& pvarRetValue, CValue** paParams, const long lSizeArray);//вызов метода

	//attribute
	virtual bool SetPropVal(const long lPropNum, const CValue& varPropVal);        //установка атрибута
	virtual bool GetPropVal(const long lPropNum, CValue& pvarPropVal);                   //значение атрибута

protected:

	virtual void Reset();
	virtual bool Read();

protected:

	IMetaObjectRegisterData* m_metaObject;

	valueArray_t m_keyValues;
	std::vector <valueArray_t> m_currentValues;
	std::map<
		valueArray_t,
		valueArray_t
	> m_objectValues;
};

#endif
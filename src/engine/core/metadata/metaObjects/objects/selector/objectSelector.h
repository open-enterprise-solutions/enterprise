#ifndef _SELECTOR_H__
#define _SELECTOR_H__

#include "metadata/metaObjects/objects/object.h"

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
	virtual CLASS_ID GetClassType() const;

	//types 
	virtual wxString GetTypeString() const;
	virtual wxString GetString() const;

protected:

	virtual void Reset() = 0;
	virtual bool Read() = 0;

protected:
	CMethods* m_methods;
};

class ISelectorDataObject : public ISelectorObject,
	public IObjectValueInfo {
public:

	ISelectorDataObject(IMetaObjectRecordDataMutableRef* metaObject);

	virtual bool Next();
	virtual IRecordDataObjectRef* GetObject(const Guid& guid) const;

	//get metadata from object 
	virtual IMetaObjectRecordData* GetMetaObject() const {
		return m_metaObject;
	}

	virtual CMethods* GetPMethods() const { PrepareNames();  return m_methods; } //получить ссылку на класс помощник разбора имен атрибутов и методов
	virtual void PrepareNames() const;                         //этот метод автоматически вызывается для инициализации имен атрибутов и методов
	virtual CValue Method(methodArg_t& aParams);//вызов метода

	//attribute
	virtual void SetAttribute(attributeArg_t& aParams, CValue& cVal);        //установка атрибута
	virtual CValue GetAttribute(attributeArg_t& aParams);                   //значение атрибута

protected:

	virtual void Reset();
	virtual bool Read();

protected:

	IMetaObjectRecordDataMutableRef* m_metaObject;
	std::vector<Guid> m_aCurrentValues;
};

/////////////////////////////////////////////////////////////////////////////

class ISelectorRegisterObject :
	public ISelectorObject {
public:
	ISelectorRegisterObject(IMetaObjectRegisterData* metaObject);

	virtual bool Next();
	virtual IRecordManagerObject* GetRecordManager(const std::map<meta_identifier_t, CValue>& keyValues) const;

	//get metadata from object 
	virtual IMetaObjectRegisterData* GetMetaObject() const {
		return m_metaObject;
	}

	virtual CMethods* GetPMethods() const { PrepareNames();  return m_methods; } //получить ссылку на класс помощник разбора имен атрибутов и методов
	virtual void PrepareNames() const;                         //этот метод автоматически вызывается для инициализации имен атрибутов и методов
	virtual CValue Method(methodArg_t& aParams);//вызов метода

	//attribute
	virtual void SetAttribute(attributeArg_t& aParams, CValue& cVal);        //установка атрибута
	virtual CValue GetAttribute(attributeArg_t& aParams);                   //значение атрибута

protected:

	virtual void Reset();
	virtual bool Read();

protected:

	IMetaObjectRegisterData* m_metaObject;

	std::map<meta_identifier_t, CValue> m_keyValues;
	std::vector <std::map<meta_identifier_t, CValue>> m_aCurrentValues;
	std::map<
		std::map<meta_identifier_t, CValue>,
		std::map<meta_identifier_t, CValue>
	> m_objectValues;
};

#endif
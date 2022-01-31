#ifndef _SELECTOR_H__
#define _SELECTOR_H__

#include "metadata/objects/baseObject.h"

class IDataSelectorValue : public CValue,
	public IObjectValueInfo {
	CMethods *m_methods;
private:
	void Reset();
	bool Read();
public:
	IDataSelectorValue(IMetaObjectRefValue *metaObject);
	virtual ~IDataSelectorValue();

	virtual bool Next();
	virtual IDataObjectRefValue *GetObject(const Guid &guid) const;

	//is empty
	virtual inline bool IsEmpty() const { return false; }

	//get metadata from object 
	virtual IMetaObjectValue *GetMetaObject() const { return m_metaObject; };

	virtual CMethods* GetPMethods() const { PrepareNames();  return m_methods; }; //получить ссылку на класс помощник разбора имен атрибутов и методов
	virtual void PrepareNames() const;                         //этот метод автоматически вызывается для инициализации имен атрибутов и методов
	virtual CValue Method(methodArg_t &aParams);//вызов метода

	//attribute
	virtual void SetAttribute(attributeArg_t &aParams, CValue &cVal);        //установка атрибута
	virtual CValue GetAttribute(attributeArg_t &aParams);                   //значение атрибута

	//Get ref class 
	virtual CLASS_ID GetTypeID() const;

	//types 
	virtual wxString GetTypeString() const;
	virtual wxString GetString() const;

protected:

	IMetaObjectRefValue *m_metaObject;
	std::vector<Guid> m_aObjectData;
};

#endif
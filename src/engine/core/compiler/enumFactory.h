#ifndef _ENUMFACTORY_H__
#define _ENUMFACTORY_H__

#include "value.h"

#define enumFactory         	(CEnumFactory::Get())
#define enumFactoryDestroy()  	(CEnumFactory::Destroy())

//realization factory pattern 
class CEnumFactory : public CValue {
	static CEnumFactory *s_instance;
private:
	CEnumFactory();
public:

	virtual ~CEnumFactory();

	static CEnumFactory* Get();
	static void Destroy();

	//эти методы нужно переопределить в ваших агрегатных объектах:
	virtual CMethodHelper* GetPMethods() const { //получить ссылку на класс помощник разбора имен атрибутов и методов	
		PrepareNames(); 
		return m_methodHelper; 
	}

	virtual void PrepareNames() const; //этот метод автоматически вызывается для инициализации имен атрибутов и методов

	//этот метод автоматически вызывается для инициализации имен атрибутов и методов
	virtual bool GetPropVal(const long lPropNum, CValue& pvarPropVal);                   //значение атрибута

	virtual wxString GetString() const { 
		return wxT("enumFactory"); 
	}
	
	virtual wxString GetTypeString() const {
		return wxT("enumFactory"); 
	}

protected:
	CMethodHelper *m_methodHelper;
};

#endif 
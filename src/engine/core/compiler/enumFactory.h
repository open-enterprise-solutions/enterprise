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
	virtual CMethods* GetPMethods() const { //получить ссылку на класс помощник разбора имен атрибутов и методов
		return m_methods; 
	}

	virtual void PrepareNames() const; //этот метод автоматически вызывается для инициализации имен атрибутов и методов

	//этот метод автоматически вызывается для инициализации имен атрибутов и методов
	virtual CValue GetAttribute(attributeArg_t &aParams);                   //значение атрибута

	virtual wxString GetString() const { 
		return wxT("enumFactory"); 
	}
	
	virtual wxString GetTypeString() const {
		return wxT("enumFactory"); 
	}

protected:
	CMethods *m_methods;
};

#endif 
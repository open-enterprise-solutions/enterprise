#ifndef _ENUM_FACTORY_H__
#define _ENUM_FACTORY_H__

#include "value/value.h"

//realization factory pattern 
class CEnumFactory : public CValue {
public:

	CEnumFactory();
	virtual ~CEnumFactory();

	//эти методы нужно переопределить в ваших агрегатных объектах:
	virtual CMethodHelper* GetPMethods() const { //получить ссылку на класс помощник разбора имен атрибутов и методов	
		//PrepareNames(); 
		return m_methodHelper; 
	}

	virtual void PrepareNames() const; //этот метод автоматически вызывается для инициализации имен атрибутов и методов

	//этот метод автоматически вызывается для инициализации имен атрибутов и методов
	virtual bool GetPropVal(const long lPropNum, CValue& pvarPropVal);                   //значение атрибута

protected:
	CMethodHelper *m_methodHelper;
};

#endif 
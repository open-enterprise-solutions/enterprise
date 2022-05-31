#ifndef _VALUETYPES_H_
#define _VALUETYPES_H_

#include "compiler/value.h"

class CValueTypes : public CValue
{
	wxDECLARE_DYNAMIC_CLASS(CValueTypes);

private:

	std::vector<wxClassInfo *> aClassInfo; 

private:

	static CMethods m_methods;

public:

	CValueTypes();

	wxString GetTypeString()const;
	wxString GetString()const;

	virtual void SetAttribute(CAttributeParameters &aParams, CValue &cVal);        //установка атрибута
	virtual CValue GetAttribute(CAttributeParameters &aParams);                   //значение атрибута
	virtual int  FindAttribute(const wxString &sName);

	virtual CMethods* GetPMethods() { return &m_methods; }; //получить ссылку на класс помощник разбора имен атрибутов и методов
	virtual void PrepareNames();                          //этот метод автоматически вызывается для инициализации имен атрибутов и методов
	virtual CValue Method(CMethodParameters &aParams);       //вызов метода

	virtual ~CValueTypes();
};

#endif 
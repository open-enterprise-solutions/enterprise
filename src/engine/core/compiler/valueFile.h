#ifndef _VALUE_FILE_H__
#define _VALUE_FILE_H__

#include "compiler/value.h"

class CValueFile : public CValue {
	wxDECLARE_DYNAMIC_CLASS(CValueFile);
public:

	//эти методы нужно переопределить в ваших агрегатных объектах:
	virtual CMethods* GetPMethods() const { return &m_methods; }//получить ссылку на класс помощник разбора имен атрибутов и методов
	virtual void PrepareNames() const;//этот метод автоматически вызывается для инициализации имен атрибутов и методов
	virtual CValue Method(methodArg_t &aParams);//вызов метода

	virtual void SetAttribute(attributeArg_t &aParams, CValue &cVal);//установка атрибута
	virtual CValue GetAttribute(attributeArg_t &aParams);//значение атрибута

	CValueFile();
	virtual ~CValueFile();

	virtual inline bool IsEmpty() const override { return false; }

	virtual bool Init() { return false; }
	virtual bool Init(CValue **aParams);

	virtual wxString GetTypeString() const { return wxT("file"); }
	virtual wxString GetString() const { return wxT("file"); }

private:
	wxString m_fileName; 
	static CMethods m_methods;
};

#endif 
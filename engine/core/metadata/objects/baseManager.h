#ifndef _MANAGER_BASE_H__
#define _MANAGER_BASE_H__

#include "compiler/value.h"

class CSystemManager : public CValue {
	wxDECLARE_DYNAMIC_CLASS(CSystemManager);
public:

	CSystemManager(IMetadata *metaData = NULL);

	virtual ~CSystemManager();

	virtual CMethods* GetPMethods() const { PrepareNames();  return m_methods; }; //получить ссылку на класс помощник разбора имен атрибутов и методов
	virtual void PrepareNames() const;                         //этот метод автоматически вызывается для инициализации имен атрибутов и методов

	//attributes
	virtual CValue GetAttribute(attributeArg_t &aParams);                   //значение атрибута

	//types 
	virtual wxString GetTypeString() const { return wxT("systemManager"); }
	virtual wxString GetString() const { return wxT("systemManager"); }

protected:
	//methos 
	CMethods *m_methods;
	//metadata 
	IMetadata *m_metaData;
};

#endif 
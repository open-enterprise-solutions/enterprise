#ifndef _CONSTANTS_MANAGER_H__
#define _CONSTANTS_MANAGER_H__

#include "constant.h"

class CManagerConstantValue : public CValue {
	wxDECLARE_DYNAMIC_CLASS(CManagerConstantValue);
protected:
	CMetaConstantObject *m_metaConst;
public:

	CManagerConstantValue(CMetaConstantObject *metaConst = NULL);
	virtual ~CManagerConstantValue();

	virtual CMethods* GetPMethods() const { PrepareNames();  return &m_methods; }; //получить ссылку на класс помощник разбора имен атрибутов и методов
	virtual void PrepareNames() const;                         //этот метод автоматически вызывается для инициализации имен атрибутов и методов
	virtual CValue Method(methodArg_t &aParams);//вызов метода

	//Get ref class 
	virtual CLASS_ID GetTypeID() const;

	//types 
	virtual wxString GetTypeString() const;
	virtual wxString GetString() const;

protected:

	//methos 
	static CMethods m_methods;
};


#endif // !_CONSTANTS_MANAGER_H__

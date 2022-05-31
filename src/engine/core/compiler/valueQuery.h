#ifndef _VALUEQUERY_H__
#define _VALUEQUERY_H__

#include "value.h"

//Поддержка массивов
class CValueQuery : public CValue
{
	wxDECLARE_DYNAMIC_CLASS(CValueQuery);

private:

	wxString m_sQueryText;

private:

	void Execute();

public:

	CValueQuery();
	virtual ~CValueQuery();

	virtual wxString GetTypeString() const { return wxT("query"); }
	virtual wxString GetString() const { return wxT("query"); }

	static CMethods m_methods;

	virtual CMethods* GetPMethods() const { return &m_methods; } //получить ссылку на класс помощник разбора имен атрибутов и методов
	virtual void PrepareNames() const;                         //этот метод автоматически вызывается для инициализации имен атрибутов и методов
	virtual CValue Method(methodArg_t &aParams);       //вызов метода
};

#endif
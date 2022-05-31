#ifndef _VALUEPOINT_H__
#define _VALUEPOINT_H__

#include "compiler/value.h"

//ѕоддержка массивов
class CValuePoint : public CValue
{
	wxDECLARE_DYNAMIC_CLASS(CValuePoint);

public:

	wxPoint m_point;

public:

	CValuePoint();
	CValuePoint(const wxPoint &point);

	virtual bool Init(CValue **aParams);

	virtual wxString GetTypeString() const;
	virtual wxString GetString() const;

	//check is empty
	virtual inline bool IsEmpty() const override { return m_point == wxDefaultPosition; }

	static CMethods m_methods;

	virtual void SetAttribute(attributeArg_t &aParams, CValue &cVal);        //установка атрибута
	virtual CValue GetAttribute(attributeArg_t &aParams);                   //значение атрибута

	virtual CMethods* GetPMethods() const { return &m_methods; } //получить ссылку на класс помощник разбора имен атрибутов и методов
	virtual void PrepareNames() const;                         //этот метод автоматически вызываетс€ дл€ инициализации имен атрибутов и методов

	operator wxPoint() { return m_point; }
	virtual ~CValuePoint();
};

#endif
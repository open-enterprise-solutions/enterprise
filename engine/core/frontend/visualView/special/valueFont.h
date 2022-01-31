#ifndef _VALUEFONT_H__
#define _VALUEFONT_H__

#include "compiler/value.h"

//ѕоддержка массивов
class CValueFont : public CValue
{
	wxDECLARE_DYNAMIC_CLASS(CValueFont);

public:

	wxFont m_font;

public:

	CValueFont();
	CValueFont(const wxFont &font);

	virtual bool Init(CValue **aParams);

	virtual wxString GetTypeString() const;
	virtual wxString GetString() const;

	//check is empty
	virtual inline bool IsEmpty() const override { return !m_font.IsOk(); }

	static CMethods m_methods;

	virtual void SetAttribute(attributeArg_t &aParams, CValue &cValue);        //установка атрибута
	virtual CValue GetAttribute(attributeArg_t &aParams);                   //значение атрибута

	virtual CMethods* GetPMethods() const { return &m_methods; }; //получить ссылку на класс помощник разбора имен атрибутов и методов
	virtual void PrepareNames() const;                         //этот метод автоматически вызываетс€ дл€ инициализации имен атрибутов и методов

	operator wxFont() { return m_font; }

	virtual ~CValueFont();
};

#endif
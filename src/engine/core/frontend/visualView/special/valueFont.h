#ifndef _VALUEFONT_H__
#define _VALUEFONT_H__

#include "core/compiler/value.h"

//ѕоддержка массивов
class CValueFont : public CValue
{
	wxDECLARE_DYNAMIC_CLASS(CValueFont);
private:
	wxFont m_font;
public:

	CValueFont();
	CValueFont(const wxFont& font);
	virtual ~CValueFont();

	virtual bool Init(CValue** paParams, const long lSizeArray);

	virtual wxString GetTypeString() const;
	virtual wxString GetString() const;

	//check is empty
	virtual inline bool IsEmpty() const override {
		return !m_font.IsOk();
	}

	static CMethodHelper m_methodHelper;

	virtual bool SetPropVal(const long lPropNum, const CValue& varPropVal);        //установка атрибута
	virtual bool GetPropVal(const long lPropNum, CValue& pvarPropVal);                   //значение атрибута

	virtual CMethodHelper* GetPMethods() const {
		PrepareNames();
		return &m_methodHelper;
	}
	virtual void PrepareNames() const;                         //этот метод автоматически вызываетс€ дл€ инициализации имен атрибутов и методов

	operator wxFont() {
		return m_font;
	}
};

#endif
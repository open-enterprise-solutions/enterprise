#ifndef _VALUESIZE_H__
#define _VALUESIZE_H__

#include "core/compiler/value.h"

//ѕоддержка массивов
class CValueSize : public CValue
{
	wxDECLARE_DYNAMIC_CLASS(CValueSize);

public:

	wxSize m_size;

public:

	CValueSize();
	CValueSize(const wxSize& size);

	virtual bool Init(CValue** paParams, const long lSizeArray);

	virtual wxString GetTypeString() const;
	virtual wxString GetString() const;

	virtual inline bool IsEmpty() const override {
		return m_size == wxDefaultSize;
	}

	static CMethodHelper m_methodHelper;

	virtual bool SetPropVal(const long lPropNum, const CValue& varPropVal);        //установка атрибута
	virtual bool GetPropVal(const long lPropNum, CValue& pvarPropVal);                   //значение атрибута

	virtual CMethodHelper* GetPMethods() const { //получить ссылку на класс помощник разбора имен атрибутов и методов
		PrepareNames();
		return &m_methodHelper;
	}

	virtual void PrepareNames() const; //этот метод автоматически вызываетс€ дл€ инициализации имен атрибутов и методов

	operator wxSize() const {
		return m_size;
	}

	virtual ~CValueSize();

};

#endif
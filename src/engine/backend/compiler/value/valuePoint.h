#ifndef _VALUEPOINT_H__
#define _VALUEPOINT_H__

#include "backend/compiler/value/value.h"

//ѕоддержка массивов
class BACKEND_API CValuePoint : public CValue
{
	wxDECLARE_DYNAMIC_CLASS(CValuePoint);

public:

	wxPoint m_point;

public:

	CValuePoint();
	CValuePoint(const wxPoint& point);
	virtual ~CValuePoint() {}

	virtual bool Init(CValue** paParams, const long lSizeArray);
	virtual wxString GetString() const {
		return typeConv::PointToString(m_point); 
	}

	//check is empty
	virtual inline bool IsEmpty() const {
		return m_point == wxDefaultPosition;
	}

	static CMethodHelper m_methodHelper;

	virtual bool SetPropVal(const long lPropNum, const CValue& varPropVal);        //установка атрибута
	virtual bool GetPropVal(const long lPropNum, CValue& pvarPropVal);                   //значение атрибута

	virtual CMethodHelper* GetPMethods() const { //получить ссылку на класс помощник разбора имен атрибутов и методов
		//PrepareNames(); 
		return &m_methodHelper;
	}
	virtual void PrepareNames() const;                         //этот метод автоматически вызываетс€ дл€ инициализации имен атрибутов и методов

	operator wxPoint() const { return m_point; }
};

#endif
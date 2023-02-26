#ifndef _VALUEQUERY_H__
#define _VALUEQUERY_H__

#include "value.h"

//Поддержка массивов
class CORE_API CValueGrid : public CValue {
	wxDECLARE_DYNAMIC_CLASS(CValueGrid);
private:
	enum Func {
		enShowGrid = 0,
	};
public:

	CValueGrid();
	virtual ~CValueGrid();

	virtual wxString GetTypeString()const { 
		return wxT("table.document");
	}
	
	virtual wxString GetString()const { 
		return wxT("table.document"); 
	}

	static CMethodHelper m_methodHelper;

	virtual bool SetPropVal(const long lPropNum, CValue &varPropVal);        //установка атрибута
	virtual bool GetPropVal(const long lPropNum, CValue& pvarPropVal);                   //значение атрибута

	virtual CMethodHelper* GetPMethods() const { 
		PrepareNames(); return &m_methodHelper; 
	} 
	virtual void PrepareNames() const;                         //этот метод автоматически вызывается для инициализации имен атрибутов и методов
	virtual bool CallAsFunc(const long lMethodNum, CValue& pvarRetValue, CValue** paParams, const long lSizeArray);       //вызов метода

protected:

	bool ShowGrid(const wxString &sTitle);
};

#endif
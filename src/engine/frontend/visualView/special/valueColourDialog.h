#ifndef  _VALUE_COLOUR_DIALOG_H__
#define _VALUE_COLOUR_DIALOG_H__

#include "core/compiler/value.h"

#include <wx/colordlg.h>

class CValueColourDialog : public CValue {
	wxDECLARE_DYNAMIC_CLASS(CValueFileDialog);
public:

	//эти методы нужно переопределить в ваших агрегатных объектах:
	virtual CMethodHelper* GetPMethods() const { 
		PrepareNames();
		return &m_methodHelper; 
	}
	//получить ссылку на класс помощник разбора имен атрибутов и методов
	virtual void PrepareNames() const;//этот метод автоматически вызывается для инициализации имен атрибутов и методов
	virtual bool CallAsFunc(const long lMethodNum, CValue& pvarRetValue, CValue** paParams, const long lSizeArray);//вызов метода

	virtual bool SetPropVal(const long lPropNum, CValue &varPropVal);//установка атрибута
	virtual bool GetPropVal(const long lPropNum, CValue& pvarPropVal);//значение атрибута

	CValueColourDialog();
	virtual ~CValueColourDialog();

	virtual inline bool IsEmpty() const override { 
		return false;
	}

	virtual wxString GetTypeString() const { 
		return wxT("colourDialog");
	}
	
	virtual wxString GetString() const { 
		return wxT("colourDialog"); 
	}

private:
	static CMethodHelper m_methodHelper;

	wxColourDialog *m_colourDialog;
};


#endif // ! _VALUE_FONT_DIALOG_H__

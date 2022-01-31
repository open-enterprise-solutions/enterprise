#ifndef  _VALUE_COLOUR_DIALOG_H__
#define _VALUE_COLOUR_DIALOG_H__

#include "compiler/value.h"

#include <wx/colordlg.h>

class CValueColourDialog : public CValue {
	wxDECLARE_DYNAMIC_CLASS(CValueFileDialog);
public:

	//эти методы нужно переопределить в ваших агрегатных объектах:
	virtual CMethods* GetPMethods() const { return &m_methods; };//получить ссылку на класс помощник разбора имен атрибутов и методов
	virtual void PrepareNames() const;//этот метод автоматически вызывается для инициализации имен атрибутов и методов
	virtual CValue Method(methodArg_t &aParams);//вызов метода

	virtual void SetAttribute(attributeArg_t &aParams, CValue &cVal);//установка атрибута
	virtual CValue GetAttribute(attributeArg_t &aParams);//значение атрибута

	CValueColourDialog();
	virtual ~CValueColourDialog();

	virtual inline bool IsEmpty() const override { return false; }

	virtual wxString GetTypeString() const { return wxT("colourDialog"); }
	virtual wxString GetString() const { return wxT("colourDialog"); }

private:
	static CMethods m_methods;

	wxColourDialog *m_colourDialog;
};


#endif // ! _VALUE_FONT_DIALOG_H__

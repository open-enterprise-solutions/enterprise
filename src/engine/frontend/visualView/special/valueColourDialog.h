#ifndef  _VALUE_COLOUR_DIALOG_H__
#define _VALUE_COLOUR_DIALOG_H__

#include "backend/compiler/value.h"

#include <wx/colordlg.h>

class CValueColourDialog : public CValue {
	wxDECLARE_DYNAMIC_CLASS(CValueFileDialog);
public:

	// override these methods in your aggregate objects:
	virtual CMethodHelper* GetPMethods() const { 
		PrepareNames();
		return &m_methodHelper; 
	}
	// get the helper that resolves attribute and method names
	virtual void PrepareNames() const;// called automatically to initialise attribute and method names
	virtual bool CallAsFunc(const long lMethodNum, CValue& pvarRetValue, CValue** paParams, const long lSizeArray);// method call

	virtual bool SetPropVal(const long lPropNum, CValue &varPropVal);// set attribute
	virtual bool GetPropVal(const long lPropNum, CValue& pvarPropVal);// attribute value

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

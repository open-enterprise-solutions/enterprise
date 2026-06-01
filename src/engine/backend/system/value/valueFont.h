#ifndef __VALUE_FONT_H__
#define __VALUE_FONT_H__

#include "backend/compiler/value.h"

//Array support
class BACKEND_API ibValueFont : public ibValue
{
	public:
	wxFont m_font;
public:

	ibValueFont();
	ibValueFont(const wxFont& font);
	virtual ~ibValueFont() {}

	virtual bool Init(ibValue** paParams, const long lSizeArray);

	virtual wxString GetString() const {
		return typeConv::FontToString(m_font);
	}

	//check is empty
	virtual bool IsEmpty() const {
		return !m_font.IsOk();
	}

	static ibValueMethodHelper m_methodHelper;

	virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal);        //setting attribute
	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal);                   //attribute value

	virtual ibValueMethodHelper* GetPMethods() const {
		//PrepareNames();
		return &m_methodHelper;
	}
	virtual void PrepareNames() const;                         // this method is automatically called to initialize attribute and method names.

	operator wxFont() {
		return m_font;
	}
};

#endif
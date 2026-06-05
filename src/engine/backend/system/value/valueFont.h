#ifndef __VALUE_FONT_H__
#define __VALUE_FONT_H__

#include "backend/compiler/value.h"

//Array support
void ibValueFont_BindNames(ibValue::ibMemberTable& helper, const ibValue* ctx);

class BACKEND_API ibValueFont : public ibValueStaticMembers<&ibValueFont_BindNames>
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

	virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal);        //setting attribute
	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal);                   //attribute value
	// DoGetPMethods (protected) + Shared<&ibValueFont_BindNames> come from the base.

	operator wxFont() {
		return m_font;
	}
};

#endif
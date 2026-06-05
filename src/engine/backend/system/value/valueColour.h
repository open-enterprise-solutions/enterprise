#ifndef __VALUE_COLOUR_H__
#define __VALUE_COLOUR_H__

#include "backend/compiler/value.h"

//Array support
void ibValueColour_BindNames(ibValue::ibMemberTable& helper, const ibValue* ctx);

class BACKEND_API ibValueColour : public ibValueStaticMembers<&ibValueColour_BindNames>
{
	public:

public:

	wxColour m_colour;

public:

	ibValueColour();
	ibValueColour(const wxColour& colour);
	virtual ~ibValueColour() {}

	virtual bool Init(ibValue** paParams, const long lSizeArray);
	virtual wxString GetString() const {
		return typeConv::ColourToString(m_colour);
	}

	//check is empty
	virtual bool IsEmpty() const {
		return !m_colour.IsOk();
	}

	virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal);        //setting attribute
	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal);                   //attribute value
	// DoGetPMethods (protected) + Shared<&ibValueColour_BindNames> come from the base.

	operator wxColour() const { return m_colour; }
};

#endif
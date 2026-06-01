#ifndef __VALUE_COLOUR_H__
#define __VALUE_COLOUR_H__

#include "backend/compiler/value.h"

//Array support
class BACKEND_API ibValueColour : public ibValue
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

	static ibValueMethodHelper m_methodHelper;

	virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal);        //setting attribute
	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal);                   //attribute value

	virtual ibValueMethodHelper* GetPMethods() const {
		//PrepareNames();
		return &m_methodHelper;
	}
	
	virtual void PrepareNames() const;                         // this method is automatically called to initialize attribute and method names.

	operator wxColour() const { return m_colour; }
};

#endif
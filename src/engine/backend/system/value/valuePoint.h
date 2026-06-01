#ifndef __VALUE_POINT_H__
#define __VALUE_POINT_H__

#include "backend/compiler/value.h"

class BACKEND_API ibValuePoint : public ibValue
{
	public:

public:

	wxPoint m_point;

public:

	ibValuePoint();
	ibValuePoint(const wxPoint& point);
	virtual ~ibValuePoint() {}

	virtual bool Init(ibValue** paParams, const long lSizeArray);
	virtual wxString GetString() const {
		return typeConv::PointToString(m_point); 
	}

	//check is empty
	virtual bool IsEmpty() const {
		return m_point == wxDefaultPosition;
	}

	static ibValueMethodHelper m_methodHelper;

	virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal);        //setting attribute
	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal);                   //attribute value

	virtual ibValueMethodHelper* GetPMethods() const { // get a reference to the class helper for parsing attribute and method names
		//PrepareNames(); 
		return &m_methodHelper;
	}
	virtual void PrepareNames() const;                         // this method is automatically called to initialize attribute and method names.

	operator wxPoint() const { return m_point; }
};

#endif
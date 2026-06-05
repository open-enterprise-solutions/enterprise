#ifndef __VALUE_POINT_H__
#define __VALUE_POINT_H__

#include "backend/compiler/value.h"

void ibValuePoint_BindNames(ibValue::ibMemberTable& helper, const ibValue* ctx);

class BACKEND_API ibValuePoint : public ibValueStaticMembers<&ibValuePoint_BindNames>
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

	virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal);        //setting attribute
	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal);                   //attribute value
	// DoGetPMethods (protected) + Shared<&ibValuePoint_BindNames> come from the base.

	operator wxPoint() const { return m_point; }
};

#endif
#ifndef __VALUE_POINT_H__
#define __VALUE_POINT_H__

#include "backend/compiler/value.h"
#include "core/graphics/geometry.h"   // ibPoint — the point, as data

void ibValuePoint_BindNames(ibValue::ibMemberTable& helper, const ibValue* ctx);

class BACKEND_API ibValuePoint : public ibValueStaticMembers<&ibValuePoint_BindNames>
{
	public:

public:

	ibPoint m_point;

public:

	ibValuePoint();
	ibValuePoint(const ibPoint& point);
	virtual ~ibValuePoint() {}

	virtual bool Init(ibValue** paParams, const long lSizeArray);
	virtual ibString GetString() const {
		return m_point.ToString();
	}

	//check is empty
	virtual bool IsEmpty() const {
		return m_point == ibDefaultPosition;
	}

	virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal);        //setting attribute
	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal);                   //attribute value
	// DoGetPMethods (protected) + Shared<&ibValuePoint_BindNames> come from the base.
};

#endif
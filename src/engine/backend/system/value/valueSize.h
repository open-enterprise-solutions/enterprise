#ifndef __VALUE_SIZE_H__
#define __VALUE_SIZE_H__

#include "backend/compiler/value.h"
#include "core/graphics/geometry.h"   // ibSize — the size, as data

void ibValueSize_BindNames(ibValue::ibMemberTable& helper, const ibValue* ctx);

class BACKEND_API ibValueSize : public ibValueStaticMembers<&ibValueSize_BindNames> {
	public:

	ibValueSize();
	ibValueSize(const ibSize& size);
	virtual ~ibValueSize() {}

	virtual bool Init(ibValue** paParams, const long lSizeArray);
	virtual ibString GetString() const {
		return m_size.ToString();
	}

	virtual bool IsEmpty() const {
		return m_size == ibDefaultSize;
	}

	virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal);        //setting attribute
	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal);                   //attribute value
	// DoGetPMethods (protected) + Shared<&ibValueSize_BindNames> come from the base.

public:
	ibSize m_size;
};

#endif
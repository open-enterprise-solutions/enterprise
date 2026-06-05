#ifndef __VALUE_SIZE_H__
#define __VALUE_SIZE_H__

#include "backend/compiler/value.h"

void ibValueSize_BindNames(ibValue::ibMemberTable& helper, const ibValue* ctx);

class BACKEND_API ibValueSize : public ibValueStaticMembers<&ibValueSize_BindNames> {
	public:

	ibValueSize();
	ibValueSize(const wxSize& size);
	virtual ~ibValueSize() {}

	virtual bool Init(ibValue** paParams, const long lSizeArray);
	virtual wxString GetString() const {
		return typeConv::SizeToString(m_size);
	}

	virtual bool IsEmpty() const {
		return m_size == wxDefaultSize;
	}

	virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal);        //setting attribute
	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal);                   //attribute value
	// DoGetPMethods (protected) + Shared<&ibValueSize_BindNames> come from the base.

	operator wxSize() const { return m_size; }

public:
	wxSize m_size;
};

#endif
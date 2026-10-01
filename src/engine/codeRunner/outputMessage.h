#ifndef _OUTPUT_MESSAGE_
#define _OUTPUT_MESSAGE_

#include "backend/compiler/value.h"

class ibValueOutput : public ibValueDynamicMembers
{
	public:

	ibValueOutput() : ibValueDynamicMembers(ibValueTypes::TYPE_VALUE, true) {
		m_members.Bind(this, &ibValueOutput::FillMembers);
	}

	virtual ~ibValueOutput() {
	}

	//****************************************************************************
	//*                              Support methods                             *
	//****************************************************************************

	void FillMembers(ibMemberTable& helper) const;   // bound in ctor (was PrepareNames)
	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray);

	//check is empty
	virtual bool IsEmpty() const {
		return false;
	}
};

#endif 
#ifndef _OUTPUT_MESSAGE_
#define _OUTPUT_MESSAGE_

#include "backend/compiler/value.h"

class ibValueOutput : public ibValue
{
	public:

	ibValueOutput() :
		ibValue(ibValueTypes::TYPE_VALUE, true), m_methodHelper(new ibValueMethodHelper) {
	}

	virtual ~ibValueOutput() {
		wxDELETE(m_methodHelper);
	}

	//****************************************************************************
	//*                              Support methods                             *
	//****************************************************************************

	virtual ibValueMethodHelper* GetPMethods() const { // get a reference to the class helper for parsing attribute and method names
		//PrepareNames(); 
		return m_methodHelper;
	}

	virtual void PrepareNames() const;
	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray);

	//check is empty
	virtual bool IsEmpty() const {
		return false;
	}

protected:

	ibValueMethodHelper* m_methodHelper;
};

#endif 
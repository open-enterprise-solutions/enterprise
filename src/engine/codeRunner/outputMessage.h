#ifndef _OUTPUT_MESSAGE_
#define _OUTPUT_MESSAGE_

#include "backend/compiler/value/value.h"

class CValueOutput : public CValue
{
public:

	CValueOutput() :
		CValue(eValueTypes::TYPE_VALUE, true), m_methodHelper(new CMethodHelper) {
	}

	virtual ~CValueOutput() {
		wxDELETE(m_methodHelper);
	}

	//****************************************************************************
	//*                              Support methods                             *
	//****************************************************************************

	virtual CMethodHelper* GetPMethods() const { //получить ссылку на класс помощник разбора имен атрибутов и методов
		//PrepareNames(); 
		return m_methodHelper;
	}

	virtual void PrepareNames() const;
	virtual bool CallAsFunc(const long lMethodNum, CValue& pvarRetValue, CValue** paParams, const long lSizeArray);

	//check is empty
	virtual inline bool IsEmpty() const {
		return false;
	}

protected:

	CMethodHelper* m_methodHelper;
};

#endif 
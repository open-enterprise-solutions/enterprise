#ifndef __ENUM_FACTORY_H__
#define __ENUM_FACTORY_H__

#include "value.h"

//realization factory pattern 
class ibValueEnumFactory : public ibValue {
	public:

	ibValueEnumFactory();
	virtual ~ibValueEnumFactory();

	// these methods need to be overridden in your aggregate objects:
	virtual ibValueMethodHelper* GetPMethods() const { // get a reference to the class helper for parsing attribute and method names	
		//PrepareNames(); 
		return m_methodHelper; 
	}

	virtual void PrepareNames() const; // this method is automatically called to initialize attribute and method names.

	// this method is automatically called to initialize attribute and method names.
	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal);                   //attribute value

protected:
	ibValueMethodHelper *m_methodHelper;
};

#endif 
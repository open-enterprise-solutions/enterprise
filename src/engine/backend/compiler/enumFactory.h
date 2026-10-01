#ifndef __ENUM_FACTORY_H__
#define __ENUM_FACTORY_H__

#include "value.h"

//realization factory pattern 
class ibValueEnumFactory : public ibValueDynamicMembers {
	public:

	ibValueEnumFactory();
	virtual ~ibValueEnumFactory();

	// Surface = the registered enum classes; rebuilt from the global ctor registry
	// each Build(). Bound in the ctor. (Helper + DoGetPMethods come from the base.)
	void FillMembers(ibMemberTable& helper) const;

	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal);                   //attribute value
};

#endif 
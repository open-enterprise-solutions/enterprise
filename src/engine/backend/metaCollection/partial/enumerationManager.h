#ifndef _ENUMERATION_MANAGER_H__
#define _ENUMERATION_MANAGER_H__

#include "enumeration.h"

class ibValueManagerDataObjectEnumeration :
	public ibValueManagerDataObject {
	public:

	ibValueManagerDataObjectEnumeration(const ibValueMetaObjectEnumeration* metaObject = nullptr) : m_metaObject(metaObject) {}
	virtual ~ibValueManagerDataObjectEnumeration() {}

	virtual const ibValueMetaObjectCommonModule* GetManagerModule() const;
	virtual const ibValueMetaObjectEnumeration* GetMetaObject() const { return m_metaObject; }

	virtual void PrepareNames() const;                         // this method is automatically called to initialize attribute and method names.
	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray);//method call

	virtual bool SetPropVal(const long lPropNum, ibValue& varPropVal);        //setting attribute
	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal);                   //attribute value

protected:
	const ibValueMetaObjectEnumeration* m_metaObject;
private:
};

#endif 
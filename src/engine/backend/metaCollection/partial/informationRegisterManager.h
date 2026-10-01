#ifndef _INFO_REGISTER_MANAGER_H__
#define _INFO_REGISTER_MANAGER_H__

#include "informationRegister.h"

class ibValueManagerDataObjectInformationRegister :
	public ibValueManagerDataObject {
	public:

	ibValue Get(const ibValue& cFilter = ibValue());
	ibValue Get(const ibValue& cPeriod, const ibValue& cFilter);

	ibValue GetFirst(const ibValue& cPeriod, const ibValue& cFilter = ibValue());
	ibValue GetLast(const ibValue& cPeriod, const ibValue& cFilter = ibValue());

	ibValue SliceFirst(const ibValue& cPeriod, const ibValue& cFilter = ibValue());
	ibValue SliceLast(const ibValue& cPeriod, const ibValue& cFilter = ibValue());

	ibValueManagerDataObjectInformationRegister(const ibValueMetaObjectInformationRegister* metaObject = nullptr) : m_metaObject(metaObject) { m_members.Bind(this, &ibValueManagerDataObjectInformationRegister::FillManagerMethods); }
	virtual ~ibValueManagerDataObjectInformationRegister() {}

	virtual const ibValueMetaObjectCommonModule* GetManagerModule() const;
	virtual const ibValueMetaObjectInformationRegister* GetMetaObject() const { return m_metaObject; }

	void FillManagerMethods(ibMemberTable& helper) const;
	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray); //method call

protected:
	const ibValueMetaObjectInformationRegister* m_metaObject;
private:
};

#endif 
#ifndef _CONSTANTS_MANAGER_H__
#define _CONSTANTS_MANAGER_H__

#include "constant.h"

class ibValueManagerDataObjectConstant :
	public ibValueManagerObject {
	public:

	ibValueManagerDataObjectConstant(ibValueMetaObjectConstant* metaConst = nullptr) : m_metaObject(metaConst) {
		m_members.Bind(this, &ibValueManagerDataObjectConstant::FillManagerMethods);
	}
	virtual ~ibValueManagerDataObjectConstant() {}

	virtual const ibValueMetaObjectConstant* GetMetaObject() const { return m_metaObject; }

	void FillManagerMethods(ibMemberTable& helper) const;
	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray);

	//Get ref class
	virtual ibClassID GetClassType() const;

	//types
	virtual wxString GetClassName() const;
	virtual wxString GetString() const;

protected:
	const ibValueMetaObjectConstant* m_metaObject;
private:
};


#endif // !_CONSTANTS_MANAGER_H__

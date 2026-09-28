#ifndef _CONSTANTS_MANAGER_H__
#define _CONSTANTS_MANAGER_H__

#include "backend/metaCollection/partial/commonObject.h"   // ibValueManagerObject — what a manager is
#include "backend/metaCollection/metaStoredValueObject.h"  // every stored value has this manager — a constant's and an option's alike

class ibValueManagerDataObjectConstant :
	public ibValueManagerObject {
	public:

	ibValueManagerDataObjectConstant(ibValueMetaObjectStoredValue* metaConst = nullptr) : m_metaObject(metaConst) {
		m_members.Bind(this, &ibValueManagerDataObjectConstant::FillManagerMethods);
	}
	virtual ~ibValueManagerDataObjectConstant() {}

	virtual const ibValueMetaObjectStoredValue* GetMetaObject() const { return m_metaObject; }

	void FillManagerMethods(ibMemberTable& helper) const;
	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray);

	//Get ref class
	virtual ibClassID GetClassType() const;

	//types
	virtual wxString GetClassName() const;
	virtual ibString GetString() const;

protected:
	const ibValueMetaObjectStoredValue* m_metaObject;
private:
};


#endif // !_CONSTANTS_MANAGER_H__

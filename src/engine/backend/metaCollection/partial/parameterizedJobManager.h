#ifndef _MANAGER_PARAMETERIZED_JOB_H__
#define _MANAGER_PARAMETERIZED_JOB_H__

// The manager of a parameterized job — a catalog manager plus one verb.
//
// It is where the EXECUTION lives, and that is not a stylistic choice: the scheduler knows the
// METAOBJECT, never a row, so the entry point is a type-level operation and the reference is its
// argument. A manager module also registers with the module manager, so the handler is compiled
// once per session (not once per row), its module-level variables live for the whole session, and
// it is resolvable by name — which is what lets the same entry be called by hand while debugging
// instead of waiting out an interval.

#include "parameterizedJob.h"

class ibValueManagerDataObjectJob :
	public ibValueManagerDataObjectPredefined {
	public:

	ibValueReferenceDataObject* FindByCode(const ibValue& vCode) const;
	ibValueReferenceDataObject* FindByDescription(const ibValue& cParam) const;

	ibValueReferenceDataObject* EmptyRef() const;

	ibValueManagerDataObjectJob(const ibValueMetaObjectParameterizedJob* metaObject = nullptr) : m_metaObject(metaObject) {
		m_members.Bind(this, &ibValueManagerDataObjectJob::FillManagerMethods);
	}
	virtual ~ibValueManagerDataObjectJob() {}

	virtual const ibValueMetaObjectCommonModule* GetManagerModule() const;
	virtual const ibValueMetaObjectParameterizedJob* GetMetaObject() const { return m_metaObject; }

	void FillManagerMethods(ibMemberTable& helper) const;
	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray);//method call

protected:
	const ibValueMetaObjectParameterizedJob* m_metaObject;
private:
};

#endif // !_MANAGER_PARAMETERIZED_JOB_H__

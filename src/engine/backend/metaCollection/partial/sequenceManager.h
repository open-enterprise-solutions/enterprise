#ifndef _SEQUENCE_MANAGER_H__
#define _SEQUENCE_MANAGER_H__

#include "sequence.h"

// `Sequences.<Name>` — where the border is read and said.
//
// The two verbs are the whole manager, and they are deliberate ones: `GetBorder` answers the moment
// a key has got to, `SetBorder` says it outright ("up to here we consider everything in order").
// Everything else about a sequence happens where the rows are written — the document's posting
// handler — or by itself, when the engine moves the border along the registrations that are already
// there (docs/private/sequence-arc.md).
//
// (No Restore: reposting everything after the border is a loop with filters, transactions and
// progress, and the platform has scheduled jobs to run a configuration's own loop in.)
class ibValueManagerDataObjectSequence : public ibValueManagerDataObject {
public:

	ibValueManagerDataObjectSequence(const ibValueMetaObjectSequence* metaObject = nullptr) : m_metaObject(metaObject) {
		m_members.Bind(this, &ibValueManagerDataObjectSequence::FillManagerMethods);
	}
	virtual ~ibValueManagerDataObjectSequence() {}

	// The sequence's own manager module: its Public methods are this manager's too, beside the two verbs.
	virtual const ibValueMetaObjectCommonModule* GetManagerModule() const override { return m_metaObject->GetManagerModule(); }
	virtual const ibValueMetaObjectSequence* GetMetaObject() const override { return m_metaObject; }

	void FillManagerMethods(ibMemberTable& helper) const;
	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray) override;

protected:
	const ibValueMetaObjectSequence* m_metaObject;
};

#endif // !_SEQUENCE_MANAGER_H__

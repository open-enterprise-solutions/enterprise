#ifndef _SEQUENCE_MANAGER_H__
#define _SEQUENCE_MANAGER_H__

#include "sequence.h"

// `Sequences.<Name>` — where the border is read and said.
//
// The border's two verbs are deliberate ones: `GetBorder` answers the moment a key has got to,
// `SetBorder` says it outright ("up to here we consider everything in order"). Everything else about
// a sequence happens where the rows are written — the document's posting handler — or by itself, when
// the engine moves the border along the registrations that are already there
// (docs/private/sequence-arc.md).
//
// `Restore` is the third, and it adds no rule of its own: it posts the documents after the border
// again, in their order, and the border follows them by the rule it always follows (ibSequenceRestore).
// Its loop is the platform's because the order, the stop at the first failure and the transaction per
// document are what make it safe, and a configuration writing that loop itself gets them wrong.
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

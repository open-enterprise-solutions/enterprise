#ifndef _DOCUMENT_JOURNAL_MANAGER_H__
#define _DOCUMENT_JOURNAL_MANAGER_H__

#include "documentJournal.h"

// `DocumentJournals.<Name>` — the documents listed, read in date order, and the list form.
//
// No module: a journal holds no code of its own, so its manager is exactly its verbs.
class ibValueManagerDataObjectDocumentJournal : public ibValueManagerDataObject {
public:

	ibValueManagerDataObjectDocumentJournal(const ibValueMetaObjectDocumentJournal* metaObject = nullptr) : m_metaObject(metaObject) {
		m_members.Bind(this, &ibValueManagerDataObjectDocumentJournal::FillManagerMethods);
	}
	virtual ~ibValueManagerDataObjectDocumentJournal() {}

	virtual const ibValueMetaObjectCommonModule* GetManagerModule() const override { return nullptr; }
	virtual const ibValueMetaObjectDocumentJournal* GetMetaObject() const override { return m_metaObject; }

	void FillManagerMethods(ibMemberTable& helper) const;
	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray) override;

protected:
	const ibValueMetaObjectDocumentJournal* m_metaObject;
};

#endif // !_DOCUMENT_JOURNAL_MANAGER_H__

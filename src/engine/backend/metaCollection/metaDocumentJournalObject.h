#ifndef __META_DOCUMENT_JOURNAL_OBJECT_H__
#define __META_DOCUMENT_JOURNAL_OBJECT_H__
#include "metaObject.h"
#include "backend/propertyManager/property/propertyString.h"
#include <memory>
// A document journal gathers the documents it names. Select reads their references.
// A query reads the same rows as DocumentJournal.<Name> (Ref, Date, Number).
class ibDocumentJournalSource;
class BACKEND_API ibValueMetaObjectDocumentJournal : public ibValueMetaObject {
public:
	static constexpr unsigned s_features = ibMetaFeature_Manager;
	ibValueMetaObjectDocumentJournal();
	virtual ~ibValueMetaObjectDocumentJournal();
	virtual ibClassID ResolveChild(const ibClassID&) const override { return 0; }
	wxString GetContent() const { return m_propertyContent->GetValueAsString(); }
	void SetContent(const wxString& content) { m_propertyContent->SetValue(content); }
	// The references of the documents this journal lists, one per written row.
	ibValue Select() const;
	virtual bool OnBeforeRunMetaObject(int flags) override;
	virtual bool OnAfterRunMetaObject(int flags) override;
	virtual bool OnBeforeCloseMetaObject() override;
	virtual bool OnRenameMetaObject(const wxString& newName) override;
	virtual bool OnDeleteMetaObject() override;
protected:
	virtual bool ReadData(const ibDataNode& node) override;
	virtual bool WriteData(ibDataNode& node) const override;
private:
	void Reindex(const wxString& name);
	ibPropertyCategory* m_categoryContent = ibPropertyObject::CreatePropertyCategory(wxT("Content"), _("Content"));
	ibPropertyString* m_propertyContent = ibPropertyObject::CreateProperty<ibPropertyString>(m_categoryContent, wxT("Content"), _("Content"), _("The documents this journal lists, one per line. Document.Name or Name."), wxEmptyString);
	std::unique_ptr<ibDocumentJournalSource> m_source;
};
#endif

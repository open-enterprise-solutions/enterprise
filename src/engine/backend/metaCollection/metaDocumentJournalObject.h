#ifndef __META_DOCUMENT_JOURNAL_OBJECT_H__
#define __META_DOCUMENT_JOURNAL_OBJECT_H__
#include "metaObject.h"
#include "backend/propertyManager/property/propertyString.h"
// A document journal names the documents it gathers. Select and the query source stay out of this slice.
class BACKEND_API ibValueMetaObjectDocumentJournal : public ibValueMetaObject {
public:
	static constexpr unsigned s_features = ibMetaFeature_Manager;
	ibValueMetaObjectDocumentJournal() : ibValueMetaObject() {}
	virtual ibClassID ResolveChild(const ibClassID&) const override { return 0; }
	wxString GetContent() const { return m_propertyContent->GetValueAsString(); }
	void SetContent(const wxString& content) { m_propertyContent->SetValue(content); }
	virtual bool OnBeforeRunMetaObject(int flags) override;
	virtual bool OnDeleteMetaObject() override;
protected:
	virtual bool ReadData(const ibDataNode& node) override;
	virtual bool WriteData(ibDataNode& node) const override;
private:
	ibPropertyCategory* m_categoryContent = ibPropertyObject::CreatePropertyCategory(wxT("Content"), _("Content"));
	ibPropertyString* m_propertyContent = ibPropertyObject::CreateProperty<ibPropertyString>(m_categoryContent, wxT("Content"), _("Content"), _("The documents this journal lists, one per line. Document.Name or Name."), wxEmptyString);
};
#endif

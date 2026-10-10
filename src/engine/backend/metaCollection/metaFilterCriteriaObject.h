#ifndef __META_FILTER_CRITERIA_OBJECT_H__
#define __META_FILTER_CRITERIA_OBJECT_H__
#include "metaObject.h"
#include "backend/propertyManager/property/propertyString.h"
class BACKEND_API ibValueMetaObjectFilterCriteria : public ibValueMetaObject {
public:
	static constexpr unsigned s_features = ibMetaFeature_Manager;
	ibValueMetaObjectFilterCriteria() : ibValueMetaObject() {}
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
	ibPropertyString* m_propertyContent = ibPropertyObject::CreateProperty<ibPropertyString>(m_categoryContent, wxT("Content"), _("Content"), _("The attributes this criterion searches, one per line. Document.Name.Attribute or Catalog.Name.Attribute. A line may also say Document.Name.Attribute.Field."), wxEmptyString);
};
#endif

#ifndef _METAGRIDOBJECT_H__
#define _METAGRIDOBJECT_H__

#include "metaObject.h"

class BACKEND_API ibValueMetaObjectSpreadsheetBase : public ibValueMetaObject {
	public:
protected:

public:

	//set spreadsheet code 
	virtual void SetSpreadsheetDesc(const ibSpreadsheetDescription& spreadsheetDescription) = 0;
	virtual ibSpreadsheetDescription& GetSpreadsheetDesc() const = 0;

	//support icons
	virtual ibPicture GetIcon() const;
	static ibPicture GetIconGroup();

	//module manager is started or exit 
	virtual bool OnBeforeRunMetaObject(int flags);
	virtual bool OnAfterCloseMetaObject();

	//prepare menu for item
	virtual bool CollectContextMenu(std::vector<ibMetaMenuItem>& items);
};

class BACKEND_API ibValueMetaObjectSpreadsheet : public ibValueMetaObjectSpreadsheetBase {
	public:
	//set spreadsheet code 
	virtual void SetSpreadsheetDesc(const ibSpreadsheetDescription& spreadsheetDescription) { m_propertyTemplate->SetValue(spreadsheetDescription); }
	virtual ibSpreadsheetDescription& GetSpreadsheetDesc() const { return m_propertyTemplate->GetValueAsSpreadsheetDesc(); }

	virtual bool ReadData(const ibDataNode& node) override;
	virtual bool WriteData(ibDataNode& node) const override;
protected:
private:
	ibPropertyCategory* m_categoryTemplate = ibPropertyObject::CreatePropertyCategory(ibT("Template"), _("Template"));
	ibPropertySpreadsheet* m_propertyTemplate = ibPropertyObject::CreateProperty<ibPropertySpreadsheet>(m_categoryTemplate, ibT("TemplateData"), _("Template data"),
		_("The template's spreadsheet: cells, named areas and parameters a print form is assembled from. Code gets it with the owner's GetTemplate(name), takes areas by name, fills their parameters and puts them into an output spreadsheet."));
};

class BACKEND_API ibValueMetaObjectCommonSpreadsheet : public ibValueMetaObjectSpreadsheetBase {
	public:
	//set spreadsheet code 
	virtual void SetSpreadsheetDesc(const ibSpreadsheetDescription& spreadsheetDescription) { m_propertyTemplate->SetValue(spreadsheetDescription); }
	virtual ibSpreadsheetDescription& GetSpreadsheetDesc() const { return m_propertyTemplate->GetValueAsSpreadsheetDesc(); }

	virtual bool ReadData(const ibDataNode& node) override;
	virtual bool WriteData(ibDataNode& node) const override;
protected:
private:
	ibPropertyCategory* m_categoryTemplate = ibPropertyObject::CreatePropertyCategory(ibT("CommonTemplate"), _("Common template"));
	ibPropertySpreadsheet* m_propertyTemplate = ibPropertyObject::CreateProperty<ibPropertySpreadsheet>(m_categoryTemplate, ibT("TemplateData"), _("Template data"),
		_("The common template's spreadsheet: cells, named areas and parameters shared by the whole configuration. Code gets it with GetCommonTemplate(name), takes areas by name, fills their parameters and puts them into an output spreadsheet."));
};

#endif 
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
	virtual wxIcon GetIcon() const;
	static wxIcon GetIconGroup();

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
	ibPropertyCategory* m_categoryTemplate = ibPropertyObject::CreatePropertyCategory(wxT("Template"), _("Template"));
	ibPropertySpreadsheet* m_propertyTemplate = ibPropertyObject::CreateProperty<ibPropertySpreadsheet>(m_categoryTemplate, wxT("TemplateData"), _("Template data"));
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
	ibPropertyCategory* m_categoryTemplate = ibPropertyObject::CreatePropertyCategory(wxT("CommonTemplate"), _("Common template"));
	ibPropertySpreadsheet* m_propertyTemplate = ibPropertyObject::CreateProperty<ibPropertySpreadsheet>(m_categoryTemplate, wxT("TemplateData"), _("Template data"));
};

#endif 
#ifndef _METAGRIDOBJECT_H__
#define _METAGRIDOBJECT_H__

#include "metaObject.h"

class BACKEND_API ibValueMetaObjectSpreadsheetBase : public ibValueMetaObject {
	wxDECLARE_ABSTRACT_CLASS(ibValueMetaObjectSpreadsheetBase);
protected:
	enum
	{
		ID_METATREE_OPEN_TEMPLATE = 19000,
	};

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
	virtual bool PrepareContextMenu(wxMenu* defaultMenu);
	virtual void ProcessCommand(unsigned int id);
};

class BACKEND_API ibValueMetaObjectSpreadsheet : public ibValueMetaObjectSpreadsheetBase {
	wxDECLARE_DYNAMIC_CLASS(ibValueMetaObjectCommonSpreadsheet);
public:
	//set spreadsheet code 
	virtual void SetSpreadsheetDesc(const ibSpreadsheetDescription& spreadsheetDescription) { m_propertyTemplate->SetValue(spreadsheetDescription); }
	virtual ibSpreadsheetDescription& GetSpreadsheetDesc() const { return m_propertyTemplate->GetValueAsSpreadsheetDesc(); }
protected:
	virtual bool LoadData(ibReaderMemory& reader);
	virtual bool SaveData(ibWriterMemory& writer);
private:
	ibPropertyCategory* m_categoryTemplate = ibPropertyObject::CreatePropertyCategory(wxT("Template"), _("Template"));
	ibPropertySpreadsheet* m_propertyTemplate = ibPropertyObject::CreateProperty<ibPropertySpreadsheet>(m_categoryTemplate, wxT("TemplateData"), _("Template data"));
};

class BACKEND_API ibValueMetaObjectCommonSpreadsheet : public ibValueMetaObjectSpreadsheetBase {
	wxDECLARE_DYNAMIC_CLASS(ibValueMetaObjectCommonSpreadsheet);
public:
	//set spreadsheet code 
	virtual void SetSpreadsheetDesc(const ibSpreadsheetDescription& spreadsheetDescription) { m_propertyTemplate->SetValue(spreadsheetDescription); }
	virtual ibSpreadsheetDescription& GetSpreadsheetDesc() const { return m_propertyTemplate->GetValueAsSpreadsheetDesc(); }
protected:
	virtual bool LoadData(ibReaderMemory& reader);
	virtual bool SaveData(ibWriterMemory& writer);
private:
	ibPropertyCategory* m_categoryTemplate = ibPropertyObject::CreatePropertyCategory(wxT("CommonTemplate"), _("Common template"));
	ibPropertySpreadsheet* m_propertyTemplate = ibPropertyObject::CreateProperty<ibPropertySpreadsheet>(m_categoryTemplate, wxT("TemplateData"), _("Template data"));
};

#endif 
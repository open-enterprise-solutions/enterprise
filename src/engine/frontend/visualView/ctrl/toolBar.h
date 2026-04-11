#ifndef _TOOLBAR_H__
#define _TOOLBAR_H__

#include "window.h"
#include "frontend/win/ctrls/toolBar.h"

class ibValueToolBarItem;
class ibValueToolBarSeparator;

//********************************************************************************************
//*                                 define commom clsid									     *
//********************************************************************************************

//COMMON FORM
const ibClassID g_controlToolBarCLSID = string_to_clsid("CT_TLBR");
const ibClassID g_controlToolBarItemCLSID = string_to_clsid("CT_TLIT");
const ibClassID g_controlToolBarSeparatorCLSID = string_to_clsid("CT_TLIS");

//********************************************************************************************
//*                                 Value Toolbar                                            *
//********************************************************************************************

class ibValueToolbar : public ibValueWindow {
	wxDECLARE_DYNAMIC_CLASS(ibValueToolbar);
public:

	void SetActionSrc(const ibFormID& action) { return m_actSource->SetValue(action); }
	ibFormID GetActionSrc() const { return m_actSource->GetValueAsInteger(); }

	ibValueToolbar();

	//get title
	virtual wxString GetControlTitle() const {

		if (!m_actSource->IsEmptyProperty()) {
			ibValue pvarPropVal;
			if (m_actSource->GetDataValue(pvarPropVal))
				return _("ToolBar") + wxT(": ") + stringUtils::GenerateSynonym(pvarPropVal.GetClassName());
		}

		return _("ToolBar") + wxT(": ") + _("<empty source>");
	}

	//control factory 
	virtual wxObject* Create(wxWindow* wxparent, ibVisualHost* visualHost) override;
	virtual void OnCreated(wxObject* wxobject, wxWindow* wxparent, ibVisualHost* visualHost, bool firstСreated) override;
	virtual void Update(wxObject* wxobject, ibVisualHost* visualHost) override;
	virtual void OnUpdated(wxObject* wxobject, wxWindow* wxparent, ibVisualHost* visualHost) override;
	virtual void Cleanup(wxObject* obj, ibVisualHost* visualHost) override;

	//support icons
	virtual wxIcon GetIcon() const;
	static wxIcon GetIconGroup();

	/**
	* Property events
	*/
	virtual void OnPropertyCreated(ibProperty* property);
	virtual void OnPropertySelected(ibProperty* property);
	virtual void OnPropertyChanged(ibProperty* property, const wxVariant& oldValue, const wxVariant& newValue);

	//load & save object in control 
	virtual bool LoadData(ibReaderMemory& reader);
	virtual bool SaveData(ibWriterMemory writer = ibWriterMemory());

	/**
	* Support default menu
	*/
	virtual void PrepareDefaultMenu(wxMenu* m_menu);
	virtual void ExecuteMenu(ibVisualHost* visualHost, int id);

	//specific function 
	void AddToolItem();
	void AddToolSeparator();

	//array of the commands 
	const ibActionCollection& GetActionArray() const { return m_actionArray; }

protected:

	//events 
	void OnToolBarLeftDown(wxMouseEvent& event);
	void OnTool(wxCommandEvent& event);
	void OnToolDropDown(wxAuiToolBarEvent& event);
	void OnRightDown(wxMouseEvent& event);

private:

	bool GetActionSource(ibPropertyList*);

	//storage for action array 
	ibActionCollection m_actionArray;

	ibPropertyCategory* m_categoryAction = ibPropertyObject::CreatePropertyCategory(wxT("Action"), _("Toolbar"));
	ibPropertyList* m_actSource = ibPropertyObject::CreateProperty<ibPropertyList>(m_categoryAction, wxT("ActionSource"), _("Source"), &ibValueToolbar::GetActionSource, wxNOT_FOUND);

	friend class ibValueForm;
};

#include "frontend/artProvider/null/null.xpm"

class ibValueToolBarItem : public ibValueControl {
	wxDECLARE_DYNAMIC_CLASS(ibValueToolBarItem);

public:

	void SetCaption(const wxString& caption) { return m_propertyTitle->SetValue(caption); }
	wxString GetCaption() const { return m_propertyTitle->GetValueAsTranslateString(); }

	void SetToolTip(const wxString& caption) { return m_properyTooltip->SetValue(caption); }
	wxString GetToolTip() const { return m_properyTooltip->GetValueAsTranslateString(); }

	void SetAction(const ibActionDescription& action) { return m_eventAction->SetValue(action); }
	const ibActionDescription& GetAction() const { return m_eventAction->GetValueAsActionDesc(); }

	///////////////////////////////////////////////////////////////////////

	ibValueToolbar* GetOwner() const { return m_parent->ConvertToType<ibValueToolbar>(); }

#pragma region __tool_item_desc_h__
	wxBitmap GetItemPicture(const ibActionCollection& collection) const {
		const ibActionDescription& actionDesc = m_eventAction->GetValueAsActionDesc();
		if (m_propertyPicture->IsEmptyProperty()) {
			const ibActionID selected = actionDesc.GetSystemAction();
			if (selected != wxNOT_FOUND) {
				for (unsigned int i = 0; i < collection.GetCount(); i++) {
					const ibActionID& id = collection.GetID(i);
					if (selected == collection.GetID(i)) {
						const ibPictureDescription& pictureDesc = collection.GetPictureByID(actionDesc.GetSystemAction());
						if (pictureDesc.IsEmptyPicture())
							return wxNullBitmap;
						return ibBackendPicture::CreatePicture(pictureDesc, GetMetaData());
					}
				}
			}
			else if (m_propertyTitle->IsEmptyProperty()) {
				return wxBitmap(s_null_xpm);
			}
		}

		return m_propertyPicture->GetValueAsBitmap();
	}

	wxString GetItemCaption(const ibActionCollection& collection) const {
		const ibActionDescription& actionDesc = m_eventAction->GetValueAsActionDesc();
		if (m_propertyTitle->IsEmptyProperty()) {
			const ibActionID selected = actionDesc.GetSystemAction();
			if (selected != wxNOT_FOUND) {
				for (unsigned int i = 0; i < collection.GetCount(); i++) {
					const ibActionID& id = collection.GetID(i);
					if (selected == collection.GetID(i)) {
						return collection.GetCaptionByID(selected);
					}
				}
			}
		}
		return m_propertyTitle->GetValueAsTranslateString();
	}

	wxString GetItemToolTip(const ibActionCollection& collection) const {
		const ibActionDescription& actionDesc = m_eventAction->GetValueAsActionDesc();
		if (m_properyTooltip->IsEmptyProperty()) {
			const ibActionID selected = actionDesc.GetSystemAction();
			if (selected != wxNOT_FOUND) {
				for (unsigned int i = 0; i < collection.GetCount(); i++) {
					const ibActionID& id = collection.GetID(i);
					if (selected == collection.GetID(i)) {
						return collection.GetCaptionByID(selected);
					}
				}
			}
		}
		return m_properyTooltip->GetValueAsTranslateString();
	}

	ibRepresentation GetItemRepresentation(const ibActionCollection& collection) const {
		const ibActionDescription& actionDesc = m_eventAction->GetValueAsActionDesc();
		if (m_propertyPicture->IsEmptyProperty()) {
			const ibActionID selected = actionDesc.GetSystemAction();
			if (selected != wxNOT_FOUND) {
				for (unsigned int i = 0; i < collection.GetCount(); i++) {
					const ibActionID& id = collection.GetID(i);
					if (selected == collection.GetID(i)) {
						const ibPictureDescription& pictureDesc = collection.GetPictureByID(actionDesc.GetSystemAction());
						if (pictureDesc.IsEmptyPicture())
							return ibRepresentation::ibRepresentation_PictureAndText;
						return collection.IsCreatePictureAndText(id) ?
							ibRepresentation::ibRepresentation_PictureAndText : ibRepresentation::ibRepresentation_Picture;
					}
				}
			}
		}
		return ibRepresentation::ibRepresentation_PictureAndText;
	}

#pragma endregion 

	///////////////////////////////////////////////////////////////////////

	ibValueToolBarItem();

	//get title
	virtual wxString GetControlTitle() const {
		if (!m_propertyTitle->IsEmptyProperty())
			return GetCaption();
		return _("<empty caption>");
	}

	//control factory
	virtual void OnCreated(wxObject* wxobject, wxWindow* wxparent, ibVisualHost* visualHost, bool firstСreated) override;
	virtual void OnUpdated(wxObject* wxobject, wxWindow* wxparent, ibVisualHost* visualHost) override;
	virtual void Cleanup(wxObject* obj, ibVisualHost* visualHost) override;

	virtual bool CanDeleteControl() const;
	virtual int GetComponentType() const { return COMPONENT_TYPE_ABSTRACT; }

	//support icons
	virtual wxIcon GetIcon() const;
	static wxIcon GetIconGroup();

	//load & save object in control 
	virtual bool LoadData(ibReaderMemory& reader);
	virtual bool SaveData(ibWriterMemory writer = ibWriterMemory());

private:
	bool GetToolAction(ibEventAction* evtList);
private:

	ibPropertyCategory* m_categoryToolbar = ibPropertyObject::CreatePropertyCategory(wxT("ToolBarItem"), _("Item"));

	ibPropertyTString* m_propertyTitle = ibPropertyObject::CreateProperty<ibPropertyTString>(m_categoryToolbar, wxT("Title"), _("Title"), wxT(""));
	ibPropertyEnum<ibValueEnumRepresentation>* m_propertyRepresentation = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumRepresentation>>(m_categoryToolbar, wxT("Representation"), _("Representation"), ibRepresentation::ibRepresentation_Auto);
	ibPropertyPicture* m_propertyPicture = ibPropertyObject::CreateProperty<ibPropertyPicture>(m_categoryToolbar, wxT("Picture"), _("Picture"));
	ibPropertyBoolean* m_propertyContextMenu = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categoryToolbar, wxT("ContextMenu"), _("Context menu"), false);
	ibPropertyTString* m_properyTooltip = ibPropertyObject::CreateProperty<ibPropertyTString>(m_categoryToolbar, wxT("Tooltip"), _("Tooltip"), wxEmptyString);
	ibPropertyBoolean* m_propertyEnabled = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categoryToolbar, wxT("Enabled"), _("Enabled"), true);

	ibEventAction* m_eventAction = ibPropertyObject::CreateEvent<ibEventAction>(m_categoryToolbar, wxT("Action"), _("Action"), wxArrayString{ wxT("Control") }, &ibValueToolBarItem::GetToolAction, wxNOT_FOUND);

	friend class ibValueForm;
	friend class ibValueToolbar;
};

class ibValueToolBarSeparator : public ibValueControl {
	wxDECLARE_DYNAMIC_CLASS(ibValueToolBarSeparator);
public:

	///////////////////////////////////////////////////////////////////////

	ibValueToolbar* GetOwner() const {
		return m_parent->ConvertToType<ibValueToolbar>();
	}

	///////////////////////////////////////////////////////////////////////

	ibValueToolBarSeparator();

	//get title
	virtual wxString GetControlTitle() const {
		return _("Separator");
	}

	//control factory
	virtual void OnCreated(wxObject* wxobject, wxWindow* wxparent, ibVisualHost* visualHost, bool firstСreated) override;
	virtual void OnUpdated(wxObject* wxobject, wxWindow* wxparent, ibVisualHost* visualHost) override;
	virtual void Cleanup(wxObject* obj, ibVisualHost* visualHost) override;

	virtual bool CanDeleteControl() const;

	virtual int GetComponentType() const { return COMPONENT_TYPE_ABSTRACT; }

	//support icons
	virtual wxIcon GetIcon() const;
	static wxIcon GetIconGroup();

	//load & save object in control 
	virtual bool LoadData(ibReaderMemory& reader);
	virtual bool SaveData(ibWriterMemory writer = ibWriterMemory());

	friend class ibValueForm;
	friend class ibValueToolbar;
};

#endif


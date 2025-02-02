#ifndef _TOOLBAR_H__
#define _TOOLBAR_H__

#include "window.h"
#include "frontend/win/ctrls/toolBar.h"

class CValueToolBarItem;
class CValueToolBarSeparator;

//********************************************************************************************
//*                                 define commom clsid									     *
//********************************************************************************************

//COMMON FORM
const class_identifier_t g_controlToolBarCLSID = string_to_clsid("CT_TLBR");
const class_identifier_t g_controlToolBarItemCLSID = string_to_clsid("CT_TLIT");
const class_identifier_t g_controlToolBarSeparatorCLSID = string_to_clsid("CT_TLIS");

//********************************************************************************************
//*                                 Value Toolbar                                            *
//********************************************************************************************

class CValueToolbar : public IValueWindow {
	wxDECLARE_DYNAMIC_CLASS(CValueToolbar);
protected:

	OptionList* GetActionSource(PropertyOption*);
	PropertyCategory* m_categoryAction = IPropertyObject::CreatePropertyCategory({ "action", _("action") });
	Property* m_actSource = IPropertyObject::CreateProperty(m_categoryAction, { "action_source", "action source" }, &CValueToolbar::GetActionSource, wxNOT_FOUND);
public:

	void SetActionSrc(const form_identifier_t& action) {
		return m_actSource->SetValue(action);
	}

	form_identifier_t GetActionSrc() const {
		return m_actSource->GetValueAsInteger();
	}

	CValueToolbar();

	virtual wxObject* Create(wxWindow* wxparent, IVisualHost* visualHost) override;
	virtual void OnCreated(wxObject* wxobject, wxWindow* wxparent, IVisualHost* visualHost, bool first—reated) override;
	virtual void Update(wxObject* wxobject, IVisualHost* visualHost) override;
	virtual void OnUpdated(wxObject* wxobject, wxWindow* wxparent, IVisualHost* visualHost) override;
	virtual void Cleanup(wxObject* obj, IVisualHost* visualHost) override;

	//support icons
	virtual wxIcon GetIcon() const;
	static wxIcon GetIconGroup();

	/**
	* Property events
	*/
	virtual void OnPropertyCreated(Property* property);
	virtual void OnPropertySelected(Property* property);
	virtual void OnPropertyChanged(Property* property, const wxVariant& oldValue, const wxVariant& newValue);

	//load & save object in control 
	virtual bool LoadData(CMemoryReader& reader);
	virtual bool SaveData(CMemoryWriter& writer = CMemoryWriter());

	/**
	* Support default menu
	*/
	virtual void PrepareDefaultMenu(wxMenu* m_menu);
	virtual void ExecuteMenu(IVisualHost* visualHost, int id);

	//specific function 
	void AddToolItem();
	void AddToolSeparator();

protected:

	//events 
	void OnToolBarLeftDown(wxMouseEvent& event);
	void OnTool(wxCommandEvent& event);
	void OnToolDropDown(wxAuiToolBarEvent& event);
	void OnRightDown(wxMouseEvent& event);

	friend class CValueForm;
};

class CValueToolBarItem : public IValueControl {
	wxDECLARE_DYNAMIC_CLASS(CValueToolBarItem);
private:

	OptionList* GetToolAction(EventAction* act);

private:

	PropertyCategory* m_categoryToolbar = IPropertyObject::CreatePropertyCategory({ "toolBarItem", _("toolBar item") });

	Property* m_propertyCaption = IPropertyObject::CreateProperty(m_categoryToolbar, "caption", PropertyType::PT_WXSTRING, _("New tool"));
	Property* m_propertyBitmap = IPropertyObject::CreateProperty(m_categoryToolbar, "bitmap", PropertyType::PT_BITMAP);
	Property* m_propertyContextMenu = IPropertyObject::CreateProperty(m_categoryToolbar, "context_menu", PropertyType::PT_BOOL, false);
	Property* m_properyTooltip = IPropertyObject::CreateProperty(m_categoryToolbar, "tooltip", PropertyType::PT_WXSTRING);
	Property* m_propertyEnabled = IPropertyObject::CreateProperty(m_categoryToolbar, "enabled", PropertyType::PT_BOOL, true);

	Event* m_eventAction = IPropertyObject::CreateEvent(m_categoryToolbar, "action", { "control" }, &CValueToolBarItem::GetToolAction);

public:

	void SetCaption(const wxString& caption) {
		return m_propertyCaption->SetValue(caption);
	}

	wxString GetCaption() const {
		return m_propertyCaption->GetValueAsString();
	}

	void SetAction(const wxString& action) {
		return m_eventAction->SetValue(action);
	}

	wxString GetAction() const {
		return m_eventAction->GetValue();
	}

	CValueToolBarItem();

	virtual void OnCreated(wxObject* wxobject, wxWindow* wxparent, IVisualHost* visualHost, bool first—reated) override;
	virtual void OnUpdated(wxObject* wxobject, wxWindow* wxparent, IVisualHost* visualHost) override;
	virtual void Cleanup(wxObject* obj, IVisualHost* visualHost) override;

	virtual bool CanDeleteControl() const;

	virtual int GetComponentType() const {
		return COMPONENT_TYPE_ABSTRACT;
	}

	//support icons
	virtual wxIcon GetIcon() const;
	static wxIcon GetIconGroup();

	//load & save object in control 
	virtual bool LoadData(CMemoryReader& reader);
	virtual bool SaveData(CMemoryWriter& writer = CMemoryWriter());

	friend class CValueForm;
	friend class CValueToolbar;
};

class CValueToolBarSeparator : public IValueControl {
	wxDECLARE_DYNAMIC_CLASS(CValueToolBarSeparator);
public:

	CValueToolBarSeparator();
	virtual void OnCreated(wxObject* wxobject, wxWindow* wxparent, IVisualHost* visualHost, bool first—reated) override;
	virtual void OnUpdated(wxObject* wxobject, wxWindow* wxparent, IVisualHost* visualHost) override;
	virtual void Cleanup(wxObject* obj, IVisualHost* visualHost) override;

	virtual bool CanDeleteControl() const;

	virtual int GetComponentType() const {
		return COMPONENT_TYPE_ABSTRACT;
	}

	//support icons
	virtual wxIcon GetIcon() const;
	static wxIcon GetIconGroup();

	//load & save object in control 
	virtual bool LoadData(CMemoryReader& reader);
	virtual bool SaveData(CMemoryWriter& writer = CMemoryWriter());

	friend class CValueForm;
	friend class CValueToolbar;
};

#endif


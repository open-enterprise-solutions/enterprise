#ifndef _TOOLBAR_H__
#define _TOOLBAR_H__

#include "window.h"
#include "frontend/controls/toolBar.h"

class CValueToolBarItem;
class CValueToolBarSeparator;

class CValueToolbar : public IValueWindow {
	wxDECLARE_DYNAMIC_CLASS(CValueToolbar);
public:
	wxSize m_bitmapsize;
	wxSize m_margins;
	int m_packing;
	int m_separation;
	int m_actionSource;
private:
	OptionList* GetActionSource(Property*);
public:

	CValueToolbar();

	virtual wxObject* Create(wxObject* parent, IVisualHost* visualHost) override;
	virtual void OnCreated(wxObject* wxobject, wxWindow* wxparent, IVisualHost* visualHost, bool first—reated) override;
	virtual void Update(wxObject* wxobject, IVisualHost* visualHost) override;
	virtual void OnUpdated(wxObject* wxobject, wxWindow* wxparent, IVisualHost* visualHost) override;
	virtual void Cleanup(wxObject* obj, IVisualHost* visualHost) override;

	virtual wxString GetClassName() const override { return wxT("toolbar"); }
	virtual wxString GetObjectTypeName() const override { return wxT("toolbar"); }

	/**
	* Property events
	*/
	virtual void OnPropertyCreated(Property* property);
	virtual void OnPropertySelected(Property* property);
	virtual void OnPropertyChanged(Property* property);

	//load & save object in control 
	virtual bool LoadData(CMemoryReader& reader);
	virtual bool SaveData(CMemoryWriter& writer = CMemoryWriter());

	//read&save propery 
	virtual void ReadProperty() override;
	virtual void SaveProperty() override;

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
	void OnTool(wxCommandEvent& event);
	void OnRightDown(wxMouseEvent& event);

	friend class CValueForm;
};

class CValueToolBarItem : public IValueControl
{
	wxDECLARE_DYNAMIC_CLASS(CValueToolBarItem);
private:

	wxString m_caption;
	wxBitmap m_bitmap;
	bool m_context_menu;
	wxString m_tooltip;

	wxString m_action;

	bool m_enabled;

private:

	OptionList* GetActions(Property*);

public:

	CValueToolBarItem();

	virtual void OnCreated(wxObject* wxobject, wxWindow* wxparent, IVisualHost* visualHost, bool first—reated) override;
	virtual void OnUpdated(wxObject* wxobject, wxWindow* wxparent, IVisualHost* visualHost) override;
	virtual void Cleanup(wxObject* obj, IVisualHost* visualHost) override;

	virtual wxString GetClassName() const override { return wxT("tool"); }
	virtual wxString GetObjectTypeName() const override { return wxT("tool"); }

	virtual bool CanDeleteControl() const;

	virtual int GetComponentType() const override { 
		return COMPONENT_TYPE_ABSTRACT; 
	}
	virtual bool IsItem() override { return false; }

	//load & save object in control 
	virtual bool LoadData(CMemoryReader& reader);
	virtual bool SaveData(CMemoryWriter& writer = CMemoryWriter());

	//read&save propery 
	virtual void ReadProperty() override;
	virtual void SaveProperty() override;

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

	virtual wxString GetClassName() const override { return wxT("toolSeparator"); }
	virtual wxString GetObjectTypeName() const override { return wxT("tool"); }

	virtual bool CanDeleteControl() const;

	virtual int GetComponentType() const override { 
		return COMPONENT_TYPE_ABSTRACT; 
	}
	virtual bool IsItem() override { return false; }

	//load & save object in control 
	virtual bool LoadData(CMemoryReader& reader);
	virtual bool SaveData(CMemoryWriter& writer = CMemoryWriter());

	//read&save propery 
	virtual void ReadProperty() override;
	virtual void SaveProperty() override;

	friend class CValueForm;
	friend class CValueToolbar;
};

#endif


#ifndef _NOTEBOOKS_H__
#define _NOTEBOOKS_H__

#include "window.h"
#include <wx/aui/auibook.h>

class CValueNotebookPage;

class CValueNotebook : public IValueWindow {
	wxDECLARE_DYNAMIC_CLASS(CValueNotebook);
protected:

	friend class CValueNotebookPage;

	OptionList *GetOrientPage(Property *property)
	{
		OptionList *optList = new OptionList();
		optList->AddOption("Top", wxAUI_NB_TOP);
		//optList->AddOption("Left", wxAUI_NB_LEFT);
		//optList->AddOption("Right", wxAUI_NB_RIGHT);
		optList->AddOption("Bottom", wxAUI_NB_BOTTOM);
		return optList;
	}

private:
	wxAuiNotebookOption m_orientPage;
	bool m_bInitialized; 

private:

	CValueNotebookPage *m_activePage;

	std::vector< CValueNotebookPage*> m_aPages;

public:

	CValueNotebook();

	virtual wxObject* Create(wxObject* parent, IVisualHost *visualHost) override;
	virtual void OnCreated(wxObject* wxobject, wxWindow* wxparent, IVisualHost *visualHost, bool firstСreated) override;
	virtual void OnUpdated(wxObject* wxobject, wxWindow* wxparent, IVisualHost *visualHost) override;
	virtual void OnSelected(wxObject* wxobject) override;
	virtual void Update(wxObject* wxobject, IVisualHost *visualHost) override;
	virtual void Cleanup(wxObject* obj, IVisualHost *visualHost) override;

	virtual wxString GetClassName() const override { return wxT("notebook"); }
	virtual wxString GetObjectTypeName() const override { return wxT("notebook"); }

	//methods 
	virtual void PrepareNames() const override;                          //этот метод автоматически вызывается для инициализации имен атрибутов и методов
	virtual CValue Method(methodArg_t &aParams) override;       //вызов метода

	/**
	* Support default menu
	*/
	virtual void PrepareDefaultMenu(wxMenu *m_menu);
	virtual void ExecuteMenu(IVisualHost *visualHost, int id);

	void AddNotebookPage();

	//load & save object in control 
	virtual bool LoadData(CMemoryReader &reader);
	virtual bool SaveData(CMemoryWriter &writer = CMemoryWriter());

	//read&save propery 
	virtual void ReadProperty() override;
	virtual void SaveProperty() override;

protected:

	//Events
	void OnEnablePage(wxCommandEvent &event); 
	void OnChangedPage(wxAuiNotebookEvent& event);
};

class CValueNotebookPage : public IValueControl {
	wxDECLARE_DYNAMIC_CLASS(CValueNotebookPage);
protected:
	OptionList *GetOrient(Property *property)
	{
		OptionList *optList = new OptionList();
		optList->AddOption("Vertical", wxVERTICAL);
		optList->AddOption("Horizontal", wxHORIZONTAL);
		return optList;
	}
private:
	wxString m_caption;
	wxBitmap m_bitmap;
	bool m_visible;
	int m_orient;
public:

	CValueNotebookPage();

	virtual wxObject* Create(wxObject* parent, IVisualHost *visualHost) override;
	virtual void OnCreated(wxObject* wxobject, wxWindow* wxparent, IVisualHost *visualHost, bool firstСreated) override;
	virtual void OnUpdated(wxObject* wxobject, wxWindow* wxparent, IVisualHost *visualHost) override;
	virtual void OnSelected(wxObject* wxobject) override;
	virtual void Cleanup(wxObject* obj, IVisualHost *visualHost) override;

	virtual wxString GetClassName() const override { return wxT("page"); }
	virtual wxString GetObjectTypeName() const override { return wxT("page"); }

	virtual bool CanDeleteControl() const;

	virtual int GetComponentType() override { return COMPONENT_TYPE_WINDOW; }
	virtual bool IsItem() override { return false; }

	//load & save object in control 
	virtual bool LoadData(CMemoryReader &reader);
	virtual bool SaveData(CMemoryWriter &writer = CMemoryWriter());

	//read&save propery 
	virtual void ReadProperty() override;
	virtual void SaveProperty() override;

	friend class CValueNotebook;
};

#endif 
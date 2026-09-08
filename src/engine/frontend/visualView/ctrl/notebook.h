#ifndef _NOTEBOOKS_H__
#define _NOTEBOOKS_H__

#include "window.h"
#include <wx/aui/auibook.h>

class ibValueNotebookPage;

//********************************************************************************************
//*                                 define commom clsid									     *
//********************************************************************************************

//COMMON FORM
constexpr ibClassID g_controlNotebookCLSID = control_to_clsid("CT_NTBK");
constexpr ibClassID g_controlNotebookPageCLSID = control_to_clsid("CT_NTPG");

//********************************************************************************************
//*                                 Value Notebook                                           *
//********************************************************************************************

class ibValueNotebook : public ibValueWindow {
	public:

public:

	ibValueNotebook();

	//get title
	virtual wxString GetControlTitle() const {
		return _("Notebook");
	}

	//control factory 
	virtual wxObject* Create(ibFrontendWindow* wxparent, ibVisualHost* visualHost) override;
	virtual void OnCreated(wxObject* wxobject, ibFrontendWindow* wxparent, ibVisualHost* visualHost, bool firstCreated) override;
	virtual void OnUpdated(wxObject* wxobject, ibFrontendWindow* wxparent, ibVisualHost* visualHost) override;
	virtual void OnSelected(wxObject* wxobject) override;
	virtual void Update(wxObject* wxobject, ibVisualHost* visualHost) override;
	virtual void Cleanup(wxObject* obj, ibVisualHost* visualHost) override;

	//methods
	void FillControlMembers(ibMemberTable& helper) const;   // bound in ctor (was PrepareNames)
	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray) override;       //method call

	/**
	* Support default menu
	*/
	virtual void PrepareDefaultMenu(wxMenu* m_menu);
	virtual void ExecuteMenu(ibVisualHost* visualHost, int id);

	void AddNotebookPage();

	//support icons
	virtual wxIcon GetIcon() const;
	static wxIcon GetIconGroup();

	//load & save object in control 
	virtual bool ReadData(const ibDataNode& node);
	virtual bool WriteData(ibDataNode& node) const;

private:

	//Events
#ifdef OES_USE_WEB
	// The browser names the page by its control id -- there is no notebook
	// widget here to hold an index into.
	void OnWebPageChanged(wxCommandEvent& event);
#else
	void OnPageChanged(wxAuiNotebookEvent& event);
	void OnBGDClick(wxAuiNotebookEvent& event);
	void OnEndDrag(wxAuiNotebookEvent& event);
#endif

	// Which page is in front. It is the notebook's, not the widget's: on the
	// web road the tree is written out afresh on every answer, and a choice
	// kept only in the browser would not survive one.
	ibValueNotebookPage* m_activePage;

#ifdef OES_USE_WEB
	// The page that should be in front when nobody has picked one: the first
	// visible one. Also repairs an m_activePage that has gone invisible or is
	// no longer a child.
	ibValueNotebookPage* ResolveActivePage();
#endif
	std::vector< ibValueNotebookPage*> m_pageArray;

	ibPropertyCategory* m_categoryNotebook = ibPropertyObject::CreatePropertyCategory(wxT("Notebook"), _("Notebook"));
	ibPropertyEnum<ibValueEnumOrientNotebookPage>* m_propertyOrient = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumOrientNotebookPage>>(m_categoryNotebook, wxT("OrientPage"), _("Orient page"), wxAUI_NB_TOP);
	ibPropertyCategory* m_categoryEvent = ibPropertyObject::CreatePropertyCategory(wxT("Event"), _("Event"));
	ibEventControl* m_eventOnPageChanged = ibPropertyObject::CreateEvent<ibEventControl>(m_categoryEvent, wxT("OnPageChanged"), _("Page changed"), wxArrayString{ wxT("Page") });

	friend class ibValueNotebookPage;
};

class ibValueNotebookPage : public ibValueControl {
	public:

public:

	///////////////////////////////////////////////////////////////////////

	ibValueNotebook* GetOwner() const { return m_parent->ConvertToType<ibValueNotebook>(); }

	///////////////////////////////////////////////////////////////////////

	ibValueNotebookPage();

	//get title
	virtual wxString GetControlTitle() const {
		if (!m_propertyTitle->IsEmptyProperty())
			return _("Page item: ") + m_propertyTitle->GetValueAsTranslateString();
		return _("Page item: ") + _("<empty caption>");
	}

	//control factory 
	virtual wxObject* Create(ibFrontendWindow* wxparent, ibVisualHost* visualHost) override;
	virtual void OnCreated(wxObject* wxobject, ibFrontendWindow* wxparent, ibVisualHost* visualHost, bool firstCreated) override;
	virtual void OnUpdated(wxObject* wxobject, ibFrontendWindow* wxparent, ibVisualHost* visualHost) override;
	virtual void OnSelected(wxObject* wxobject) override;
	virtual void Cleanup(wxObject* obj, ibVisualHost* visualHost) override;

	virtual bool CanDeleteControl() const;

	virtual int GetComponentType() const { return COMPONENT_TYPE_WINDOW; }

	//support icons
	virtual wxIcon GetIcon() const;
	static wxIcon GetIconGroup();

	//load & save object in control 
	virtual bool ReadData(const ibDataNode& node);
	virtual bool WriteData(ibDataNode& node) const;

private:

	ibPropertyCategory* m_categoryPage = ibPropertyObject::CreatePropertyCategory(wxT("Page"), _("Page"));
	ibPropertyTString* m_propertyTitle = ibPropertyObject::CreateProperty<ibPropertyTString>(m_categoryPage, wxT("Title"), _("Title"), wxT("New page"));
	ibPropertyBoolean* m_propertyVisible = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categoryPage, wxT("Visible"), _("Visible"), true);
	ibPropertyEnum<ibValueEnumRepresentation>* m_propertyRepresentation = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumRepresentation>>(m_categoryPage, wxT("Representation"), _("Representation"), ibRepresentation::ibRepresentation_Auto);
	ibPropertyPicture* m_propertyPicture = ibPropertyObject::CreateProperty<ibPropertyPicture>(m_categoryPage, wxT("Picture"), _("Picture"));
	ibPropertyCategory* m_categorySizer = ibPropertyObject::CreatePropertyCategory(wxT("Sizer"), _("Sizer"));
	ibPropertyEnum<ibValueEnumOrient>* m_propertyOrient = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumOrient>>(m_categorySizer, wxT("Orient"), _("Orient"), wxVERTICAL);

	friend class ibValueNotebook;
};

#endif 

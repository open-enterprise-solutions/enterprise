#ifndef _NOTEBOOKS_H__
#define _NOTEBOOKS_H__

#include "window.h"

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

	// The window's state, and the page selected now (ActivePage, by its control id; 0 while no page is shown).
	virtual void OnUpdate(ibDataNode& state, ibVisualHost* host) override;

	// Page — a page picked (Page: its control id); Move — the active page's tab dragged (Position).
	virtual bool OnClientEvent(ibClientEvent event, const ibDataNode& args) override;

	//methods
	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray) override;       //method call

	//support icons
	virtual wxIcon GetIcon() const;
	static wxIcon GetIconGroup();

	//load & save object in control
	virtual bool ReadData(const ibDataNode& node);
	virtual bool WriteData(ibDataNode& node) const;

	// THE PAGE SELECTED NOW — the one the user picked while it is still shown, otherwise the first page that
	// is; null while none is. One answer for the frame and for the script's ActivePage(): asked in two places
	// with two rules, a script read null for a page the person was plainly looking at.
	ibValueNotebookPage* GetActivePage() const;

	void FillControlMembers(ibMemberTable& helper) const;   // bound in ctor (was PrepareNames)

private:

	// The user selected the page with this control id: it becomes the active page, and the form's
	// OnPageChanged handler runs with it. An id that names none of this notebook's pages changes nothing.
	void OnPageChanged(const ibFormID& pageId);

	// The user dragged the active page's tab to this position among the notebook's pages.
	void OnEndDrag(unsigned int position);

	ibValueNotebookPage* m_activePage;
	std::vector< ibValueNotebookPage*> m_pageArray;

	ibPropertyCategory* m_categoryNotebook = ibPropertyObject::CreatePropertyCategory(wxT("Notebook"), _("Notebook"));
	ibPropertyEnum<ibValueEnumOrientNotebookPage>* m_propertyOrient = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumOrientNotebookPage>>(m_categoryNotebook, wxT("OrientPage"), _("Orient page"),
		_("Where the page tabs are drawn: above the pages (the default) or below them."),
		wxAUI_NB_TOP);
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

	// Whether the page is on the notebook, and what its tab carries: the title in the user's language
	// unless the tab is a picture only, the picture unless it is text only (Representation).
	virtual void OnUpdate(ibDataNode& state, ibVisualHost* host) override;

	// Picked out: the notebook brings this page up, as the window's did.
	virtual void OnSelected(ibVisualHost* host) override;

	virtual bool CanDeleteControl() const;

	virtual int GetComponentType() const { return COMPONENT_TYPE_WINDOW; }

	//support icons
	virtual wxIcon GetIcon() const;
	static wxIcon GetIconGroup();

	//load & save object in control 
	virtual bool ReadData(const ibDataNode& node);
	virtual bool WriteData(ibDataNode& node) const;

private:

	// On the notebook: its own Visible, and available by the functional options of this base — a page they
	// switch off is off the notebook the way an invisible one is. Asked by the page and by its notebook.
	bool IsPageShown() const { return m_propertyVisible->GetValueAsBoolean() && IsAvailable(); }

	ibPropertyCategory* m_categoryPage = ibPropertyObject::CreatePropertyCategory(wxT("Page"), _("Page"));
	ibPropertyTString* m_propertyTitle = ibPropertyObject::CreateProperty<ibPropertyTString>(m_categoryPage, wxT("Title"), _("Title"),
		_("The text on the page's tab. Can be written per language and changed from code while the form is open."), wxT("New page"));
	ibPropertyBoolean* m_propertyVisible = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categoryPage, wxT("Visible"), _("Visible"),
		_("Whether the page's tab is shown. A hidden page keeps its controls and their data and can be shown again from code."), true);
	ibPropertyEnum<ibValueEnumRepresentation>* m_propertyRepresentation = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumRepresentation>>(m_categoryPage, wxT("Representation"), _("Representation"),
		_("What the page's tab shows: text, picture, or both. Auto: the title and the picture, whichever are set."),
		ibRepresentation::ibRepresentation_Auto);
	ibPropertyPicture* m_propertyPicture = ibPropertyObject::CreateProperty<ibPropertyPicture>(m_categoryPage, wxT("Picture"), _("Picture"),
		_("The icon on the page's tab, beside or instead of the title (see Representation)."));
	ibPropertyCategory* m_categorySizer = ibPropertyObject::CreatePropertyCategory(wxT("Sizer"), _("Sizer"));
	ibPropertyEnum<ibValueEnumOrient>* m_propertyOrient = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumOrient>>(m_categorySizer, wxT("Orient"), _("Orient"),
		_("How the page lays out the controls placed on it: vertically (the default, one under another) or horizontally (side by side)."),
		wxVERTICAL);

	friend class ibValueNotebook;
};

#endif 

#ifndef _FRMCLIENT_VIEW_NOTEBOOK_H__
#define _FRMCLIENT_VIEW_NOTEBOOK_H__

#include <wx/aui/auibook.h>

#include "frmclient/visualView/ctrl/window.h"
#include "frmclient/visualView/controlEnum.h"   // where the tabs are, a page's representation and orient
#include "frmclient/backend/propertyManager/property/propertyEnum.h"
#include "frmclient/backend/propertyManager/property/propertyString.h"

class ibPanelPage;

// THE NOTEBOOK OF A VIEW — the desktop's (frontend/visualView/ctrl/notebook.h): its pages the controls in it, the one
// the State's ActivePage names brought up. A page picked goes to the server (Page), a tab dragged too (Move).
class ibValueNotebook : public ibValueWindow {
public:
	ibValueNotebook(ibVisualHostClient& host, long long controlId) : ibValueWindow(host, controlId) {}

	virtual void Create(wxWindow* parent, const ibProtocolNode& node) override;
	virtual void Update(const ibProtocolNode& node) override;
	virtual wxWindow* GetWindow() const override { return m_notebook; }

	//load & save object in control
	virtual bool ReadData(const ibDataNode& node) override;
	virtual bool WriteData(ibDataNode& node) const override;

private:

	// The page the server selected, brought up once it is on the notebook — asked by the notebook as it is drawn, and by
	// each page put on it after: the pages are drawn after the notebook.
	void SelectActivePage();

	void OnPageChanged(wxAuiNotebookEvent& event);
	void OnEndDrag(wxAuiNotebookEvent& event);

	ibPropertyCategory* m_categoryNotebook = ibPropertyObject::CreatePropertyCategory(wxT("Notebook"), _("Notebook"));
	ibPropertyEnum<ibValueEnumOrientNotebookPage>* m_propertyOrient = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumOrientNotebookPage>>(m_categoryNotebook, wxT("OrientPage"), _("Orient page"),
		_("Where the page tabs are drawn: above the pages (the default) or below them."),
		wxAUI_NB_TOP);

	wxAuiNotebook* m_notebook = nullptr;
	long long      m_activePage = 0;   // the State's ActivePage, by its control id; 0 — none

	friend class ibValueNotebookPage;
};

// A page of a notebook: on it while the State says it is Visible, its tab the State's Title and Picture.
class ibValueNotebookPage : public ibValueControl {
public:
	ibValueNotebookPage(ibVisualHostClient& host, long long controlId) : ibValueControl(host, controlId) {}

	virtual int GetComponentType() const override { return COMPONENT_TYPE_WINDOW; }

	virtual void Create(wxWindow* parent, const ibProtocolNode& node) override;
	virtual void Update(const ibProtocolNode& node) override;
	virtual wxWindow* GetWindow() const override;
	virtual wxSizer*  GetSizer() const override;

	//load & save object in control
	virtual bool ReadData(const ibDataNode& node) override;
	virtual bool WriteData(ibDataNode& node) const override;

private:

	// The notebook it is a page of.
	ibValueNotebook* GetNotebook() const;

	ibPropertyCategory* m_categoryPage = ibPropertyObject::CreatePropertyCategory(wxT("Page"), _("Page"));
	ibPropertyTString* m_propertyTitle = ibPropertyObject::CreateProperty<ibPropertyTString>(m_categoryPage, wxT("Title"), _("Title"),
		_("The text on the page's tab. Can be written per language and changed from code while the form is open."), wxT("New page"));
	ibPropertyBoolean* m_propertyVisible = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categoryPage, wxT("Visible"), _("Visible"),
		_("Whether the page's tab is shown. A hidden page keeps its controls and their data and can be shown again from code."), true);
	ibPropertyEnum<ibValueEnumRepresentation>* m_propertyRepresentation = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumRepresentation>>(m_categoryPage, wxT("Representation"), _("Representation"),
		_("What the page's tab shows: text, picture, or both. Auto: the title and the picture, whichever are set."),
		ibRepresentation::ibRepresentation_Auto);
	ibPropertyCategory* m_categorySizer = ibPropertyObject::CreatePropertyCategory(wxT("Sizer"), _("Sizer"));
	ibPropertyEnum<ibValueEnumOrient>* m_propertyOrient = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumOrient>>(m_categorySizer, wxT("Orient"), _("Orient"),
		_("How the page lays out the controls placed on it: vertically (the default, one under another) or horizontally (side by side)."),
		wxVERTICAL);

	ibPanelPage* m_page = nullptr;
};

#endif

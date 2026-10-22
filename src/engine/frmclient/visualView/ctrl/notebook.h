#ifndef _FRMCLIENT_VIEW_NOTEBOOK_H__
#define _FRMCLIENT_VIEW_NOTEBOOK_H__

#include <wx/aui/auibook.h>

#include "frmclient/visualView/ctrl/window.h"

class ibPanelPage;

// THE NOTEBOOK OF A VIEW — the desktop's (frontend/visualView/ctrl/notebook.h): its pages the controls in it, the one
// the State's ActivePage names brought up. A page picked goes to the server (Page), a tab dragged too (Move).
class ibValueNotebook : public ibValueWindow {
public:
	ibValueNotebook(ibVisualHostClient& host, long long controlId) : ibValueWindow(host, controlId) {}

	virtual void Create(wxWindow* parent, const ibProtocolNode& node) override;
	virtual void Update(const ibProtocolNode& node) override;
	virtual wxWindow* GetWindow() const override { return m_notebook; }

private:

	// The active page brought up once it is on the notebook — the server's, or the one the designer selected (a page
	// selected in the editor, ibValueNotebookPage::OnSelected); asked by the notebook as it is drawn, and by each page put
	// on it after: the pages are drawn after the notebook.
	void SelectActivePage();

	void OnPageChanged(wxAuiNotebookEvent& event);
	void OnEndDrag(wxAuiNotebookEvent& event);

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
	virtual void OnSelected() override;
	virtual wxWindow* GetWindow() const override;
	virtual wxSizer*  GetSizer() const override;

private:

	// The notebook it is a page of.
	ibValueNotebook* GetNotebook() const;

	ibPanelPage* m_page = nullptr;
};

#endif

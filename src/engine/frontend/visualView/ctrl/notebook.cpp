#include "notebook.h"
#include "core/serialize/dataBuilder.h"   // ibDataNode (control -> node)
#include "form.h"

//***********************************************************************************
//*                           IMPLEMENT_DYNAMIC_CLASS                               *
//***********************************************************************************


//***********************************************************************************
//*                                 Special Notebook func                           *
//***********************************************************************************

void ibValueNotebook::AddNotebookPage()
{
	wxASSERT(m_formOwner);

	ibValueFrame* newNotebookPage = m_formOwner->NewObject(g_controlNotebookPageCLSID, this);
	g_visualHostContext->InsertControl(newNotebookPage, this);
	g_visualHostContext->RefreshEditor();
}

//***********************************************************************************
//*                                 Value Notebook                                  *
//***********************************************************************************

ibValueNotebook::ibValueNotebook() : ibValueWindow(), m_activePage(nullptr)
{
	m_members.Bind(this, &ibValueNotebook::FillControlMembers);
	//set default params
	//m_minimum_size = wxSize(300, 100);
}

#include "frontend/win/theme/luna_tabart.h"

wxObject* ibValueNotebook::Create(wxWindow* wxparent, ibVisualHost* visualHost)
{
	long style = m_propertyOrient->GetValueAsInteger() |
		wxAUI_NB_TAB_MOVE | 
		wxAUI_NB_SCROLL_BUTTONS;
	if (!visualHost->IsDesignerHost())
		style |= wxAUI_NB_TAB_SPLIT;
	wxAuiNotebook* notebook = new wxAuiNotebook(wxparent, wxID_ANY,
		wxDefaultPosition,
		wxDefaultSize, style);
	notebook->SetArtProvider(new wxAuiLunaTabArt());
	return notebook;
}

void ibValueNotebook::OnCreated(wxObject* wxobject, wxWindow* wxparent, ibVisualHost* visualHost, bool firstCreated)
{
	if (visualHost->IsDesignerHost() && GetChildCount() == 0
		&& firstCreated) {
		ibValueNotebook::AddNotebookPage();
	}

	wxAuiNotebook* notebook = dynamic_cast<wxAuiNotebook*>(wxobject);
	if (notebook != nullptr) {
		notebook->Bind(wxEVT_AUINOTEBOOK_PAGE_CHANGED, &ibValueNotebook::OnPageChanged, this);
		notebook->Bind(wxEVT_AUINOTEBOOK_END_DRAG, &ibValueNotebook::OnEndDrag, this);
		notebook->Bind(wxEVT_AUINOTEBOOK_BG_DCLICK, &ibValueNotebook::OnBGDClick, this);
		// See OnNotebookSize: the tab strip's own geometry (and each page's layout)
		// can only be settled correctly once the notebook has its REAL size from the
		// form's own layout pass, not the small/default size it has right after
		// construction.
		notebook->Bind(wxEVT_SIZE, &ibValueNotebook::OnNotebookSize, this);
	}
}

void ibValueNotebook::OnSelected(wxObject* wxobject)
{
}

void ibValueNotebook::Update(wxObject* wxobject, ibVisualHost* visualHost)
{
	wxAuiNotebook* notebook = dynamic_cast<wxAuiNotebook*>(wxobject);
	if (notebook != nullptr) {
	}

	UpdateWindow(notebook);
}

void ibValueNotebook::OnUpdated(wxObject* wxobject, wxWindow* wxparent, ibVisualHost* visualHost)
{
	wxAuiNotebook* notebook = dynamic_cast<wxAuiNotebook*>(wxobject);

	if (notebook != nullptr) {
		long style = m_propertyOrient->GetValueAsInteger() |
			wxAUI_NB_TAB_MOVE |
			wxAUI_NB_SCROLL_BUTTONS;
		if (!visualHost->IsDesignerHost())
			style |= wxAUI_NB_TAB_SPLIT;
		notebook->SetWindowStyle(style);

		// By the time this (post-order) runs, every NotebookPage child has already run
		// AddPage() with select=false — none has a reason to prefer itself over a
		// sibling. On a notebook grown one page at a time through the designer's own
		// AddNotebookPage(), that's harmless (wx auto-selects a lone first page). On one
		// materialized wholesale from stored metadata, nothing ever explicitly selects a
		// page — yet between the OnCreated pass and this OnUpdated pass, wx's standard
		// "focusing a descendant switches its notebook to that page" behavior has
		// already fired for whichever control the platform happened to give initial
		// keyboard focus to (observed landing on a page deep in the tree, with the tab
		// strip and the visible page panel left disagreeing about which one that is —
		// confirmed by tracing: OnCreated still reads selection 0, this OnUpdated
		// already reads a different one). Settle it to page 0 the first time this
		// control is ever updated, and never again, so a tab the user later switches to
		// is never stolen back. This alone needs no real size, so it is safe to do here
		// rather than waiting for OnNotebookSize.
		if (!m_selectionSettled) {
			m_selectionSettled = true;
			if (notebook->GetPageCount() > 0)
				notebook->ChangeSelection(0);  // not SetSelection: must not fire PAGE_CHANGED (see OnPageChanged)
		}
	}
}

void ibValueNotebook::OnNotebookSize(wxSizeEvent& event)
{
	event.Skip();   // let wxAuiNotebook's own handler run (it recomputes the tab strip against the real size)

	wxAuiNotebook* notebook = wxDynamicCast(event.GetEventObject(), wxAuiNotebook);
	if (notebook == nullptr || m_pagesLaidOut || notebook->GetPageCount() == 0)
		return;

	// A notebook materialized wholesale from stored metadata has every AddPage()
	// call happen before the top-level form window is ever shown — each page's tab
	// geometry (wxAuiTabArt::SetSizingInfo sizes off the control's current rect) and
	// the tab strip's own Render() (which stops drawing once it runs out of its
	// CURRENT width — src/3rdparty/wxWidgets/src/aui/auibook.cpp) get computed
	// against whatever small/default size the control happens to have at
	// construction time, not its real final size. Forcing a resize ourselves at that
	// point (an earlier version of this fix called SendSizeEvent() straight out of
	// OnUpdated) just recomputes the same wrong geometry against the same wrong
	// size — it has to wait for a size event the window did not ask for itself,
	// i.e. one the form's own layout pass actually generates once it knows the
	// real size. This handler is that: the first time the notebook is resized to
	// something that isn't its tiny construction default, settle it once.
	const wxSize size = event.GetSize();
	if (size.GetWidth() < 2 || size.GetHeight() < 2)
		return;   // still the construction-time placeholder — wait for a real one

	m_pagesLaidOut = true;

	// Showing a page does not retroactively lay out ITS OWN sizer — a page built
	// while never shown (every one but whichever ends up selected first) never had
	// its buttons/controls positioned, so they sit at (0,0) with no size and paint
	// nothing. Every page needs this once, not just the selected one, since the
	// user will switch to the rest.
	for (size_t p = 0; p < notebook->GetPageCount(); ++p) {
		wxWindow* page = notebook->GetPage(p);
		if (page != nullptr)
			page->Layout();
	}
	notebook->Refresh();
}

void ibValueNotebook::Cleanup(wxObject* wxobject, ibVisualHost* visualHost)
{
	wxAuiNotebook* notebook = dynamic_cast<wxAuiNotebook*>(wxobject);
	if (notebook != nullptr) {
		notebook->Unbind(wxEVT_AUINOTEBOOK_PAGE_CHANGED, &ibValueNotebook::OnPageChanged, this);
		notebook->Unbind(wxEVT_AUINOTEBOOK_END_DRAG, &ibValueNotebook::OnEndDrag, this);
		notebook->Unbind(wxEVT_SIZE, &ibValueNotebook::OnNotebookSize, this);
	}
}

//**********************************************************************************
//*                                   Data		                                   *
//**********************************************************************************

bool ibValueNotebook::ReadData(const ibDataNode& node)
{
	m_propertyOrient->SetNodeValue(node.GetProperty(m_propertyOrient->GetName()));

	//events
	m_eventOnPageChanged->SetNodeValue(node.GetProperty(m_eventOnPageChanged->GetName()));
	return ibValueWindow::ReadData(node);
}

bool ibValueNotebook::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertyOrient->GetName(), m_propertyOrient->GetNodeValue());

	//events
	node.SetProperty(m_eventOnPageChanged->GetName(), m_eventOnPageChanged->GetNodeValue());
	return ibValueWindow::WriteData(node);
}

//**********************************************************************************

enum Func {
	enPages = 0,
	enActivePage
};

void ibValueNotebook::FillControlMembers(ibMemberTable& helper) const
{
	helper.AppendFunc(wxT("Pages"), wxT("Pages()"));
	helper.AppendFunc(wxT("ActivePage"), wxT("ActivePage()"));
}

#include "backend/system/value/valueMap.h"

bool ibValueNotebook::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)       //method call
{
	switch (lMethodNum)
	{
	case enPages:
	{
		ibValueStructure* structurePage = new ibValueStructure(true);
		for (unsigned int i = 0; i < GetChildCount(); i++) {
			ibValueNotebookPage* notebookPage = dynamic_cast<ibValueNotebookPage*>(GetChild(i));
			if (notebookPage) {
				structurePage->Insert(notebookPage->GetControlName(), ibValue(notebookPage));
			}
		}
#pragma message("nouverbe to nouverbe: needs more work!")
		pvarRetValue = structurePage;
		return true; 
	}
	case enActivePage:
		pvarRetValue = m_activePage;
		return true;
	}

	return ibValueWindow::CallAsFunc(lMethodNum, pvarRetValue, paParams, lSizeArray);
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

CONTROL_TYPE_REGISTER(ibValueNotebook, "Notebook", "Notebook", g_controlNotebookCLSID);
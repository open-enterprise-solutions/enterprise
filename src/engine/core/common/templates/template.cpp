#include "template.h"
#include "frontend/mainFrame.h"

// ----------------------------------------------------------------------------
// CGridEditView implementation
// ----------------------------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(CGridEditView, CView);

wxBEGIN_EVENT_TABLE(CGridEditView, CView)
EVT_MENU(wxID_COPY, CGridEditView::OnCopy)
EVT_MENU(wxID_PASTE, CGridEditView::OnPaste)
EVT_MENU(wxID_SELECTALL, CGridEditView::OnSelectAll)

wxEND_EVENT_TABLE()

enum
{
	wxID_SECTION_ADD = wxID_HIGHEST + 100,
	wxID_SECTION_REMOVE,

	wxID_MERGE_CELLS,
};

extern wxImageList *GetImageList();

bool CGridEditView::OnCreate(CDocument *doc, long flags)
{
	m_grid = new CGrid(m_viewFrame, wxID_ANY);
	m_grid->EnableEditing(flags != wxDOC_READONLY);

	return CView::OnCreate(doc, flags);
}

void CGridEditView::OnDraw(wxDC *WXUNUSED(dc))
{
	// nothing to do here, wxGrid draws itself
}

#include "frontend/grid/print/controlprinter.h"

wxPrintout *CGridEditView::OnCreatePrintout()
{
	CGridPrintout* printOutForPrinting = new CGridPrintout(m_grid, wxGP_DEFAULT);
	printOutForPrinting->SetUserScale(0.75); //set the scale you want to use, default 1.0
	printOutForPrinting->AddColBrake(15); //at col 15 a new page will begin
	return printOutForPrinting;
}

#include <wx/artprov.h>

void CGridEditView::OnCreateToolbar(wxAuiToolBar *m_toolbar)
{
	m_toolbar->AddTool(wxID_SECTION_ADD, "Add section", GetImageList()->GetBitmap(630), "Add", wxItemKind::wxITEM_NORMAL);
	m_toolbar->EnableTool(wxID_SECTION_ADD, m_grid->IsEditable());
	m_toolbar->AddTool(wxID_SECTION_REMOVE, "Remove section", GetImageList()->GetBitmap(631), "Remove", wxItemKind::wxITEM_NORMAL);
	m_toolbar->EnableTool(wxID_SECTION_REMOVE, m_grid->IsEditable());
	m_toolbar->AddSeparator();
	m_toolbar->AddTool(wxID_MERGE_CELLS, "Merge cells", GetImageList()->GetBitmap(629), "Merge cells", wxItemKind::wxITEM_NORMAL);
	m_toolbar->EnableTool(wxID_MERGE_CELLS, m_grid->IsEditable());

	m_toolbar->Bind(wxEVT_TOOL, &CGridEditView::OnToolbarClicked, this, wxID_SECTION_ADD, wxID_MERGE_CELLS);
}

bool CGridEditView::OnClose(bool deleteWindow)
{
	Activate(false);

	if (deleteWindow)
	{
		GetFrame()->Destroy();
		SetFrame(NULL);
	}

	return CView::OnClose(deleteWindow);
}

void CGridEditView::OnToolbarClicked(wxCommandEvent &event)
{
	switch (event.GetId())
	{
	case wxID_SECTION_ADD: m_grid->AddSection(); break;
	case wxID_SECTION_REMOVE: m_grid->RemoveSection(); break;
	case wxID_MERGE_CELLS: m_grid->MergeCells(); break;
	}

	//event.Skip();
}

// ----------------------------------------------------------------------------
// CGridDocument: wxDocument and wxGrid married
// ----------------------------------------------------------------------------

wxIMPLEMENT_CLASS(CGridDocument, CDocument);

bool CGridDocument::OnCreate(const wxString& path, long flags)
{
	if (!CDocument::OnCreate(path, flags))
		return false;

	return true;
}

// Since text windows have their own method for saving to/loading from files,
// we override DoSave/OpenDocument instead of Save/LoadObject
bool CGridDocument::DoSaveDocument(const wxString& filename)
{
	return true;
}

bool CGridDocument::DoOpenDocument(const wxString& filename)
{
	return true;
}

bool CGridDocument::IsModified() const
{
	return CDocument::IsModified(); 
}

void CGridDocument::Modify(bool modified)
{
	CDocument::Modify(modified);
}

// ----------------------------------------------------------------------------
// CGridEditDocument implementation
// ----------------------------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(CGridEditDocument, CDocument);

wxGrid * CGridEditDocument::GetGridCtrl() const
{
	wxView* view = GetFirstView();
	return view ? wxDynamicCast(view, CGridEditView)->GetGridCtrl() : NULL;
}
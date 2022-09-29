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
	wxID_MERGE_CELLS = wxID_HIGHEST + 100,

	wxID_SECTION_ADD,
	wxID_SECTION_REMOVE,

	wxID_SHOW_CELLS,
	wxID_SHOW_HEADERS,
	wxID_SHOW_SECTIONS,

	wxID_BORDERS,
	wxID_DOCK_TABLE,
};

bool CGridEditView::OnCreate(CDocument* doc, long flags)
{
	m_grid = new CGrid(m_viewFrame, wxID_ANY);
	m_grid->EnableEditing(flags != wxDOC_READONLY);

	return CView::OnCreate(doc, flags);
}

void CGridEditView::OnDraw(wxDC* WXUNUSED(dc))
{
	// nothing to do here, wxGrid draws itself
}

#include "frontend/grid/printout/gridPrintout.h"

wxPrintout* CGridEditView::OnCreatePrintout()
{
	return new CGridPrintout(m_grid, wxGP_SHOW_NONE);
}

#include <wx/artprov.h>

#include "core/resources/template/mergeCells.xpm"
#include "core/resources/template/addSection.xpm"
#include "core/resources/template/removeSection.xpm"

#include "core/resources/template/showCells.xpm"
#include "core/resources/template/showHeaders.xpm"
#include "core/resources/template/showSections.xpm"

#include "core/resources/template/borders.xpm"
#include "core/resources/template/dockTable.xpm"

void CGridEditView::OnCreateToolbar(wxAuiToolBar* toolbar)
{
	toolbar->AddTool(wxID_MERGE_CELLS, _("Merge cells"), wxIcon(s_mergeCells_xpm), _("Merge cells"), wxItemKind::wxITEM_NORMAL);
	toolbar->EnableTool(wxID_MERGE_CELLS, m_grid->IsEditable());
	toolbar->AddSeparator();
	toolbar->AddTool(wxID_SECTION_ADD, _("Add section"), wxIcon(s_addSection_xpm), _("Add"), wxItemKind::wxITEM_NORMAL);
	toolbar->EnableTool(wxID_SECTION_ADD, m_grid->IsEditable());
	toolbar->AddTool(wxID_SECTION_REMOVE, _("Remove section"), wxIcon(s_removeSection_xpm), _("Remove"), wxItemKind::wxITEM_NORMAL);
	toolbar->EnableTool(wxID_SECTION_REMOVE, m_grid->IsEditable());
	toolbar->AddSeparator();
	toolbar->AddTool(wxID_SHOW_CELLS, _("Show cells"), wxIcon(s_showCells_xpm), _("Show cells"));
	toolbar->AddTool(wxID_SHOW_HEADERS, _("Show headers"), wxIcon(s_showHeaders_xpm), _("Show headers"));
	toolbar->AddTool(wxID_SHOW_SECTIONS, _("Show sections"), wxIcon(s_showSections_xpm), _("Show sections"));
	toolbar->AddTool(wxID_BORDERS, _("Borders"), wxIcon(s_borders_xpm), _("Borders"));
	toolbar->SetToolDropDown(wxID_BORDERS, true);
	toolbar->AddSeparator();
	toolbar->AddTool(wxID_DOCK_TABLE, _("Dock table"), wxIcon(s_dockTable_xpm), _("Dock table"));

	toolbar->Bind(wxEVT_TOOL, &CGridEditView::OnToolClicked, this, wxID_MERGE_CELLS, wxID_DOCK_TABLE);
	toolbar->Bind(wxEVT_AUITOOLBAR_TOOL_DROPDOWN, &CGridEditView::OnToolDropDown, this);
}

bool CGridEditView::OnClose(bool deleteWindow)
{
	Activate(false);

	if (deleteWindow) {
		GetFrame()->Destroy();
		SetFrame(NULL);
	}

	return CView::OnClose(deleteWindow);
}

void CGridEditView::OnToolClicked(wxCommandEvent& event)
{
	switch (event.GetId())
	{
	case wxID_MERGE_CELLS:
		m_grid->MergeCells();
		break;
	case wxID_SECTION_ADD:
		m_grid->AddSection();
		break;
	case wxID_SECTION_REMOVE:
		m_grid->RemoveSection();
		break;
	case wxID_SHOW_CELLS:
		m_grid->ShowCells();
		break;
	case wxID_SHOW_HEADERS:
		m_grid->ShowHeader();
		break;
	case wxID_SHOW_SECTIONS:
		m_grid->ShowSection();
		break;
	case wxID_DOCK_TABLE:
		m_grid->DockTable();
		break;
	}

	//event.Skip();
}

void CGridEditView::OnToolDropDown(wxAuiToolBarEvent& event)
{
	event.Skip();
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

wxGrid* CGridEditDocument::GetGridCtrl() const
{
	wxView* view = GetFirstView();
	return view ? wxDynamicCast(view, CGridEditView)->GetGridCtrl() : NULL;
}
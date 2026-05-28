#include "docViewSpreadsheet.h"
#include "frontend/docView/docManager.h"   // full ibDocTemplate type
#include "frontend/mainFrame/mainFrame.h"

enum
{
	wxID_MERGE_CELL = wxID_HIGHEST + 150,
	wxID_AREA_ADD,
	wxID_AREA_DELETE,
	wxID_FREEZE_ROW,
	wxID_FREEZE_COL,
	wxID_PRINT_BRAKE_ROW_ADD,
	wxID_PRINT_BRAKE_ROW_DELETE,
	wxID_PRINT_BRAKE_COL_ADD,
	wxID_PRINT_BRAKE_COL_DELETE,
	wxID_SHOW_CELL,
	wxID_SHOW_HEADER,
	wxID_SHOW_AREA,
	wxID_DOCK_TABLE,
	wxID_BORDER_LEFT,
	wxID_BORDER_TOP,
	wxID_BORDER_RIGHT,
	wxID_BORDER_BOTTOM,
	wxID_BORDER_ALL,
	wxID_BORDER_AROUND,
	wxID_BORDER_NONE,
	wxID_EDITABLE,

	wxID_GROUP_ROW,
	wxID_UNGROUP_ROW,
	wxID_GROUP_COL,
	wxID_UNGROUP_COL,
};

// ----------------------------------------------------------------------------
// ibSpreadsheetEditView implementation
// ----------------------------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(ibSpreadsheetEditView, ibMetaView);

wxBEGIN_EVENT_TABLE(ibSpreadsheetEditView, ibMetaView)

EVT_MENU(wxID_COPY, ibSpreadsheetEditView::OnCopy)
EVT_MENU(wxID_PASTE, ibSpreadsheetEditView::OnPaste)
EVT_MENU(wxID_SELECTALL, ibSpreadsheetEditView::OnSelectAll)
EVT_MENU(wxID_MERGE_CELL, ibSpreadsheetEditView::OnMenuEvent)
EVT_MENU(wxID_AREA_ADD, ibSpreadsheetEditView::OnMenuEvent)
EVT_MENU(wxID_AREA_DELETE, ibSpreadsheetEditView::OnMenuEvent)
EVT_MENU(wxID_FREEZE_ROW, ibSpreadsheetEditView::OnMenuEvent)
EVT_MENU(wxID_FREEZE_COL, ibSpreadsheetEditView::OnMenuEvent)
EVT_MENU(wxID_PRINT_BRAKE_ROW_ADD, ibSpreadsheetEditView::OnMenuEvent)
EVT_MENU(wxID_PRINT_BRAKE_ROW_DELETE, ibSpreadsheetEditView::OnMenuEvent)
EVT_MENU(wxID_PRINT_BRAKE_COL_ADD, ibSpreadsheetEditView::OnMenuEvent)
EVT_MENU(wxID_PRINT_BRAKE_COL_DELETE, ibSpreadsheetEditView::OnMenuEvent)
EVT_MENU(wxID_SHOW_CELL, ibSpreadsheetEditView::OnMenuEvent)
EVT_MENU(wxID_SHOW_HEADER, ibSpreadsheetEditView::OnMenuEvent)
EVT_MENU(wxID_SHOW_AREA, ibSpreadsheetEditView::OnMenuEvent)
EVT_MENU(wxID_DOCK_TABLE, ibSpreadsheetEditView::OnMenuEvent)
EVT_MENU(wxID_BORDER_LEFT, ibSpreadsheetEditView::OnMenuEvent)
EVT_MENU(wxID_BORDER_TOP, ibSpreadsheetEditView::OnMenuEvent)
EVT_MENU(wxID_BORDER_RIGHT, ibSpreadsheetEditView::OnMenuEvent)
EVT_MENU(wxID_BORDER_BOTTOM, ibSpreadsheetEditView::OnMenuEvent)
EVT_MENU(wxID_BORDER_ALL, ibSpreadsheetEditView::OnMenuEvent)
EVT_MENU(wxID_BORDER_AROUND, ibSpreadsheetEditView::OnMenuEvent)
EVT_MENU(wxID_BORDER_NONE, ibSpreadsheetEditView::OnMenuEvent)
EVT_MENU(wxID_EDITABLE, ibSpreadsheetEditView::OnMenuEvent)
EVT_MENU(wxID_GROUP_ROW, ibSpreadsheetEditView::OnMenuEvent)
EVT_MENU(wxID_UNGROUP_ROW, ibSpreadsheetEditView::OnMenuEvent)
EVT_MENU(wxID_GROUP_COL, ibSpreadsheetEditView::OnMenuEvent)
EVT_MENU(wxID_UNGROUP_COL, ibSpreadsheetEditView::OnMenuEvent)
wxEND_EVENT_TABLE()

bool ibSpreadsheetEditView::OnCreate(ibMetaDocument* doc, long flags)
{
	m_gridEditor = new ibGridEditor(doc, m_viewFrame, wxID_ANY);
	m_gridEditor->EnableEditing(flags != ibDOC_READONLY);
	m_gridEditor->EnableGridArea(doc->ConvertMetaObjectToType<ibValueMetaObjectSpreadsheetBase>());

	return ibMetaView::OnCreate(doc, flags);
}

#if wxUSE_MENUS	
wxMenuBar* ibSpreadsheetEditView::CreateMenuBar() const
{
	wxMenuBar* mb = new wxMenuBar;
	wxMenu* menu = new wxMenu;

	if (m_gridEditor != nullptr) {

		wxMenuItem* menuItem = nullptr;

		wxMenu* menuArea = new wxMenu;
		menuItem = menuArea->Append(wxID_AREA_ADD, _("Add area"));
		menuItem->Enable(m_gridEditor->IsEditable());
		menuItem = menuArea->Append(wxID_AREA_DELETE, _("Delete area"));
		menuItem->Enable(m_gridEditor->IsEditable());
		menu->AppendSubMenu(menuArea, _("Area"));

		wxMenu* menuFreeze = new wxMenu;
		menuItem = menuFreeze->Append(wxID_FREEZE_ROW, _("Freeze row"));
		menuItem->Enable(m_gridEditor->IsEditable());
		menuItem = menuFreeze->Append(wxID_FREEZE_COL, _("Freeze column"));
		menuItem->Enable(m_gridEditor->IsEditable());
		menu->AppendSubMenu(menuFreeze, _("Freeze"));

		wxMenu* menuPrintBrake = new wxMenu;
		menuItem = menuPrintBrake->Append(wxID_PRINT_BRAKE_ROW_ADD, _("Add brake row"));
		menuItem->Enable(m_gridEditor->IsEditable());
		menuItem = menuPrintBrake->Append(wxID_PRINT_BRAKE_COL_ADD, _("Add brake column"));
		menuItem->Enable(m_gridEditor->IsEditable());
		menuItem = menuPrintBrake->Append(wxID_PRINT_BRAKE_ROW_DELETE, _("Delete brake row"));
		menuItem->Enable(m_gridEditor->IsEditable());
		menuItem = menuPrintBrake->Append(wxID_PRINT_BRAKE_COL_DELETE, _("Delete brake column"));
		menuItem->Enable(m_gridEditor->IsEditable());
		menu->AppendSubMenu(menuPrintBrake, _("Print brake"));

		wxMenu* menuBorder = new wxMenu;
		menuItem = menuBorder->Append(wxID_BORDER_LEFT, _("Left border"));
		menuItem->Enable(m_gridEditor->IsEditable());
		menuItem = menuBorder->Append(wxID_BORDER_TOP, _("Top border"));
		menuItem->Enable(m_gridEditor->IsEditable());
		menuItem = menuBorder->Append(wxID_BORDER_RIGHT, _("Right border"));
		menuItem->Enable(m_gridEditor->IsEditable());
		menuItem = menuBorder->Append(wxID_BORDER_BOTTOM, _("Bottom border"));
		menuItem->Enable(m_gridEditor->IsEditable());
		menuBorder->AppendSeparator();
		menuItem = menuBorder->Append(wxID_BORDER_ALL, _("Border all"));
		menuItem->Enable(m_gridEditor->IsEditable());
		menuItem = menuBorder->Append(wxID_BORDER_AROUND, _("Border around"));
		menuItem->Enable(m_gridEditor->IsEditable());
		menuItem = menuBorder->Append(wxID_BORDER_NONE, _("No border"));
		menuItem->Enable(m_gridEditor->IsEditable());
		menu->AppendSubMenu(menuBorder, _("Border"));

		menuItem = menu->AppendSeparator();

		menuItem = menu->AppendCheckItem(wxID_SHOW_CELL, _("Show cells"));
		menuItem->Check(m_gridEditor->GridLinesEnabled());

		menuItem = menu->AppendCheckItem(wxID_SHOW_HEADER, _("Show headers"));
		menuItem->Check(m_gridEditor->GridHeaderEnabled());

		menuItem = menu->AppendCheckItem(wxID_SHOW_AREA, _("Show area"));
		menuItem->Check(m_gridEditor->GridAreaEnabled());

		const ibValueMetaObjectSpreadsheetBase* doc = 
			GetDocument()->ConvertMetaObjectToType<ibValueMetaObjectSpreadsheetBase>();

		menuItem = menu->AppendCheckItem(wxID_EDITABLE, _("Editable"));
		menuItem->Enable(doc ? doc->IsEditable() : true);
		menuItem->Check(m_gridEditor->IsEditable());

		wxMenu* menuGroup = new wxMenu;
		menuItem = menuGroup->Append(wxID_GROUP_ROW, _("Group rows"));
		menuItem->Enable(m_gridEditor->IsEditable());
		menuItem = menuGroup->Append(wxID_UNGROUP_ROW, _("Ungroup rows"));
		menuItem->Enable(m_gridEditor->IsEditable());
		menuGroup->AppendSeparator();
		menuItem = menuGroup->Append(wxID_GROUP_COL, _("Group columns"));
		menuItem->Enable(m_gridEditor->IsEditable());
		menuItem = menuGroup->Append(wxID_UNGROUP_COL, _("Ungroup columns"));
		menuItem->Enable(m_gridEditor->IsEditable());
		menu->AppendSubMenu(menuGroup, _("Grouping"));

		menuItem = menu->AppendSeparator();
		menuItem = menu->Append(wxID_MERGE_CELL, _("Merge cells"));
		menuItem->Enable(m_gridEditor->IsEditable());
	}

	mb->Append(menu, _("Spreadsheet"));
	return mb;
}
#endif 

void ibSpreadsheetEditView::OnActivateView(bool activate, ibView* activeView, ibView* deactiveView)
{
	if (activate) m_gridEditor->ActivateEditor();
}

void ibSpreadsheetEditView::OnDraw(wxDC* WXUNUSED(dc))
{
	// nothing to do here, wxGrid draws itself
}

#include "frontend/win/editor/gridEditor/gridPrintout.h"

wxPrintout* ibSpreadsheetEditView::OnCreatePrintout()
{
	return m_gridEditor->CreatePrintout();
}

#include "frontend/artProvider/artProvider.h"

void ibSpreadsheetEditView::OnCreateToolbar(wxAuiToolBar* toolbar)
{
	toolbar->AddTool(wxID_MERGE_CELL, _("Merge cells"), wxArtProvider::GetBitmapBundle(wxART_MERGE_CELL, wxART_DOC_TEMPLATE), _("Merge cells"), wxItemKind::wxITEM_NORMAL);
	toolbar->EnableTool(wxID_MERGE_CELL, m_gridEditor->IsEditable());
	toolbar->AddSeparator();
	toolbar->AddTool(wxID_AREA_ADD, _("Add area"), wxArtProvider::GetBitmapBundle(wxART_ADD_SECTION, wxART_DOC_TEMPLATE), _("Add area"), wxItemKind::wxITEM_NORMAL);
	toolbar->EnableTool(wxID_AREA_ADD, m_gridEditor->IsEditable());
	toolbar->AddTool(wxID_AREA_DELETE, _("Delete area"), wxArtProvider::GetBitmapBundle(wxART_REMOVE_SECTION, wxART_DOC_TEMPLATE), _("Remove area"), wxItemKind::wxITEM_NORMAL);
	toolbar->EnableTool(wxID_AREA_DELETE, m_gridEditor->IsEditable());
	toolbar->AddSeparator();
	toolbar->AddTool(wxID_SHOW_CELL, _("Show cells"), wxArtProvider::GetBitmapBundle(wxART_SHOW_CELL, wxART_DOC_TEMPLATE), _("Show cells"));
	toolbar->AddTool(wxID_SHOW_HEADER, _("Show headers"), wxArtProvider::GetBitmapBundle(wxART_SHOW_HEADER, wxART_DOC_TEMPLATE), _("Show headers"));
	toolbar->AddTool(wxID_SHOW_AREA, _("Show area"), wxArtProvider::GetBitmapBundle(wxART_SHOW_SECTION, wxART_DOC_TEMPLATE), _("Show area"));
	toolbar->AddSeparator();
	toolbar->AddTool(wxID_DOCK_TABLE, _("Freeze row/col table"), wxArtProvider::GetBitmapBundle(wxART_BORDER, wxART_DOC_TEMPLATE), _("Freeze row/col table"));
	toolbar->EnableTool(wxID_DOCK_TABLE, m_gridEditor->IsEditable());
}

bool ibSpreadsheetEditView::OnClose(bool deleteWindow)
{
	//Activate(false);

	if (deleteWindow) {
		GetFrame()->Destroy();
		SetFrame(nullptr);
	}

	if (ibMetaView::OnClose(deleteWindow)) {

		m_gridEditor->Freeze();

		m_gridEditor->Destroy();
		m_gridEditor = nullptr;

		return true;
	}

	return false;
}

void ibSpreadsheetEditView::OnMenuEvent(wxCommandEvent& event)
{
	wxArrayInt arrRows = m_gridEditor->GetSelectedRows();
	const int selected_row = arrRows.Count() > 0 ? arrRows[0] : -1;

	wxArrayInt arrCols = m_gridEditor->GetSelectedCols();
	const int selected_col = arrCols.Count() > 0 ? arrCols[0] : -1;

	ibGridBlockCoords coords = m_gridEditor->GetSelectedCellRange();

	switch (event.GetId())
	{
	case wxID_MERGE_CELL:
		m_gridEditor->MergeCells();
		break;
	case wxID_GROUP_ROW:
		m_gridEditor->GroupSelectedRows();
		break;
	case wxID_UNGROUP_ROW:
		m_gridEditor->UngroupSelectedRows();
		break;
	case wxID_GROUP_COL:
		m_gridEditor->GroupSelectedCols();
		break;
	case wxID_UNGROUP_COL:
		m_gridEditor->UngroupSelectedCols();
		break;
	case wxID_AREA_ADD:
		m_gridEditor->AddArea();
		break;
	case wxID_AREA_DELETE:
		m_gridEditor->DeleteArea();
		break;
	case wxID_FREEZE_ROW:
		if (selected_row >= 0 && m_gridEditor->IsSelection())
			m_gridEditor->FreezeTo(selected_row, 0);
		break;
	case wxID_FREEZE_COL:
		if (selected_col >= 0 && m_gridEditor->IsSelection())
			m_gridEditor->FreezeTo(0, selected_col);
		break;
	case wxID_PRINT_BRAKE_ROW_ADD:
		if (selected_row >= 0 && m_gridEditor->IsSelection())
			m_gridEditor->AddRowBrake(selected_row);
		break;
	case wxID_PRINT_BRAKE_COL_ADD:
		if (selected_col >= 0 && m_gridEditor->IsSelection())
			m_gridEditor->AddColBrake(selected_col);
		break;
	case wxID_PRINT_BRAKE_ROW_DELETE:
		if (selected_row >= 0 && m_gridEditor->IsSelection())
			m_gridEditor->DeleteRowBrake(selected_row);
		break;
	case wxID_PRINT_BRAKE_COL_DELETE:
		if (selected_col >= 0 && m_gridEditor->IsSelection())
			m_gridEditor->DeleteColBrake(selected_col);
		break;
	case wxID_SHOW_CELL:
		m_gridEditor->ShowCells();
		break;
	case wxID_SHOW_HEADER:
		m_gridEditor->ShowHeader();
		break;
	case wxID_SHOW_AREA:
		m_gridEditor->ShowArea();
		break;
	case wxID_DOCK_TABLE:
		m_gridEditor->DockTable();
		break;

	case wxID_BORDER_LEFT:
		for (int row = coords.GetTopRow(); row <= coords.GetBottomRow(); row++)
			m_gridEditor->SetCellBorderLeft(row, coords.GetLeftCol(), wxPenStyle::wxPENSTYLE_SOLID, *wxBLACK, 1);
		break;
	case wxID_BORDER_TOP:
		for (int col = coords.GetLeftCol(); col <= coords.GetRightCol(); col++)
			m_gridEditor->SetCellBorderTop(coords.GetTopRow(), col, wxPenStyle::wxPENSTYLE_SOLID, *wxBLACK, 1);
		break;
	case wxID_BORDER_RIGHT:
		for (int row = coords.GetTopRow(); row <= coords.GetBottomRow(); row++)
			m_gridEditor->SetCellBorderRight(row, coords.GetRightCol(), wxPenStyle::wxPENSTYLE_SOLID, *wxBLACK, 1);
		break;
	case wxID_BORDER_BOTTOM:
		for (int col = coords.GetLeftCol(); col <= coords.GetRightCol(); col++)
			m_gridEditor->SetCellBorderBottom(coords.GetBottomRow(), col, wxPenStyle::wxPENSTYLE_SOLID, *wxBLACK, 1);
		break;
	case wxID_BORDER_ALL:
		for (int row = coords.GetTopRow(); row <= coords.GetBottomRow(); row++)
			for (int col = coords.GetLeftCol(); col <= coords.GetRightCol(); col++) {
				m_gridEditor->SetCellBorderLeft(row, col, wxPenStyle::wxPENSTYLE_SOLID, *wxBLACK, 1);
				m_gridEditor->SetCellBorderTop(row, col, wxPenStyle::wxPENSTYLE_SOLID, *wxBLACK, 1);
				m_gridEditor->SetCellBorderRight(row, col, wxPenStyle::wxPENSTYLE_SOLID, *wxBLACK, 1);
				m_gridEditor->SetCellBorderBottom(row, col, wxPenStyle::wxPENSTYLE_SOLID, *wxBLACK, 1);
			}
		break;
	case wxID_BORDER_AROUND:
		for (int row = coords.GetTopRow(); row <= coords.GetBottomRow(); row++)
			m_gridEditor->SetCellBorderLeft(row, coords.GetLeftCol(), wxPenStyle::wxPENSTYLE_SOLID, *wxBLACK, 1);
		for (int col = coords.GetLeftCol(); col <= coords.GetRightCol(); col++)
			m_gridEditor->SetCellBorderTop(coords.GetTopRow(), col, wxPenStyle::wxPENSTYLE_SOLID, *wxBLACK, 1);
		for (int row = coords.GetTopRow(); row <= coords.GetBottomRow(); row++)
			m_gridEditor->SetCellBorderRight(row, coords.GetRightCol(), wxPenStyle::wxPENSTYLE_SOLID, *wxBLACK, 1);
		for (int col = coords.GetLeftCol(); col <= coords.GetRightCol(); col++)
			m_gridEditor->SetCellBorderBottom(coords.GetBottomRow(), col, wxPenStyle::wxPENSTYLE_SOLID, *wxBLACK, 1);
		break;
	case wxID_BORDER_NONE:
		for (int row = coords.GetTopRow(); row <= coords.GetBottomRow(); row++)
			for (int col = coords.GetLeftCol(); col <= coords.GetRightCol(); col++) {
				m_gridEditor->SetCellBorderLeft(row, col, wxPenStyle::wxPENSTYLE_TRANSPARENT, *wxBLACK, 1);
				m_gridEditor->SetCellBorderTop(row, col, wxPenStyle::wxPENSTYLE_TRANSPARENT, *wxBLACK, 1);
				m_gridEditor->SetCellBorderRight(row, col, wxPenStyle::wxPENSTYLE_TRANSPARENT, *wxBLACK, 1);
				m_gridEditor->SetCellBorderBottom(row, col, wxPenStyle::wxPENSTYLE_TRANSPARENT, *wxBLACK, 1);
			}
		break;

	case wxID_EDITABLE:
		m_gridEditor->EnableEditing(!m_gridEditor->IsEditable());
		// Toolbar buttons and menu items were gated on IsEditable() at build
		// time (OnCreateToolbar / CreateMenuBar) but no wxUpdateUIEvent
		// handlers exist to resync them on mode flip. Force a rebuild through
		// mainFrame->ActivateView so both come back in the correct state.
		if (mainFrame != nullptr)
			mainFrame->ActivateView(this, true);
		break;
	}

	//event.Skip();
}

// ----------------------------------------------------------------------------
// ibSpreadsheetDocument: ibDocument and wxGrid married
// ----------------------------------------------------------------------------

wxIMPLEMENT_ABSTRACT_CLASS(ibSpreadsheetDocument, ibMetaDocument);

wxIMPLEMENT_DYNAMIC_CLASS(ibSpreadsheetFileDocument, ibSpreadsheetDocument);
wxIMPLEMENT_DYNAMIC_CLASS(ibSpreadsheetEditDocument, ibSpreadsheetDocument);

wxCommandProcessor* ibSpreadsheetDocument::OnCreateCommandProcessor()
{
	return new ibGridCommandProcessor(GetGridCtrl());
}

ibGridEditor* ibSpreadsheetDocument::GetGridCtrl() const
{
	ibView* view = GetFirstView();
	return view ? wxDynamicCast(view, ibSpreadsheetEditView)->GetGridCtrl() : nullptr;
}

// ----------------------------------------------------------------------------
// ibSpreadsheetFileDocument: ibDocument and wxGrid married
// ----------------------------------------------------------------------------

bool ibSpreadsheetFileDocument::OnCreate(const wxString& path, long flags)
{
	if (!ibMetaDocument::OnCreate(path, flags))
		return false;

	return GetGridCtrl()->AssociatibDocument(m_spreadSheetDocument);
}

// Since text windows have their own method for saving to/loading from files,
// we override DoSave/OpenDocument instead of Save/LoadObject
bool ibSpreadsheetFileDocument::DoOpenDocument(const wxString& filename)
{
	if (!m_spreadSheetDocument->LoadFromFile(filename))
		return false;

	return GetGridCtrl()->LoadDocument(m_spreadSheetDocument->GetSpreadsheetDesc());
}

bool ibSpreadsheetFileDocument::DoSaveDocument(const wxString& filename)
{
	if (!GetGridCtrl()->SaveDocument(m_spreadSheetDocument->GetSpreadsheetDesc()))
		return false;

	return m_spreadSheetDocument->SaveToFile(filename);
}

// ----------------------------------------------------------------------------
// ibSpreadsheetEditDocument: ibDocument and wxGrid married
// ----------------------------------------------------------------------------

bool ibSpreadsheetEditDocument::OnCreate(const wxString& path, long flags)
{
	if (!ibMetaDocument::OnCreate(path, flags))
		return false;

	const ibValueMetaObjectSpreadsheetBase* creator = m_metaObject->ConvertToType<ibValueMetaObjectSpreadsheetBase>();
	if (creator != nullptr)
		return GetGridCtrl()->LoadDocument(creator->GetSpreadsheetDesc());

	return false;
}

bool ibSpreadsheetEditDocument::SaveAs()
{
	ibDocTemplate* docTemplate = GetDocumentTemplate();
	if (!docTemplate)
		return false;

	const ibValueMetaObjectSpreadsheetBase* creator = m_metaObject->ConvertToType<ibValueMetaObjectSpreadsheetBase>();
	if (creator == nullptr)
		return false;

#ifdef wxHAS_MULTIPLE_FILEDLG_FILTERS
	wxString filter = docTemplate->GetDescription() + wxT(" (") +
		docTemplate->GetFileFilter() + wxT(")|") +
		docTemplate->GetFileFilter();

	// Now see if there are some other template with identical view and document
	// classes, whose filters may also be used.
	if (docTemplate->GetViewClassInfo() && docTemplate->GetDocClassInfo())
	{
		wxList::compatibility_iterator
			node = docTemplate->GetDocumentManager()->GetTemplates().GetFirst();
		while (node)
		{
			ibDocTemplate* t = (ibDocTemplate*)node->GetData();

			if (t->IsVisible() && t != docTemplate &&
				t->GetViewClassInfo() == docTemplate->GetViewClassInfo() &&
				t->GetDocClassInfo() == docTemplate->GetDocClassInfo())
			{
				// add a '|' to separate this filter from the previous one
				if (!filter.empty())
					filter << wxT('|');

				filter << t->GetDescription()
					<< wxT(" (") << t->GetFileFilter() << wxT(") |")
					<< t->GetFileFilter();
			}

			node = node->GetNext();
		}
	}
#else
	wxString filter = docTemplate->GetFileFilter();
#endif

	wxString defaultDir = docTemplate->GetDirectory();
	if (defaultDir.empty())
	{
		defaultDir = wxPathOnly(creator->GetName());
		if (defaultDir.empty())
			defaultDir = GetDocumentManager()->GetLastDirectory();
	}

	wxString fileName = wxFileSelector(_("Save As"),
		defaultDir,
		wxFileNameFromPath(creator->GetName()),
		docTemplate->GetDefaultExtension(),
		filter,
		wxFD_SAVE | wxFD_OVERWRITE_PROMPT,
		GetDocumentWindow());

	if (fileName.empty())
		return false; // cancelled by user

	// Files that were not saved correctly are not added to the FileHistory.
	if (!DoSaveDocument(fileName))
		return false;

	// A file that doesn't use the default extension of its document template
	// cannot be opened via the FileHistory, so we do not add it.
	if (docTemplate->FileMatchesTemplate(fileName))
	{
		GetDocumentManager()->AddFileToHistory(fileName);
	}
	//else: the user will probably not be able to open the file again, so we
	//      could warn about the wrong file-extension here

	return true;
}

bool ibSpreadsheetEditDocument::DoSaveDocument(const wxString& filename)
{
	wxObjectDataPtr<ibBackendSpreadsheetObject>spreadSheetDocument;
	if (!GetGridCtrl()->GetActivibDocument(spreadSheetDocument))
		return false;
	return spreadSheetDocument->SaveToFile(filename);
}

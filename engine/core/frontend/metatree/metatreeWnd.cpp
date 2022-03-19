////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : metatree window
////////////////////////////////////////////////////////////////////////////

#include "metatreeWnd.h"
#include "compiler/debugger/debugClient.h"
#include "frontend/theme/luna_auitoolbar.h"

wxIMPLEMENT_DYNAMIC_CLASS(CMetadataTree, wxPanel);

//**********************************************************************************
//*                                  metatree									   *
//**********************************************************************************

extern wxImageList *GetImageList();

wxBEGIN_EVENT_TABLE(CMetadataTree, wxPanel)
wxEND_EVENT_TABLE()

CMetadataTree::CMetadataTree()
	: wxPanel(),
	m_metaData(NULL), m_docParent(NULL),
	m_bReadOnly(false)
{
	m_metaTreeToolbar = NULL;
	m_metaTreeWnd = NULL;
}

CMetadataTree::CMetadataTree(wxWindow *parent, int id)
	: wxPanel(parent, id),
	m_metaData(NULL), m_docParent(NULL), 
	m_bReadOnly(false)
{
	wxPanel *m_toolbarPanel = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize);
	wxASSERT(m_metaTreeToolbar);

	m_metaTreeToolbar = new wxAuiToolBar(m_toolbarPanel, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxAUI_TB_HORZ_TEXT);
	m_metaTreeToolbar->AddTool(ID_METATREE_NEW, _("new"), wxGetImageBMPFromResource(IDB_EDIT_NEW), wxNullBitmap, wxItemKind::wxITEM_NORMAL, _("new item"), _("new item"), NULL);
	m_metaTreeToolbar->AddTool(ID_METATREE_EDIT, _("edit"), wxGetImageBMPFromResource(IDB_EDIT), wxNullBitmap, wxItemKind::wxITEM_NORMAL, _("edit item"), _("edit item"), NULL);
	m_metaTreeToolbar->AddTool(ID_METATREE_REMOVE, _("remove"), wxGetImageBMPFromResource(IDB_EDIT_CUT), wxNullBitmap, wxItemKind::wxITEM_NORMAL, _("remove item"), _("remove item"), NULL);
	m_metaTreeToolbar->Realize();

	m_metaTreeToolbar->SetArtProvider(new CAuiGenericToolBarArt());

	//Create main tree
	m_metaTreeWnd = new CMetadataTreeWnd(this);

	//set image list
	m_metaTreeWnd->SetImageList(::GetImageList());

	m_metaTreeToolbar->Bind(wxEVT_MENU, &CMetadataTree::CMetadataTreeWnd::OnCreateItem, m_metaTreeWnd, ID_METATREE_NEW);
	m_metaTreeToolbar->Bind(wxEVT_MENU, &CMetadataTree::CMetadataTreeWnd::OnEditItem, m_metaTreeWnd, ID_METATREE_EDIT);
	m_metaTreeToolbar->Bind(wxEVT_MENU, &CMetadataTree::CMetadataTreeWnd::OnRemoveItem, m_metaTreeWnd, ID_METATREE_REMOVE);

	// Set up the sizer for the panel
	wxBoxSizer* panelSizerToolBar = new wxBoxSizer(wxVERTICAL);
	panelSizerToolBar->Add(m_metaTreeToolbar, 1, wxEXPAND);
	m_toolbarPanel->SetSizer(panelSizerToolBar);

	// Set up the sizer for the panel
	wxBoxSizer* panelSizer = new wxBoxSizer(wxVERTICAL);
	panelSizer->Add(m_toolbarPanel, 0, wxEXPAND, 1);
	panelSizer->Add(m_metaTreeWnd, 1, wxEXPAND, 1);
	SetSizer(panelSizer);

	//Init tree
	InitTree();
}

CMetadataTree::CMetadataTree(CDocument *docParent, wxWindow *parent, int id)
	: wxPanel(parent, id),
	m_metaData(NULL), m_docParent(docParent),
	m_bReadOnly(false)
{
	wxPanel *m_toolbarPanel = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize);
	wxASSERT(m_metaTreeToolbar);

	m_metaTreeToolbar = new wxAuiToolBar(m_toolbarPanel, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxAUI_TB_HORZ_TEXT);
	m_metaTreeToolbar->AddTool(ID_METATREE_NEW, _("new"), wxGetImageBMPFromResource(IDB_EDIT_NEW), wxNullBitmap, wxItemKind::wxITEM_NORMAL, _("new item"), _("new item"), NULL);
	m_metaTreeToolbar->AddTool(ID_METATREE_EDIT, _("edit"), wxGetImageBMPFromResource(IDB_EDIT), wxNullBitmap, wxItemKind::wxITEM_NORMAL, _("edit item"), _("edit item"), NULL);
	m_metaTreeToolbar->AddTool(ID_METATREE_REMOVE, _("remove"), wxGetImageBMPFromResource(IDB_EDIT_CUT), wxNullBitmap, wxItemKind::wxITEM_NORMAL, _("remove item"), _("remove item"), NULL);
	m_metaTreeToolbar->Realize();

	m_metaTreeToolbar->SetArtProvider(new CAuiGenericToolBarArt());

	//Create main tree
	m_metaTreeWnd = new CMetadataTreeWnd(this);

	//set image list
	m_metaTreeWnd->SetImageList(::GetImageList());

	m_metaTreeToolbar->Bind(wxEVT_MENU, &CMetadataTree::CMetadataTreeWnd::OnCreateItem, m_metaTreeWnd, ID_METATREE_NEW);
	m_metaTreeToolbar->Bind(wxEVT_MENU, &CMetadataTree::CMetadataTreeWnd::OnEditItem, m_metaTreeWnd, ID_METATREE_EDIT);
	m_metaTreeToolbar->Bind(wxEVT_MENU, &CMetadataTree::CMetadataTreeWnd::OnRemoveItem, m_metaTreeWnd, ID_METATREE_REMOVE);

	// Set up the sizer for the panel
	wxBoxSizer* panelSizerToolBar = new wxBoxSizer(wxVERTICAL);
	panelSizerToolBar->Add(m_metaTreeToolbar, 1, wxEXPAND);
	m_toolbarPanel->SetSizer(panelSizerToolBar);

	// Set up the sizer for the panel
	wxBoxSizer* panelSizer = new wxBoxSizer(wxVERTICAL);
	panelSizer->Add(m_toolbarPanel, 0, wxEXPAND, 1);
	panelSizer->Add(m_metaTreeWnd, 1, wxEXPAND, 1);
	SetSizer(panelSizer);

	//Init tree
	InitTree();
}

CMetadataTree::~CMetadataTree()
{
	/*if (m_metaData)
		m_metaData->SetMetaTree(NULL);*/

	m_metaTreeToolbar->Unbind(wxEVT_MENU, &CMetadataTree::CMetadataTreeWnd::OnCreateItem, m_metaTreeWnd, ID_METATREE_NEW);
	m_metaTreeToolbar->Unbind(wxEVT_MENU, &CMetadataTree::CMetadataTreeWnd::OnEditItem, m_metaTreeWnd, ID_METATREE_EDIT);
	m_metaTreeToolbar->Unbind(wxEVT_MENU, &CMetadataTree::CMetadataTreeWnd::OnRemoveItem, m_metaTreeWnd, ID_METATREE_REMOVE);
}

wxIMPLEMENT_DYNAMIC_CLASS(CMetadataTree::CMetadataTreeWnd, wxTreeCtrl);

//**********************************************************************************
//*                                  metatree window						       *
//**********************************************************************************

wxBEGIN_EVENT_TABLE(CMetadataTree::CMetadataTreeWnd, wxTreeCtrl)

EVT_PROPERTY_MODIFIED(CMetadataTree::CMetadataTreeWnd::OnPropertyModified)

EVT_LEFT_UP(CMetadataTree::CMetadataTreeWnd::OnLeftUp)
EVT_LEFT_DOWN(CMetadataTree::CMetadataTreeWnd::OnLeftDown)
EVT_LEFT_DCLICK(CMetadataTree::CMetadataTreeWnd::OnLeftDClick)
EVT_RIGHT_UP(CMetadataTree::CMetadataTreeWnd::OnRightUp)
EVT_RIGHT_DOWN(CMetadataTree::CMetadataTreeWnd::OnRightDown)
EVT_RIGHT_DCLICK(CMetadataTree::CMetadataTreeWnd::OnRightDClick)
EVT_MOTION(CMetadataTree::CMetadataTreeWnd::OnMouseMove)
EVT_KEY_UP(CMetadataTree::CMetadataTreeWnd::OnKeyUp)
EVT_KEY_DOWN(CMetadataTree::CMetadataTreeWnd::OnKeyDown)

EVT_TREE_SEL_CHANGING(wxID_ANY,CMetadataTree::CMetadataTreeWnd::OnSelecting)
EVT_TREE_SEL_CHANGED(wxID_ANY, CMetadataTree::CMetadataTreeWnd::OnSelected)

EVT_TREE_ITEM_COLLAPSING(wxID_ANY, CMetadataTree::CMetadataTreeWnd::OnCollapsing)
EVT_TREE_ITEM_EXPANDING(wxID_ANY, CMetadataTree::CMetadataTreeWnd::OnExpanding)

EVT_MENU(ID_METATREE_NEW, CMetadataTree::CMetadataTreeWnd::OnCreateItem)
EVT_MENU(ID_METATREE_EDIT, CMetadataTree::CMetadataTreeWnd::OnEditItem)
EVT_MENU(ID_METATREE_REMOVE, CMetadataTree::CMetadataTreeWnd::OnRemoveItem)
EVT_MENU(ID_METATREE_PROPERTY, CMetadataTree::CMetadataTreeWnd::OnPropertyItem)

EVT_MENU(ID_METATREE_INSERT, CMetadataTree::CMetadataTreeWnd::OnInsertItem)
EVT_MENU(ID_METATREE_REPLACE, CMetadataTree::CMetadataTreeWnd::OnReplaceItem)
EVT_MENU(ID_METATREE_SAVE, CMetadataTree::CMetadataTreeWnd::OnSaveItem)

EVT_MENU(wxID_COPY, CMetadataTree::CMetadataTreeWnd::OnCopyItem)
EVT_MENU(wxID_PASTE, CMetadataTree::CMetadataTreeWnd::OnPasteItem)

EVT_DEBUG(CMetadataTree::CMetadataTreeWnd::OnDebugEvent)

wxEND_EVENT_TABLE()

CMetadataTree::CMetadataTreeWnd::CMetadataTreeWnd()
	: wxTreeCtrl(), m_ownerTree(NULL)
{
	//set double buffered
	SetDoubleBuffered(true); 
}

CMetadataTree::CMetadataTreeWnd::CMetadataTreeWnd(CMetadataTree *parent)
	: wxTreeCtrl(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTR_HAS_BUTTONS | wxTR_SINGLE), m_ownerTree(parent)
{
	debugClient->AddHandler(this);

	wxAcceleratorEntry entries[2];
	entries[0].Set(wxACCEL_CTRL, (int) 'C', wxID_COPY);
	entries[1].Set(wxACCEL_CTRL, (int) 'V', wxID_PASTE);

	wxAcceleratorTable accel(2, entries);
	SetAcceleratorTable(accel);

	//set double buffered
	SetDoubleBuffered(true);
}

CMetadataTree::CMetadataTreeWnd::~CMetadataTreeWnd()
{
	debugClient->RemoveHandler(this);
}
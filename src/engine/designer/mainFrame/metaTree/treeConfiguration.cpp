////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : metaTree window
////////////////////////////////////////////////////////////////////////////

#include "treeConfiguration.h"
#include "backend/debugger/debugClient.h"
#include "frontend/win/theme/luna_toolbarart.h"

wxIMPLEMENT_ABSTRACT_CLASS(ibMetaTreeBase, wxPanel);
wxIMPLEMENT_DYNAMIC_CLASS(ibConfigurationTree, ibMetaTreeBase);

//**********************************************************************************
//*                                  metaTree									   *
//**********************************************************************************

#define ICON_SIZE 16

#include "frontend/artProvider/artProvider.h"

// THE TOOLBAR, once. This existed all along and was called by nobody — all three trees inlined the
// same eight AddTool calls, in five constructors. (The wxASSERT that used to open it asserted the
// pointer was non-null *before* creating it — an indeterminate read, and backwards besides.)
void ibMetaTreeBase::CreateToolBar(wxWindow* parent)
{
	m_metaTreeToolbar = new wxAuiToolBar(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxAUI_TB_HORZ_LAYOUT);
	m_metaTreeToolbar->AddTool(ID_METATREE_NEW, _("New"), wxArtProvider::GetBitmapBundle(wxART_ADD, wxART_FRONTEND, wxSize(16, 16)), _("New item"));
	m_metaTreeToolbar->AddTool(ID_METATREE_EDIT, _("Edit"), wxArtProvider::GetBitmapBundle(wxART_EDIT, wxART_FRONTEND, wxSize(16, 16)), _("Edit item"));
	m_metaTreeToolbar->AddTool(ID_METATREE_DELETE, _("Delete"), wxArtProvider::GetBitmapBundle(wxART_DELETE, wxART_FRONTEND, wxSize(16, 16)), _("Delete item"));
	m_metaTreeToolbar->AddSeparator();
	m_metaTreeToolbar->AddTool(ID_METATREE_UP, _("Up"), wxArtProvider::GetBitmapBundle(wxART_UP, wxART_FRONTEND, wxSize(16, 16)), _("Up item"));
	m_metaTreeToolbar->AddTool(ID_METATREE_DOWM, _("Down"), wxArtProvider::GetBitmapBundle(wxART_DOWN, wxART_FRONTEND, wxSize(16, 16)), _("Down item"));
	m_metaTreeToolbar->AddSeparator();
	m_metaTreeToolbar->AddTool(ID_METATREE_SORT, _("Sort"), wxArtProvider::GetBitmapBundle(wxART_SORT, wxART_FRONTEND, wxSize(16, 16)), _("Sort item"));
	m_metaTreeToolbar->Realize();

	m_metaTreeToolbar->SetArtProvider(new wxAuiLunaToolBarArt());
}

ibConfigurationTree::ibConfigurationTree()
	: ibMetaTreeBase(), m_metaData(nullptr)
{
	m_metaTreeToolbar = nullptr;
	m_metaTreeCtrl = nullptr;
}

ibConfigurationTree::ibConfigurationTree(wxWindow* parent, int id)
	: ibMetaTreeBase(parent, id), m_metaData(nullptr)
{
	CreateToolBar(this);

	// Card-style depth: panel slightly darker than tree creates a
	// "raised card" effect — the tree floats above the panel surround
	// rather than blending into pure-white background. AUI's dark
	// dock background already frames the pane; this is the inner tier.
	this->SetBackgroundColour(wxColour(184, 201, 212));   // #B8C9D4 powder-blue panel
	//Create main tree
	m_metaTreeCtrl = new ibMetaTreeCtrl(this);
	m_metaTreeCtrl->SetBackgroundColour(wxColour(250, 247, 240));  // #FAF7F0 cream tree (matches editor)

	//set image list
	m_metaTreeCtrl->AssignImageList(
		new wxImageList(ICON_SIZE, ICON_SIZE)
	);

	m_searchTree = new wxSearchCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
	m_searchTree->Bind(wxEVT_SEARCH, &ibConfigurationTree::ibMetaTreeCtrl::OnStartSearch, m_metaTreeCtrl);
	m_searchTree->Bind(wxEVT_COMMAND_TEXT_UPDATED, &ibConfigurationTree::ibMetaTreeCtrl::OnCancelSearch, m_metaTreeCtrl);
	m_searchTree->ShowCancelButton(true);

	m_metaTreeToolbar->Bind(wxEVT_MENU, &ibConfigurationTree::ibMetaTreeCtrl::OnCreateItem, m_metaTreeCtrl, ID_METATREE_NEW);
	m_metaTreeToolbar->Bind(wxEVT_MENU, &ibConfigurationTree::ibMetaTreeCtrl::OnEditItem, m_metaTreeCtrl, ID_METATREE_EDIT);
	m_metaTreeToolbar->Bind(wxEVT_MENU, &ibConfigurationTree::ibMetaTreeCtrl::OnRemoveItem, m_metaTreeCtrl, ID_METATREE_DELETE);

	m_metaTreeToolbar->Bind(wxEVT_MENU, &ibConfigurationTree::ibMetaTreeCtrl::OnUpItem, m_metaTreeCtrl, ID_METATREE_UP);
	m_metaTreeToolbar->Bind(wxEVT_MENU, &ibConfigurationTree::ibMetaTreeCtrl::OnDownItem, m_metaTreeCtrl, ID_METATREE_DOWM);
	m_metaTreeToolbar->Bind(wxEVT_MENU, &ibConfigurationTree::ibMetaTreeCtrl::OnSortItem, m_metaTreeCtrl, ID_METATREE_SORT);

	// Set up the sizer for the panel
	wxBoxSizer* panelSizer = new wxBoxSizer(wxVERTICAL);
	panelSizer->Add(m_searchTree, 0, wxEXPAND, 1);
	panelSizer->Add(m_metaTreeToolbar, 0, wxEXPAND, 1);
	panelSizer->Add(m_metaTreeCtrl, 1, wxEXPAND, 1);
	SetSizer(panelSizer);

	//Init tree
	InitTree();
}

ibConfigurationTree::ibConfigurationTree(ibMetaDocument* docParent, wxWindow* parent, int id)
	: ibMetaTreeBase(docParent, parent, id), m_metaData(nullptr)
{
	CreateToolBar(this);

	// Card-style depth: panel slightly darker than tree creates a
	// "raised card" effect — the tree floats above the panel surround
	// rather than blending into pure-white background. AUI's dark
	// dock background already frames the pane; this is the inner tier.
	this->SetBackgroundColour(wxColour(184, 201, 212));   // #B8C9D4 powder-blue panel
	//Create main tree
	m_metaTreeCtrl = new ibMetaTreeCtrl(this);
	m_metaTreeCtrl->SetBackgroundColour(wxColour(250, 247, 240));  // #FAF7F0 cream tree (matches editor)

	//set image list
	m_metaTreeCtrl->AssignImageList(
		new wxImageList(ICON_SIZE, ICON_SIZE)
	);

	m_metaTreeToolbar->Bind(wxEVT_MENU, &ibConfigurationTree::ibMetaTreeCtrl::OnCreateItem, m_metaTreeCtrl, ID_METATREE_NEW);
	m_metaTreeToolbar->Bind(wxEVT_MENU, &ibConfigurationTree::ibMetaTreeCtrl::OnEditItem, m_metaTreeCtrl, ID_METATREE_EDIT);
	m_metaTreeToolbar->Bind(wxEVT_MENU, &ibConfigurationTree::ibMetaTreeCtrl::OnRemoveItem, m_metaTreeCtrl, ID_METATREE_DELETE);

	m_metaTreeToolbar->Bind(wxEVT_MENU, &ibConfigurationTree::ibMetaTreeCtrl::OnUpItem, m_metaTreeCtrl, ID_METATREE_UP);
	m_metaTreeToolbar->Bind(wxEVT_MENU, &ibConfigurationTree::ibMetaTreeCtrl::OnDownItem, m_metaTreeCtrl, ID_METATREE_DOWM);
	m_metaTreeToolbar->Bind(wxEVT_MENU, &ibConfigurationTree::ibMetaTreeCtrl::OnSortItem, m_metaTreeCtrl, ID_METATREE_SORT);

	// Set up the sizer for the panel
	wxBoxSizer* panelSizer = new wxBoxSizer(wxVERTICAL);
	panelSizer->Add(m_metaTreeToolbar, 0, wxEXPAND, 1);
	panelSizer->Add(m_metaTreeCtrl, 1, wxEXPAND, 1);
	SetSizer(panelSizer);

	//Init tree
	InitTree();
}

ibConfigurationTree::~ibConfigurationTree()
{
	if (m_metaData != nullptr) m_metaData->SetMetaTree(nullptr);

	// The default constructor leaves both of these null and is reachable through the RTTI factory,
	// so the teardown asks before it unbinds.
	if (m_metaTreeToolbar != nullptr && m_metaTreeCtrl != nullptr) {
		m_metaTreeToolbar->Unbind(wxEVT_MENU, &ibConfigurationTree::ibMetaTreeCtrl::OnCreateItem, m_metaTreeCtrl, ID_METATREE_NEW);
		m_metaTreeToolbar->Unbind(wxEVT_MENU, &ibConfigurationTree::ibMetaTreeCtrl::OnEditItem, m_metaTreeCtrl, ID_METATREE_EDIT);
		m_metaTreeToolbar->Unbind(wxEVT_MENU, &ibConfigurationTree::ibMetaTreeCtrl::OnRemoveItem, m_metaTreeCtrl, ID_METATREE_DELETE);

		m_metaTreeToolbar->Unbind(wxEVT_MENU, &ibConfigurationTree::ibMetaTreeCtrl::OnUpItem, m_metaTreeCtrl, ID_METATREE_UP);
		m_metaTreeToolbar->Unbind(wxEVT_MENU, &ibConfigurationTree::ibMetaTreeCtrl::OnDownItem, m_metaTreeCtrl, ID_METATREE_DOWM);
		m_metaTreeToolbar->Unbind(wxEVT_MENU, &ibConfigurationTree::ibMetaTreeCtrl::OnSortItem, m_metaTreeCtrl, ID_METATREE_SORT);
	}

	if (m_searchTree)
		m_searchTree->Destroy();
}

//**********************************************************************************

wxIMPLEMENT_DYNAMIC_CLASS(ibConfigurationTree::ibMetaTreeCtrl, wxTreeCtrl);

//**********************************************************************************

#include "win/dlg/predefinedEditor.h"

void ibConfigurationTree::EditPredefinedValues(ibValueMetaObjectRecordDataHierarchyMutableRef* valueMetaObjectHierarchy)
{
	ibDialogPredefinedEditor dlg(this, valueMetaObjectHierarchy);
	dlg.ShowModal();
}

#include "win/dlg/homePageEditor.h"

void ibConfigurationTree::EditHomePage(ibValueMetaObjectConfiguration* metaObjectConfiguration)
{
	ibDialogHomePageEditor dlg(this, metaObjectConfiguration);
	dlg.ShowModal();
}

//**********************************************************************************
//*                                  metaTree window						       *
//**********************************************************************************

wxBEGIN_EVENT_TABLE(ibConfigurationTree::ibMetaTreeCtrl, wxTreeCtrl)

EVT_LEFT_UP(ibConfigurationTree::ibMetaTreeCtrl::OnLeftUp)
EVT_LEFT_DOWN(ibConfigurationTree::ibMetaTreeCtrl::OnLeftDown)
EVT_LEFT_DCLICK(ibConfigurationTree::ibMetaTreeCtrl::OnLeftDClick)
EVT_RIGHT_UP(ibConfigurationTree::ibMetaTreeCtrl::OnRightUp)
EVT_RIGHT_DOWN(ibConfigurationTree::ibMetaTreeCtrl::OnRightDown)
EVT_RIGHT_DCLICK(ibConfigurationTree::ibMetaTreeCtrl::OnRightDClick)
EVT_MOTION(ibConfigurationTree::ibMetaTreeCtrl::OnMouseMove)
EVT_KEY_UP(ibConfigurationTree::ibMetaTreeCtrl::OnKeyUp)
EVT_KEY_DOWN(ibConfigurationTree::ibMetaTreeCtrl::OnKeyDown)

EVT_TREE_SEL_CHANGING(wxID_ANY, ibConfigurationTree::ibMetaTreeCtrl::OnSelecting)
EVT_TREE_SEL_CHANGED(wxID_ANY, ibConfigurationTree::ibMetaTreeCtrl::OnSelected)

EVT_TREE_ITEM_COLLAPSING(wxID_ANY, ibConfigurationTree::ibMetaTreeCtrl::OnCollapsing)
EVT_TREE_ITEM_EXPANDING(wxID_ANY, ibConfigurationTree::ibMetaTreeCtrl::OnExpanding)

EVT_TREE_BEGIN_DRAG(wxID_ANY, ibConfigurationTree::ibMetaTreeCtrl::OnBeginDrag)
EVT_TREE_END_DRAG(wxID_ANY, ibConfigurationTree::ibMetaTreeCtrl::OnEndDrag)

EVT_TREE_ITEM_ACTIVATED(wxID_ANY, ibConfigurationTree::ibMetaTreeCtrl::OnItemActivated)

EVT_MENU(ID_METATREE_NEW, ibConfigurationTree::ibMetaTreeCtrl::OnCreateItem)
EVT_MENU(ID_METATREE_EDIT, ibConfigurationTree::ibMetaTreeCtrl::OnEditItem)
EVT_MENU(ID_METATREE_DELETE, ibConfigurationTree::ibMetaTreeCtrl::OnRemoveItem)
EVT_MENU(ID_METATREE_PROPERTY, ibConfigurationTree::ibMetaTreeCtrl::OnPropertyItem)

EVT_MENU(ID_METATREE_INSERT, ibConfigurationTree::ibMetaTreeCtrl::OnInsertItem)
EVT_MENU(ID_METATREE_REPLACE, ibConfigurationTree::ibMetaTreeCtrl::OnReplaceItem)
EVT_MENU(ID_METATREE_SAVE, ibConfigurationTree::ibMetaTreeCtrl::OnSaveItem)

EVT_SET_FOCUS(ibConfigurationTree::ibMetaTreeCtrl::OnSetFocus)
EVT_KILL_FOCUS(ibConfigurationTree::ibMetaTreeCtrl::OnSetFocus)

EVT_MENU(wxID_COPY, ibConfigurationTree::ibMetaTreeCtrl::OnCopyItem)
EVT_MENU(wxID_PASTE, ibConfigurationTree::ibMetaTreeCtrl::OnPasteItem)

wxEND_EVENT_TABLE()

ibConfigurationTree::ibMetaTreeCtrl::ibMetaTreeCtrl()
	: wxTreeCtrl(), m_ownerTree(nullptr), m_metaView(new ibMetaTreeView(this))
{
	//set double buffered
	SetDoubleBuffered(true);
}

ibConfigurationTree::ibMetaTreeCtrl::ibMetaTreeCtrl(ibConfigurationTree* parent)
	: wxTreeCtrl(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTR_HAS_BUTTONS | wxTR_SINGLE | wxTR_NO_LINES | wxTR_TWIST_BUTTONS), m_ownerTree(parent), m_metaView(new ibMetaTreeView(this))
{
	wxAcceleratorEntry entries[2];
	entries[0].Set(wxACCEL_CTRL, (int)'C', wxID_COPY);
	entries[1].Set(wxACCEL_CTRL, (int)'V', wxID_PASTE);

	wxAcceleratorTable accel(2, entries);
	SetAcceleratorTable(accel);

	//set double buffered
	SetDoubleBuffered(true);
}

#include "frontend/docView/docView.h"

ibConfigurationTree::ibMetaTreeCtrl::~ibMetaTreeCtrl()
{
	if (docManager != nullptr &&
		m_metaView == docManager->GetAnyUsableView()) {
		docManager->ActivateView(nullptr);
	}

	wxDELETE(m_metaView);
}

/////////////////////////////////////////////////////////////////

#include "frontend/mainFrame/mainFrame.h"

void ibConfigurationTree::ibMetaTreeCtrl::ibMetaTreeView::OnActivateView(bool activate, ibView* activeView, ibView* deactiveView)
{
	if (activate) {
		const wxTreeItemId& item = m_ownerTree->GetSelection();
		objectInspector->SelectObject(
			item.IsOk() ? m_ownerTree->GetMetaObject(item) : nullptr);
	}
}

/////////////////////////////////////////////////////////////////
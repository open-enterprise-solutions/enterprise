////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : metaTree window
////////////////////////////////////////////////////////////////////////////

#include "treeConfiguration.h"
#include "backend/debugger/debugClient.h"
#include "frontend/win/theme/luna_toolbarart.h"

wxIMPLEMENT_ABSTRACT_CLASS(ibMetaDataTree, wxPanel);
wxIMPLEMENT_DYNAMIC_CLASS(ibMetadataTree, ibMetaDataTree);

//**********************************************************************************
//*                                  metaTree									   *
//**********************************************************************************

#define ICON_SIZE 16

#include "frontend/artProvider/artProvider.h"

void ibMetaDataTree::CreateToolBar(wxWindow* parent)
{
	wxASSERT(m_metaTreeToolbar);

	m_metaTreeToolbar = new wxAuiToolBar(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxAUI_TB_HORZ_LAYOUT);
	m_metaTreeToolbar->AddTool(ID_METATREE_NEW, _("New"), wxArtProvider::GetBitmap(wxART_ADD, wxART_FRONTEND, wxSize(16, 16)), _("New item"));
	m_metaTreeToolbar->AddTool(ID_METATREE_EDIT, _("Edit"), wxArtProvider::GetBitmap(wxART_EDIT, wxART_FRONTEND, wxSize(16, 16)), _("Edit item"));
	m_metaTreeToolbar->AddTool(ID_METATREE_DELETE, _("Delete"), wxArtProvider::GetBitmap(wxART_DELETE, wxART_FRONTEND, wxSize(16, 16)), _("Delete item"));
	m_metaTreeToolbar->AddSeparator();
	m_metaTreeToolbar->AddTool(ID_METATREE_UP, _("Up"), wxArtProvider::GetBitmap(wxART_UP, wxART_FRONTEND, wxSize(16, 16)), _("Up item"));
	m_metaTreeToolbar->AddTool(ID_METATREE_DOWM, _("Down"), wxArtProvider::GetBitmap(wxART_DOWN, wxART_FRONTEND, wxSize(16, 16)), _("Down item"));
	m_metaTreeToolbar->AddSeparator();
	m_metaTreeToolbar->AddTool(ID_METATREE_SORT, _("Sort"), wxArtProvider::GetBitmap(wxART_SORT, wxART_FRONTEND, wxSize(16, 16)), _("Sort item"));
	m_metaTreeToolbar->Realize();

	m_metaTreeToolbar->SetArtProvider(new wxAuiLunaToolBarArt());
}

ibMetadataTree::ibMetadataTree()
	: ibMetaDataTree(), m_metaData(nullptr)
{
	m_metaTreeToolbar = nullptr;
	m_metaTreeCtrl = nullptr;
}

ibMetadataTree::ibMetadataTree(wxWindow* parent, int id)
	: ibMetaDataTree(parent, id), m_metaData(nullptr)
{
	m_metaTreeToolbar = new wxAuiToolBar(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxAUI_TB_HORZ_LAYOUT);
	m_metaTreeToolbar->AddTool(ID_METATREE_NEW, _("New"), wxArtProvider::GetBitmap(wxART_ADD, wxART_FRONTEND, wxSize(16, 16)), _("New item"));
	m_metaTreeToolbar->AddTool(ID_METATREE_EDIT, _("Edit"), wxArtProvider::GetBitmap(wxART_EDIT, wxART_FRONTEND, wxSize(16, 16)), _("Edit item"));
	m_metaTreeToolbar->AddTool(ID_METATREE_DELETE, _("Delete"), wxArtProvider::GetBitmap(wxART_DELETE, wxART_FRONTEND, wxSize(16, 16)), _("Delete item"));
	m_metaTreeToolbar->AddSeparator();
	m_metaTreeToolbar->AddTool(ID_METATREE_UP, _("Up"), wxArtProvider::GetBitmap(wxART_UP, wxART_FRONTEND, wxSize(16, 16)), _("Up item"));
	m_metaTreeToolbar->AddTool(ID_METATREE_DOWM, _("Down"), wxArtProvider::GetBitmap(wxART_DOWN, wxART_FRONTEND, wxSize(16, 16)), _("Down item"));
	m_metaTreeToolbar->AddSeparator();
	m_metaTreeToolbar->AddTool(ID_METATREE_SORT, _("Sort"), wxArtProvider::GetBitmap(wxART_SORT, wxART_FRONTEND, wxSize(16, 16)), _("Sort item"));
	m_metaTreeToolbar->Realize();

	m_metaTreeToolbar->SetArtProvider(new wxAuiLunaToolBarArt());

	//Create main tree
	m_metaTreeCtrl = new ibMetaTreeCtrl(this);
	m_metaTreeCtrl->SetBackgroundColour(wxColour(250, 250, 250));

	//set image list
	m_metaTreeCtrl->AssignImageList(
		new wxImageList(ICON_SIZE, ICON_SIZE)
	);

	m_searchTree = new wxSearchCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
	m_searchTree->Bind(wxEVT_SEARCH, &ibMetadataTree::ibMetaTreeCtrl::OnStartSearch, m_metaTreeCtrl);
	m_searchTree->Bind(wxEVT_COMMAND_TEXT_UPDATED, &ibMetadataTree::ibMetaTreeCtrl::OnCancelSearch, m_metaTreeCtrl);
	m_searchTree->ShowCancelButton(true);

	m_metaTreeToolbar->Bind(wxEVT_MENU, &ibMetadataTree::ibMetaTreeCtrl::OnCreateItem, m_metaTreeCtrl, ID_METATREE_NEW);
	m_metaTreeToolbar->Bind(wxEVT_MENU, &ibMetadataTree::ibMetaTreeCtrl::OnEditItem, m_metaTreeCtrl, ID_METATREE_EDIT);
	m_metaTreeToolbar->Bind(wxEVT_MENU, &ibMetadataTree::ibMetaTreeCtrl::OnRemoveItem, m_metaTreeCtrl, ID_METATREE_DELETE);

	m_metaTreeToolbar->Bind(wxEVT_MENU, &ibMetadataTree::ibMetaTreeCtrl::OnUpItem, m_metaTreeCtrl, ID_METATREE_UP);
	m_metaTreeToolbar->Bind(wxEVT_MENU, &ibMetadataTree::ibMetaTreeCtrl::OnDownItem, m_metaTreeCtrl, ID_METATREE_DOWM);
	m_metaTreeToolbar->Bind(wxEVT_MENU, &ibMetadataTree::ibMetaTreeCtrl::OnSortItem, m_metaTreeCtrl, ID_METATREE_SORT);

	// Set up the sizer for the panel
	wxBoxSizer* panelSizer = new wxBoxSizer(wxVERTICAL);
	panelSizer->Add(m_searchTree, 0, wxEXPAND, 1);
	panelSizer->Add(m_metaTreeToolbar, 0, wxEXPAND, 1);
	panelSizer->Add(m_metaTreeCtrl, 1, wxEXPAND, 1);
	SetSizer(panelSizer);

	//Init tree
	InitTree();
}

ibMetadataTree::ibMetadataTree(ibMetaDocument* docParent, wxWindow* parent, int id)
	: ibMetaDataTree(docParent, parent, id), m_metaData(nullptr)
{
	wxASSERT(m_metaTreeToolbar);

	m_metaTreeToolbar = new wxAuiToolBar(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxAUI_TB_HORZ_LAYOUT);
	m_metaTreeToolbar->AddTool(ID_METATREE_NEW, _("New"), wxArtProvider::GetBitmap(wxART_ADD, wxART_FRONTEND, wxSize(16, 16)), _("New item"));
	m_metaTreeToolbar->AddTool(ID_METATREE_EDIT, _("Edit"), wxArtProvider::GetBitmap(wxART_EDIT, wxART_FRONTEND, wxSize(16, 16)), _("Edit item"));
	m_metaTreeToolbar->AddTool(ID_METATREE_DELETE, _("Delete"), wxArtProvider::GetBitmap(wxART_DELETE, wxART_FRONTEND, wxSize(16, 16)), _("Delete item"));
	m_metaTreeToolbar->AddSeparator();
	m_metaTreeToolbar->AddTool(ID_METATREE_UP, _("Up"), wxArtProvider::GetBitmap(wxART_UP, wxART_FRONTEND, wxSize(16, 16)), _("Up item"));
	m_metaTreeToolbar->AddTool(ID_METATREE_DOWM, _("Down"), wxArtProvider::GetBitmap(wxART_DOWN, wxART_FRONTEND, wxSize(16, 16)), _("Down item"));
	m_metaTreeToolbar->AddSeparator();
	m_metaTreeToolbar->AddTool(ID_METATREE_SORT, _("Sort"), wxArtProvider::GetBitmap(wxART_SORT, wxART_FRONTEND, wxSize(16, 16)), _("Sort item"));
	m_metaTreeToolbar->Realize();

	m_metaTreeToolbar->SetArtProvider(new wxAuiLunaToolBarArt());

	//Create main tree
	m_metaTreeCtrl = new ibMetaTreeCtrl(this);
	m_metaTreeCtrl->SetBackgroundColour(wxColour(250, 250, 250));

	//set image list
	m_metaTreeCtrl->AssignImageList(
		new wxImageList(ICON_SIZE, ICON_SIZE)
	);

	m_metaTreeToolbar->Bind(wxEVT_MENU, &ibMetadataTree::ibMetaTreeCtrl::OnCreateItem, m_metaTreeCtrl, ID_METATREE_NEW);
	m_metaTreeToolbar->Bind(wxEVT_MENU, &ibMetadataTree::ibMetaTreeCtrl::OnEditItem, m_metaTreeCtrl, ID_METATREE_EDIT);
	m_metaTreeToolbar->Bind(wxEVT_MENU, &ibMetadataTree::ibMetaTreeCtrl::OnRemoveItem, m_metaTreeCtrl, ID_METATREE_DELETE);

	m_metaTreeToolbar->Bind(wxEVT_MENU, &ibMetadataTree::ibMetaTreeCtrl::OnUpItem, m_metaTreeCtrl, ID_METATREE_UP);
	m_metaTreeToolbar->Bind(wxEVT_MENU, &ibMetadataTree::ibMetaTreeCtrl::OnDownItem, m_metaTreeCtrl, ID_METATREE_DOWM);
	m_metaTreeToolbar->Bind(wxEVT_MENU, &ibMetadataTree::ibMetaTreeCtrl::OnSortItem, m_metaTreeCtrl, ID_METATREE_SORT);

	// Set up the sizer for the panel
	wxBoxSizer* panelSizer = new wxBoxSizer(wxVERTICAL);
	panelSizer->Add(m_metaTreeToolbar, 0, wxEXPAND, 1);
	panelSizer->Add(m_metaTreeCtrl, 1, wxEXPAND, 1);
	SetSizer(panelSizer);

	//Init tree
	InitTree();
}

ibMetadataTree::~ibMetadataTree()
{
	if (m_metaData != nullptr) m_metaData->SetMetaTree(nullptr);

	m_metaTreeToolbar->Unbind(wxEVT_MENU, &ibMetadataTree::ibMetaTreeCtrl::OnCreateItem, m_metaTreeCtrl, ID_METATREE_NEW);
	m_metaTreeToolbar->Unbind(wxEVT_MENU, &ibMetadataTree::ibMetaTreeCtrl::OnEditItem, m_metaTreeCtrl, ID_METATREE_EDIT);
	m_metaTreeToolbar->Unbind(wxEVT_MENU, &ibMetadataTree::ibMetaTreeCtrl::OnRemoveItem, m_metaTreeCtrl, ID_METATREE_DELETE);

	m_metaTreeToolbar->Unbind(wxEVT_MENU, &ibMetadataTree::ibMetaTreeCtrl::OnUpItem, m_metaTreeCtrl, ID_METATREE_UP);
	m_metaTreeToolbar->Unbind(wxEVT_MENU, &ibMetadataTree::ibMetaTreeCtrl::OnDownItem, m_metaTreeCtrl, ID_METATREE_DOWM);
	m_metaTreeToolbar->Unbind(wxEVT_MENU, &ibMetadataTree::ibMetaTreeCtrl::OnSortItem, m_metaTreeCtrl, ID_METATREE_SORT);

	if (m_searchTree)
		m_searchTree->Destroy();
}

//**********************************************************************************

wxIMPLEMENT_DYNAMIC_CLASS(ibMetadataTree::ibMetaTreeCtrl, wxTreeCtrl);

//**********************************************************************************

#include "win/dlg/predefinedEditor.h"

void ibMetadataTree::EditPredefinedValues(ibValueMetaObjectRecordDataHierarchyMutableRef* valueMetaObjectHierarchy)
{
	ibDialogPredefinedEditor dlg(this, valueMetaObjectHierarchy);
	dlg.ShowModal();
}

//**********************************************************************************
//*                                  metaTree window						       *
//**********************************************************************************

wxBEGIN_EVENT_TABLE(ibMetadataTree::ibMetaTreeCtrl, wxTreeCtrl)

EVT_LEFT_UP(ibMetadataTree::ibMetaTreeCtrl::OnLeftUp)
EVT_LEFT_DOWN(ibMetadataTree::ibMetaTreeCtrl::OnLeftDown)
EVT_LEFT_DCLICK(ibMetadataTree::ibMetaTreeCtrl::OnLeftDClick)
EVT_RIGHT_UP(ibMetadataTree::ibMetaTreeCtrl::OnRightUp)
EVT_RIGHT_DOWN(ibMetadataTree::ibMetaTreeCtrl::OnRightDown)
EVT_RIGHT_DCLICK(ibMetadataTree::ibMetaTreeCtrl::OnRightDClick)
EVT_MOTION(ibMetadataTree::ibMetaTreeCtrl::OnMouseMove)
EVT_KEY_UP(ibMetadataTree::ibMetaTreeCtrl::OnKeyUp)
EVT_KEY_DOWN(ibMetadataTree::ibMetaTreeCtrl::OnKeyDown)

EVT_TREE_SEL_CHANGING(wxID_ANY, ibMetadataTree::ibMetaTreeCtrl::OnSelecting)
EVT_TREE_SEL_CHANGED(wxID_ANY, ibMetadataTree::ibMetaTreeCtrl::OnSelected)

EVT_TREE_ITEM_COLLAPSING(wxID_ANY, ibMetadataTree::ibMetaTreeCtrl::OnCollapsing)
EVT_TREE_ITEM_EXPANDING(wxID_ANY, ibMetadataTree::ibMetaTreeCtrl::OnExpanding)

EVT_TREE_BEGIN_DRAG(wxID_ANY, ibMetadataTree::ibMetaTreeCtrl::OnBeginDrag)
EVT_TREE_END_DRAG(wxID_ANY, ibMetadataTree::ibMetaTreeCtrl::OnEndDrag)

EVT_TREE_ITEM_ACTIVATED(wxID_ANY, ibMetadataTree::ibMetaTreeCtrl::OnItemActivated)

EVT_MENU(ID_METATREE_NEW, ibMetadataTree::ibMetaTreeCtrl::OnCreateItem)
EVT_MENU(ID_METATREE_EDIT, ibMetadataTree::ibMetaTreeCtrl::OnEditItem)
EVT_MENU(ID_METATREE_DELETE, ibMetadataTree::ibMetaTreeCtrl::OnRemoveItem)
EVT_MENU(ID_METATREE_PROPERTY, ibMetadataTree::ibMetaTreeCtrl::OnPropertyItem)

EVT_MENU(ID_METATREE_INSERT, ibMetadataTree::ibMetaTreeCtrl::OnInsertItem)
EVT_MENU(ID_METATREE_REPLACE, ibMetadataTree::ibMetaTreeCtrl::OnReplaceItem)
EVT_MENU(ID_METATREE_SAVE, ibMetadataTree::ibMetaTreeCtrl::OnSaveItem)

EVT_SET_FOCUS(ibMetadataTree::ibMetaTreeCtrl::OnSetFocus)
EVT_KILL_FOCUS(ibMetadataTree::ibMetaTreeCtrl::OnSetFocus)

EVT_MENU(wxID_COPY, ibMetadataTree::ibMetaTreeCtrl::OnCopyItem)
EVT_MENU(wxID_PASTE, ibMetadataTree::ibMetaTreeCtrl::OnPasteItem)

wxEND_EVENT_TABLE()

ibMetadataTree::ibMetaTreeCtrl::ibMetaTreeCtrl()
	: wxTreeCtrl(), m_ownerTree(nullptr), m_metaView(new ibMatadataTreeView(this))
{
	//set double buffered
	SetDoubleBuffered(true);
}

ibMetadataTree::ibMetaTreeCtrl::ibMetaTreeCtrl(ibMetadataTree* parent)
	: wxTreeCtrl(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTR_HAS_BUTTONS | wxTR_SINGLE | wxTR_NO_LINES | wxTR_TWIST_BUTTONS), m_ownerTree(parent), m_metaView(new ibMatadataTreeView(this))
{
	wxAcceleratorEntry entries[2];
	entries[0].Set(wxACCEL_CTRL, (int)'C', wxID_COPY);
	entries[1].Set(wxACCEL_CTRL, (int)'V', wxID_PASTE);

	wxAcceleratorTable accel(2, entries);
	SetAcceleratorTable(accel);

	//set double buffered
	SetDoubleBuffered(true);
}

#include "frontend/docView/docManager.h"

ibMetadataTree::ibMetaTreeCtrl::~ibMetaTreeCtrl()
{
	if (docManager != nullptr &&
		m_metaView == docManager->GetAnyUsableView()) {
		docManager->ActivateView(nullptr);
	}

	wxDELETE(m_metaView);
}

/////////////////////////////////////////////////////////////////

#include "frontend/mainFrame/mainFrame.h"

void ibMetadataTree::ibMetaTreeCtrl::ibMatadataTreeView::OnActivateView(bool activate, wxView* activeView, wxView* deactiveView)
{
	if (activate) {
		const wxTreeItemId& item = m_ownerTree->GetSelection();
		objectInspector->SelectObject(
			item.IsOk() ? m_ownerTree->GetMetaObject(item) : nullptr);
	}
}

/////////////////////////////////////////////////////////////////
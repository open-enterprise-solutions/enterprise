////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : metaTree window
////////////////////////////////////////////////////////////////////////////

#include "treeConfiguration.h"

#include <wx/statbox.h>

#include "frmclient/artProvider/artProvider.h"
#include "frmclient/docView/docView.h"   // ibMetaView — the tree's view
#include "frmclient/win/theme/luna_toolbarart.h"

//**********************************************************************************

#include "designer-thin/win/dlg/predefinedEditor.h"

void ibConfigurationTree::EditPredefinedValues(ibValueMetaObjectRecordDataHierarchyMutableRef* valueMetaObjectHierarchy)
{
	// What the object declares is the server's — read as the editor opens.
	valueMetaObjectHierarchy->ReadPredefinedValues();

	ibDialogPredefinedEditor dlg(this, valueMetaObjectHierarchy);
	dlg.ShowModal();
}

#include "designer-thin/win/dlg/homePageEditor.h"

void ibConfigurationTree::EditHomePage(ibValueMetaObjectConfiguration* metaObjectConfiguration)
{
	// The workspace and the forms it may show are the server's — read as the editor opens.
	metaObjectConfiguration->ReadHomePage();

	ibDialogHomePageEditor dlg(this, metaObjectConfiguration);
	dlg.ShowModal();
}

//**********************************************************************************
//*                                  metaTree									   *
//**********************************************************************************

#define ICON_SIZE 16

class ibMetaTreeBase::ibMetaTreeView : public ibMetaView {
public:

	ibMetaTreeView(ibMetaTreeBase* tree) : m_ownerTree(tree) {}
	virtual void OnActivateView(bool activate, ibFrontendView* activeView, ibFrontendView* deactiveView) override;

private:
	ibMetaTreeBase* m_ownerTree;
};

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

ibMetaTreeBase::ibMetaTreeBase(wxWindow* parent, long long tab, ibProtocol::ibView::ibKind kind)
	: wxPanel(parent, wxID_ANY), m_tab(tab), m_metaView(new ibMetaTreeView(this))
{
	const bool configuration = kind == ibProtocol::ibView::ibKind::Configuration;

	// …an external file's toolbar and rows framed in a box, under its header (the desktop's).
	wxStaticBoxSizer* const treeBox = !configuration ? new wxStaticBoxSizer(new wxStaticBox(this, wxID_ANY, wxT("")), wxVERTICAL) : nullptr;
	wxWindow* const treeParent = treeBox != nullptr ? static_cast<wxWindow*>(treeBox->GetStaticBox()) : this;

	CreateToolBar(treeParent);

	// Card-style depth: panel slightly darker than tree creates a
	// "raised card" effect — the tree floats above the panel surround
	// rather than blending into pure-white background. AUI's dark
	// dock background already frames the pane; this is the inner tier.
	this->SetBackgroundColour(wxColour(184, 201, 212));   // #B8C9D4 powder-blue panel
	//Create main tree
	// …an external file's root hidden: its groups are the tree (the desktop's).
	m_metaTreeCtrl = new wxTreeCtrl(treeParent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		wxTR_HAS_BUTTONS | wxTR_SINGLE | wxTR_NO_LINES | wxTR_TWIST_BUTTONS | (!configuration ? wxTR_HIDE_ROOT : 0));
	m_metaTreeCtrl->SetBackgroundColour(wxColour(250, 247, 240));  // #FAF7F0 cream tree (matches editor)
	m_metaTreeCtrl->SetDoubleBuffered(true);

	//set image list
	m_metaTreeCtrl->AssignImageList(
		new wxImageList(ICON_SIZE, ICON_SIZE)
	);

	wxAcceleratorEntry entries[2];
	entries[0].Set(wxACCEL_CTRL, (int)'C', wxID_COPY);
	entries[1].Set(wxACCEL_CTRL, (int)'V', wxID_PASTE);

	wxAcceleratorTable accel(2, entries);
	m_metaTreeCtrl->SetAcceleratorTable(accel);

	m_metaTreeCtrl->Bind(wxEVT_LEFT_DOWN, &ibMetaTreeBase::OnLeftDown, this);
	m_metaTreeCtrl->Bind(wxEVT_LEFT_DCLICK, &ibMetaTreeBase::OnLeftDClick, this);
	m_metaTreeCtrl->Bind(wxEVT_TREE_ITEM_ACTIVATED, &ibMetaTreeBase::OnItemActivated, this);
	m_metaTreeCtrl->Bind(wxEVT_TREE_SEL_CHANGED, &ibMetaTreeBase::OnSelected, this);
	m_metaTreeCtrl->Bind(wxEVT_SET_FOCUS, &ibMetaTreeBase::OnSetFocus, this);
	m_metaTreeCtrl->Bind(wxEVT_KILL_FOCUS, &ibMetaTreeBase::OnSetFocus, this);
	m_metaTreeCtrl->Bind(wxEVT_TREE_ITEM_EXPANDED, &ibMetaTreeBase::OnExpanded, this);
	m_metaTreeCtrl->Bind(wxEVT_TREE_ITEM_COLLAPSED, &ibMetaTreeBase::OnCollapsed, this);
	m_metaTreeCtrl->Bind(wxEVT_TREE_ITEM_MENU, &ibMetaTreeBase::OnItemMenu, this);
	m_metaTreeCtrl->Bind(wxEVT_TREE_BEGIN_DRAG, &ibMetaTreeBase::OnBeginDrag, this);
	m_metaTreeCtrl->Bind(wxEVT_TREE_END_DRAG, &ibMetaTreeBase::OnEndDrag, this);

	// The toolbar's, the context menu's and the keys' — each a command event that comes up to the panel.
	Bind(wxEVT_MENU, [this](wxCommandEvent&) { CreateItem(); }, ID_METATREE_NEW);
	Bind(wxEVT_MENU, [this](wxCommandEvent&) { EditItem(); }, ID_METATREE_EDIT);
	Bind(wxEVT_MENU, [this](wxCommandEvent&) { RemoveItem(); }, ID_METATREE_DELETE);
	Bind(wxEVT_MENU, [this](wxCommandEvent&) { PropertyItem(); }, ID_METATREE_PROPERTY);
	Bind(wxEVT_MENU, &ibMetaTreeBase::OnEditHelp, this, ID_METATREE_HELP);
	Bind(wxEVT_MENU, &ibMetaTreeBase::OnEditNotes, this, ID_METATREE_NOTES);

	Bind(wxEVT_MENU, [this](wxCommandEvent&) { UpItem(); }, ID_METATREE_UP);
	Bind(wxEVT_MENU, [this](wxCommandEvent&) { DownItem(); }, ID_METATREE_DOWM);
	Bind(wxEVT_MENU, [this](wxCommandEvent&) { SortItem(); }, ID_METATREE_SORT);

	Bind(wxEVT_MENU, [this](wxCommandEvent&) { CopyItem(); }, wxID_COPY);
	Bind(wxEVT_MENU, [this](wxCommandEvent&) { PasteItem(); }, wxID_PASTE);

	// Set up the sizer for the panel
	wxBoxSizer* panelSizer = new wxBoxSizer(wxVERTICAL);

	// …the search, the configuration's alone (an external file's tree has none — the desktop's).
	if (configuration) {
		m_searchTree = new wxSearchCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
		m_searchTree->Bind(wxEVT_SEARCH, &ibMetaTreeBase::OnStartSearch, this);
		m_searchTree->Bind(wxEVT_COMMAND_TEXT_UPDATED, &ibMetaTreeBase::OnCancelSearch, this);
		m_searchTree->ShowCancelButton(true);
		panelSizer->Add(m_searchTree, 0, wxEXPAND, 1);
	}

	if (treeBox != nullptr) {
		treeBox->Add(m_metaTreeToolbar, 0, wxALL | wxEXPAND, 0);
		treeBox->Add(m_metaTreeCtrl, 1, wxALL | wxEXPAND, 0);
		panelSizer->Add(treeBox, 1, wxEXPAND, FromDIP(5));
	}
	else {
		panelSizer->Add(m_metaTreeToolbar, 0, wxEXPAND, 1);
		panelSizer->Add(m_metaTreeCtrl, 1, wxEXPAND, 1);
	}
	SetSizer(panelSizer);

	UpdateToolbar();
}

ibMetaTreeBase::~ibMetaTreeBase()
{
	// …the manager not left holding it as its current view: a view lets go of itself there only when it has a document.
	if (docManager != nullptr)
		docManager->ActivateView(m_metaView, false);
	wxDELETE(m_metaView);
}

/////////////////////////////////////////////////////////////////

void ibMetaTreeBase::ibMetaTreeView::OnActivateView(bool activate, ibFrontendView* WXUNUSED(activeView),
	ibFrontendView* WXUNUSED(deactiveView))
{
	if (activate)
		m_ownerTree->ActivateTree();
}

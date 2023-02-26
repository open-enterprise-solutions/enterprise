////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : metatree window
////////////////////////////////////////////////////////////////////////////

#include "metatreeWnd.h"
#include "core/compiler/debugger/debugClient.h"
#include "frontend/theme/luna_auitoolbar.h"

wxIMPLEMENT_ABSTRACT_CLASS(IMetadataTree, wxPanel);
wxIMPLEMENT_DYNAMIC_CLASS(CMetadataTree, IMetadataTree);

//**********************************************************************************
//*                                  metatree									   *
//**********************************************************************************

#define ICON_SIZE 16

#include <wx/artprov.h>

void IMetadataTree::CreateToolBar(wxWindow* parent)
{
	wxASSERT(m_metaTreeToolbar);

	m_metaTreeToolbar = new wxAuiToolBar(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxAUI_TB_HORZ_LAYOUT);
	m_metaTreeToolbar->AddTool(ID_METATREE_NEW, _("New"), wxArtProvider::GetBitmap(wxART_PLUS, wxART_MENU), _("New item"));
	m_metaTreeToolbar->AddTool(ID_METATREE_EDIT, _("Edit"), wxArtProvider::GetBitmap(wxART_EDIT, wxART_MENU), _("Edit item"));
	m_metaTreeToolbar->AddTool(ID_METATREE_REMOVE, _("remove"), wxArtProvider::GetBitmap(wxART_DELETE, wxART_MENU), _("Remove item"));
	m_metaTreeToolbar->AddSeparator();
	m_metaTreeToolbar->AddTool(ID_METATREE_UP, _("Up"), wxArtProvider::GetBitmap(wxART_GO_UP, wxART_MENU), _("Up item"));
	m_metaTreeToolbar->AddTool(ID_METATREE_DOWM, _("Down"), wxArtProvider::GetBitmap(wxART_GO_DOWN, wxART_MENU), _("Down item"));
	m_metaTreeToolbar->AddTool(ID_METATREE_SORT, _("Sort"), wxArtProvider::GetBitmap(wxART_HELP_SETTINGS, wxART_MENU), _("Sort item"));
	m_metaTreeToolbar->Realize();

	m_metaTreeToolbar->SetArtProvider(new CAuiGenericToolBarArt());
}


CMetadataTree::CMetadataTree()
	: IMetadataTree(), m_metaData(NULL)
{
	m_metaTreeToolbar = NULL;
	m_metaTreeWnd = NULL;
}

CMetadataTree::CMetadataTree(wxWindow* parent, int id)
	: IMetadataTree(parent, id), m_metaData(NULL)
{
	wxASSERT(m_metaTreeToolbar);

	m_metaTreeToolbar = new wxAuiToolBar(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxAUI_TB_HORZ_LAYOUT);
	m_metaTreeToolbar->AddTool(ID_METATREE_NEW, _("New"), wxArtProvider::GetBitmap(wxART_PLUS, wxART_MENU), _("New item"));
	m_metaTreeToolbar->AddTool(ID_METATREE_EDIT, _("Edit"), wxArtProvider::GetBitmap(wxART_EDIT, wxART_MENU), _("Edit item"));
	m_metaTreeToolbar->AddTool(ID_METATREE_REMOVE, _("Remove"), wxArtProvider::GetBitmap(wxART_DELETE, wxART_MENU), _("Remove item"));
	m_metaTreeToolbar->AddSeparator();
	m_metaTreeToolbar->AddTool(ID_METATREE_UP, _("Up"), wxArtProvider::GetBitmap(wxART_GO_UP, wxART_MENU), _("Up item"));
	m_metaTreeToolbar->AddTool(ID_METATREE_DOWM, _("Down"), wxArtProvider::GetBitmap(wxART_GO_DOWN, wxART_MENU), _("Down item"));
	m_metaTreeToolbar->AddTool(ID_METATREE_SORT, _("Sort"), wxArtProvider::GetBitmap(wxART_HELP_SETTINGS, wxART_MENU), _("Sort item"));
	m_metaTreeToolbar->Realize();

	m_metaTreeToolbar->SetArtProvider(new CAuiGenericToolBarArt());

	//Create main tree
	m_metaTreeWnd = new CMetadataTreeWnd(this);
	m_metaTreeWnd->SetBackgroundColour(RGB(250, 250, 250));

	//set image list
	m_metaTreeWnd->SetImageList(
		new wxImageList(ICON_SIZE, ICON_SIZE)
	);

	m_metaTreeToolbar->Bind(wxEVT_MENU, &CMetadataTree::CMetadataTreeWnd::OnCreateItem, m_metaTreeWnd, ID_METATREE_NEW);
	m_metaTreeToolbar->Bind(wxEVT_MENU, &CMetadataTree::CMetadataTreeWnd::OnEditItem, m_metaTreeWnd, ID_METATREE_EDIT);
	m_metaTreeToolbar->Bind(wxEVT_MENU, &CMetadataTree::CMetadataTreeWnd::OnRemoveItem, m_metaTreeWnd, ID_METATREE_REMOVE);

	m_metaTreeToolbar->Bind(wxEVT_MENU, &CMetadataTree::CMetadataTreeWnd::OnUpItem, m_metaTreeWnd, ID_METATREE_UP);
	m_metaTreeToolbar->Bind(wxEVT_MENU, &CMetadataTree::CMetadataTreeWnd::OnDownItem, m_metaTreeWnd, ID_METATREE_DOWM);
	m_metaTreeToolbar->Bind(wxEVT_MENU, &CMetadataTree::CMetadataTreeWnd::OnSortItem, m_metaTreeWnd, ID_METATREE_SORT);

	// Set up the sizer for the panel
	wxBoxSizer* panelSizer = new wxBoxSizer(wxVERTICAL);
	panelSizer->Add(m_metaTreeToolbar, 0, wxEXPAND, 1);
	panelSizer->Add(m_metaTreeWnd, 1, wxEXPAND, 1);
	SetSizer(panelSizer);

	//Init tree
	InitTree();
}

CMetadataTree::CMetadataTree(CDocument* docParent, wxWindow* parent, int id)
	: IMetadataTree(docParent, parent, id), m_metaData(NULL)
{
	wxASSERT(m_metaTreeToolbar);

	m_metaTreeToolbar = new wxAuiToolBar(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxAUI_TB_HORZ_LAYOUT);
	m_metaTreeToolbar->AddTool(ID_METATREE_NEW, _("New"), wxArtProvider::GetBitmap(wxART_PLUS, wxART_MENU), _("New item"));
	m_metaTreeToolbar->AddTool(ID_METATREE_EDIT, _("Edit"), wxArtProvider::GetBitmap(wxART_EDIT, wxART_MENU), _("Edit item"));
	m_metaTreeToolbar->AddTool(ID_METATREE_REMOVE, _("Remove"), wxArtProvider::GetBitmap(wxART_DELETE, wxART_MENU), _("Remove item"));
	m_metaTreeToolbar->AddSeparator();
	m_metaTreeToolbar->AddTool(ID_METATREE_UP, _("Up"), wxArtProvider::GetBitmap(wxART_GO_UP, wxART_MENU), _("Up item"));
	m_metaTreeToolbar->AddTool(ID_METATREE_DOWM, _("Down"), wxArtProvider::GetBitmap(wxART_GO_DOWN, wxART_MENU), _("Down item"));
	m_metaTreeToolbar->AddTool(ID_METATREE_SORT, _("Sort"), wxArtProvider::GetBitmap(wxART_HELP_SETTINGS, wxART_MENU), _("Sort item"));
	m_metaTreeToolbar->Realize();

	m_metaTreeToolbar->SetArtProvider(new CAuiGenericToolBarArt());

	//Create main tree
	m_metaTreeWnd = new CMetadataTreeWnd(this);
	m_metaTreeWnd->SetBackgroundColour(RGB(250, 250, 250));

	//set image list
	m_metaTreeWnd->SetImageList(
		new wxImageList(ICON_SIZE, ICON_SIZE)
	);

	m_metaTreeToolbar->Bind(wxEVT_MENU, &CMetadataTree::CMetadataTreeWnd::OnCreateItem, m_metaTreeWnd, ID_METATREE_NEW);
	m_metaTreeToolbar->Bind(wxEVT_MENU, &CMetadataTree::CMetadataTreeWnd::OnEditItem, m_metaTreeWnd, ID_METATREE_EDIT);
	m_metaTreeToolbar->Bind(wxEVT_MENU, &CMetadataTree::CMetadataTreeWnd::OnRemoveItem, m_metaTreeWnd, ID_METATREE_REMOVE);

	m_metaTreeToolbar->Bind(wxEVT_MENU, &CMetadataTree::CMetadataTreeWnd::OnUpItem, m_metaTreeWnd, ID_METATREE_UP);
	m_metaTreeToolbar->Bind(wxEVT_MENU, &CMetadataTree::CMetadataTreeWnd::OnDownItem, m_metaTreeWnd, ID_METATREE_DOWM);
	m_metaTreeToolbar->Bind(wxEVT_MENU, &CMetadataTree::CMetadataTreeWnd::OnSortItem, m_metaTreeWnd, ID_METATREE_SORT);

	// Set up the sizer for the panel
	wxBoxSizer* panelSizer = new wxBoxSizer(wxVERTICAL);
	panelSizer->Add(m_metaTreeToolbar, 0, wxEXPAND, 1);
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

	m_metaTreeToolbar->Unbind(wxEVT_MENU, &CMetadataTree::CMetadataTreeWnd::OnUpItem, m_metaTreeWnd, ID_METATREE_UP);
	m_metaTreeToolbar->Unbind(wxEVT_MENU, &CMetadataTree::CMetadataTreeWnd::OnDownItem, m_metaTreeWnd, ID_METATREE_DOWM);
	m_metaTreeToolbar->Unbind(wxEVT_MENU, &CMetadataTree::CMetadataTreeWnd::OnSortItem, m_metaTreeWnd, ID_METATREE_SORT);

}

//**********************************************************************************

#include "frontend/mainFrame.h"

CMetadataTree* CMetadataTree::GetMetadataTree()
{
	if (wxAuiDocMDIFrame::GetFrame())
		return mainFrame->GetMetadataTree();
	return NULL;
}

//**********************************************************************************

wxIMPLEMENT_DYNAMIC_CLASS(CMetadataTree::CMetadataTreeWnd, wxTreeCtrl);

//**********************************************************************************
//*                                  metatree window						       *
//**********************************************************************************

wxBEGIN_EVENT_TABLE(CMetadataTree::CMetadataTreeWnd, wxTreeCtrl)

EVT_LEFT_UP(CMetadataTree::CMetadataTreeWnd::OnLeftUp)
EVT_LEFT_DOWN(CMetadataTree::CMetadataTreeWnd::OnLeftDown)
EVT_LEFT_DCLICK(CMetadataTree::CMetadataTreeWnd::OnLeftDClick)
EVT_RIGHT_UP(CMetadataTree::CMetadataTreeWnd::OnRightUp)
EVT_RIGHT_DOWN(CMetadataTree::CMetadataTreeWnd::OnRightDown)
EVT_RIGHT_DCLICK(CMetadataTree::CMetadataTreeWnd::OnRightDClick)
EVT_MOTION(CMetadataTree::CMetadataTreeWnd::OnMouseMove)
EVT_KEY_UP(CMetadataTree::CMetadataTreeWnd::OnKeyUp)
EVT_KEY_DOWN(CMetadataTree::CMetadataTreeWnd::OnKeyDown)

EVT_TREE_SEL_CHANGING(wxID_ANY, CMetadataTree::CMetadataTreeWnd::OnSelecting)
EVT_TREE_SEL_CHANGED(wxID_ANY, CMetadataTree::CMetadataTreeWnd::OnSelected)

EVT_TREE_ITEM_COLLAPSING(wxID_ANY, CMetadataTree::CMetadataTreeWnd::OnCollapsing)
EVT_TREE_ITEM_EXPANDING(wxID_ANY, CMetadataTree::CMetadataTreeWnd::OnExpanding)

EVT_TREE_BEGIN_DRAG(wxID_ANY, CMetadataTree::CMetadataTreeWnd::OnBeginDrag)
EVT_TREE_END_DRAG(wxID_ANY, CMetadataTree::CMetadataTreeWnd::OnEndDrag)

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

CMetadataTree::CMetadataTreeWnd::CMetadataTreeWnd(CMetadataTree* parent)
	: wxTreeCtrl(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTR_HAS_BUTTONS | wxTR_SINGLE), m_ownerTree(parent)
{
	debugClient->AddHandler(this);

	wxAcceleratorEntry entries[2];
	entries[0].Set(wxACCEL_CTRL, (int)'C', wxID_COPY);
	entries[1].Set(wxACCEL_CTRL, (int)'V', wxID_PASTE);

	wxAcceleratorTable accel(2, entries);
	SetAcceleratorTable(accel);

	//set double buffered
	SetDoubleBuffered(true);
}

CMetadataTree::CMetadataTreeWnd::~CMetadataTreeWnd()
{
	debugClient->RemoveHandler(this);
}
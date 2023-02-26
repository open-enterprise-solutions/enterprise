#include "interfaceEditor.h"
#include <wx/artprov.h>

#define ICON_SIZE 16

CInterfaceEditor::CInterfaceEditor(wxWindow* parent,
	wxWindowID winid, CDocument* document, IMetaObject* metaObject) :
	wxPanel(parent, winid), m_document(document), m_metaObject(metaObject)
{
	m_metaTreeToolbar = new wxAuiToolBar(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxAUI_TB_HORZ_TEXT);
	m_metaTreeToolbar->SetArtProvider(new CAuiGenericToolBarArt());
	m_metaTreeToolbar->AddTool(ID_MENUEDIT_NEW, _("New"), wxArtProvider::GetBitmap(wxART_PLUS, wxART_MENU));
	m_metaTreeToolbar->AddTool(ID_MENUEDIT_EDIT, _("Edit"), wxArtProvider::GetBitmap(wxART_EDIT, wxART_MENU));
	m_metaTreeToolbar->AddTool(ID_MENUEDIT_REMOVE, _("Remove"), wxArtProvider::GetBitmap(wxART_DELETE, wxART_MENU));
	m_metaTreeToolbar->Realize();

	m_metaTreeToolbar->Bind(wxEVT_MENU, &CInterfaceEditor::OnCreateItem, this, ID_MENUEDIT_NEW);
	m_metaTreeToolbar->Bind(wxEVT_MENU, &CInterfaceEditor::OnEditItem, this, ID_MENUEDIT_EDIT);
	m_metaTreeToolbar->Bind(wxEVT_MENU, &CInterfaceEditor::OnRemoveItem, this, ID_MENUEDIT_REMOVE);

	m_menuCtrl = new wxTreeCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTR_HAS_BUTTONS | wxTR_LINES_AT_ROOT | wxTR_SINGLE | wxSIMPLE_BORDER);
	m_menuCtrl->SetDoubleBuffered(true);

	m_menuCtrl->Bind(wxEVT_TREE_SEL_CHANGED, &CInterfaceEditor::OnSelectedItem, this);
	m_menuCtrl->Bind(wxEVT_TREE_BEGIN_DRAG, &CInterfaceEditor::OnBeginDrag, this);
	m_menuCtrl->Bind(wxEVT_TREE_END_DRAG, &CInterfaceEditor::OnEndDrag, this);
	m_menuCtrl->Bind(wxEVT_TREE_ITEM_RIGHT_CLICK, &CInterfaceEditor::OnRightClickItem, this);

	m_menuCtrl->Bind(wxEVT_MENU, &CInterfaceEditor::OnCreateItem, this, ID_MENUEDIT_NEW);
	m_menuCtrl->Bind(wxEVT_MENU, &CInterfaceEditor::OnEditItem, this, ID_MENUEDIT_EDIT);
	m_menuCtrl->Bind(wxEVT_MENU, &CInterfaceEditor::OnRemoveItem, this, ID_MENUEDIT_REMOVE);

	CreateDefaultMenu();

	wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);
	mainSizer->Add(m_metaTreeToolbar, 0, wxEXPAND, 1);
	mainSizer->Add(m_menuCtrl, 1, wxEXPAND, 1);
	wxPanel::SetSizer(mainSizer);
	wxPanel::Layout();
}

CInterfaceEditor::~CInterfaceEditor() {

	m_metaTreeToolbar->Unbind(wxEVT_MENU, &CInterfaceEditor::OnCreateItem, this, ID_MENUEDIT_NEW);
	m_metaTreeToolbar->Unbind(wxEVT_MENU, &CInterfaceEditor::OnEditItem, this, ID_MENUEDIT_EDIT);
	m_metaTreeToolbar->Unbind(wxEVT_MENU, &CInterfaceEditor::OnRemoveItem, this, ID_MENUEDIT_REMOVE);
}

void CInterfaceEditor::OnCreateItem(wxCommandEvent& event) {

	wxItemId selection = m_menuCtrl->GetSelection();
	if (!selection.IsOk())
		return;
	CInterfaceEditor::AppendMenu(selection, _("new item"));
	event.Skip();
}

void CInterfaceEditor::OnEditItem(wxCommandEvent& event) {
	event.Skip();
}

void CInterfaceEditor::OnRemoveItem(wxCommandEvent& event) {
	wxItemId selection = m_menuCtrl->GetSelection();
	if (!selection.IsOk())
		return;
	m_menuCtrl->Delete(selection);
	event.Skip();
}

void CInterfaceEditor::OnPropertyItem(wxCommandEvent& event) {
	event.Skip();
}

void CInterfaceEditor::OnBeginDrag(wxTreeEvent& event) {

	// need to explicitly allow drag
	if (event.GetItem() == m_menuCtrl->GetRootItem())
		return;

	m_draggedItem = event.GetItem();
	event.Allow();
}

void CInterfaceEditor::OnEndDrag(wxTreeEvent& event) {

	bool copy = ::wxGetKeyState(WXK_CONTROL);
	wxTreeItemId itemSrc = m_draggedItem, itemDst = event.GetItem();
	m_draggedItem = (wxTreeItemId)0l;

	// ensure that itemDst is not itemSrc or a child of itemSrc
	wxTreeItemId item = itemDst;
	while (item.IsOk()) {
		if (item == itemSrc)
			return;
		item = m_menuCtrl->GetItemParent(item);
	}

	event.Skip();
}

#include "frontend/mainFrame.h"

void CInterfaceEditor::OnRightClickItem(wxTreeEvent& event)
{
	wxMenu menu; wxMenuItem* item = NULL;

	m_menuCtrl->SelectItem(event.GetItem());

	item = menu.Append(ID_MENUEDIT_NEW, _("New"));
	item->SetBitmap(wxArtProvider::GetBitmap(wxART_PLUS, wxART_MENU));
	item = menu.Append(ID_MENUEDIT_EDIT, _("Edit"));
	item->SetBitmap(wxArtProvider::GetBitmap(wxART_EDIT, wxART_MENU));
	item = menu.Append(ID_MENUEDIT_REMOVE, _("Remove"));
	item->SetBitmap(wxArtProvider::GetBitmap(wxART_DELETE, wxART_MENU));

	if (m_menuCtrl->PopupMenu(&menu, event.GetPoint())) {
		event.Skip();
	}
}

void CInterfaceEditor::OnSelectedItem(wxTreeEvent& event)
{
	// need to explicitly allow drag
	if (event.GetItem() == m_menuCtrl->GetRootItem())
		return;
	wxInterfaceItemData* itemData = dynamic_cast<wxInterfaceItemData*>(m_menuCtrl->GetItemData(event.GetItem()));
	objectInspector->SelectObject(itemData->GetProperty());
	event.Skip();
}

#include "core/metadata/metaObjects/metaObject.h"

wxTreeItemId CInterfaceEditor::AppendMenu(const wxTreeItemId& parent, wxStandardID id) const
{
	wxImageList* imageList = m_menuCtrl->GetImageList();
	wxASSERT(imageList);
	int imageIndex = imageList->Add(m_metaObject->GetIcon());
	wxTreeItemId newItem = m_menuCtrl->AppendItem(parent, wxGetStockLabel(id, wxSTOCK_NOFLAGS),
		imageIndex,
		imageIndex,
		new wxInterfaceItemData(eMenuType::eMenu, wxGetStockLabel(id, wxSTOCK_NOFLAGS))
	);
	m_menuCtrl->SelectItem(newItem);
	return newItem;
}

wxTreeItemId CInterfaceEditor::AppendMenu(const wxTreeItemId& parent, const wxString& name) const
{
	wxImageList* imageList = m_menuCtrl->GetImageList();
	wxASSERT(imageList);
	int imageIndex = imageList->Add(m_metaObject->GetIcon());
	wxTreeItemId newItem = m_menuCtrl->AppendItem(parent, name,
		imageIndex,
		imageIndex,
		new wxInterfaceItemData(eMenuType::eMenu, name)
	);
	m_menuCtrl->SelectItem(newItem);
	return newItem;
}

wxTreeItemId CInterfaceEditor::AppendSubMenu(const wxTreeItemId& parent, wxStandardID id) const
{
	wxImageList* imageList = m_menuCtrl->GetImageList();
	wxASSERT(imageList);
	int imageIndex = imageList->Add(m_metaObject->GetIcon());
	wxTreeItemId newItem = m_menuCtrl->AppendItem(parent, wxGetStockLabel(id, wxSTOCK_NOFLAGS),
		imageIndex,
		imageIndex,
		new wxInterfaceItemData(eMenuType::eSubMenu, wxGetStockLabel(id, wxSTOCK_NOFLAGS))
	);
	m_menuCtrl->SelectItem(newItem);
	return newItem;
}

wxTreeItemId CInterfaceEditor::AppendSubMenu(const wxTreeItemId& parent, const wxString& name) const
{
	wxImageList* imageList = m_menuCtrl->GetImageList();
	wxASSERT(imageList);
	int imageIndex = imageList->Add(m_metaObject->GetIcon());
	wxTreeItemId newItem = m_menuCtrl->AppendItem(parent, name,
		imageIndex,
		imageIndex,
		new wxInterfaceItemData(eMenuType::eSubMenu, name)
	);
	m_menuCtrl->SelectItem(newItem);
	return newItem;
}

wxTreeItemId CInterfaceEditor::AppendSeparator(const wxTreeItemId& parent) const
{
	wxImageList* imageList = m_menuCtrl->GetImageList();
	wxASSERT(imageList);
	int imageIndex = imageList->Add(m_metaObject->GetIcon());
	wxTreeItemId newItem = m_menuCtrl->AppendItem(parent, wxT("---...---"),
		imageIndex,
		imageIndex,
		new wxInterfaceItemData(eMenuType::eSeparator)
	);
	m_menuCtrl->SelectItem(newItem);
	return newItem;
}

void CInterfaceEditor::CreateDefaultMenu()
{
	//set image list
	m_menuCtrl->AssignImageList(
		new wxImageList(ICON_SIZE, ICON_SIZE)
	);

	m_menuCtrl->DeleteAllItems();

	wxImageList* imageList = m_menuCtrl->GetImageList();
	wxASSERT(imageList);
	int imageIndex = imageList->Add(m_metaObject->GetIcon());

	const wxTreeItemId& root = m_menuCtrl->AddRoot(m_metaObject->GetClassName(),
		imageIndex, imageIndex);

	CInterfaceEditor::AppendSubMenu(root, wxID_FILE);
	CInterfaceEditor::AppendSubMenu(root, wxID_EDIT);
	CInterfaceEditor::AppendSubMenu(root, wxID_HELP);

	m_menuCtrl->ExpandAll();
}

void CInterfaceEditor::FillMenu(const wxTreeItemId& parent, wxMenu* menu)
{
	wxTreeItemIdValue cookie;
	wxTreeItemId nextItem = m_menuCtrl->GetFirstChild(
		parent,
		cookie
	);

	while (nextItem.IsOk()) {
		if (m_menuCtrl->HasChildren(nextItem)) {
			wxMenu* child = new wxMenu;
			FillMenu(nextItem, child);
			menu->AppendSubMenu(child, m_menuCtrl->GetItemText(nextItem));
		}
		else {
			menu->Append(wxID_ANY, m_menuCtrl->GetItemText(nextItem));
		}
		nextItem = m_menuCtrl->GetNextChild(nextItem, cookie);
	}
}

#include "core/frontend/docView/docManager.h"

#include "frontend/mainFrame.h"
#include "frontend/mainFrameChild.h"

#include "frontend/visualView/menuBar.h"

bool CInterfaceEditor::TestInterface()
{
	class CInterfaceDocument : public CDocument {
	public:
		CInterfaceDocument(CDocument* parent, IMetaObject* metaObject) : CDocument(parent) {
			CDocument::SetIcon(metaObject->GetIcon());
			CDocument::SetMetaObject(metaObject);
			CDocument::SetTitle(_("interface demonstration"));
			CDocument::SetFilename((wxString)Guid::newGuid());
		}
	};

	CInterfaceDocument* interfaceDoc = new CInterfaceDocument(m_document, m_metaObject);

	class CInterfaceView : public CView {
	public:
		CInterfaceView(CInterfaceDocument* doc) : CView() {
			CView::SetDocument(doc);
		}
	};

	wxScopedPtr<CInterfaceView> view(
		new CInterfaceView(interfaceDoc)
	);

	// create a child valueForm of appropriate class for the current mode
	wxAuiDocMDIFrame::CreateChildFrame(view.get(), wxDefaultPosition, wxDefaultSize, wxDEFAULT_FRAME_STYLE);

	wxTreeItemIdValue cookie;
	wxTreeItemId nextItem = m_menuCtrl->GetFirstChild(
		m_menuCtrl->GetRootItem(),
		cookie
	);

	//Create menubar 
	CMenuBar* menuBar = new CMenuBar(view->GetFrame(), wxID_ANY);

	while (nextItem.IsOk()) {
		wxMenu* menu = new wxMenu;
		if (m_menuCtrl->HasChildren(nextItem)) {
			FillMenu(nextItem, menu);
		}
		menuBar->AppendMenu(menu, m_menuCtrl->GetItemText(nextItem));
		nextItem = m_menuCtrl->GetNextChild(nextItem, cookie);
	}

	if (!view->ShowFrame())
		return false;

	return view.release() != NULL;
}

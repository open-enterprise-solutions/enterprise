////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : metaTree events
////////////////////////////////////////////////////////////////////////////

#include "treeConfiguration.h"

#include "frmclient/docView/docView.h"                    // the views the tree's focus moves between
#include "frmclient/mainFrame/mainFrame.h"
#include "frmclient/mainFrame/mainFrameChild.h"           // the active tab's view
#include "frmclient/mainFrame/objinspect/objinspect.h"

// THE DESKTOP'S CLICK ON THE ROW SELECTED — its selection does not change, so the control says nothing: the row's object
// is shown made anew all the same.
void ibMetaTreeBase::OnLeftDown(wxMouseEvent& event)
{
	const wxTreeItemId item = m_metaTreeCtrl->HitTest(event.GetPosition());
	if (item.IsOk() && item == m_metaTreeCtrl->GetSelection())
		SelectItem();
	event.Skip();
}

// THE DESKTOP'S DOUBLE CLICK — an object's row selected and opened, the click not let on to the control: let on, it took
// the focus back from the editor it opened, and the inspector showed the row again over what the editor shows.
void ibMetaTreeBase::OnLeftDClick(wxMouseEvent& event)
{
	const wxTreeItemId item = m_metaTreeCtrl->HitTest(event.GetPosition());
	if (!item.IsOk() || GetObject(item) == 0) {
		event.Skip();
		return;
	}
	m_metaTreeCtrl->SelectItem(item);
	EditItem();
}

void ibMetaTreeBase::OnItemActivated(wxTreeEvent& event)
{
	// An object's row opens its editor; a group's opens and closes as any row does.
	if (GetObject(event.GetItem()) != 0)
		EditItem();
	else
		event.Skip();
}

void ibMetaTreeBase::OnSelected(wxTreeEvent& event)
{
	// …not while the panel goes: the control deletes its rows as it is destroyed, the toolbar gone before it (a tab closed).
	if (!m_drawing && !IsBeingDeleted())
		SelectItem();
	event.Skip();
}

// THE TREE THE PERSON COMES TO IS ACTIVATED — the desktop's (ibMetaTreeCtrl::OnSetFocus). The configuration's tree is the
// current view in the editor's place: the editor's view deactivated (its menu and toolbar shut), the tree's activated
// (its object shown). The focus gone anywhere but the inspector, the active tab's view is activated again. An external
// file's tree stands in its tab: only its object shown.
//
// ⭐⭐ The handler does not answer the focus events it causes itself (m_switchingActivation): on Cocoa activating a view
// moves the focus, and the window that held it is told inside SetFocus — the desktop died of it, the stack exhausted.
void ibMetaTreeBase::OnSetFocus(wxFocusEvent& event)
{
	event.Skip();
	if (m_drawing || IsBeingDeleted() || m_switchingActivation)
		return;

	if (m_tab != 0) {
		if (event.GetEventType() == wxEVT_SET_FOCUS)
			ActivateTree();
		return;
	}
	if (docManager == nullptr)
		return;

	// Cleared by scope — an exception out of Activate does not leave the tree deaf to its focus.
	class ibSwitching {
	public:
		explicit ibSwitching(bool& flag) : m_flag(flag) { m_flag = true; }
		~ibSwitching() { m_flag = false; }
	private:
		bool& m_flag;
	} const switching(m_switchingActivation);

	if (event.GetEventType() == wxEVT_SET_FOCUS) {
		ibFrontendView* const view = docManager->GetCurrentView();
		if (m_metaView != view) {
			if (view != nullptr)
				view->Activate(false);
			m_metaView->Activate(true);
		}
		return;
	}

	// …gone into the inspector — it shows the tree's object; anywhere else — the active tab's view again, whichever is
	// current: a tab opened from the tree is current before the tree loses the focus, its toolbar shut by the tree's.
	wxWindow* focus = event.GetWindow();
	while (focus != nullptr && focus != objectInspector)
		focus = focus->GetParent();
	if (focus != nullptr)
		return;

	const ibAuiDocChildFrame* const child = static_cast<ibAuiDocChildFrame*>(mainFrame->GetActiveChild());
	ibFrontendView* const view = child != nullptr ? child->GetView() : docManager->GetAnyUsableView();
	if (m_metaView != view) {
		m_metaView->Activate(false);
		if (view != nullptr)
			view->Activate(true);
	}
}

// ⭐⭐ THE ROW THE EVENT IS ABOUT, NOT THE ONE THAT HAPPENS TO BE SELECTED — and only what the person opens: a search
// opens everything, the drawing opens what was open.
void ibMetaTreeBase::OnExpanded(wxTreeEvent& event)
{
	const ibTreeRow* const row = GetRow(event.GetItem());
	if (row != nullptr && !m_drawing && m_search.IsEmpty())
		m_expanded.insert(row->m_path);
	event.Skip();
}

void ibMetaTreeBase::OnCollapsed(wxTreeEvent& event)
{
	const ibTreeRow* const row = GetRow(event.GetItem());
	if (row != nullptr && !m_drawing && m_search.IsEmpty())
		m_expanded.erase(row->m_path);
	event.Skip();
}

void ibMetaTreeBase::OnItemMenu(wxTreeEvent& event)
{
	const wxTreeItemId item = event.GetItem();
	if (!item.IsOk())
		return;

	m_metaTreeCtrl->SelectItem(item);
	m_metaTreeCtrl->SetFocus();
	ShowContextMenu(item, event.GetPoint());
}

// A ROW DRAGGED ONTO ANOTHER IS COPIED THERE — the desktop's tree (ibMetaTreeCtrl::OnBeginDrag / OnEndDrag): an object's
// row, not the root's or a group's, and not in a read-only configuration — refused at the start, so the row does not
// follow the cursor as if it would land.
void ibMetaTreeBase::OnBeginDrag(wxTreeEvent& event)
{
	const wxTreeItemId item = event.GetItem();
	if (m_readOnly || !item.IsOk() || item == m_metaTreeCtrl->GetRootItem() || GetObject(item) == 0)
		return;
	m_draggedItem = item;
	event.Allow();
}

void ibMetaTreeBase::OnEndDrag(wxTreeEvent& event)
{
	const wxTreeItemId item = m_draggedItem;
	m_draggedItem = wxTreeItemId();
	if (item.IsOk() && event.GetItem().IsOk())
		DuplicateItem(item, event.GetItem());
}

#include "designer-thin/win/dlg/textEditor.h"

// The two texts a metaobject carries. One dialog serves both — the caption is what differs, and
// the object is asked for its own text rather than a copy being kept anywhere.
//
// Written back ONLY on OK and ONLY when it actually changed: marking a configuration modified
// because somebody opened a note and closed it is how a save prompt appears for no reason, and a
// prompt that appears for no reason is one a person learns to dismiss without reading. The server
// marks it, as it takes the text.
static void EditOneText(ibValueMetaObject* metaObject, wxWindow* parent, bool help)
{
	if (metaObject == nullptr)
		return;

	const wxString before = help ? metaObject->GetHelpContent() : metaObject->GetNoteContent();

	ibDialogTextEditor dlg(parent,
		help ? _("Help information") : _("Technical information"),
		metaObject->GetName(), before);

	if (dlg.ShowModal() != wxID_OK)
		return;

	const wxString after = dlg.GetText();
	if (after == before)
		return;

	if (help) metaObject->SetHelpContent(after);
	else      metaObject->SetNoteContent(after);
}

// The selected row's object, what the server says of it received — as the inspector receives the one it shows.
void ibMetaTreeBase::OnEditHelp(wxCommandEvent& event)
{
	const long long id = GetObject(m_metaTreeCtrl->GetSelection());
	if (id != 0) {
		ibValueMetaObject metaObject(static_cast<ibMetaID>(id), ibMetaData(m_tab));
		metaObject.Fetch();
		EditOneText(&metaObject, this, /*help*/ true);
	}

	event.Skip();
}

void ibMetaTreeBase::OnEditNotes(wxCommandEvent& event)
{
	const long long id = GetObject(m_metaTreeCtrl->GetSelection());
	if (id != 0) {
		ibValueMetaObject metaObject(static_cast<ibMetaID>(id), ibMetaData(m_tab));
		metaObject.Fetch();
		EditOneText(&metaObject, this, /*help*/ false);
	}

	event.Skip();
}

void ibMetaTreeBase::OnStartSearch(wxCommandEvent& event)
{
	Search(event.GetString());
	event.Skip();
}

void ibMetaTreeBase::OnCancelSearch(wxCommandEvent& event)
{
	if (event.GetString().IsEmpty())
		Search(wxEmptyString);
	event.Skip();
}

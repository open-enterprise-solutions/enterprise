////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : metaTree events
////////////////////////////////////////////////////////////////////////////

#include "treeConfiguration.h"
#include <wx/wupdlock.h>   // wxWindowUpdateLocker - RAII Freeze/Thaw (a throwing paste must not leave the tree frozen)

void ibConfigurationTree::ibMetaTreeCtrl::OnLeftDClick(wxMouseEvent& event)
{
	const wxTreeItemId curItem = HitTest(event.GetPosition());
	if (curItem.IsOk()) {
		SelectItem(curItem);
		m_ownerTree->ActivateItem(curItem);
	}
	//event.Skip();
}

#include "frontend/mainFrame/mainFrame.h"

void ibConfigurationTree::ibMetaTreeCtrl::OnLeftUp(wxMouseEvent& event)
{
	event.Skip();
}

void ibConfigurationTree::ibMetaTreeCtrl::OnLeftDown(wxMouseEvent& event)
{
	const wxTreeItemId curItem = HitTest(event.GetPosition());
	if (curItem.IsOk() && curItem == GetSelection()) m_ownerTree->SelectItem();
	event.Skip();
}

void ibConfigurationTree::ibMetaTreeCtrl::OnRightUp(wxMouseEvent& event)
{
#ifdef __WXOSX__
	// On macOS, context menu is shown from OnRightDown, skip here
	event.Skip();
#else
	wxTreeItemId curItem = HitTest(event.GetPosition());

	if (curItem.IsOk())
	{
		SelectItem(curItem); SetFocus();
		m_ownerTree->ShowContextMenu(this, curItem, event.GetPosition());
	}

	event.Skip();
#endif
}

void ibConfigurationTree::ibMetaTreeCtrl::OnRightDown(wxMouseEvent& event)
{
	wxTreeItemId curItem = HitTest(event.GetPosition());

	if (curItem.IsOk())
	{
		SelectItem(curItem); SetFocus();

		// On macOS, show context menu on mouse-down
		m_ownerTree->ShowContextMenu(this, curItem, event.GetPosition());
	}

#ifndef __WXOSX__
	event.Skip();
#endif
}

void ibConfigurationTree::ibMetaTreeCtrl::OnRightDClick(wxMouseEvent& event)
{
	event.Skip();
}

void ibConfigurationTree::ibMetaTreeCtrl::OnKeyUp(wxKeyEvent& event)
{
	event.Skip();
}

void ibConfigurationTree::ibMetaTreeCtrl::OnKeyDown(wxKeyEvent& event)
{
	event.Skip();
}

void ibConfigurationTree::ibMetaTreeCtrl::OnMouseMove(wxMouseEvent& event)
{
	event.Skip();
}

void ibConfigurationTree::ibMetaTreeCtrl::OnBeginDrag(wxTreeEvent& event) {

	wxTreeItemId curItem = event.GetItem();
	if (!curItem.IsOk())
		return;
	// need to explicitly allow drag
	if (curItem == GetRootItem())
		return;
	ibValueMetaObject* metaObject = m_ownerTree->GetMetaObject(curItem);
	if (metaObject == nullptr)
		return;
	// A read-only configuration is not restructured — the drag copies a metaobject onto another node. Refuse it at
	// the START: OnEndDrag already refused the DROP, but the item still followed the cursor as if it would land.
	if (!m_ownerTree->IsEditable())
		return;
	m_draggedItem = curItem;
	event.Allow();
}

void ibConfigurationTree::ibMetaTreeCtrl::OnEndDrag(wxTreeEvent& event) {

	bool copy = ::wxGetKeyState(WXK_CONTROL);
	wxTreeItemId itemSrc = m_draggedItem, itemDst = event.GetItem();
	m_draggedItem = (wxTreeItemId)0l;

	if (!m_ownerTree->IsEditable())
		return;

	// ensure that itemDst is not itemSrc or a child of itemSrc
	ibValueMetaObject* metaSrcObject = m_ownerTree->GetMetaObject(itemSrc);

	if (metaSrcObject != nullptr) {

		const wxTreeItemId& item = m_ownerTree->GetSelectionIdentifier(itemDst);

		if (!item.IsOk())
			return;

		ibMetaData* const metaData = m_ownerTree->GetMetaData();
		if (metaData == nullptr)
			return;

		ibWriterMemory dataWritter;
		if (metaData->CopyMetaObject(metaSrcObject, dataWritter)) {

			// ⭐ ONE DOOR, AND IT IS THE METADATA'S. Making the shell, reading the payload into it,
			// announcing the result and taking it away again if the payload was bad were four steps
			// written out here; all four belong to the paste and are now inside it. What comes back
			// is the finished object, or nothing.
			//
			// The row is not drawn here either — the Created stage brings it back, which also
			// selects it, which is what moves the object inspector. Doing any of that here as well
			// would be the second road.
			ibReaderMemory reader(dataWritter.pointer(), dataWritter.size());

			metaData->PasteMetaObject(
				m_ownerTree->GetClassIdentifier(item),
				m_ownerTree->GetMetaIdentifier(item),
				reader);
		}
	}

	Update();
	event.Skip();
}

void ibConfigurationTree::ibMetaTreeCtrl::OnStartSearch(wxCommandEvent& event)
{
	m_ownerTree->Search(event.GetString()); //Fill all data from metaData
	event.Skip();
}

void ibConfigurationTree::ibMetaTreeCtrl::OnCancelSearch(wxCommandEvent& event)
{
	const wxString& strSearch = event.GetString();
	if (strSearch.IsEmpty())
		m_ownerTree->Search(wxEmptyString); //Fill all data from metaData	
	event.Skip();
}

void ibConfigurationTree::ibMetaTreeCtrl::OnCreateItem(wxCommandEvent& event)
{
	m_ownerTree->CreateItem(); event.Skip();
}

void ibConfigurationTree::ibMetaTreeCtrl::OnItemActivated(wxTreeEvent& event)
{
	m_ownerTree->EditItem(); event.Skip();
}

void ibConfigurationTree::ibMetaTreeCtrl::OnEditItem(wxCommandEvent& event)
{
	m_ownerTree->EditItem(); event.Skip();
}

void ibConfigurationTree::ibMetaTreeCtrl::OnRemoveItem(wxCommandEvent& event)
{
	m_ownerTree->RemoveItem(); event.Skip();
}

void ibConfigurationTree::ibMetaTreeCtrl::OnPropertyItem(wxCommandEvent& event)
{
	m_ownerTree->PropertyItem(); event.Skip();
}

void ibConfigurationTree::ibMetaTreeCtrl::OnUpItem(wxCommandEvent& event)
{
	m_ownerTree->UpItem();
	event.Skip();
}

void ibConfigurationTree::ibMetaTreeCtrl::OnDownItem(wxCommandEvent& event)
{
	m_ownerTree->DownItem();
	event.Skip();
}

void ibConfigurationTree::ibMetaTreeCtrl::OnSortItem(wxCommandEvent& event)
{
	m_ownerTree->SortItem();
	event.Skip();
}

void ibConfigurationTree::ibMetaTreeCtrl::OnInsertItem(wxCommandEvent& event)
{
	m_ownerTree->InsertItem();
	event.Skip();
}

void ibConfigurationTree::ibMetaTreeCtrl::OnReplaceItem(wxCommandEvent& event)
{
	m_ownerTree->ReplaceItem();
	event.Skip();
}

void ibConfigurationTree::ibMetaTreeCtrl::OnSaveItem(wxCommandEvent& event)
{
	m_ownerTree->SaveItem();
	event.Skip();
}


#include <wx/clipbrd.h>
#include "clipboardLock.h"   // the Open/Close pair, taken as a guard — one mechanism, all three trees

void ibConfigurationTree::ibMetaTreeCtrl::OnCopyItem(wxCommandEvent& event)
{
	const wxTreeItemId& item = GetSelection();
	if (!item.IsOk())
		return;

	// Write some text to the clipboard
	const ibClipboardLock clipboard;   // closes on every path out — see clipboardLock.h
	if (clipboard.IsOpen()) {

		ibValueMetaObject* metaObject = m_ownerTree->GetMetaObject(item);
		ibMetaData* const metaData = m_ownerTree->GetMetaData();

		if (metaObject != nullptr && metaData != nullptr) {

			ibWriterMemory dataWritter;
			if (metaData->CopyMetaObject(metaObject, dataWritter)) {

				wxDataObjectComposite* composite_object = new wxDataObjectComposite;
				wxCustomDataObject* custom_object = new wxCustomDataObject(oes_clipboard_metadata);
				custom_object->SetData(dataWritter.size(), dataWritter.pointer());

				composite_object->Add(custom_object);
				composite_object->Add(new wxTextDataObject(metaObject->GetName()), true);

				// tell clipboard
				wxTheClipboard->SetData(composite_object);
			}
		}
	}

	event.Skip();
}

void ibConfigurationTree::ibMetaTreeCtrl::OnPasteItem(wxCommandEvent& event)
{
	if (!m_ownerTree->IsEditable())
		return;

	const wxTreeItemId& item = m_ownerTree->GetSelectionIdentifier();
	if (!item.IsOk())
		return;

	// RAII Freeze/Thaw: a throw out of PasteObject (or of the cleanup that follows it) used to
	// fly past the paired Thaw() and leave the tree frozen and unresponsive for the rest of the
	// session - the error dialog appeared over a navigator that never came back.
	wxWindowUpdateLocker freeze(m_ownerTree);

	const ibClipboardLock clipboard;
	if (clipboard.IsOpen() && wxTheClipboard->IsSupported(oes_clipboard_metadata)) {

		wxCustomDataObject data(oes_clipboard_metadata);

		if (wxTheClipboard->GetData(data)) {

			// ⭐ WHERE IT GOES, AND WHAT GOES THERE — the two things a paste is, and nothing else is
			// written out here any more. The shell, the read, the announcement that draws the row
			// and the cleanup of a payload that turned out to be bad all live behind the door.
			if (ibMetaData* metaData = m_ownerTree->GetMetaData()) {
				ibReaderMemory reader(data.GetData(), data.GetDataSize());
				metaData->PasteMetaObject(
					m_ownerTree->GetClassIdentifier(),
					m_ownerTree->GetMetaIdentifier(),
					reader);
			}
		}
	}


	RefreshSelectedItem();
	event.Skip();
}

#include "frontend/docView/docView.h"
#include "frontend/mainFrame/mainFrameChild.h"

// ⭐⭐ THE HANDLER MUST NOT ANSWER THE FOCUS EVENTS IT ITSELF CAUSES — without this the designer
// died on macOS the moment a common module was added: EXC_BAD_ACCESS at the main thread's guard
// page, twelve frames repeating to the bottom of an exhausted stack.
//
// The loop, read from a live backtrace rather than reasoned about:
//
//     OnSetFocus → ibView::Activate(true) → wxWidgetCocoaImpl::SetFocus()
//       → -[NSWindow _realMakeFirstResponder:] → resignFirstResponder
//         → DoNotifyFocusEvent → a wx focus event, dispatched inline → OnSetFocus …
//
// Activating a view moves the focus, moving the focus tells the window that was holding it, and
// on Cocoa that telling is SYNCHRONOUS: -[NSWindow _realMakeFirstResponder:] calls
// resignFirstResponder inside SetFocus, and wx turns it into a focus event dispatched in the same
// stack. Windows does not close the circle because it posts the notification through the message
// queue instead, which is why this was never seen there.
//
// 🛑 AND THE GUARD THAT WAS HERE COULD NOT CLOSE IT, because it asks a question the call it
// guards has not yet answered. `m_metaView != docManager->GetCurrentView()` looks like it stops
// the second entry — but ibView::Activate updates the current view LAST (docManager.cpp: the
// mgr->ActivateView call sits after mainFrame->ActivateView, which is where the focus actually
// moves). On re-entry the current view is still the old one, so the condition is still true and
// the handler activates again. The KILL_FOCUS branch below already stopped trusting that same
// state, for its own reason, and its comment describes the same unreliability from the other end.
//
// So the state this needs is not "which view is current" but "am I in the middle of changing
// that" — and only this handler knows it. One flag, cleared on the way out by scope, both
// branches covered: KILL_FOCUS activates too, and would recurse the same way from the other side.
void ibConfigurationTree::ibMetaTreeCtrl::OnSetFocus(wxFocusEvent& event)
{
	if (m_switchingActivation) {
		event.Skip();
		return;
	}

	// Cleared by scope, so an exception out of Activate cannot leave the tree permanently deaf to
	// focus — a stuck flag would be a quieter bug than the crash it replaces, and harder to see.
	class Switching {
	public:
		explicit Switching(bool& flag) : m_flag(flag) { m_flag = true; }
		~Switching() { m_flag = false; }
	private:
		bool& m_flag;
	} const switching(m_switchingActivation);

	if (docManager != nullptr && event.GetEventType() == wxEVT_SET_FOCUS) {

		const wxTreeItemId& item = GetSelection();

		ibView* view = docManager->GetCurrentView();
		if (m_ownerTree->m_docParent == nullptr &&
			m_metaView != view) {
			if (view != nullptr) view->Activate(false);
			m_metaView->Activate(true);
		}
	}
	else if (docManager != nullptr && event.GetEventType() == wxEVT_KILL_FOCUS) {

		wxWindow* focus_win = event.GetWindow();
		while (focus_win != nullptr && focus_win != objectInspector) {
			focus_win = focus_win->GetParent();
		}

		if (focus_win == nullptr) {

			const ibAuiDocChildFrame* focus_child_win =
				static_cast<ibAuiDocChildFrame*>(mainFrame->GetActiveChild());

			ibView* view = focus_child_win ? focus_child_win->GetView() : docManager->GetAnyUsableView();
			// Do NOT gate this on `m_metaView == docManager->GetCurrentView()`:
			// when the user opens a doc directly from the tree (double-click on
			// a metadata item), the new doc becomes the current view before
			// KILL_FOCUS fires. That guard then skipped re-activation, leaving
			// the toolbar and menu of the newly-opened doc disabled (the tree's
			// SET_FOCUS path had already cleared them via view->Activate(false)).
			// ibDocManager::ActivateView(v, false) is a no-op when v is not the
			// current view, so unconditionally calling it is safe.
			if (m_ownerTree->m_docParent == nullptr &&
				m_metaView != view) {
				m_metaView->Activate(false);
				if (view != nullptr) view->Activate(true);
			}
		}
	}

	event.Skip();
}

void ibConfigurationTree::ibMetaTreeCtrl::OnSelecting(wxTreeEvent& event)
{
	event.Skip();
}

void ibConfigurationTree::ibMetaTreeCtrl::OnSelected(wxTreeEvent& event)
{
	m_ownerTree->SelectItem(); event.Skip();
}

void ibConfigurationTree::ibMetaTreeCtrl::OnCollapsing(wxTreeEvent& event)
{
	if (GetRootItem() != event.GetItem()) {
		m_ownerTree->Collapse(event.GetItem()); event.Skip();
	}
	else {
		event.Veto();
	}
}

void ibConfigurationTree::ibMetaTreeCtrl::OnExpanding(wxTreeEvent& event)
{
	m_ownerTree->Expand(event.GetItem()); event.Skip();
}

#include "win/dlg/textEditor.h"
#include "backend/metaCollection/metaObject.h"

// The two texts a metaobject carries. One dialog serves both — the caption is what differs, and
// the object is asked for its own text rather than a copy being kept anywhere.
//
// Written back ONLY on OK and ONLY when it actually changed: marking a configuration modified
// because somebody opened a note and closed it is how a save prompt appears for no reason, and a
// prompt that appears for no reason is one a person learns to dismiss without reading.
static void EditOneText(ibConfigurationTree* tree, ibValueMetaObject* metaObject,
	wxWindow* parent, bool help)
{
	if (tree == nullptr || metaObject == nullptr)
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

	tree->Modify(true);
}

// The selection is asked of the CONTROL and resolved by the TREE — the same two lines OnCopyItem
// uses, because it is the same question.
void ibConfigurationTree::ibMetaTreeCtrl::OnEditHelp(wxCommandEvent& event)
{
	const wxTreeItemId& item = GetSelection();
	if (item.IsOk())
		EditOneText(m_ownerTree, m_ownerTree->GetMetaObject(item), this, /*help*/ true);

	event.Skip();
}

void ibConfigurationTree::ibMetaTreeCtrl::OnEditNotes(wxCommandEvent& event)
{
	const wxTreeItemId& item = GetSelection();
	if (item.IsOk())
		EditOneText(m_ownerTree, m_ownerTree->GetMetaObject(item), this, /*help*/ false);

	event.Skip();
}

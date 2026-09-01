////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, unknownworlds team
//	Description : watch window
////////////////////////////////////////////////////////////////////////////

#include "watchWindow.h"
#include "watchdroptarget.h"

#include "backend/debugger/debugClient.h"
#include "backend/fileSystem/fs.h"

#include <functional>

BEGIN_EVENT_TABLE(ibWatchWindow, ibWatchCtrl)
EVT_TREE_BEGIN_LABEL_EDIT(wxID_ANY, ibWatchWindow::OnBeginLabelEdit)
EVT_TREE_END_LABEL_EDIT(wxID_ANY, ibWatchWindow::OnEndLabelEdit)
EVT_TREE_ITEM_ACTIVATED(wxID_ANY, ibWatchWindow::OnItemSelected)
EVT_TREE_DELETE_ITEM(wxID_ANY, ibWatchWindow::OnItemDeleted)
EVT_TREE_KEY_DOWN(wxID_ANY, ibWatchWindow::OnKeyDown)
EVT_TREE_ITEM_EXPANDING(wxID_ANY, ibWatchWindow::OnItemExpanding)
END_EVENT_TABLE()

//
// Constructor
//

ibWatchWindow::ibWatchWindow(wxWindow* parent, wxWindowID winid)
	: ibWatchCtrl(parent, winid, wxDefaultPosition, wxDefaultSize)
{
	// Light cream content surface — one tier lighter than the editor
	// cream so debugger lists read as the lightest content tier (where
	// the eye lands most often during debugging).
	SetBackgroundColour(wxColour(253, 251, 245));  // #FDFBF5 light cream

	m_root = AddRoot(_T("Root"));
	SetItemText(m_root, 1, _T("Root"));

	SetDropTarget(new ibWatchDropTarget(this));

	CreateEmptySlotIfNeeded();

	m_editing = false;
	m_updating = false;
}

#include "mainFrame/mainFrameDesigner.h"

ibWatchWindow* ibWatchWindow::GetWatchWindow()
{
	if (ibFrontendMainFrameDesigner::GetFrame())
		return mainFrame->GetWatchWindow();
	return nullptr;
}

void ibWatchWindow::SetVariable(const ibWatchWindowData& watchData)
{
	m_updating = true;
	Freeze();

	for (unsigned int i = 0; i < watchData.GetWatchCount(); i++) {

		const wxTreeItemId& item = watchData.GetItem(i);

		// The id is a pointer into THIS window's tree, and it is safe to use because the answer
		// reached us at all: a question now carries who asked it, and the bridge lets through only
		// its own (ibDebuggerClientBridgeDesigner::OnSetVariable). An id from another listener no
		// longer arrives here to be sorted out by resemblance.
		if (!item.IsOk())
			continue;

		SetItemText(item, 1, watchData.GetValue(i));
		SetItemText(item, 2, watchData.GetType(i));

		//collapse childern items
		Collapse(item);

		//delete children elements
		DeleteChildren(item);

		if (watchData.HasAttributes(i)) {
			AppendItem(item, wxT("..."));
		}
	}

	Thaw();
	m_updating = false;
}

void ibWatchWindow::SetExpanded(const ibWatchWindowData& watchData)
{
	const wxTreeItemId& item = watchData.GetItem();

	if (!item.IsOk())
		return;

	m_updating = true;
	Freeze();

	//delete children elements
	DeleteChildren(item);

	for (unsigned int i = 0; i < watchData.GetWatchCount(); i++) {

		const wxTreeItemId& child = AppendItem(item, watchData.GetName(i));

		SetItemText(child, 1, watchData.GetValue(i));
		SetItemText(child, 2, watchData.GetType(i));

		if (watchData.HasAttributes(i)) {
			AppendItem(child, wxT("..."));
		}
	}

	Expand(item);
	Thaw();
	m_updating = false;
}

void ibWatchWindow::UpdateItem(const wxTreeItemId& item)
{
	const wxString& expression = GetItemText(item);

#if _USE_64_BIT_POINT_IN_DEBUGGER == 1
	if (!expression.IsEmpty())
		debugClient->AddExpression(expression, reinterpret_cast<u64>(item.GetID()), debugClient->BridgeId());
#else
	if (!expression.IsEmpty())
		debugClient->AddExpression(expression, reinterpret_cast<u32>(item.GetID()), debugClient->BridgeId());
#endif

	DeleteChildren(item);

	SetItemFont(item, m_valueFont);

	// Keep the previous value/type visible until the debugger replies with
	// fresh data. Clearing them here caused the columns to blank out for the
	// full round-trip, producing the "values disappear, then reappear" flicker.
}

void ibWatchWindow::UpdateItems()
{
	Freeze();

	wxTreeItemIdValue cookie;
	wxTreeItemId item = GetFirstChild(m_root, cookie);

	while (item.IsOk()) {
		UpdateItem(item);
		item = GetNextSibling(item);
	}

	Thaw();
}

void ibWatchWindow::AddWatch(const wxString& expression)
{
	wxString temp = expression;
	temp.Trim().Trim(false);

	if (!temp.empty())
	{
		wxTreeItemIdValue cookie;
		wxTreeItemId lastItem = GetLastChild(m_root, cookie);

		// The last item should be our blank item.
		assert(lastItem.IsOk());
		lastItem = GetPrevSibling(lastItem);

		wxTreeItemId item;

		if (lastItem.IsOk()) {
			item = InsertItem(m_root, lastItem, temp);
		}
		else {
			item = PrependItem(m_root, temp);
		}

		UpdateItem(item);
	}
}

void ibWatchWindow::OnKeyDown(wxTreeEvent& event)
{
	if (event.GetKeyCode() == WXK_DELETE ||
		event.GetKeyCode() == WXK_BACK)
	{
		wxTreeItemId item = GetSelection();

		if (item.IsOk() && GetItemParent(item) == m_root)
		{
			wxTreeItemId next = GetNextSibling(item);

			Delete(item);
			CreateEmptySlotIfNeeded();

			// Select the next item.
			if (!next.IsOk())
			{
				wxTreeItemIdValue cookie;
				next = GetLastChild(GetRootItem(), cookie);
			}

			SelectItem(next);
		}
	}
	else
	{
		// If we're not currently editing a field, begin editing. This
		// eliminates the need to double click to begin editing.
		int code = event.GetKeyCode();

		if (!m_editing && code < 256 && (isgraph(code) || stringUtils::IsSpace(code)))
		{
			// Get the currently selected item in the list.
			wxTreeItemId item = GetSelection();

			if (item.IsOk())
			{
				if (stringUtils::IsSpace(code))
				{
					EditLabel(item);
				}
				else
				{
					EditLabel(item, code);
				}

				event.Skip(false);
			}
		}
		else
		{
			event.Skip(true);
		}
	}
}

void ibWatchWindow::OnBeginLabelEdit(wxTreeEvent& event)
{
	if (GetItemParent(event.GetItem()) == m_root)
	{
		m_editing = true;
	}
	else
	{
		event.Veto();
	}
}

void ibWatchWindow::OnEndLabelEdit(wxTreeEvent& event)
{
	if (!event.IsEditCancelled())
	{
		wxTreeItemId item = event.GetItem();
		wxTreeItemId next = GetNextSibling(item);

		wxString expression = event.GetLabel();
		expression.Trim().Trim(false);

		if (expression.empty())
		{
			Delete(item);
			event.Veto();
		}
		else
		{
			SetItemText(item, expression);
			UpdateItem(item);
		}

		CreateEmptySlotIfNeeded();

		// Select the next item.
		if (!next.IsOk())
		{
			wxTreeItemIdValue cookie;
			next = GetLastChild(GetRootItem(), cookie);
		}

		SelectItem(next);
	}

	m_editing = false;
}

void ibWatchWindow::OnItemSelected(wxTreeEvent& event)
{
	// Initiate the label editing.
	EditLabel(event.GetItem());
	event.Skip();
}

void ibWatchWindow::OnItemDeleted(wxTreeEvent& event)
{
	wxTreeItemId item = event.GetItem();
#if _USE_64_BIT_POINT_IN_DEBUGGER == 1
	if (!m_updating) {
		debugClient->RemoveExpression(reinterpret_cast<u64>(item.GetID()));
	}
#else 
	if (!m_updating) {
		debugClient->RemoveExpression(reinterpret_cast<u32>(item.GetID()));
	}
#endif 
	event.Skip();
}

void ibWatchWindow::OnItemExpanding(wxTreeEvent& event)
{
	wxTreeItemId item = event.GetItem();
	wxString expression = GetItemText(item);

	wxTreeItemId parent = item;
	while ((parent = GetItemParent(parent)) != nullptr)
	{
		if (parent != GetRootItem()) {
			expression = wxT(".") + expression;
			expression = GetItemText(parent) + expression;
		}
	}

#if _USE_64_BIT_POINT_IN_DEBUGGER == 1
	if (!m_updating) {
		debugClient->ExpandExpression(expression, reinterpret_cast<u64>(item.GetID()), debugClient->BridgeId());
	}
#else
	if (!m_updating) {
		debugClient->ExpandExpression(expression, reinterpret_cast<u32>(item.GetID()), debugClient->BridgeId());
	}
#endif

	if (!m_updating)
		event.Veto();
	else event.Skip();
}

void ibWatchWindow::CreateEmptySlotIfNeeded()
{
	unsigned int numItems = GetChildrenCount(m_root, false); bool needsEmptySlot = true;

	if (numItems > 0) {
		wxTreeItemIdValue cookie;
		wxTreeItemId lastItem = GetLastChild(m_root, cookie);
		needsEmptySlot = !GetItemText(lastItem).IsEmpty();
	}

	if (needsEmptySlot) {
		wxTreeItemId item = AppendItem(m_root, _T(""));
	}
}
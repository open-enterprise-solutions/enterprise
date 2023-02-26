////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, unknownworlds team
//	Description : watch window
////////////////////////////////////////////////////////////////////////////

#include "watchwindow.h"
#include "watchdroptarget.h"
#include "core/compiler/debugger/debugClient.h"
#include "utils/stringUtils.h"
#include "utils/fs/fs.h"

BEGIN_EVENT_TABLE(CWatchWindow, CWatchCtrl)
EVT_TREE_BEGIN_LABEL_EDIT(wxID_ANY, CWatchWindow::OnBeginLabelEdit)
EVT_TREE_END_LABEL_EDIT(wxID_ANY, CWatchWindow::OnEndLabelEdit)
EVT_TREE_ITEM_ACTIVATED(wxID_ANY, CWatchWindow::OnItemSelected)
EVT_TREE_DELETE_ITEM(wxID_ANY, CWatchWindow::OnItemDeleted)
EVT_TREE_KEY_DOWN(wxID_ANY, CWatchWindow::OnKeyDown)
EVT_TREE_ITEM_EXPANDING(wxID_ANY, CWatchWindow::OnItemExpanding)
END_EVENT_TABLE()

//
// Constructor
//

CWatchWindow::CWatchWindow(wxWindow* parent, wxWindowID winid)
	: CWatchCtrl(parent, winid, wxDefaultPosition, wxDefaultSize)
{
	m_root = AddRoot(_T("Root"));
	SetItemText(m_root, 1, _T("Root"));

	SetDropTarget(new CWatchDropTarget(this));

	CreateEmptySlotIfNeeded();

	m_editing = false;
	m_updating = false;
}

#include "frontend/mainFrame.h"

CWatchWindow* CWatchWindow::GetWatchWindow()
{
	if (wxAuiDocMDIFrame::GetFrame())
		return mainFrame->GetWatchWindow();
	return NULL; 
}

void CWatchWindow::SetVariable(const watchWindowData_t& watchData)
{
	m_updating = true;

	for (unsigned int i = 0; i < watchData.GetWatchCount(); i++) {

		const wxTreeItemId& item = watchData.GetItem(i);

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

	m_updating = false;

	// update watch window 
	mainFrame->Update();
}

void CWatchWindow::SetExpanded(const watchWindowData_t& watchData)
{
	const wxTreeItemId& item = watchData.GetItem();

	//delete children elements 
	DeleteChildren(item);

	m_updating = true;

	for (unsigned int i = 0; i < watchData.GetWatchCount(); i++) {

		const wxTreeItemId& child = AppendItem(item, watchData.GetName(i));

		SetItemText(child, 1, watchData.GetValue(i));
		SetItemText(child, 2, watchData.GetType(i));

		if (watchData.HasAttributes(i)) {
			AppendItem(child, wxT("..."));
		}
	}

	Expand(item);
	m_updating = false;
}

void CWatchWindow::UpdateItem(const wxTreeItemId& item)
{
	const wxString& expression = GetItemText(item);

#if defined(_USE_64_BIT_POINT_IN_DEBUGGER)
	if (!expression.IsEmpty())
		debugClient->AddExpression(expression, reinterpret_cast<u64>(item.GetID()));
#else 
	if (!expression.IsEmpty())
		debugClient->AddExpression(expression, reinterpret_cast<u32>(item.GetID()));
#endif 

	DeleteChildren(item);

	SetItemFont(item, m_valueFont);

	SetItemText(item, 1, wxEmptyString);
	SetItemText(item, 2, wxEmptyString);
}

void CWatchWindow::UpdateItems()
{
	wxTreeItemIdValue cookie;
	wxTreeItemId item = GetFirstChild(m_root, cookie);

	while (item.IsOk()) {
		UpdateItem(item);
		item = GetNextSibling(item);
	}
}

void CWatchWindow::AddWatch(const wxString& expression)
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

void CWatchWindow::OnKeyDown(wxTreeEvent& event)
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

		if (!m_editing && code < 256 && (isgraph(code) || StringUtils::IsSpace(code)))
		{
			// Get the currently selected item in the list.
			wxTreeItemId item = GetSelection();

			if (item.IsOk())
			{
				if (StringUtils::IsSpace(code))
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

void CWatchWindow::OnBeginLabelEdit(wxTreeEvent& event)
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

void CWatchWindow::OnEndLabelEdit(wxTreeEvent& event)
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

void CWatchWindow::OnItemSelected(wxTreeEvent& event)
{
	// Initiate the label editing.
	EditLabel(event.GetItem());
	event.Skip();
}

void CWatchWindow::OnItemDeleted(wxTreeEvent& event)
{
	wxTreeItemId item = event.GetItem();
#if defined(_USE_64_BIT_POINT_IN_DEBUGGER)
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

void CWatchWindow::OnItemExpanding(wxTreeEvent& event)
{
	wxTreeItemId item = event.GetItem();
	wxString sExpression = GetItemText(item);

	wxTreeItemId parent = item;
	while ((parent = GetItemParent(parent)) != NULL)
	{
		if (parent != GetRootItem()) {
			sExpression = wxT(".") + sExpression;
			sExpression = GetItemText(parent) + sExpression;
		}
	}

#if defined(_USE_64_BIT_POINT_IN_DEBUGGER)
	if (!m_updating) {
		debugClient->ExpandExpression(sExpression, reinterpret_cast<u64>(item.GetID()));
	}
#else 
	if (!m_updating) {
		debugClient->ExpandExpression(sExpression, reinterpret_cast<u32>(item.GetID()));
	}
#endif 

	if (!m_updating)
		event.Veto();
	else event.Skip();
}

void CWatchWindow::CreateEmptySlotIfNeeded()
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
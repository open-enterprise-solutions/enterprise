////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, unknownworlds team
//	Description : watch window
////////////////////////////////////////////////////////////////////////////

#include "watchwindow.h"
#include "watchdroptarget.h"
#include "compiler/debugger/debugClient.h"
#include "utils/stringUtils.h"
#include "utils/fs/fs.h"

BEGIN_EVENT_TABLE(CWatchWindow, CWatchCtrl)
EVT_TREE_BEGIN_LABEL_EDIT(wxID_ANY, CWatchWindow::OnBeginLabelEdit)
EVT_TREE_END_LABEL_EDIT(wxID_ANY, CWatchWindow::OnEndLabelEdit)
EVT_TREE_ITEM_ACTIVATED(wxID_ANY, CWatchWindow::OnItemSelected)
EVT_TREE_DELETE_ITEM(wxID_ANY, CWatchWindow::OnItemDeleted)
EVT_TREE_KEY_DOWN(wxID_ANY, CWatchWindow::OnKeyDown)
EVT_TREE_ITEM_EXPANDING(wxID_ANY, CWatchWindow::OnItemExpanding)

EVT_WATCHWINDOW_VARIABLE_EVENT(CWatchWindow::OnDataSetVariable)
EVT_WATCHWINDOW_EXPAND_EVENT(CWatchWindow::OnDataExpanding)

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

void CWatchWindow::SetVariable(CMemoryReader &commandReader)
{
	unsigned int nCountExpression = commandReader.r_u32(); wxWatchWindowDataEvent event(wxEVT_WATCHWINDOW_VARIABLE_EVENT);

	for (unsigned int i = 0; i < nCountExpression; i++)
	{
#if defined(_USE_64_BIT_POINT_IN_DEBUGGER)
		wxTreeItemId item = reinterpret_cast<void *>(commandReader.r_u64());
#else 
		wxTreeItemId item = reinterpret_cast<void *>(commandReader.r_u32());
#endif 

		wxString sExpression, sValue, sType;
		//expressions
		commandReader.r_stringZ(sExpression);
		commandReader.r_stringZ(sValue);
		commandReader.r_stringZ(sType);

		//refresh child elements 
		unsigned int m_attributeCount = commandReader.r_u32();
		event.AddWatch(sExpression, sValue, sType, m_attributeCount > 0, item);
	}

	wxPostEvent(this, event);
}

void CWatchWindow::SetExpanded(CMemoryReader &commandReader)
{
#if defined(_USE_64_BIT_POINT_IN_DEBUGGER)
	wxTreeItemId item = reinterpret_cast<void *>(commandReader.r_u64());
#else 
	wxTreeItemId item = reinterpret_cast<void *>(commandReader.r_u32());
#endif 

	wxWatchWindowDataEvent event(wxEVT_WATCHWINDOW_EXPAND_EVENT, item);
	//generate event 
	unsigned int m_attributeCount = commandReader.r_u32();

	for (unsigned int i = 0; i < m_attributeCount; i++) {
		wxString name, value, type;

		commandReader.r_stringZ(name);
		commandReader.r_stringZ(value);
		commandReader.r_stringZ(type);

		unsigned int m_attributeChildCount = commandReader.r_u32();
		event.AddWatch(name, value, type, m_attributeChildCount > 0);
	}

	wxPostEvent(this, event);
}

void CWatchWindow::UpdateItem(wxTreeItemId item)
{
	wxString m_expression = GetItemText(item);

#if defined(_USE_64_BIT_POINT_IN_DEBUGGER)
	if (!m_expression.IsEmpty()) debugClient->AddExpression(m_expression, reinterpret_cast<u64>(item.GetID()));
#else 
	if (!m_expression.IsEmpty()) debugClient->AddExpression(m_expression, reinterpret_cast<u32>(item.GetID()));
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

	while (item.IsOk())
	{
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

		if (lastItem.IsOk())
		{
			item = InsertItem(m_root, lastItem, temp);
		}
		else
		{
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

void CWatchWindow::OnItemExpanding(wxTreeEvent &event)
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

	if (!m_updating) event.Veto();
	else event.Skip();
}

void CWatchWindow::OnDataExpanding(wxWatchWindowDataEvent& event)
{
	wxTreeItemId item = event.GetItem(); m_updating = true;

	//delete children elements 
	DeleteChildren(item);

	for (unsigned int i = 0; i < event.GetWatchCount(); i++)
	{
		wxTreeItemId child = AppendItem(item, event.GetName(i));

		SetItemText(child, 1, event.GetValue(i));
		SetItemText(child, 2, event.GetType(i));

		if (event.HasAttributes(i)) { 
			AppendItem(child, wxT("...")); 
		}
	}

	Expand(item);

	event.Skip(); m_updating = false;
}

#include "frontend/mainFrame.h"

void CWatchWindow::OnDataSetVariable(wxWatchWindowDataEvent& event)
{
	m_updating = true;

	for (unsigned int i = 0; i < event.GetWatchCount(); i++)
	{
		wxTreeItemId item = event.GetItem(i);

		SetItemText(item, 1, event.GetValue(i));
		SetItemText(item, 2, event.GetType(i));

		//collapse childern items 
		Collapse(item);
	
		//delete children elements 
		DeleteChildren(item);

		if (event.HasAttributes(i)) {
			AppendItem(item, wxT("..."));
		}
	}

	// update watch window 
	mainFrame->Update();

	event.Skip(); m_updating = false;
}

void CWatchWindow::CreateEmptySlotIfNeeded()
{
	bool needsEmptySlot = true;

	unsigned int numItems = GetChildrenCount(m_root, false);

	if (numItems > 0)
	{
		wxTreeItemIdValue cookie;
		wxTreeItemId lastItem = GetLastChild(m_root, cookie);
		needsEmptySlot = !GetItemText(lastItem).IsEmpty();
	}

	if (needsEmptySlot) {
		wxTreeItemId item = AppendItem(m_root, _T(""));
	}
}
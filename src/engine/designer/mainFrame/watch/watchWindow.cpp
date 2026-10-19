#include "watchWindow.h"

#include <wx/dnd.h>

#include "backend/debugger/debugClient.h"
#include "mainFrame/mainFrameDesigner.h"

enum
{
	kColumnName,
	kColumnValue,
	kColumnType,
};

// A row with members shows this one child until it is opened and the debugger sends the real ones.
static const wxString kMembersPlaceholder = wxT("...");

// The row's id as the debugger protocol carries it.
#if _USE_64_BIT_POINT_IN_DEBUGGER == 1
static unsigned long long GetWatchId(const wxTreeItemId& item) { return reinterpret_cast<unsigned long long>(item.GetID()); }
#else
static unsigned int GetWatchId(const wxTreeItemId& item) { return reinterpret_cast<unsigned int>(item.GetID()); }
#endif

// Text dropped on the pane — a selection dragged from the code — becomes a watched expression.
class ibWatchDropTarget : public wxTextDropTarget
{
public:

	explicit ibWatchDropTarget(ibWatchWindow* window) : m_window(window) {}

	virtual bool OnDropText(wxCoord, wxCoord, const wxString& text) override
	{
		m_window->AddWatch(text);
		return false;   // copied, not moved: the code keeps its text
	}

private:

	ibWatchWindow* m_window;
};

ibWatchWindow::ibWatchWindow(wxWindow* parent, wxWindowID id)
	: ibTreeListCtrl(parent, id, wxDefaultPosition, wxDefaultSize,
		wxTR_HIDE_ROOT | wxTR_FULL_ROW_HIGHLIGHT | wxTR_ROW_LINES | wxTR_HAS_BUTTONS,
		wxDefaultValidator, wxT("watch"))   // the name given: the control's default one is not exported from its library
{
	// Light cream: one tier lighter than the editor, so the debugger's lists read as the lightest
	// content, where the eye lands most while debugging.
	SetBackgroundColour(wxColour(0xFD, 0xFB, 0xF5));

	AddColumn(_("Name"), 0, wxALIGN_LEFT);
	AddColumn(_("Value"), 0, wxALIGN_LEFT);
	AddColumn(_("Type"), FromDIP(120), wxALIGN_LEFT);
	SetColumnEditable(kColumnName, true);
	SetColumnEditable(kColumnValue, false);
	SetColumnEditable(kColumnType, false);

	m_root = AddRoot(wxT("Root"));
	CreateEmptySlotIfNeeded();
	SetDropTarget(new ibWatchDropTarget(this));

	Bind(wxEVT_SIZE, &ibWatchWindow::OnSize, this);
	Bind(wxEVT_LIST_COL_END_DRAG, &ibWatchWindow::OnColumnDragged, this);
	Bind(wxEVT_TREE_KEY_DOWN, &ibWatchWindow::OnKeyDown, this);
	Bind(wxEVT_TREE_BEGIN_LABEL_EDIT, &ibWatchWindow::OnBeginLabelEdit, this);
	Bind(wxEVT_TREE_END_LABEL_EDIT, &ibWatchWindow::OnEndLabelEdit, this);
	Bind(wxEVT_TREE_ITEM_ACTIVATED, &ibWatchWindow::OnItemActivated, this);
	Bind(wxEVT_TREE_DELETE_ITEM, &ibWatchWindow::OnItemDeleted, this);
	Bind(wxEVT_TREE_ITEM_EXPANDING, &ibWatchWindow::OnItemExpanding, this);
}

ibWatchWindow::~ibWatchWindow()
{
	// The rows go with the window, deleted by the tree's own destructor after this one: no person removes them, and
	// the debugger they would be removed from may be gone already.
	Unbind(wxEVT_TREE_DELETE_ITEM, &ibWatchWindow::OnItemDeleted, this);
}

ibWatchWindow* ibWatchWindow::GetWatchWindow()
{
	return ibFrontendMainFrameDesigner::GetFrame() != nullptr ? mainFrame->GetWatchWindow() : nullptr;
}

void ibWatchWindow::SetVariable(const ibWatchWindowData& watchData)
{
	m_updating = true;
	Freeze();
	for (unsigned int i = 0; i < watchData.GetWatchCount(); ++i) {
		// The id is a row of THIS tree: the bridge lets through only the answers to its own
		// questions (ibDebuggerClientBridgeDesigner::OnSetVariable).
		const wxTreeItemId& item = watchData.GetItem(i);
		if (!item.IsOk())
			continue;

		SetItemText(item, kColumnValue, watchData.GetValue(i));
		SetItemText(item, kColumnType, watchData.GetType(i));

		// A new value, so the members shown for the old one go; opening the row asks again.
		Collapse(item);
		DeleteChildren(item);
		if (watchData.HasAttributes(i))
			AppendItem(item, kMembersPlaceholder);
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
	DeleteChildren(item);
	for (unsigned int i = 0; i < watchData.GetWatchCount(); ++i) {
		const wxTreeItemId member = AppendItem(item, watchData.GetName(i));
		SetItemText(member, kColumnValue, watchData.GetValue(i));
		SetItemText(member, kColumnType, watchData.GetType(i));
		if (watchData.HasAttributes(i))
			AppendItem(member, kMembersPlaceholder);
	}
	Expand(item);
	Thaw();
	m_updating = false;
}

void ibWatchWindow::AddWatch(const wxString& expression)
{
	wxString text = expression;
	text.Trim().Trim(false);
	if (text.IsEmpty())
		return;

	// Before the empty last row, which stays last.
	wxTreeItemIdValue cookie;
	const wxTreeItemId before = GetPrevSibling(GetLastChild(m_root, cookie));
	const wxTreeItemId item = before.IsOk() ? InsertItem(m_root, before, text) : PrependItem(m_root, text);
	UpdateItem(item);
}

void ibWatchWindow::UpdateItem(const wxTreeItemId& item)
{
	DeleteChildren(item);

	// The old value and type stay until the answer replaces them: blanking them here made every
	// row flicker empty for the whole round trip.
	const wxString expression = GetItemText(item);
	if (!expression.IsEmpty())
		debugClient->AddExpression(expression, GetWatchId(item), debugClient->BridgeId());
}

void ibWatchWindow::CreateEmptySlotIfNeeded()
{
	wxTreeItemIdValue cookie;
	const wxTreeItemId last = GetLastChild(m_root, cookie);
	if (!last.IsOk() || !GetItemText(last).IsEmpty())
		AppendItem(m_root, wxString());
}

void ibWatchWindow::SelectNext(const wxTreeItemId& next)
{
	wxTreeItemIdValue cookie;
	SelectItem(next.IsOk() ? next : GetLastChild(m_root, cookie));
}

void ibWatchWindow::FitColumns()
{
	const int share = GetClientSize().GetWidth() - GetColumnWidth(kColumnType);
	if (share <= 0)
		return;

	const int name = GetColumnWidth(kColumnName);
	const int value = GetColumnWidth(kColumnValue);
	const int nameWidth = name + value > 0 ? share * name / (name + value) : share / 2;
	SetColumnWidth(kColumnName, nameWidth);
	SetColumnWidth(kColumnValue, share - nameWidth);
}

void ibWatchWindow::OnSize(wxSizeEvent& event)
{
	FitColumns();
	event.Skip();
}

void ibWatchWindow::OnColumnDragged(wxListEvent& event)
{
	FitColumns();
	event.Skip();
}

void ibWatchWindow::OnKeyDown(wxTreeEvent& event)
{
	const int code = event.GetKeyCode();
	const wxTreeItemId item = GetSelection();

	// Delete or Backspace on a watched row removes it; members of a row are not the person's to remove.
	if (code == WXK_DELETE || code == WXK_BACK) {
		if (item.IsOk() && GetItemParent(item) == m_root) {
			const wxTreeItemId next = GetNextSibling(item);
			Delete(item);
			CreateEmptySlotIfNeeded();
			SelectNext(next);
		}
		return;
	}

	// Typing on a row starts editing it, without the double click first.
	const bool printable = code >= WXK_SPACE && code < WXK_DELETE;
	if (!m_editing && printable && item.IsOk()) {
		EditLabel(item);
		return;
	}
	event.Skip();
}

void ibWatchWindow::OnBeginLabelEdit(wxTreeEvent& event)
{
	// Only a watched row's expression is a person's to write; a member's name is the debugger's.
	if (GetItemParent(event.GetItem()) != m_root) {
		event.Veto();
		return;
	}
	m_editing = true;
}

void ibWatchWindow::OnEndLabelEdit(wxTreeEvent& event)
{
	m_editing = false;
	if (event.IsEditCancelled())
		return;

	const wxTreeItemId row = event.GetItem();
	const wxTreeItemId following = GetNextSibling(row);
	const wxString text = wxString(event.GetLabel()).Trim().Trim(false);

	if (!text.IsEmpty()) {
		SetItemText(row, text);
		UpdateItem(row);
	}
	else {
		// Emptied, so gone; vetoed, or the tree would write the label into the deleted row.
		event.Veto();
		Delete(row);
	}

	CreateEmptySlotIfNeeded();
	SelectNext(following);
}

void ibWatchWindow::OnItemActivated(wxTreeEvent& event)
{
	EditLabel(event.GetItem());
	event.Skip();
}

void ibWatchWindow::OnItemDeleted(wxTreeEvent& event)
{
	if (!m_updating)
		debugClient->RemoveExpression(GetWatchId(event.GetItem()));
	event.Skip();
}

void ibWatchWindow::OnItemExpanding(wxTreeEvent& event)
{
	// Opened by the answer (SetExpanded): let it open.
	if (m_updating) {
		event.Skip();
		return;
	}

	// Opened by a person: ask for the members by the row's full path, "object.field.field", and
	// stay closed until they come.
	const wxTreeItemId item = event.GetItem();
	wxString path = GetItemText(item);
	for (wxTreeItemId parent = GetItemParent(item); parent.IsOk() && parent != m_root; parent = GetItemParent(parent))
		path = GetItemText(parent) + wxT(".") + path;

	debugClient->ExpandExpression(path, GetWatchId(item), debugClient->BridgeId());
	event.Veto();
}

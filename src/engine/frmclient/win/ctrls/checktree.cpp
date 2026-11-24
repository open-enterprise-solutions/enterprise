#include "checktree.h"

#include <wx/dcgraph.h>    // wxGCDC — a box drawn with its corners transparent
#include <wx/dcmemory.h>
#include <wx/imaglist.h>
#include <wx/renderer.h>   // wxCONTROL_* — the box's state

#include "deskgui/univ/renderer.h"
#include "deskgui/univ/theme.h"   // ibThemeEngine — the box, as the theme draws a check box

wxDEFINE_EVENT(wxEVT_CHECKTREE_FOCUS, wxTreeEvent);
wxDEFINE_EVENT(wxEVT_CHECKTREE_CHOICE, wxTreeEvent);

wxIMPLEMENT_DYNAMIC_CLASS(ibCheckTree, ibTreeCtrl)

bool ibCheckTree::on_check_or_label(int flags)
{
	return flags & (wxTREE_HITTEST_ONITEMSTATEICON | wxTREE_HITTEST_ONITEMLABEL) ? true : false;
}

void ibCheckTree::unhighlight(const wxTreeItemId& id)
{
	if (!id.IsOk())
		return;

	int i = ibCheckTree::GetItemState(id);

	if (ibCheckTree::UNCHECKED <= i && i < ibCheckTree::UNCHECKED_DISABLED)
	{
		ibCheckTree::SetItemState(id, ibCheckTree::UNCHECKED);

	}
	else if (ibCheckTree::CHECKED <= i && i < ibCheckTree::CHECKED_DISABLED)
	{
		ibCheckTree::SetItemState(id, ibCheckTree::CHECKED);
	}
}

void ibCheckTree::mohighlight(const wxTreeItemId& id, bool toggle)
{
	if (!id.IsOk())
		return;

	int i = ibCheckTree::GetItemState(id);

	if (!id.IsOk() || i < 0) {
		return;
	}

	int checkedCount = 0;
	for (auto &i : m_checkedItems) {
		checkedCount += i.second ? 1 : 0;
	}

	bool is_checked = false;

	if (ibCheckTree::UNCHECKED <= i && i < ibCheckTree::UNCHECKED_DISABLED)
	{
		ibCheckTree::SetItemState(id, toggle ? ibCheckTree::CHECKED_MOUSE_OVER : ibCheckTree::UNCHECKED_MOUSE_OVER);
		is_checked = true;
	}
	else if (ibCheckTree::CHECKED <= i && i < ibCheckTree::CHECKED_DISABLED)
	{
		if (!m_allowEmpty && checkedCount <= 1)
			return;
		ibCheckTree::SetItemState(id, toggle ? ibCheckTree::UNCHECKED_MOUSE_OVER : ibCheckTree::CHECKED_MOUSE_OVER);
		is_checked = false;
	}
	else
	{
		// A state outside BOTH ranges is one of the *_DISABLED ones — an item that refuses to be
		// toggled. Nothing was set above, so there is no answer to report and nothing to file:
		// falling through announced a choice nobody made and recorded it in m_checkedItems, which
		// is also what checkedCount is counted from. The flag stayed uninitialised on that path,
		// so the value announced was whatever the stack held (Debug: Run-Time Check #3).
		return;
	}

	if (toggle)
	{
		// as ibTreeCtrl's own: wxTreeEvent's constructor from the tree takes wx's tree (wxTreeCtrlBase)
		wxTreeEvent eventChoice(wxEVT_CHECKTREE_CHOICE, GetId());
		eventChoice.SetEventObject(this);
		eventChoice.SetItem(id);
		eventChoice.SetExtraLong(is_checked ? 1 : 0);
		ibCheckTree::ProcessWindowEvent(eventChoice);

		if (m_singleCheck) {
			for (auto item : m_checkedItems) {
				if (item.second) {
					ibCheckTree::SetItemState(item.first, ibCheckTree::UNCHECKED);
				}
			}
			m_checkedItems.clear();
		}

		m_checkedItems.insert_or_assign(id, is_checked);
	}
}

void ibCheckTree::ldhighlight(const wxTreeItemId& id)
{
	if (!id.IsOk())
		return;

	int i = ibCheckTree::GetItemState(id);

	if (ibCheckTree::UNCHECKED <= i && i < ibCheckTree::UNCHECKED_DISABLED)
	{
		ibCheckTree::SetItemState(id, ibCheckTree::UNCHECKED_LEFT_DOWN);
	}
	else if (ibCheckTree::CHECKED <= i && i < ibCheckTree::CHECKED_DISABLED)
	{
		ibCheckTree::SetItemState(id, ibCheckTree::CHECKED_LEFT_DOWN);
	}
}

ibCheckTree::ibCheckTree()
	:ibTreeCtrl()
	, mouse_entered_tree_with_left_down(false), last_mo(), last_ld(), last_kf(), m_singleCheck(false), m_allowEmpty(false)
{
}

ibCheckTree::ibCheckTree(wxWindow* parent, const wxWindowID id, const wxPoint& pos, const wxSize& size, long style)
	: ibTreeCtrl(parent, id, pos, size, style & ~wxCR_MULTIPLE_CHECK & ~wxCR_SINGLE_CHECK & ~wxCR_EMPTY_CHECK)
	, mouse_entered_tree_with_left_down(false), last_mo(), last_ld(), last_kf(), m_singleCheck((style& wxCR_SINGLE_CHECK) == wxCR_SINGLE_CHECK), m_allowEmpty((style& wxCR_EMPTY_CHECK) == wxCR_EMPTY_CHECK)
{
	Init();
}

ibCheckTree::~ibCheckTree()
{
	ibTreeCtrl::UnselectAll();
	m_colors.clear();
}

void ibCheckTree::Init()
{
	// The box in each state, in the order of the item's states (checktree.h): unchecked, then checked — normal, under the
	// mouse, pressed (the left button down), disabled. The theme's own box, as a check box and a table's flag draw it,
	// drawn on a clear bitmap of 32 bits through a graphics DC as the theme draws it: its corners stay transparent.
	ibRenderer* const renderer = ibThemeEngine::Get()->GetRenderer();
	const wxSize box = renderer->GetCheckBitmapSize();
	wxImageList* const states = new wxImageList(box.x, box.y, true);
	for (const int checked : { 0, int(wxCONTROL_CHECKED) }) {
		for (const int state : { 0, int(wxCONTROL_CURRENT), int(wxCONTROL_PRESSED), int(wxCONTROL_DISABLED) }) {
			wxBitmap picture(box, 32);
			picture.UseAlpha();
			{
				wxMemoryDC memory(picture);
				wxGCDC dc(memory);
				dc.SetBackground(*wxTRANSPARENT_BRUSH);
				dc.Clear();
				renderer->DrawCheckBox(this, dc, wxRect(box), checked | state);
			}
			states->Add(picture);
		}
	}
	AssignStateImageList(states);

	Connect(wxEVT_COMMAND_TREE_SEL_CHANGED, wxTreeEventHandler(ibCheckTree::On_Tree_Sel_Changed), nullptr, this);

	Connect(wxEVT_CHAR, wxKeyEventHandler(ibCheckTree::On_Char), nullptr, this);
	Connect(wxEVT_KEY_DOWN, wxKeyEventHandler(ibCheckTree::On_KeyDown), nullptr, this);
	Connect(wxEVT_KEY_UP, wxKeyEventHandler(ibCheckTree::On_KeyUp), nullptr, this);

	Connect(wxEVT_ENTER_WINDOW, wxMouseEventHandler(ibCheckTree::On_Mouse_Enter_Tree), nullptr, this);
	Connect(wxEVT_LEAVE_WINDOW, wxMouseEventHandler(ibCheckTree::On_Mouse_Leave_Tree), nullptr, this);
	Connect(wxEVT_LEFT_DCLICK, wxMouseEventHandler(ibCheckTree::On_Left_DClick), nullptr, this);
	Connect(wxEVT_LEFT_DOWN, wxMouseEventHandler(ibCheckTree::On_Left_Down), nullptr, this);
	Connect(wxEVT_LEFT_UP, wxMouseEventHandler(ibCheckTree::On_Left_Up), nullptr, this);
	Connect(wxEVT_MOTION, wxMouseEventHandler(ibCheckTree::On_Mouse_Motion), nullptr, this);
	Connect(wxEVT_MOUSEWHEEL, wxMouseEventHandler(ibCheckTree::On_Mouse_Wheel), nullptr, this);

	Connect(wxEVT_SET_FOCUS, wxFocusEventHandler(ibCheckTree::On_Tree_Focus_Set), nullptr, this);
	Connect(wxEVT_KILL_FOCUS, wxFocusEventHandler(ibCheckTree::On_Tree_Focus_Lost), nullptr, this);
}

void ibCheckTree::SetItemTextColour(const wxTreeItemId& item, const wxColour& col)
{
	m_colors.insert_or_assign(item, col);

	ibTreeCtrl::SetItemTextColour(item, col);
}

// The item and then its subtree's rows: a row's id is the library tree's pointer to the item, freed with it (the native
// tree refused a stale handle), and GetSelection answers from m_checkedItems - the interface editor rebuilds the tree on
// every tick, and the next tick read a freed item's data (2026-11-06).
void ibCheckTree::Delete(const wxTreeItemId& item)
{
	DeleteChildren(item);

	m_checkedItems.erase(item);
	m_colors.erase(item);
	for (wxTreeItemId* const id : { &last_mo, &last_ld, &last_kf }) {
		if (*id == item)
			*id = wxTreeItemId();
	}

	ibTreeCtrl::Delete(item);
}

void ibCheckTree::DeleteChildren(const wxTreeItemId& item)
{
	const auto under = [this, &item](wxTreeItemId id) {
		while ((id = GetItemParent(id)).IsOk()) {
			if (id == item)
				return true;
		}
		return false;
	};

	std::erase_if(m_checkedItems, [&under](const auto& row) { return under(row.first); });
	std::erase_if(m_colors, [&under](const auto& row) { return under(row.first); });
	for (wxTreeItemId* const id : { &last_mo, &last_ld, &last_kf }) {
		if (id->IsOk() && under(*id))
			*id = wxTreeItemId();
	}

	ibTreeCtrl::DeleteChildren(item);
}

bool ibCheckTree::EnableCheckBox(const wxTreeItemId& item, bool enable)
{
	if (!item.IsOk())
	{
		return false;
	}

	int i = GetItemState(item);

	if (i<0 || i>CHECKED_DISABLED)
	{
		return false;
	}
	else if (enable)
	{
		if (i == UNCHECKED_DISABLED)
		{
			SetItemState(item, UNCHECKED);
		}
		else if (i == CHECKED_DISABLED)
		{
			SetItemState(item, CHECKED);
		}

		std::map<wxTreeItemId, wxColor>::iterator it = m_colors.find(item);

		if (it != m_colors.end())
		{
			SetItemTextColour(item, it->second);
			m_colors.erase(it);
		}

		return true;
	}
	else
	{
		if (i == UNCHECKED_DISABLED || i == CHECKED_DISABLED)
		{
			//don't disable a second time or we'll lose the
			//text color information.
			return true;
		}

		if (i == UNCHECKED || i == UNCHECKED_MOUSE_OVER || i == UNCHECKED_LEFT_DOWN)
		{
			SetItemState(item, UNCHECKED_DISABLED);
		}
		else if (i == CHECKED || i == CHECKED_MOUSE_OVER || i == CHECKED_LEFT_DOWN)
		{
			SetItemState(item, CHECKED_DISABLED);
		}

		wxColor col = GetItemTextColour(item);

		SetItemTextColour(item, wxColour(161, 161, 146));
		m_colors[item] = col;
		return true;
	}
}

bool ibCheckTree::DisableCheckBox(const wxTreeItemId& item)
{
	return EnableCheckBox(item, false);
}

void ibCheckTree::MakeCheckable(const wxTreeItemId& item, bool state)
{
	if (item.IsOk())
	{
		int i = GetItemState(item);

		if (i<0 || i>CHECKED_DISABLED)
		{
			SetItemState(item, state ? CHECKED : UNCHECKED);
			// …and noted as Check notes it: the ticks are counted from m_checkedItems, and a row ticked here unnoted was
			// counted out — the last tick not to be taken off, the dialog's ticks could not be taken off ("Computed over")
			m_checkedItems.insert_or_assign(item, state);
		}
	}
}

void ibCheckTree::Check(const wxTreeItemId& item, bool state)
{
	if (item.IsOk())
	{
		int old_state = GetItemState(item);

		if (UNCHECKED <= old_state && old_state <= CHECKED_DISABLED)
		{
			bool enab = (old_state == UNCHECKED_DISABLED || old_state == CHECKED_DISABLED) ? false : true;
			int ch = enab ? CHECKED : CHECKED_DISABLED;
			int unch = enab ? UNCHECKED : UNCHECKED_DISABLED;
			int new_state = state ? ch : unch;

			int checkedCount = 0;
			for (auto i : m_checkedItems) {
				checkedCount += i.second ? 1 : 0;
			}

			if (m_singleCheck) {
				if (checkedCount > 1) {
					new_state = unch;
				}
			}

			if (!m_allowEmpty && !state && checkedCount <= 1)
				return;

			if (new_state != old_state) {
				ibCheckTree::SetItemState(item, new_state);
			}

			if (state) {
				ibCheckTree::SelectItem(item);
			}

			m_checkedItems.insert_or_assign(item, state);
		}
	}
}

void ibCheckTree::Uncheck(const wxTreeItemId& item)
{
	Check(item, false);
}

void ibCheckTree::On_Tree_Sel_Changed(wxTreeEvent& event)
{
	wxTreeItemId id = event.GetItem();

	unhighlight(last_kf);
	mohighlight(id, false);
	last_kf = id;

	event.Skip();
}

void ibCheckTree::On_Char(wxKeyEvent& event)
{
	if (!GetSelection().IsOk()) {
		//If there is no selection, any keypress should just select the first item
		wxTreeItemIdValue cookie;
		SelectItem(
			HasFlag(wxTR_HIDE_ROOT) ?
			GetFirstChild(GetRootItem(), cookie) :
			GetRootItem()
		);
		return;
	}

	event.Skip();
}

void ibCheckTree::On_KeyDown(wxKeyEvent& event)
{
	if (event.GetKeyCode() == WXK_SPACE) {
		ldhighlight(last_kf);
	}

	event.Skip();
}

void ibCheckTree::On_KeyUp(wxKeyEvent& event)
{
	if (event.GetKeyCode() == WXK_SPACE) {
		mohighlight(last_kf, true);
	}
	else if (event.GetKeyCode() == WXK_ESCAPE) {
		unhighlight(last_kf);
		last_kf = wxTreeItemId();
		Unselect();
	}

	event.Skip();
}


void ibCheckTree::On_Mouse_Enter_Tree(wxMouseEvent& event)
{
	if (event.LeftIsDown())
	{
		mouse_entered_tree_with_left_down = true;
	}
}

void ibCheckTree::On_Mouse_Leave_Tree(wxMouseEvent& event)
{
	unhighlight(last_mo);
	unhighlight(last_ld);
	last_mo = wxTreeItemId();
	last_ld = wxTreeItemId();
}

void ibCheckTree::On_Left_DClick(wxMouseEvent& event)
{
	int flags;

	HitTest(event.GetPosition(), flags);

	//double clicks on buttons can be annoying, so we'll ignore those
	//but all other double clicks will just have 1 more click added to them

	//without this, the check boxes are not as responsive as they should be
	if (!(flags & wxTREE_HITTEST_ONITEMBUTTON))
	{
		On_Left_Down(event);
		On_Left_Up(event);
	}
}

void ibCheckTree::On_Left_Down(wxMouseEvent& event)
{
	int flags;
	wxTreeItemId id = HitTest(event.GetPosition(), flags);
	if (!id.IsOk())
		return;
	int i = GetItemState(id);

	if (id.IsOk() && i >= 0 && on_check_or_label(flags))
	{
		// The box is pressed, the label only selects: a row is looked at (what it holds, its qualifiers)
		// without being checked — the release toggles the pressed row alone (On_Left_Up).
		if (flags & wxTREE_HITTEST_ONITEMSTATEICON) {
			last_ld = id;
			ldhighlight(id);
		}

		ibCheckTree::SelectItem(id);
	}
}

void ibCheckTree::On_Left_Up(wxMouseEvent& event)
{
	SetFocus();

	int flags;
	wxTreeItemId id = HitTest(event.GetPosition(), flags);
	if (!id.IsOk())
		return;
	int i = GetItemState(id);

	if (mouse_entered_tree_with_left_down)
	{
		mouse_entered_tree_with_left_down = false;

		if (i >= 0 && on_check_or_label(flags))
		{
			mohighlight(id, false);
			last_mo = id;
		}
	}
	else if (id.IsOk())
	{
		if (flags & wxTREE_HITTEST_ONITEMBUTTON && ItemHasChildren(id))
		{
			if (IsExpanded(id))
			{
				Collapse(id);
			}
			else
			{
				Expand(id);
			}
		}
		else if (i >= 0 && on_check_or_label(flags))
		{
			if (id != last_ld)
			{
				unhighlight(last_ld);
				mohighlight(id, false);
			}
			else
			{
				mohighlight(id, true);
			}

			last_ld = wxTreeItemId();
			last_mo = id;
		}
		else
		{
			unhighlight(last_ld);
			unhighlight(last_mo);
			last_ld = wxTreeItemId();
			last_mo = wxTreeItemId();
		}
	}
	else
	{
		//id is not ok
		unhighlight(last_ld);
		unhighlight(last_mo);
		last_ld = wxTreeItemId();
		last_mo = wxTreeItemId();
	}
}

void ibCheckTree::On_Mouse_Motion(wxMouseEvent& event)
{
	if (mouse_entered_tree_with_left_down)
	{
		//just ignore everything until the left button is released
		return;
	}

	int flags;
	wxTreeItemId id = HitTest(event.GetPosition(), flags);

	if (event.LeftIsDown())
	{
		//to match the behavior of ordinary check boxes,
		//if we've moved to a new item while holding the mouse button down
		//we want to set the item where the left down click occured to have
		//mouse over highlight.  And if we return to the box where the
		//left down occured, we want to return it to the having the left down highlight

		//I don't understand why this is the behavior
		//of ordinary check boxes, but I'm goin to match it anyway.

		if (id == last_ld)
		{
			if (!last_ld.IsOk())
				return;
			int i = GetItemState(last_ld);
			if (i != UNCHECKED_LEFT_DOWN && i != CHECKED_LEFT_DOWN)
			{
				ldhighlight(last_ld);
			}
		}
		else
		{
			mohighlight(last_ld, false);
		}
	}
	else if (!id.IsOk())
	{
		unhighlight(last_mo);
		last_mo = wxTreeItemId();
	}
	else
	{
		if (!id.IsOk())
			return;

		//4 cases 1 we're still on the same item, but we've moved off the state icon or label
		//        2 we're still on the same item and on the state icon or label - do nothing
		//        3 we're on a new item but not on its state icon or label (or the new item has no state)
		//        4 we're on a new item, it has a state icon, and we're on the state icon or label

		int i = GetItemState(id);

		if (id == last_mo)
		{
			if (i < 0 || !on_check_or_label(flags))
			{
				unhighlight(last_mo);
				last_mo = wxTreeItemId();
			}
			else
			{
				//nothing
			}
		}
		else
		{
			if (i < 0 || !on_check_or_label(flags))
			{
				unhighlight(last_mo);
				last_mo = wxTreeItemId();
			}
			else
			{
				unhighlight(last_mo);
				mohighlight(id, false);
				last_mo = id;
			}
		}
	}
}

void ibCheckTree::On_Mouse_Wheel(wxMouseEvent& event)
{
	event.Skip();
}

void ibCheckTree::On_Tree_Focus_Set(wxFocusEvent& event)
{
	//event.Skip();

	//skipping this event will set the last selected item
	//to be highlighted and I want the tree items to only be
	//highlighted by keyboard actionData.
}

void ibCheckTree::On_Tree_Focus_Lost(wxFocusEvent& event)
{
	unhighlight(last_kf);
	Unselect();

	event.Skip();
}

void ibCheckTree::SetFocusFromKbd()
{
	if (last_kf.IsOk()) {
		SelectItem(last_kf);
	}

	wxTreeEvent eventChoice(wxEVT_CHECKTREE_FOCUS, GetId());
	eventChoice.SetEventObject(this);
	ProcessWindowEvent(eventChoice);

	wxWindow::SetFocusFromKbd();
}

#ifndef checktree_H_INCLUDED
#define checktree_H_INCLUDED

#include <wx/treectrl.h>

#include <set>
#include <map>

#define wxCR_EMPTY_CHECK 0x1600
#define wxCR_SINGLE_CHECK 0x2000
#define wxCR_MULTIPLE_CHECK 0x4000

#include "frontend/frontend.h"

class FRONTEND_API wxCheckTree : public wxTreeCtrl {
	bool m_singleCheck;
	bool m_allowEmpty;
	std::map<wxTreeItemId, bool> m_checkedItems;
protected:

	// common part of Append/Prepend/InsertItem()
	//
	// pos is the position at which to insert the item or (size_t)-1 to append
	// it to the end
	virtual wxTreeItemId DoInsertItem(const wxTreeItemId& parent,
		size_t pos,
		const wxString& text,
		int image, int selImage,
		wxTreeItemData* data)
	{
		wxTreeItemId id = wxTreeCtrl::DoInsertItem(
			parent,
			pos,
			text,
			image, selImage,
			data
		);
		//wxCheckTree::SetItemState(id, wxCheckTree::UNCHECKED);
		//wxCheckTree::Check(id, false);
		m_checkedItems.insert_or_assign(id, false);
		return id;
	}

	// and this function implements overloaded InsertItem() taking wxTreeItemId
	// (it can't be called InsertItem() as we'd have virtual function hiding
	// problem in derived classes then)
	virtual wxTreeItemId DoInsertAfter(const wxTreeItemId& parent,
		const wxTreeItemId& idPrevious,
		const wxString& text,
		int image = -1, int selImage = -1,
		wxTreeItemData* data = nullptr)
	{
		wxTreeItemId id = wxTreeCtrl::DoInsertAfter(
			parent,
			idPrevious,
			text,
			image, selImage,
			data
		);
		//wxCheckTree::SetItemState(id, wxCheckTree::UNCHECKED);
		//wxCheckTree::Check(id, false);
		m_checkedItems.insert_or_assign(id, false);
		return id;
	}

public:

	int GetItems(wxArrayTreeItemIds& array) const {
		array.Empty();
		for (auto sel : m_checkedItems) {
			array.Add(sel.first);
		}
		return array.Count();
	}

	wxCheckTree(wxWindow* parent, const wxWindowID id,
		const wxPoint& pos, const wxSize& size,
		long style);
	wxCheckTree();
	virtual ~wxCheckTree();

	void Init();

	//methods overriden from base class:
	virtual void SetFocusFromKbd() override;
	virtual void SetItemTextColour(const wxTreeItemId& item, const wxColour& col) override;

	virtual void SetWindowStyleFlag(long style) override
	{
		m_singleCheck = (style & wxCR_MULTIPLE_CHECK) != wxCR_MULTIPLE_CHECK;

		if (m_singleCheck) {
			bool firstSel = true;
			for (auto& sel : m_checkedItems) {
				if (sel.second) {
					if (!firstSel) {
						wxCheckTree::SetItemState(sel.first, wxCheckTree::UNCHECKED);
						sel.second = false;
					}
					else {
						wxCheckTree::SelectItem(sel.first);
					}
					firstSel = false;
				}
			}
		}
		wxTreeCtrl::SetWindowStyleFlag(style);
	}

	//interaction with the check boxes:
	bool EnableCheckBox(const wxTreeItemId& item, bool enable = true);
	bool DisableCheckBox(const wxTreeItemId& item);
	void Check(const wxTreeItemId& item, bool state = true);
	void Uncheck(const wxTreeItemId& item);
	void MakeCheckable(const wxTreeItemId& item, bool state = false);

	// get the item currently selected, only if a single item is selected
	wxTreeItemId GetSelection() const override {
		if (m_checkedItems.size() == 0)
			return wxTreeItemId(nullptr);
		for (auto sel : m_checkedItems) {
			if (sel.second) {
				return sel.first;
			}
		}
		return wxTreeItemId(nullptr);
	}

	// get all the items currently selected, return count of items
	size_t GetSelections(wxArrayTreeItemIds& array) const override {
		array.Empty();
		for (auto sel : m_checkedItems) {
			if (sel.second) {
				array.Add(sel.first);
			}
		}
		return array.Count();
	}

	enum
	{
		UNCHECKED,
		UNCHECKED_MOUSE_OVER,
		UNCHECKED_LEFT_DOWN,
		UNCHECKED_DISABLED,
		CHECKED,
		CHECKED_MOUSE_OVER,
		CHECKED_LEFT_DOWN,
		CHECKED_DISABLED
	};

private:

	inline bool on_check_or_label(int flags);
	inline void unhighlight(const wxTreeItemId& id);
	inline void mohighlight(const wxTreeItemId& id, bool toggle);
	inline void ldhighlight(const wxTreeItemId& id);

	//event handlers
	void On_Tree_Sel_Changed(wxTreeEvent& event);

	void On_Char(wxKeyEvent& event);
	void On_KeyDown(wxKeyEvent& event);
	void On_KeyUp(wxKeyEvent& event);

	void On_Mouse_Enter_Tree(wxMouseEvent& event);
	void On_Mouse_Leave_Tree(wxMouseEvent& event);
	void On_Left_DClick(wxMouseEvent& event);
	void On_Left_Down(wxMouseEvent& event);
	void On_Left_Up(wxMouseEvent& event);
	void On_Mouse_Motion(wxMouseEvent& event);
	void On_Mouse_Wheel(wxMouseEvent& event);

	void On_Tree_Focus_Set(wxFocusEvent& event);
	void On_Tree_Focus_Lost(wxFocusEvent& event);

	//private data:
	std::map<wxTreeItemId, wxColor> m_colors;

	bool mouse_entered_tree_with_left_down;

	wxTreeItemId last_mo;
	wxTreeItemId last_ld;
	wxTreeItemId last_kf;

	// NB: due to an ugly wxMSW hack you _must_ use DECLARE_DYNAMIC_CLASS()
	//     if you want your overloaded OnCompareItems() to be called.
	//     OTOH, if you don't want it you may omit the next line - this will
	//     make default (alphabetical) sorting much faster under wxMSW.
	wxDECLARE_DYNAMIC_CLASS(wxCheckTree);
};

wxDECLARE_EXPORTED_EVENT(FRONTEND_API, wxEVT_CHECKTREE_CHOICE, wxTreeEvent);
wxDECLARE_EXPORTED_EVENT(FRONTEND_API, wxEVT_CHECKTREE_FOCUS, wxTreeEvent);

#endif // checktree_H_INCLUDED

#ifndef __FORM_EDITOR_H__
#define __FORM_EDITOR_H__

#include <wx/dialog.h>
#include <wx/button.h>
#include <wx/sizer.h>
#include <wx/treectrl.h>

#include <wx/propgrid/propgrid.h>
#include <wx/propgrid/manager.h>

#include <map>

#include "frontend/visualView/ctrl/form.h"

///////////////////////////////////////////////////////////////////////////////
/// Class ibDialogFormEditor
///////////////////////////////////////////////////////////////////////////////

#pragma region __commands_h__

class ibFormEditorCmd {
	bool m_executed;
public:

	ibFormEditorCmd() : m_executed(false) {}

	// VIRTUAL: m_cmdArray holds shared_ptr<ibFormEditorCmd> built from a base-typed pointer, so the
	// deleter is base-typed too and a non-virtual destructor would skip every derived one. MSVC says
	// so out loud (C5205) — the same defect the designer's ibVisualEditorCmd had, where it cost a
	// leaked control per drag-and-drop.
	virtual ~ibFormEditorCmd() = default;

	void Execute() {
		if (!m_executed) {
			DoExecute();
			m_executed = true;
		}
	}

protected:

	/**
	 * Executes the command.
	 */
	virtual void DoExecute() = 0;
};

#pragma endregion 

class FRONTEND_API ibDialogFormEditor : public wxDialog {
public:

	ibDialogFormEditor(ibValueForm* valueForm);

protected:

	ibValueFrame* GetSelectedObject() const { return m_selectedControl; }

	/**
	* Since we can associate an object with each item, this class makes it
	* easy to get the object (ibValueFrame) associated with an item so it
	* can be selected by clicking on the item.
	 */
	class ibDialogFormEditorObjectTreeItemData : public wxTreeItemData {
	public:
		ibDialogFormEditorObjectTreeItemData(ibValueFrame* obj) : m_object(obj) {}
		ibValueFrame* GetObject() { return m_object; }
	private:
		ibValueFrame* m_object = nullptr;
	};

	/**
	* Popup menu associated with each tree item.
	*
	* This object executes the menu commands related to the selected object.
	*/
	class ibDialogFormEditorItemPopupMenu : public wxMenu {
	public:

		int GetSelectedID() const { return m_selID; }

		ibDialogFormEditorItemPopupMenu(ibDialogFormEditor* parent, ibValueFrame* obj);

		void OnUpdateEvent(wxUpdateUIEvent& e);
		void OnMenuEvent(wxCommandEvent& event);

	protected:

		ibDialogFormEditor* m_handler = nullptr;
		ibValueFrame* m_object = nullptr;
		int m_selID;

		wxDECLARE_EVENT_TABLE();
	};

	/**
	 * Builds the tree completely.
	 */
	void CreateTree();
	void RebuildTree() {

		m_treeControl->Freeze();

		// Clear the old tree and map
		m_treeControl->DeleteAllItems();
		m_listItem.clear();

		if (m_owner != nullptr) {

			wxTreeItemId dummy;
			AddChildren(m_owner, dummy, true);

			// Expand items that were previously expanded
			RestoreItemStatus(m_owner);
		}

		m_treeControl->Thaw();
	}

	void AddChildren(ibValueFrame* child, const wxTreeItemId& parent, bool is_root = false);

	int GetImageIndex(const wxString& name) const {
		auto it = m_iconIdx.find(name);
		if (it != m_iconIdx.end()) {
			return it->second;
		}
		return wxNOT_FOUND; //default icon
	}

	void UpdateItem(const wxTreeItemId& id, ibValueFrame* obj) {

		// show the name
		const wxString& caption = obj->GetControlTitle();

		// update the item
		if (caption.IsEmpty()) {
			m_treeControl->SetItemText(id, _("<empty caption>"));
		}
		else {
			m_treeControl->SetItemText(id, caption);
		}

		if (m_owner != nullptr && obj == GetSelectedObject()) {
			m_treeControl->EnsureVisible(id);
			m_treeControl->SelectItem(id);
			m_treeControl->SetItemBold(id);
		}
	}

	void RestoreItemStatus(ibValueFrame* obj) {
		std::map< ibValueFrame*, wxTreeItemId>::iterator item_it = m_listItem.find(obj);
		if (item_it != m_listItem.end()) {
			wxTreeItemId id = item_it->second;

			if (obj->GetExpanded()) {
				m_treeControl->Expand(id);
			}
		}
		unsigned int i, count = obj->GetChildCount();
		for (i = 0; i < count; i++) {
			RestoreItemStatus(obj->GetChild(i));
		}
	}

	ibValueFrame* GetObjectFromTreeItem(const wxTreeItemId& item) {
		if (item.IsOk()) {
			wxTreeItemData* item_data = m_treeControl->GetItemData(item);
			if (item_data) {
				ibValueFrame* obj(((ibDialogFormEditorObjectTreeItemData*)item_data)->GetObject());
				return obj;
			}
		}

		return nullptr;
	}

	//events
	void OnUpdateEvent(wxUpdateUIEvent& e);
	void OnMenuEvent(wxCommandEvent& e);
	void OnButtonEvent(wxCommandEvent& e);

	void OnSelChanged(wxTreeEvent& event);
	void OnRightClick(wxTreeEvent& event);
	void OnBeginDrag(wxTreeEvent& event);
	void OnEndDrag(wxTreeEvent& event);
	void OnKeyDown(wxTreeEvent& event);

	void OnPropertyGridChanging(wxPropertyGridEvent& event);
	void OnPropertyGridItemSelected(wxPropertyGridEvent& event);

	//special 
	void MovePosition(ibValueFrame* obj, bool right, unsigned int num = 1);

private:

	void ExecuteCommand(ibFormEditorCmd* cmd) {
		m_cmdArray.emplace_back(cmd);
	}

	ibValueForm* m_owner;
	ibValueFrame* m_selectedControl;

	std::vector<std::shared_ptr<ibFormEditorCmd>> m_cmdArray;

	std::map<ibValueFrame*, wxTreeItemId> m_listItem;
	std::map<wxPGProperty*, ibProperty*> m_pgArray;

	std::map<wxString, int> m_iconIdx;

	wxAuiToolBar* m_mainToolBar;
	wxAuiToolBarItem* m_toolUpItem;
	wxAuiToolBarItem* m_toolDownItem;

	wxImageList* m_iconList = nullptr;
	wxTreeCtrl* m_treeControl;

	wxPropertyGridManager* m_propertyGridManager;
	wxPropertyGridPage* m_propertyGridPage;

	wxStdDialogButtonSizer* m_sdbSizer;
	wxTreeItemId m_draggedItem;

	wxButton* m_sdbSizerOK;
	wxButton* m_sdbSizerApply;
	wxButton* m_sdbSizerCancel;

	wxDECLARE_EVENT_TABLE();
};

#endif 
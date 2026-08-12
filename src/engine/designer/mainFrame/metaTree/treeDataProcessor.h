#ifndef _DATAPROCESSOR_WND_H__
#define _DATAPROCESSOR_WND_H__

#include <wx/aui/aui.h>
#include <wx/aui/auibar.h>
#include <wx/treectrl.h>
#include <wx/statbox.h>
#include <wx/statline.h>

#include <map>

#include "mainFrame/metaTree/treeConfiguration.h"
#include "backend/metadataDataProcessor.h"
 
class ibDataProcessorTree : public ibMetaTreeBase {
	wxDECLARE_DYNAMIC_CLASS(ibDataProcessorTree);

private:

	// The ROOT — the data processor itself, not a group.
	wxTreeItemId m_treeDATAPROCESSORS;

	// THE GROUP NODES, keyed by the metatype each stands for — same shape as the configuration
	// tree (treeConfiguration.h), and for the same reason: the layout is one table in the .cpp,
	// and every pass over the tree reads it instead of a hand-kept list of fields.
	std::map<ibClassID, wxTreeItemId> m_groups;

	wxTreeItemId Group(const ibClassID& clsid) const {
		const auto it = m_groups.find(clsid);
		return it != m_groups.end() ? it->second : wxTreeItemId();
	}

private:

	bool m_initialized = false;

private:

	wxTreeItemId GetSelectionIdentifier() const {
		wxTreeItemId parentItem = m_metaTreeCtrl->GetSelection();
		while (parentItem != nullptr) {
			wxTreeItemData* item = m_metaTreeCtrl->GetItemData(parentItem);
			if (item != nullptr) {
				ibTreeDataClassIdentifier* item_clsid = dynamic_cast<ibTreeDataClassIdentifier*>(item);
				if (item_clsid != nullptr) return parentItem;
			}
			parentItem = m_metaTreeCtrl->GetItemParent(parentItem);
		}
		return wxTreeItemId(nullptr);
	}

	ibClassID GetClassIdentifier() const {
		wxTreeItemData* item = m_metaTreeCtrl->GetItemData(GetSelectionIdentifier());
		if (item != nullptr) {
			ibTreeDataClassIdentifier* item_clsid = dynamic_cast<ibTreeDataClassIdentifier*>(item);
			if (item_clsid != nullptr) return item_clsid->m_clsid;
		}
		return 0;
	}

	// THE NEAREST METAOBJECT AT OR ABOVE a group row — see the twin note in treeConfiguration.h.
	ibValueMetaObject* GetMetaIdentifier() const {
		wxTreeItemId parentItem = GetSelectionIdentifier();
		wxTreeItemData* const data = m_metaTreeCtrl->GetItemData(parentItem);
		if (dynamic_cast<ibTreeDataClassIdentifier*>(data) == nullptr)
			return nullptr;

		while (parentItem != nullptr) {
			ibValueMetaObject* parent = GetMetaObject(parentItem);
			if (parent != nullptr) return parent;
			parentItem = m_metaTreeCtrl->GetItemParent(parentItem);
		}
		return nullptr;
	}

protected:

	void OnEditCaptionName(wxCommandEvent& event);
	void OnEditCaptionSynonym(wxCommandEvent& event);
	void OnEditCaptionComment(wxCommandEvent& event);

	void OnChoiceDefForm(wxCommandEvent& event);

	void OnButtonModuleClicked(wxCommandEvent& event);

protected:

	// Initialised HERE: the default constructor (reachable through wxDECLARE_DYNAMIC_CLASS) set
	// none of these, and the destructor dereferences every one of them.
	wxStaticText* m_nameCaption = nullptr;
	wxStaticText* m_synonymCaption = nullptr;
	wxStaticText* m_commentCaption = nullptr;
	wxStaticText* m_defaultForm = nullptr;
	wxTextCtrl* m_nameValue = nullptr;
	wxTextCtrl* m_synonymValue = nullptr;
	wxTextCtrl* m_commentValue = nullptr;
	wxChoice* m_defaultFormValue = nullptr;

	wxButton* m_buttonModule = nullptr;

	class ibDataProcessorTreeCtrl : public wxTreeCtrl {
		wxDECLARE_DYNAMIC_CLASS(ibDataProcessorTreeCtrl);   // used to name the configuration tree — copy-paste
	private:

		ibDataProcessorTree* m_ownerTree;
		ibMetaView* m_metaView;

	public:

		void RefreshSelectedItem(bool scroll = true) {

			const wxTreeItemId& item = GetSelection();

			if (scroll)
				wxTreeCtrl::ScrollTo(item);

			wxTreeCtrl::Refresh();
			wxTreeCtrl::Update();
		}

		ibDataProcessorTreeCtrl();
		ibDataProcessorTreeCtrl(wxWindow* parentWnd, ibDataProcessorTree* ownerWnd);
		virtual ~ibDataProcessorTreeCtrl();

		// this function is called to compare 2 items and should return -1, 0
		// or +1 if the first item is less than, equal to or greater than the
		// second one. The base class version performs alphabetic comparison
		// of item labels (GetText)
		virtual int OnCompareItems(const wxTreeItemId& item1,
			const wxTreeItemId& item2) {
			int ret = wxStrcmp(GetItemText(item1), GetItemText(item2));
			ibTreeDataMetaItem* data1 = dynamic_cast<ibTreeDataMetaItem*>(GetItemData(item1));
			ibTreeDataMetaItem* data2 = dynamic_cast<ibTreeDataMetaItem*>(GetItemData(item2));
			if (data1 != nullptr && data2 != nullptr && ret > 0) {
				ibValueMetaObject* metaObject1 = data1->m_metaObject;
				ibValueMetaObject* metaObject2 = data2->m_metaObject;
				ibValueMetaObject* parent = metaObject1->GetParent();
				wxASSERT(parent);
				return parent->ChangeChildPosition(metaObject2,
					parent->GetChildPosition(metaObject1)
				) ? ret : wxNOT_FOUND;
			}
			return ret;
		}

		//events:
		void OnLeftDClick(wxMouseEvent& event);
		void OnLeftUp(wxMouseEvent& event);
		void OnLeftDown(wxMouseEvent& event);
		void OnRightUp(wxMouseEvent& event);
		void OnRightDClick(wxMouseEvent& event);
		void OnRightDown(wxMouseEvent& event);
		void OnKeyUp(wxKeyEvent& event);
		void OnKeyDown(wxKeyEvent& event);
		void OnMouseMove(wxMouseEvent& event);

		void OnCreateItem(wxCommandEvent& event);
		void OnEditItem(wxCommandEvent& event);
		void OnRemoveItem(wxCommandEvent& event);
		void OnPropertyItem(wxCommandEvent& event);

		void OnUpItem(wxCommandEvent& event);
		void OnDownItem(wxCommandEvent& event);

		void OnSortItem(wxCommandEvent& event);

		void OnCommandItem(wxCommandEvent& event);

		void OnCopyItem(wxCommandEvent& event);
		void OnPasteItem(wxCommandEvent& event);

		void OnSetFocus(wxFocusEvent& event);

		void OnSelecting(wxTreeEvent& event);
		void OnSelected(wxTreeEvent& event);

		void OnCollapsing(wxTreeEvent& event);
		void OnExpanding(wxTreeEvent& event);

	protected:

		wxDECLARE_EVENT_TABLE();
	};

	ibDataProcessorTreeCtrl* m_metaTreeCtrl = nullptr;
	ibMetaDataDataProcessor* m_metaData = nullptr;

private:

	wxTreeItemId AppendRootItem(const ibClassID& clsid, const wxString& name = wxEmptyString) const {
		const ibCtorAbstractType* typeCtor = ibValue::GetAvailableCtor(clsid);
		wxASSERT(typeCtor);
		wxImageList* imageList = m_metaTreeCtrl->GetImageList();
		wxASSERT(imageList);
		const int imageIndex = imageList->Add(typeCtor->GetClassIcon());
		return m_metaTreeCtrl->AddRoot(name.IsEmpty() ? typeCtor->GetClassName() : name,
			imageIndex,
			imageIndex,
			nullptr
		);
	}

	wxTreeItemId AppendGroupItem(const wxTreeItemId& parent,
		const ibClassID& clsid, const wxString& name = wxEmptyString) const {
		const ibCtorAbstractType* typeCtor = ibValue::GetAvailableCtor(clsid);
		wxASSERT(typeCtor);
		wxImageList* imageList = m_metaTreeCtrl->GetImageList();
		wxASSERT(imageList);
		const int imageIndex = imageList->Add(typeCtor->GetClassIcon());
		return m_metaTreeCtrl->AppendItem(parent, name.IsEmpty() ? typeCtor->GetClassName() : name,
			imageIndex,
			imageIndex,
			new wxTreeItemClsidData(clsid)
		);
	}

	wxTreeItemId AppendGroupItem(const wxTreeItemId& parent,
		const ibClassID& clsid, ibValueMetaObject* metaObject) const {
		wxImageList* imageList = m_metaTreeCtrl->GetImageList();
		wxASSERT(imageList);
		const int imageIndex = imageList->Add(metaObject->GetIcon());
		return m_metaTreeCtrl->AppendItem(parent, metaObject->GetName(),
			imageIndex,
			imageIndex,
			new wxTreeItemClsidMetaData(clsid, metaObject)
		);
	}

	wxTreeItemId AppendItem(const wxTreeItemId& parent,
		ibValueMetaObject* metaObject) const {
		wxImageList* imageList = m_metaTreeCtrl->GetImageList();
		wxASSERT(imageList);
		const int imageIndex = imageList->Add(metaObject->GetIcon());
		return m_metaTreeCtrl->AppendItem(parent, metaObject->GetName(),
			imageIndex,
			imageIndex,
			new wxTreeItemMetaData(metaObject)
		);
	}

	// HUB — append a command node and, recursively, its sub-commands. A data processor owns
	// commands exactly as it does inside a configuration; this tree simply had no node for them.
	void AppendCommandNode(const wxTreeItemId& parent, ibValueMetaObject* command);

	void ActivateItem(const wxTreeItemId& item);

	ibValueMetaObject* NewItem(const ibClassID& clsid, ibValueMetaObject* parent, bool runObject = true);
	ibValueMetaObject* CreateItem(bool showValue = true);

	wxTreeItemId FillItem(ibValueMetaObject* metaItem, const wxTreeItemId& item, bool select = true, bool scroll = true);

	void EditItem();
	void RemoveItem();
	void EraseItem(const wxTreeItemId& item);
	void SelectItem();
	void PropertyItem();

	void Collapse();
	void Expand();

	void UpItem();
	void DownItem();

	void SortItem();

	void CommandItem(unsigned int id);
	void PrepareContextMenu(wxMenu* menu, const wxTreeItemId& item);
	void ShowContextMenu(wxWindow* eventSrc, const wxTreeItemId& item, const wxPoint& pos);

	void FillData();

	ibValueMetaObject* GetMetaObject(const wxTreeItemId& item) const {
		if (!item.IsOk())
			return nullptr;
		ibTreeDataMetaItem* data =
			dynamic_cast<ibTreeDataMetaItem*>(m_metaTreeCtrl->GetItemData(item));
		if (data == nullptr)
			return nullptr;
		return data->m_metaObject;
	}

	void UpdateToolbar(ibValueMetaObject* obj, const wxTreeItemId& item);

public:

	virtual void UpdateChoiceSelection();

public:

	bool RenameMetaObject(ibValueMetaObject* obj, const wxString& sNewName);

public:

	virtual ibMetaData* GetMetaData() const { return m_metaData; }

	ibDataProcessorTree() { }
	ibDataProcessorTree(ibMetaDocument* docParent, wxWindow* parent, wxWindowID id = wxID_ANY);
	virtual ~ibDataProcessorTree();
	
	void InitTree();

	bool Load(ibMetaDataDataProcessor* metaData);
	bool Save();

	void ActivateTree();
	void ClearTree();
};

#endif 
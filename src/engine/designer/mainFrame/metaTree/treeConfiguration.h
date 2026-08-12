#ifndef _METATREE_H__
#define _METATREE_H__

#include "frontend/docView/docView.h"
#include "backend/metadataConfiguration.h"
#include "backend/debugger/debugDefs.h"

#include <wx/aui/aui.h>
#include <wx/srchctrl.h>
#include <wx/treectrl.h>

#include <map>

class ibMetaTreeBase : public wxPanel,
	public ibBackendMetadataTree {
	wxDECLARE_ABSTRACT_CLASS(ibMetaTreeBase);
public:

	// PAIRED UI STATE SURVIVES AN EARLY EXIT — the same rule as ibClipboardLock and wx's own
	// wxWindowUpdateLocker. Events are switched off around a total rebuild, and a throw from
	// InitTree (metadata is read there) used to fly past the switch-back: the navigator stayed
	// deaf to every click for the rest of the session, with nothing on screen to say why.
	class ibEventsOff {
		wxWindow* const m_window;
	public:
		explicit ibEventsOff(wxWindow* window) : m_window(window) { m_window->SetEvtHandlerEnabled(false); }
		~ibEventsOff() { m_window->SetEvtHandlerEnabled(true); }
	};

	ibMetaTreeBase() : wxPanel(), m_docParent(nullptr), m_searchTree(nullptr), m_bReadOnly(false) {}
	ibMetaTreeBase(wxWindow* parent, int id = wxID_ANY) : wxPanel(parent, id), m_docParent(nullptr), m_bReadOnly(false) {}
	ibMetaTreeBase(ibMetaDocument* docParent, wxWindow* parent, int id = wxID_ANY) : wxPanel(parent, id), m_docParent(docParent), m_searchTree(nullptr), m_bReadOnly(false) {}

	virtual ibFormID SelectFormType(ibValueMetaObjectForm* metaObject) const;
	virtual void Activate();

	virtual void SetReadOnly(bool readOnly = true) { m_bReadOnly = readOnly; }
	virtual bool IsEditable() const { return !m_bReadOnly; }

	virtual void Modify(bool modify);

	virtual bool OpenObjectForm(ibValueMetaObject* metaObject);
	virtual bool OpenObjectForm(ibValueMetaObject* metaObject, ibBackendMetaDocument*& foundedDoc);
	virtual bool CloseObjectForm(ibValueMetaObject* metaObject);

#pragma region __predefined_values_h__
	virtual void EditPredefinedValues(ibValueMetaObjectRecordDataHierarchyMutableRef* obj) {}
#pragma endregion

	virtual ibMetaDocument* GetDocument(ibValueMetaObject* metaObject) const;

	virtual void CloseMetaObject(ibValueMetaObject* metaObject) {
		ibMetaDocument* doc = GetDocument(metaObject);
		if (doc != nullptr) {
			doc->DeleteAllViews();
		}
	}

	virtual ibMetaData* GetMetaData() const = 0;

protected:

	class wxTreeItemClsidData : public wxTreeItemData,
		public ibTreeDataClassIdentifier {
	public:
		wxTreeItemClsidData(const ibClassID& clsid) : ibTreeDataClassIdentifier(clsid) {}
	};

	class wxTreeItemMetaData : public wxTreeItemData, public ibTreeDataMetaItem {
	public:
		wxTreeItemMetaData(ibValueMetaObject* metaObject) : ibTreeDataMetaItem(metaObject) {}
	};

	class wxTreeItemClsidMetaData : public wxTreeItemData,
		public ibTreeDataMetaItem, public ibTreeDataClassIdentifier {
	public:
		wxTreeItemClsidMetaData(const ibClassID& clsid, ibValueMetaObject* metaObject) :
			ibTreeDataClassIdentifier(clsid), ibTreeDataMetaItem(metaObject)
		{
		}
	};

	void CreateToolBar(wxWindow* parent);
	void EditModule(const ibGuid& moduleName, int lineNumber, bool setRunLine = true);

	enum
	{
		ID_METATREE_NEW = 15000,
		ID_METATREE_EDIT,
		ID_METATREE_DELETE,
		ID_METATREE_PROPERTY,

		ID_METATREE_UP,
		ID_METATREE_DOWM,

		ID_METATREE_SORT,
	};

protected:

	// Initialised HERE rather than in each constructor: there are three of them, one of them
	// (wxWindow*, int) never set m_searchTree, and the destructor calls Destroy() on it.
	wxSearchCtrl* m_searchTree = nullptr;
	wxAuiToolBar* m_metaTreeToolbar = nullptr;
	ibMetaDocument* m_docParent = nullptr;

	bool			m_bReadOnly = false;
};

class ibConfigurationTree : public ibMetaTreeBase {
	wxDECLARE_DYNAMIC_CLASS(ibConfigurationTree);
private:

	wxTreeItemId m_treeMETADATA;
	wxTreeItemId m_treeCOMMON; //special tree

	// THE GROUP NODES, keyed by the metatype each one stands for. Filled by one pass over the
	// layout table (s_groups in the .cpp), which is the only place that says what groups exist and
	// in what order; every other pass over the tree — clear, fill, filter — works through this map.
	//
	// It replaced twenty-three named fields, and the fields were not merely verbose: each pass over
	// the tree spelled them out as a list of its own, and the lists drifted (ClearTree's had fallen
	// three entries behind, invisibly, because that block ran just before DeleteAllItems).
	std::map<ibClassID, wxTreeItemId> m_groups;

	// The group node standing for a metatype. Invalid when there is none — a real state, not an
	// error: the search filter drops a group that matched nothing, and the entry goes with it.
	wxTreeItemId Group(const ibClassID& clsid) const {
		const auto it = m_groups.find(clsid);
		return it != m_groups.end() ? it->second : wxTreeItemId();
	}

	// (No named field per group. Two callsites need a specific group — "Insert data processor /
	//  report" and its context menu — and they ask for it by metatype: Group(g_metaReportCLSID).
	//  A field per group meant twenty-three assignments to serve those two, which is the same
	//  hand-kept list the layout table exists to remove.)

private:

	mutable wxString m_strSearch;

	enum
	{
		ID_METATREE_INSERT = ID_METATREE_SORT + 1,
		ID_METATREE_REPLACE,
		ID_METATREE_SAVE,
	};

	wxTreeItemId GetSelectionIdentifier() const {
		return GetSelectionIdentifier(
			m_metaTreeCtrl->GetSelection()
		);
	}

	wxTreeItemId GetSelectionIdentifier(const wxTreeItemId& id) const {
		wxTreeItemId parentItem = id;
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
		return GetClassIdentifier(GetSelectionIdentifier());
	}

	ibClassID GetClassIdentifier(const wxTreeItemId& id) const {
		wxTreeItemData* item = m_metaTreeCtrl->GetItemData(id);
		if (item != nullptr) {
			ibTreeDataClassIdentifier* item_clsid = dynamic_cast<ibTreeDataClassIdentifier*>(item);
			if (item_clsid != nullptr) return item_clsid->m_clsid;
		}
		return 0;
	}

	ibValueMetaObject* GetMetaIdentifier() const {
		return GetMetaIdentifier(GetSelectionIdentifier());
	}

	// THE NEAREST METAOBJECT AT OR ABOVE a group row — what a new object gets created under.
	// The inner `wxTreeItemData* item` that used to sit in the loop shadowed the outer one (C4456)
	// and answered nothing: GetMetaObject already returns null for a row that carries no metaobject.
	ibValueMetaObject* GetMetaIdentifier(const wxTreeItemId& id) const {
		wxTreeItemData* const data = m_metaTreeCtrl->GetItemData(id);
		if (dynamic_cast<ibTreeDataClassIdentifier*>(data) == nullptr)
			return nullptr;

		wxTreeItemId parentItem = id;
		while (parentItem != nullptr) {
			ibValueMetaObject* parent = GetMetaObject(parentItem);
			if (parent != nullptr) return parent;
			parentItem = m_metaTreeCtrl->GetItemParent(parentItem);
		}
		return nullptr;
	}

private:

	ibMetaDataConfigurationBase* m_metaData;

	class ibMetaTreeCtrl : public wxTreeCtrl {
		// NAMES ITSELF — it used to name the owner tree. The macro ignores its argument, so that
		// compiled and simply read as a lie; the matching wxIMPLEMENT in the .cpp has always been
		// the nested class. The same slip was in both external trees and was fixed with this one.
		wxDECLARE_DYNAMIC_CLASS(ibMetaTreeCtrl);

		class ibMetaTreeView : public ibMetaView
		{
		public:

			ibMetaTreeView(ibMetaTreeCtrl* tree) : m_ownerTree(tree) {}
			virtual void OnActivateView(bool activate, ibView* activeView, ibView* deactiveView) override;

		private:
			ibMetaTreeCtrl* m_ownerTree;
		};

	private:
		ibConfigurationTree* m_ownerTree;
		ibMetaView* m_metaView;
	private:
		wxTreeItemId m_draggedItem;
	public:

		ibValueMetaObject* GetMetaObject(const wxTreeItemId& item) const {
			if (!item.IsOk()) return nullptr;
			ibTreeDataMetaItem* data = dynamic_cast<ibTreeDataMetaItem*>(GetItemData(item));
			if (data == nullptr) return nullptr;
			return data->m_metaObject;
		}

		void RefreshSelectedItem(bool scroll = true) {

			const wxTreeItemId& item = GetSelection();

			if (scroll)
				wxTreeCtrl::ScrollTo(item);

			wxTreeCtrl::Refresh();
			wxTreeCtrl::Update();
		}

		ibMetaTreeCtrl();
		ibMetaTreeCtrl(ibConfigurationTree* parent);
		virtual ~ibMetaTreeCtrl();

		// this function is called to compare 2 items and should return -1, 0
		// or +1 if the first item is less than, equal to or greater than the
		// second one. The base class version performs alphabetic comparison
		// of item labels (GetText)
		// ⚠ ASK THE MIXIN, not the concrete node class. `wxTreeItemMetaData` is the payload of a
		// PLAIN row; a row that is also a group (a tabular section, a section) carries
		// `wxTreeItemClsidMetaData` instead, and a cast to the concrete class missed it — so
		// sorting the Tables group did nothing here while the external trees, which have always
		// asked the mixin, reordered the tabular sections. Sorting a group's contents is what the
		// button means, so all three now agree on the external trees' behaviour.
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

		void OnBeginDrag(wxTreeEvent& event);
		void OnEndDrag(wxTreeEvent& event);

		void OnStartSearch(wxCommandEvent& event);
		void OnCancelSearch(wxCommandEvent& event);

		void OnItemActivated(wxTreeEvent& event);
		void OnCreateItem(wxCommandEvent& event);
		void OnEditItem(wxCommandEvent& event);
		void OnRemoveItem(wxCommandEvent& event);
		void OnPropertyItem(wxCommandEvent& event);

		void OnUpItem(wxCommandEvent& event);
		void OnDownItem(wxCommandEvent& event);

		void OnSortItem(wxCommandEvent& event);

		void OnInsertItem(wxCommandEvent& event);
		void OnReplaceItem(wxCommandEvent& event);
		void OnSaveItem(wxCommandEvent& event);

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

	ibMetaTreeCtrl* m_metaTreeCtrl;

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

	// HUB — append a command node and, recursively, its sub-commands (a group command holds commands, shown
	// nested, exactly as a subsystem holds subsystems). Defined in the .cpp — needs the full command type.
	// Returns the node, or an invalid id when the search filter dropped it.
	wxTreeItemId AppendCommandNode(const wxTreeItemId& parent, ibValueMetaObject* command);

	// FILL ONE GROUP OF AN OBJECT — append what answers the search, then drop the group itself when
	// the search left it empty. This is what makes the search box a filter rather than a top-level
	// lookup: the test was written out at every one of these loops and left COMMENTED OUT in all of
	// them, so a search could find a catalog by name but never an attribute, a form or a template.
	template <typename TArray>
	wxTreeItemId AppendObjectGroup(const wxTreeItemId& parent, const ibClassID& groupClsid,
		const wxString& label, const TArray& objects) {
		const wxTreeItemId group = AppendGroupItem(parent, groupClsid, label);
		for (auto object : objects) {
			if (object->IsDeleted())
				continue;
			if (!object->IsAcceptedByParent())
				continue;
			if (!MatchesSearch(object))
				continue;
			AppendItem(group, object);
		}
		if (!m_strSearch.IsEmpty() && !m_metaTreeCtrl->HasChildren(group)) {
			m_metaTreeCtrl->Delete(group);
			return wxTreeItemId();
		}
		return group;
	}

	// TABULAR SECTIONS — the one group whose rows are groups themselves: each table shows its own
	// columns. A table survives a search if IT matched or one of its columns did.
	template <typename TArray>
	wxTreeItemId AppendTableGroup(const wxTreeItemId& parent, const ibClassID& tableClsid,
		const wxString& label, const TArray& tables) {
		const wxTreeItemId group = AppendGroupItem(parent, tableClsid, label);
		for (auto metaTable : tables) {
			if (metaTable->IsDeleted())
				continue;
			const wxTreeItemId hTable = AppendGroupItem(group, g_metaAttributeCLSID, metaTable);
			for (auto attribute : metaTable->GetAttributeArrayObject()) {
				if (attribute->IsDeleted())
					continue;
				if (!attribute->IsAcceptedByParent())
					continue;
				if (!MatchesSearch(attribute))
					continue;
				AppendItem(hTable, attribute);
			}
			if (!m_strSearch.IsEmpty() && !MatchesSearch(metaTable)
				&& !m_metaTreeCtrl->HasChildren(hTable))
				m_metaTreeCtrl->Delete(hTable);
		}
		if (!m_strSearch.IsEmpty() && !m_metaTreeCtrl->HasChildren(group)) {
			m_metaTreeCtrl->Delete(group);
			return wxTreeItemId();
		}
		return group;
	}

	// COMMANDS — the rows nest, so the filtering is AppendCommandNode's own job; this only decides
	// whether the group is left standing.
	template <typename TArray>
	wxTreeItemId AppendCommandGroup(const wxTreeItemId& parent, const wxString& label,
		const TArray& commands) {
		const wxTreeItemId group = AppendGroupItem(parent, g_metaCommandCLSID, label);
		for (auto metaCommand : commands)
			AppendCommandNode(group, metaCommand);
		if (!m_strSearch.IsEmpty() && !m_metaTreeCtrl->HasChildren(group)) {
			m_metaTreeCtrl->Delete(group);
			return wxTreeItemId();
		}
		return group;
	}

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

	void InsertItem();
	void ReplaceItem();
	void SaveItem();

	void CommandItem(unsigned int id);
	void PrepareReplaceMenu(wxMenu* menu);
	void PrepareContextMenu(wxMenu* menu, const wxTreeItemId& item);

	void ShowContextMenu(wxWindow* eventSrc, const wxTreeItemId& item, const wxPoint& pos);

	// HOW A METAOBJECT UNFOLDS — the one dispatcher, used by the initial fill AND the create path.
	// Which existing kind a metatype renders AS (a chart of accounts as a catalog, an accounting
	// register as an accumulation register) is the only real knowledge in it.
	void ExpandMetaItem(ibValueMetaObject* metaItem, const wxTreeItemId& item);

	// Does this object answer the search box — asked in one place, case-insensitive, name OR synonym.
	bool MatchesSearch(const ibValueMetaObject* metaObject) const;

	// Close every editor opened from this navigator. Part of LEAVING a configuration — deliberately
	// not part of ClearTree, which a search runs on every keystroke.
	void CloseOwnedDocuments();

	void AddInterfaceItem(ibValueMetaObject* obj, const wxTreeItemId& item);

	void AddCatalogItem(ibValueMetaObject* obj, const wxTreeItemId& item);
	void AddDocumentItem(ibValueMetaObject* obj, const wxTreeItemId& item);
	void AddEnumerationItem(ibValueMetaObject* obj, const wxTreeItemId& item);
	void AddDataProcessorItem(ibValueMetaObject* obj, const wxTreeItemId& item);
	void AddReportItem(ibValueMetaObject* obj, const wxTreeItemId& item);
	void AddInformationRegisterItem(ibValueMetaObject* obj, const wxTreeItemId& item);
	void AddAccumulationRegisterItem(ibValueMetaObject* obj, const wxTreeItemId& item);

	void FillData();

	ibValueMetaObject* GetMetaObject(const wxTreeItemId& item) const {
		return m_metaTreeCtrl->GetMetaObject(item);
	}

	void UpdateToolbar(ibValueMetaObject* obj, const wxTreeItemId& item);

public:

	bool RenameMetaObject(ibValueMetaObject* metaObject, const wxString& newName);

#pragma region __predefined_values_h__
	virtual void EditPredefinedValues(ibValueMetaObjectRecordDataHierarchyMutableRef* obj);
#pragma endregion

#pragma region __home_page_h__
	virtual void EditHomePage(ibValueMetaObjectConfiguration* obj);
#pragma endregion

	virtual ibMetaData* GetMetaData() const { return m_metaData; }

	ibConfigurationTree();
	ibConfigurationTree(wxWindow* parent, int id = wxID_ANY);
	ibConfigurationTree(ibMetaDocument* docParent, wxWindow* parent, int id = wxID_ANY);
	virtual ~ibConfigurationTree();

	void InitTree();

	bool Load(ibMetaDataConfigurationBase* metadata = nullptr);
	bool Save();

	void Search(const wxString& strSearch);

	void ActivateTree();
	void ClearTree();
};

#endif 
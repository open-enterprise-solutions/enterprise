#ifndef _METATREE_H__
#define _METATREE_H__

#include "frontend/docView/docView.h"
#include "backend/metadataConfiguration.h"
#include "backend/debugger/debugDefs.h"

#include <wx/aui/aui.h>
#include <wx/srchctrl.h>
#include <wx/treectrl.h>

class ibMetaDataTree : public wxPanel,
	public ibBackendMetadataTree {
	wxDECLARE_ABSTRACT_CLASS(ibMetaDataTree);
public:

	ibMetaDataTree() : wxPanel(), m_docParent(nullptr), m_searchTree(nullptr), m_bReadOnly(false) {}
	ibMetaDataTree(wxWindow* parent, int id = wxID_ANY) : wxPanel(parent, id), m_docParent(nullptr), m_bReadOnly(false) {}
	ibMetaDataTree(ibMetaDocument* docParent, wxWindow* parent, int id = wxID_ANY) : wxPanel(parent, id), m_docParent(docParent), m_searchTree(nullptr), m_bReadOnly(false) {}

	virtual ibFormID SelectFormType(ibValueMetaObjectForm* metaObject) const;
	virtual void Activate();

	virtual void SetReadOnly(bool readOnly = true) { m_bReadOnly = readOnly; }
	virtual bool IsEditable() const { return !m_bReadOnly; }

	virtual void Modify(bool modify);

	virtual bool OpenFormMDI(ibValueMetaObject* metaObject);
	virtual bool OpenFormMDI(ibValueMetaObject* metaObject, ibBackendMetaDocument*& foundedDoc);
	virtual bool CloseFormMDI(ibValueMetaObject* metaObject);

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

	wxSearchCtrl* m_searchTree;
	wxAuiToolBar* m_metaTreeToolbar;
	ibMetaDocument* m_docParent;

	bool			m_bReadOnly;
};

class ibMetadataTree : public ibMetaDataTree {
	wxDECLARE_DYNAMIC_CLASS(ibMetadataTree);
private:

	wxTreeItemId m_treeMETADATA;
	wxTreeItemId m_treeCOMMON; //special tree

	wxTreeItemId m_treeMODULES;
	wxTreeItemId m_treeFORMS;
	wxTreeItemId m_treeTEMPLATES;

	wxTreeItemId m_treePICTURES;
	wxTreeItemId m_treeINTERFACES;
	wxTreeItemId m_treeROLES;
	wxTreeItemId m_treeLANGUAGES;

	wxTreeItemId m_treeCONSTANTS;

	wxTreeItemId m_treeCATALOGS;
	wxTreeItemId m_treeDOCUMENTS;
	wxTreeItemId m_treeENUMERATIONS;
	wxTreeItemId m_treeDATAPROCESSORS;
	wxTreeItemId m_treeREPORTS;
	wxTreeItemId m_treeINFORMATION_REGISTERS;
	wxTreeItemId m_treeACCUMULATION_REGISTERS;
	wxTreeItemId m_treeCHARTS_OF_CHARACTERISTIC_TYPES;
	wxTreeItemId m_treeCHARTS_OF_ACCOUNTS;
	wxTreeItemId m_treeACCOUNTING_REGISTERS;

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

	ibValueMetaObject* GetMetaIdentifier(const wxTreeItemId& id) const {
		wxTreeItemId parentItem = id;
		wxTreeItemData* item = m_metaTreeCtrl->GetItemData(parentItem);
		if (item != nullptr) {
			ibTreeDataClassIdentifier* item_clsid = dynamic_cast<ibTreeDataClassIdentifier*>(item);
			if (item_clsid != nullptr) {
				while (parentItem != nullptr) {
					wxTreeItemData* item = m_metaTreeCtrl->GetItemData(parentItem);
					if (item != nullptr) {
						ibValueMetaObject* parent = GetMetaObject(parentItem);
						if (parent != nullptr) return parent;
					}
					parentItem = m_metaTreeCtrl->GetItemParent(parentItem);
				}
			}
		}
		return nullptr;
	}

private:

	ibMetaDataConfigurationBase* m_metaData;

	class ibMetaTreeCtrl : public wxTreeCtrl {
		wxDECLARE_DYNAMIC_CLASS(ibMetadataTree);

		class ibMatadataTreeView : public ibMetaView
		{
		public:

			ibMatadataTreeView(ibMetaTreeCtrl* tree) : m_ownerTree(tree) {}
			virtual void OnActivateView(bool activate, wxView* activeView, wxView* deactiveView) override;

		private:
			ibMetaTreeCtrl* m_ownerTree;
		};

	private:
		ibMetadataTree* m_ownerTree;
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
		ibMetaTreeCtrl(ibMetadataTree* parent);
		virtual ~ibMetaTreeCtrl();

		// this function is called to compare 2 items and should return -1, 0
		// or +1 if the first item is less than, equal to or greater than the
		// second one. The base class version performs alphabetic comparison
		// of item labels (GetText)
		virtual int OnCompareItems(const wxTreeItemId& item1,
			const wxTreeItemId& item2) {
			int ret = wxStrcmp(GetItemText(item1), GetItemText(item2));
			wxTreeItemMetaData* data1 = dynamic_cast<wxTreeItemMetaData*>(GetItemData(item1));
			wxTreeItemMetaData* data2 = dynamic_cast<wxTreeItemMetaData*>(GetItemData(item2));
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

	virtual ibMetaData* GetMetaData() const { return m_metaData; }

	ibMetadataTree();
	ibMetadataTree(wxWindow* parent, int id = wxID_ANY);
	ibMetadataTree(ibMetaDocument* docParent, wxWindow* parent, int id = wxID_ANY);
	virtual ~ibMetadataTree();

	void InitTree();

	bool Load(ibMetaDataConfigurationBase* metadata = nullptr);
	bool Save();

	void Search(const wxString& strSearch);

	void ActivateTree();
	void ClearTree();
};

#endif 
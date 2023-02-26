#ifndef _METATREE_H__
#define _METATREE_H__

#include "core/frontend/docView/docView.h"
#include "core/metadata/metadata.h"
#include "core/compiler/debugger/debugEvent.h"

#include <wx/aui/aui.h>
#include <wx/treectrl.h>

class IMetadataTree : public wxPanel,
	public IMetadataWrapperTree {
	wxDECLARE_ABSTRACT_CLASS(IMetadataTree);
public:

	virtual void SetReadOnly(bool readOnly = true) {
		m_bReadOnly = readOnly;
	}

	virtual IMetadata* GetMetadata() const = 0;

	IMetadataTree() : wxPanel(), m_docParent(NULL), m_bReadOnly(false) {}
	IMetadataTree(wxWindow* parent, int id = wxID_ANY) : wxPanel(parent, id), m_docParent(NULL), m_bReadOnly(false) {}
	IMetadataTree(CDocument* docParent, wxWindow* parent, int id = wxID_ANY) : wxPanel(parent, id), m_docParent(docParent), m_bReadOnly(false) {}

	virtual void Modify(bool modify);

	virtual void SetMetaName(const wxString& confName) {};

	virtual bool OpenFormMDI(IMetaObject* metaObject);
	virtual bool OpenFormMDI(IMetaObject* metaObject, CDocument*& foundedDoc);

	virtual bool CloseFormMDI(IMetaObject* metaObject);
	virtual void OnCloseDocument(CDocument* metaObject);

	virtual CDocument* GetDocument(IMetaObject* metaObject) const;

	virtual void CloseMetaObject(IMetaObject* metaObject) {
		CDocument* doc = GetDocument(metaObject);
		if (doc != NULL)
			doc->DeleteAllViews();
	}

protected:

	class wxTreeItemClsidData : public wxTreeItemData,
		public treeClsidData_t, public treeData_t {
	public:
		wxTreeItemClsidData(const CLASS_ID& clsid) : treeClsidData_t(clsid) {}
	};

	class wxTreeItemMetaData : public wxTreeItemData,
		public treeMetaData_t, public treeData_t {
	public:
		wxTreeItemMetaData(IMetaObject* metaObject) : treeMetaData_t(metaObject) {}
	};

	class wxTreeItemClsidMetaData : public wxTreeItemData,
		public treeMetaData_t, public treeClsidData_t, public treeData_t {
	public:
		wxTreeItemClsidMetaData(const CLASS_ID& clsid, IMetaObject* metaObject) :
			treeClsidData_t(clsid), treeMetaData_t(metaObject)
		{
		}
	};

	void CreateToolBar(wxWindow *parent);
	void EditModule(const wxString& fullName, int lineNumber, bool setRunLine = true);

	enum
	{
		ID_METATREE_NEW = 15000,
		ID_METATREE_EDIT,
		ID_METATREE_REMOVE,
		ID_METATREE_PROPERTY,

		ID_METATREE_UP,
		ID_METATREE_DOWM,

		ID_METATREE_SORT,
	};

protected:

	wxAuiToolBar* m_metaTreeToolbar;
	CDocument* m_docParent;

	bool m_bReadOnly;

	//список открытых элементов 
	std::vector <CDocument*> m_metaOpenedForms;
};

#define metadataTree CMetadataTree::GetMetadataTree()

class CMetadataTree : public IMetadataTree {
	wxDECLARE_DYNAMIC_CLASS(CMetadataTree);
private:

	wxTreeItemId m_treeMETADATA;
	wxTreeItemId m_treeCOMMON; //special tree

	wxTreeItemId m_treeMODULES;
	wxTreeItemId m_treeFORMS;
	wxTreeItemId m_treeTEMPLATES;

	wxTreeItemId m_treeINTERFACES;
	wxTreeItemId m_treeROLES;

	wxTreeItemId m_treeCONSTANTS;

	wxTreeItemId m_treeCATALOGS;
	wxTreeItemId m_treeDOCUMENTS;
	wxTreeItemId m_treeENUMERATIONS;
	wxTreeItemId m_treeDATAPROCESSORS;
	wxTreeItemId m_treeREPORTS;
	wxTreeItemId m_treeINFORMATION_REGISTERS;
	wxTreeItemId m_treeACCUMULATION_REGISTERS;

private:

	enum
	{
		ID_METATREE_INSERT = ID_METATREE_SORT + 1,
		ID_METATREE_REPLACE,
		ID_METATREE_SAVE,
	};

private:

	IConfigMetadata* m_metaData;

	class CMetadataTreeWnd : public wxTreeCtrl {
		wxDECLARE_DYNAMIC_CLASS(CMetadataTree);
	private:
		CMetadataTree* m_ownerTree;
	private:
		wxTreeItemId m_draggedItem;
	public:

		CMetadataTreeWnd();
		CMetadataTreeWnd(CMetadataTree* parent);
		virtual ~CMetadataTreeWnd();

		// this function is called to compare 2 items and should return -1, 0
		// or +1 if the first item is less than, equal to or greater than the
		// second one. The base class version performs alphabetic comparison
		// of item labels (GetText)
		virtual int OnCompareItems(const wxTreeItemId& item1,
			const wxTreeItemId& item2) {
			int ret = wxStrcmp(GetItemText(item1), GetItemText(item2));
			wxTreeItemMetaData* data1 = dynamic_cast<wxTreeItemMetaData*>(GetItemData(item1));
			wxTreeItemMetaData* data2 = dynamic_cast<wxTreeItemMetaData*>(GetItemData(item2));
			if (data1 != NULL && data2 != NULL && ret > 0) {
				IMetaObject* metaObject1 = data1->m_metaObject;
				IMetaObject* metaObject2 = data2->m_metaObject;
				IMetaObject* parent = metaObject1->GetParent();
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

		void OnDebugEvent(wxDebugEvent& event);

		void OnSelecting(wxTreeEvent& event);
		void OnSelected(wxTreeEvent& event);

		void OnCollapsing(wxTreeEvent& event);
		void OnExpanding(wxTreeEvent& event);

	protected:
		wxDECLARE_EVENT_TABLE();
	};

	CMetadataTreeWnd* m_metaTreeWnd;

private:

	wxTreeItemId AppendRootItem(const CLASS_ID& clsid, const wxString& name = wxEmptyString) const {
		IObjectValueAbstract* singleObject = CValue::GetAvailableObject(clsid);
		wxASSERT(singleObject);
		wxImageList* imageList = m_metaTreeWnd->GetImageList();
		wxASSERT(imageList);
		int imageIndex = imageList->Add(singleObject->GetClassIcon());
		return m_metaTreeWnd->AddRoot(name.IsEmpty() ? singleObject->GetClassName() : name,
			imageIndex,
			imageIndex,
			NULL
		);
	}

	wxTreeItemId AppendGroupItem(const wxTreeItemId& parent,
		const CLASS_ID& clsid, const wxString& name = wxEmptyString) const {
		IObjectValueAbstract* singleObject = CValue::GetAvailableObject(clsid);
		wxASSERT(singleObject);
		wxImageList* imageList = m_metaTreeWnd->GetImageList();
		wxASSERT(imageList);
		int imageIndex = imageList->Add(singleObject->GetClassIcon());
		return m_metaTreeWnd->AppendItem(parent, name.IsEmpty() ? singleObject->GetClassName() : name,
			imageIndex,
			imageIndex,
			new wxTreeItemClsidData(clsid)
		);
	}

	wxTreeItemId AppendGroupItem(const wxTreeItemId& parent,
		const CLASS_ID& clsid, IMetaObject* metaObject) const {
		IObjectValueAbstract* singleObject = CValue::GetAvailableObject(metaObject->GetClsid());
		wxASSERT(singleObject);
		wxImageList* imageList = m_metaTreeWnd->GetImageList();
		wxASSERT(imageList);
		int imageIndex = imageList->Add(singleObject->GetClassIcon());
		return m_metaTreeWnd->AppendItem(parent, metaObject->GetName(),
			imageIndex,
			imageIndex,
			new wxTreeItemClsidMetaData(clsid, metaObject)
		);
	}

	wxTreeItemId AppendItem(const wxTreeItemId& parent,
		IMetaObject* metaObject) const {
		wxImageList* imageList = m_metaTreeWnd->GetImageList();
		wxASSERT(imageList);
		int imageIndex = imageList->Add(metaObject->GetIcon());
		return m_metaTreeWnd->AppendItem(parent, metaObject->GetName(),
			imageIndex,
			imageIndex,
			new wxTreeItemMetaData(metaObject)
		);
	}

	void ActivateItem(const wxTreeItemId& item);

	IMetaObject *CreateItem(bool showValue = true);
	void EditItem();
	void RemoveItem();
	void EraseItem(const wxTreeItemId& item);
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

	void AddCatalogItem(IMetaObject* obj, const wxTreeItemId& item);
	void AddDocumentItem(IMetaObject* obj, const wxTreeItemId& item);
	void AddEnumerationItem(IMetaObject* obj, const wxTreeItemId& item);
	void AddDataProcessorItem(IMetaObject* obj, const wxTreeItemId& item);
	void AddReportItem(IMetaObject* obj, const wxTreeItemId& item);
	void AddInformationRegisterItem(IMetaObject* obj, const wxTreeItemId& item);
	void AddAccumulationRegisterItem(IMetaObject* obj, const wxTreeItemId& item);

	void FillData();

	IMetaObject* GetMetaObject(const wxTreeItemId& item) const {

		if (!item.IsOk())
			return NULL;
		treeMetaData_t* data =
			dynamic_cast<treeMetaData_t*>(m_metaTreeWnd->GetItemData(item));
		if (data == NULL)
			return NULL;
		return data->m_metaObject;
	}

	void UpdateToolbar(IMetaObject* obj, const wxTreeItemId& item);

public:

	bool RenameMetaObject(IMetaObject* metaObject, const wxString& newName);

	virtual IMetadata* GetMetadata() const {
		return m_metaData;
	}

	CMetadataTree();
	CMetadataTree(wxWindow* parent, int id = wxID_ANY);
	CMetadataTree(CDocument* docParent, wxWindow* parent, int id = wxID_ANY);
	virtual ~CMetadataTree();

	static CMetadataTree* GetMetadataTree();

	void InitTree();

	bool Load(IConfigMetadata* metaData = NULL);
	bool Save();

	void ClearTree();
};

#endif 
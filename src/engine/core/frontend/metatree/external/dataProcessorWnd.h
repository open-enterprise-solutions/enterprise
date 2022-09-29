#ifndef _DATAPROCESSOR_WND_H__
#define _DATAPROCESSOR_WND_H__

#include <wx/aui/aui.h>
#include <wx/aui/auibar.h>
#include <wx/treectrl.h>
#include <wx/statbox.h>
#include <wx/statline.h>

#include "metadata/external/metadataDataProcessor.h"
#include "frontend/metatree/metatreeWnd.h"
 
class CDataProcessorTree : public IMetadataTree {
	wxDECLARE_DYNAMIC_CLASS(CDataProcessorTree);

private:

	wxTreeItemId m_treeDATAPROCESSORS;

	wxTreeItemId m_treeATTRIBUTES;
	wxTreeItemId m_treeTABLES;
	wxTreeItemId m_treeFORM;
	wxTreeItemId m_treeTEMPLATES;

private:

	bool m_initialize;

protected:

	void OnEditCaptionName(wxCommandEvent& event);
	void OnEditCaptionSynonym(wxCommandEvent& event);
	void OnEditCaptionComment(wxCommandEvent& event);

	void OnChoiceDefForm(wxCommandEvent& event);

	void OnButtonModuleClicked(wxCommandEvent& event);

protected:

	wxStaticText* m_nameCaption;
	wxStaticText* m_synonymCaption;
	wxStaticText* m_commentCaption;
	wxStaticText* m_defaultForm;
	wxTextCtrl* m_nameValue;
	wxTextCtrl* m_synonymValue;
	wxTextCtrl* m_commentValue;
	wxChoice* m_defaultFormValue;

	wxButton* m_buttonModule;

	class CDataProcessorTreeWnd : public wxTreeCtrl {
		wxDECLARE_DYNAMIC_CLASS(CMetadataTree);
	private:

		CDataProcessorTree* m_ownerTree;

	public:

		CDataProcessorTreeWnd();
		CDataProcessorTreeWnd(wxWindow* parentWnd, CDataProcessorTree* ownerWnd);
		virtual ~CDataProcessorTreeWnd();

		// this function is called to compare 2 items and should return -1, 0
		// or +1 if the first item is less than, equal to or greater than the
		// second one. The base class version performs alphabetic comparison
		// of item labels (GetText)
		virtual int OnCompareItems(const wxTreeItemId& item1,
			const wxTreeItemId& item2) {
			int ret = wxStrcmp(GetItemText(item1), GetItemText(item2));
			ITreeMetaData* data1 = dynamic_cast<ITreeMetaData*>(GetItemData(item1));
			ITreeMetaData* data2 = dynamic_cast<ITreeMetaData*>(GetItemData(item2));
			if (data1 != NULL && data2 != NULL && ret > 0) {
				IMetaObject* metaObject1 = data1->GetMetaObject();
				IMetaObject* metaObject2 = data2->GetMetaObject();
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

		void OnDebugEvent(wxDebugEvent& event);
		void OnPropertyModified(wxFramePropertyEvent& event);

		void OnSelecting(wxTreeEvent& event);
		void OnSelected(wxTreeEvent& event);

		void OnCollapsing(wxTreeEvent& event);
		void OnExpanding(wxTreeEvent& event);

	protected:

		wxDECLARE_EVENT_TABLE();
	};

	CDataProcessorTreeWnd* m_metaTreeWnd;
	CMetadataDataProcessor* m_metaData;

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

	IMetaObject* CreateItem(bool showValue = true);
	void EditItem();
	void RemoveItem();
	void EraseItem(const wxTreeItemId& item);
	void PropertyItem();

	void UpItem();
	void DownItem();

	void SortItem();

	void CommandItem(unsigned int id);
	void PrepareContextMenu(wxMenu* menu, const wxTreeItemId& item);

	void FillData();

	IMetaObject* GetMetaObject(const wxTreeItemId& item) {
		if (!item.IsOk())
			return NULL;
		ITreeMetaData* data =
			dynamic_cast<ITreeMetaData*>(m_metaTreeWnd->GetItemData(item));
		if (data == NULL)
			return NULL;
		return data->GetMetaObject();
	}

	void UpdateToolbar(IMetaObject* obj, const wxTreeItemId& item);

public:

	virtual void UpdateChoiceSelection();

public:

	bool RenameMetaObject(IMetaObject* obj, const wxString& sNewName);

public:

	virtual IMetadata* GetMetadata() const {
		return m_metaData;
	}

	CDataProcessorTree() { }
	CDataProcessorTree(CDocument* docParent, wxWindow* parent, wxWindowID id = wxID_ANY);
	virtual ~CDataProcessorTree();

	void InitTree();

	bool Load(CMetadataDataProcessor* metaData);
	bool Save();

	void ClearTree();

	wxDECLARE_EVENT_TABLE();
};

#endif 
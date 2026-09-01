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
			ibTreeDataObject* data1 = dynamic_cast<ibTreeDataObject*>(GetItemData(item1));
			ibTreeDataObject* data2 = dynamic_cast<ibTreeDataObject*>(GetItemData(item2));
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


	void ActivateItem(const wxTreeItemId& item);

	ibValueMetaObject* NewItem(const ibClassID& clsid, ibValueMetaObject* parent, bool runObject = true);
	ibValueMetaObject* CreateItem(bool showValue = true);


	wxTreeItemId FillItem(ibValueMetaObject* metaItem, const wxTreeItemId& item, bool select = true, bool scroll = true);
	void EditItem();
	void RemoveItem();
	void EraseItem(const wxTreeItemId& item);
	void SelectItem();
	void PropertyItem();

	// The row is the EVENT's — see the note on the bodies.
	void Collapse(const wxTreeItemId& item);
	void Expand(const wxTreeItemId& item);

	void UpItem();
	void DownItem();

	void SortItem();

	void PrepareContextMenu(wxMenu* menu, const wxTreeItemId& item);
	void ShowContextMenu(wxWindow* eventSrc, const wxTreeItemId& item, const wxPoint& pos);

	void FillData();


	void UpdateToolbar(ibValueMetaObject* obj, const wxTreeItemId& item);

	// Close every editor opened from this navigator. Part of LEAVING a file — deliberately not part
	// of ClearTree, which now runs on every change to the metadata.

protected:

	// Nothing forwards from here any more — see the note on ibMetaTreeBase.

public:

	virtual void UpdateChoiceSelection() override;

	// Added, announced, handled — see the implementation.

public:

	bool RenameMetaObject(ibValueMetaObject* obj, const wxString& sNewName);

public:

	// ITS OWN TYPE, not the base's — the override is covariant, so a caller that needs the data
	// processor's own verbs gets them without a cast, and the base gets what it asks for.
	virtual ibMetaDataDataProcessor* GetMetaData() const { return m_metaData; }

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
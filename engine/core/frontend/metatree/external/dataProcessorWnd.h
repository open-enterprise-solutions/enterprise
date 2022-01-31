#ifndef _DATAPROCESSOR_WND_H__
#define _DATAPROCESSOR_WND_H__

#include <wx/aui/aui.h>
#include <wx/aui/auibar.h>
#include <wx/treectrl.h>
#include <wx/statbox.h>
#include <wx/statline.h>

#include "common/docinfo.h"
#include "metadata/external/metadataDataProcessor.h"
#include "compiler/debugger/debugEvent.h"
#include "frontend/objinspect/events.h"

class CDataProcessorTree : public wxPanel,
	public IMetadataTree {
	wxDECLARE_DYNAMIC_CLASS(CDataProcessorTree);

private:

	wxTreeItemId m_treeDATAPROCESSORS;

	wxTreeItemId m_treeATTRIBUTES;
	wxTreeItemId m_treeTABLES;
	wxTreeItemId m_treeFORM;
	wxTreeItemId m_treeTEMPLATES;

private:

	bool m_initialize; 
	bool m_bReadOnly;

	enum
	{
		ID_METATREE_NEW = 15000,
		ID_METATREE_EDIT,
		ID_METATREE_REMOVE,
		ID_METATREE_PROPERTY,
	};

	struct CObjectData
	{
		CLASS_ID m_clsid; //тип элемента
		int  m_nChildImage;//картинка для подч. элементов группы
	};

	//сами объекты
	std::map<wxTreeItemId, IMetaObject *> m_aMetaObj;
	//типы объекта
	std::map <wxTreeItemId, CObjectData> m_aMetaClassObj;
	//список открытых элементов 
	std::vector <CDocument *> m_aMetaOpenedForms;

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

	wxAuiToolBar* m_metaTreeToolbar;

	wxButton *m_buttonModule;

	class CDataProcessorTreeWnd : public wxTreeCtrl
	{
		wxDECLARE_DYNAMIC_CLASS(CMetadataTree);

	private:

		CDataProcessorTree *m_ownerTree;

	public:

		CDataProcessorTreeWnd();
		CDataProcessorTreeWnd(wxWindow *parentWnd, CDataProcessorTree *ownerWnd);
		virtual ~CDataProcessorTreeWnd();

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

		void OnCreateItem(wxCommandEvent &event);
		void OnEditItem(wxCommandEvent &event);
		void OnRemoveItem(wxCommandEvent &event);
		void OnPropertyItem(wxCommandEvent &event);

		void OnCommandItem(wxCommandEvent &event);

		void OnCopyItem(wxCommandEvent &event);
		void OnPasteItem(wxCommandEvent &event);

		void OnDebugEvent(wxDebugEvent &event);
		void OnPropertyModified(wxFramePropertyEvent &event);

		void OnSelecting(wxTreeEvent &event);
		void OnSelected(wxTreeEvent &event);

		void OnCollapsing(wxTreeEvent &event);
		void OnExpanding(wxTreeEvent &event);

	protected:

		wxDECLARE_EVENT_TABLE();
	};

	CDataProcessorTreeWnd* m_metaTreeWnd;

	CMetadataDataProcessor *m_metaData;
	CDocument *m_docParent;

private:

	void ActivateItem(const wxTreeItemId &item);

	void CreateItem();
	void EditItem();
	void RemoveItem();
	void EraseItem(const wxTreeItemId &item);
	void PropertyItem();

	void CommandItem(unsigned int id);
	void PrepareContextMenu(wxMenu *menu, const wxTreeItemId &item);

	void EditModule(const wxString &fullName, int lineNumber, bool setRunLine = true);
	void FillData();

	IMetaObject *GetMetaObject(const wxTreeItemId &item)
	{
		if (!item.IsOk())
			return NULL;

		auto foundedIt = m_aMetaObj.find(item);
		if (foundedIt == m_aMetaObj.end())
			return NULL;

		return foundedIt->second;
	}

	void UpdateToolbar(IMetaObject *obj, const wxTreeItemId &item);
	void UpdateChoiceSelection();

public:

	virtual void SetReadOnly(bool readOnly = true) { m_bReadOnly = readOnly; }

	virtual void Modify(bool modify) { if (m_docParent) m_docParent->Modify(modify); };

	virtual bool OpenFormMDI(IMetaObject *obj);
	virtual bool OpenFormMDI(IMetaObject *obj, CDocument *&foundedDoc);

	virtual bool CloseFormMDI(IMetaObject *obj);

	virtual void OnPropertyChanged() { UpdateChoiceSelection(); }

	virtual void OnCloseDocument(CDocument *doc);

	virtual CDocument *GetDocument(IMetaObject *obj);

	virtual void CloseMetaObject(IMetaObject *obj) {
		CDocument *doc = GetDocument(obj);
		if (doc) {
			doc->DeleteAllViews();
		}
	}

	bool RenameMetaObject(IMetaObject *obj, const wxString &sNewName);

public:

	CDataProcessorTree() { }
	CDataProcessorTree(CDocument *docParent, wxWindow *parent, wxWindowID id = wxID_ANY);
	virtual ~CDataProcessorTree();

	void InitTree();

	bool Load(CMetadataDataProcessor *metaData);
	bool Save();

	void ClearTree();

	wxDECLARE_EVENT_TABLE();
};

#endif 
#ifndef _METATREE_H__
#define _METATREE_H__

#include "common/docinfo.h"
#include "metadata/metadata.h"
#include "compiler/debugger/debugEvent.h"
#include "frontend/objinspect/events.h"

#include <wx/aui/aui.h>
#include <wx/treectrl.h>

class CMetadataTree : public wxPanel,
	public IMetadataTree
{
	wxDECLARE_DYNAMIC_CLASS(CMetadataTree);

private:

	wxTreeItemId m_treeMETADATA;
	wxTreeItemId m_treeCOMMON; //special tree

	wxTreeItemId m_treeMODULES;
	wxTreeItemId m_treeFORMS;
	wxTreeItemId m_treeTEMPLATES;

	wxTreeItemId m_treeCONSTANTS;

	wxTreeItemId m_treeCATALOGS;
	wxTreeItemId m_treeDOCUMENTS;
	wxTreeItemId m_treeENUMERATIONS;
	wxTreeItemId m_treeDATAPROCESSORS;
	wxTreeItemId m_treeREPORTS;

private:

	bool m_bReadOnly;

	enum
	{
		ID_METATREE_NEW = 15000,
		ID_METATREE_EDIT,
		ID_METATREE_REMOVE,
		ID_METATREE_PROPERTY,

		ID_METATREE_INSERT,
		ID_METATREE_REPLACE,
		ID_METATREE_SAVE,
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

private:

	IConfigMetadata *m_metaData;

	class CMetadataTreeWnd : public wxTreeCtrl
	{
		wxDECLARE_DYNAMIC_CLASS(CMetadataTree);

	private:

		CMetadataTree *m_ownerTree;

	public:

		CMetadataTreeWnd();
		CMetadataTreeWnd(CMetadataTree *parent);
		virtual ~CMetadataTreeWnd();

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

		void OnInsertItem(wxCommandEvent &event);
		void OnReplaceItem(wxCommandEvent &event);
		void OnSaveItem(wxCommandEvent &event);

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

	wxAuiToolBar *m_metaTreeToolbar;
	CMetadataTreeWnd *m_metaTreeWnd;

	CDocument *m_docParent;

private:

	void ActivateItem(const wxTreeItemId &item);

	void CreateItem();
	void EditItem();
	void RemoveItem();
	void EraseItem(const wxTreeItemId &item);
	void PropertyItem();

	void InsertItem();
	void ReplaceItem();
	void SaveItem();

	void CommandItem(unsigned int id);
	void PrepareReplaceMenu(wxMenu *menu);
	void PrepareContextMenu(wxMenu *menu, const wxTreeItemId &item);

	void AddCatalogItem(IMetaObject *obj, const wxTreeItemId &item);
	void AddDocumentItem(IMetaObject *obj, const wxTreeItemId &item);
	void AddEnumerationItem(IMetaObject *obj, const wxTreeItemId &item);
	void AddDataProcessorItem(IMetaObject *obj, const wxTreeItemId &item);
	void AddReportItem(IMetaObject *obj, const wxTreeItemId &item);

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

public:

	virtual void SetReadOnly(bool readOnly = true) { m_bReadOnly = readOnly; }
	virtual void Modify(bool modify); 

	virtual bool OpenFormMDI(IMetaObject *obj);
	virtual bool OpenFormMDI(IMetaObject *obj, CDocument *&foundedDoc);

	virtual bool CloseFormMDI(IMetaObject *obj);

	virtual void OnPropertyChanged() {}

	virtual void OnCloseDocument(CDocument *doc);

	virtual CDocument *GetDocument(IMetaObject *obj);

	virtual void CloseMetaObject(IMetaObject *obj) {
		CDocument *doc = GetDocument(obj);
		if (doc) {
			doc->DeleteAllViews();
		}
	}

	bool RenameMetaObject(IMetaObject *obj, const wxString &sNewName);

	CMetadataTree();
	CMetadataTree(wxWindow *parent, int id = wxID_ANY);
	CMetadataTree(CDocument *docParent, wxWindow *parent, int id = wxID_ANY);

	virtual ~CMetadataTree();

	void InitTree();

	bool Load(CConfigFileMetadata *metaData = NULL);
	bool Save();

	void ClearTree();

	wxDECLARE_EVENT_TABLE();
};

#endif 
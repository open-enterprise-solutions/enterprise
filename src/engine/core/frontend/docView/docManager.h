#ifndef _REPORTMANAGER_H__
#define _REPORTMANAGER_H__

#include "docView.h"
#include "compiler/compiler.h"

#define docManager CDocManager::GetDocumentManager()
#define docManagerDestroy() CDocManager::Destroy()

#include <wx/fdrepdlg.h>

// Document template flags
enum
{
	wxTEMPLATE_ONLY_OPEN = wxTEMPLATE_INVISIBLE + 1
};

class CDocManager : public wxDocManager
{
	class CDocTemplate : public wxDocTemplate {
	public:
		// Associate document and view types. They're for identifying what view is
		// associated with what template/document type
		CDocTemplate(wxDocManager* manager,
			const wxString& descr,
			const wxString& filter,
			const wxString& dir,
			const wxString& ext,
			const wxString& docTypeName,
			const wxString& viewTypeName,
			wxClassInfo* docClassInfo = NULL,
			wxClassInfo* viewClassInfo = NULL,
			long flags = wxDEFAULT_TEMPLATE_FLAGS) : wxDocTemplate(manager,
				descr,
				filter,
				dir,
				ext,
				docTypeName,
				viewTypeName,
				docClassInfo,
				viewClassInfo,
				flags)
		{
		}

		// Helper method for CreateDocument; also allows you to do your own document
		// creation
		virtual bool InitDocument(wxDocument* doc,
			const wxString& path,
			long flags = 0);
	};

	struct docElement_t {
		CLASS_ID m_clsid;
		wxString m_className;
		wxString m_classDescription;
		wxDocTemplate* m_docTemplate;
	};

	std::vector <docElement_t> m_aTemplates;

	wxFindReplaceData m_findData;
	wxFindReplaceDialog* m_findDialog;

private:

	CDocument* OpenForm(IMetaObject* metaObject, CDocument* docParent, long flags);

	// Handlers for common user commands
	void OnFileClose(wxCommandEvent& event);
	void OnFileCloseAll(wxCommandEvent& event);
	void OnFileNew(wxCommandEvent& event);
	void OnFileOpen(wxCommandEvent& event);
	void OnFileRevert(wxCommandEvent& event);
	void OnFileSave(wxCommandEvent& event);
	void OnFileSaveAs(wxCommandEvent& event);
	void OnMRUFile(wxCommandEvent& event);
#if wxUSE_PRINTING_ARCHITECTURE
	void OnPrint(wxCommandEvent& event);
	void OnPreview(wxCommandEvent& event);
	void OnPageSetup(wxCommandEvent& event);
#endif // wxUSE_PRINTING_ARCHITECTURE
	void OnUndo(wxCommandEvent& event);
	void OnRedo(wxCommandEvent& event);

	// Handlers for UI update commands
	void OnUpdateFileOpen(wxUpdateUIEvent& event);
	void OnUpdateDisableIfNoDoc(wxUpdateUIEvent& event);
	void OnUpdateFileRevert(wxUpdateUIEvent& event);
	void OnUpdateFileNew(wxUpdateUIEvent& event);
	void OnUpdateFileSave(wxUpdateUIEvent& event);
	void OnUpdateFileSaveAs(wxUpdateUIEvent& event);
	void OnUpdateUndo(wxUpdateUIEvent& event);
	void OnUpdateRedo(wxUpdateUIEvent& event);

	void OnUpdateSaveMetadata(wxUpdateUIEvent& event);

	//find dialog
	void OnFindDialog(wxCommandEvent& event);
	void OnFind(wxFindDialogEvent& event);
	void OnFindClose(wxFindDialogEvent& event);

public:

	static CDocument* OpenFormMDI(IMetaObject* metaObject, long flags = wxDOC_NEW);
	static CDocument* OpenFormMDI(IMetaObject* metaObject, CDocument* docParent, long flags = wxDOC_NEW);

	// Get the current document manager
	static CDocManager* GetDocumentManager() {
		return dynamic_cast<CDocManager*>(sm_docManager);
	}

	//destroy manager 
	static void Destroy();

	CDocManager();
	virtual ~CDocManager();

	void AddDocTemplate(const wxString& descr,
		const wxString& filter,
		const wxString& dir,
		const wxString& ext,
		const wxString& docTypeName,
		const wxString& viewTypeName,
		wxClassInfo* docClassInfo,
		wxClassInfo* viewClassInfo,
		long flags);

	void AddDocTemplate(const CLASS_ID& id, wxClassInfo* docClassInfo, wxClassInfo* viewClassInfo);

	CDocument* GetCurrentDocument() const;

	virtual wxDocument* CreateDocument(const wxString& pathOrig, long flags) override;

	virtual wxDocTemplate* SelectDocumentPath(wxDocTemplate** templates,
		int noTemplates, wxString& path, long flags, bool save = false) override;

	virtual wxDocTemplate* SelectDocumentType(wxDocTemplate** templates,
		int noTemplates, bool sort = false) override;

	bool CloseDocument(wxDocument* doc, bool force = false);
	bool CloseDocuments(bool force);
	bool Clear(bool force);

protected:

	wxDECLARE_DYNAMIC_CLASS(CDocManager);
	wxDECLARE_NO_COPY_CLASS(CDocManager);

	wxDECLARE_EVENT_TABLE();
};

#endif

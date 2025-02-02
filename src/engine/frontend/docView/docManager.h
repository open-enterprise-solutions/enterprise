#ifndef _REPORT_MANAGER_H__
#define _REPORT_MANAGER_H__

#include "docView.h"

#define docManager CMetaDocManager::GetDocumentManager()

#include <wx/fdrepdlg.h>

// Document template flags
enum
{
	wxTEMPLATE_ONLY_OPEN = wxTEMPLATE_INVISIBLE + 1
};

class FRONTEND_API CMetaDocManager : public wxDocManager {

	class CMetaDocTemplate : public wxDocTemplate {
	public:
		// Associate document and view types. They're for identifying what view is
		// associated with what template/document type
		CMetaDocTemplate(wxDocManager* manager,
			const wxString& descr,
			const wxString& filter,
			const wxString& dir,
			const wxString& ext,
			const wxString& docTypeName,
			const wxString& viewTypeName,
			wxClassInfo* docClassInfo = nullptr,
			wxClassInfo* viewClassInfo = nullptr,
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
		class_identifier_t m_clsid;
		wxString m_className;
		wxString m_classDescr;
		CMetaDocTemplate* m_docTemplate;
	public:
		
		docElement_t() : m_clsid(0), m_docTemplate(nullptr)
		{
		}
	
		docElement_t(const docElement_t& src) :
			m_clsid(src.m_clsid),
			m_className(src.m_className),
			m_classDescr(src.m_classDescr),
			m_docTemplate(src.m_docTemplate)
		{
		}
	};

	std::vector <docElement_t> m_metaTemplates;

	wxFindReplaceData m_findData;
	wxFindReplaceDialog* m_findDialog;

private:

	CMetaDocument* OpenForm(IMetaObject* metaObject, CMetaDocument* docParent, long flags);

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

	static CMetaDocument* OpenFormMDI(IMetaObject* metaObject, long flags = wxDOC_NEW);
	static CMetaDocument* OpenFormMDI(IMetaObject* metaObject, CMetaDocument* docParent, long flags = wxDOC_NEW);

	// Get the current document manager
	static CMetaDocManager* GetDocumentManager() {
		return dynamic_cast<CMetaDocManager*>(sm_docManager);
	}

	CMetaDocManager();
	virtual ~CMetaDocManager();

	void AddDocTemplate(const wxString& descr,
		const wxString& filter,
		const wxString& dir,
		const wxString& ext,
		const wxString& docTypeName,
		const wxString& viewTypeName,
		wxClassInfo* docClassInfo,
		wxClassInfo* viewClassInfo,
		long flags);

	void AddDocTemplate(const class_identifier_t& id, wxClassInfo* docClassInfo, wxClassInfo* viewClassInfo);

	CMetaDocument* GetCurrentDocument() const;

	virtual wxDocument* CreateDocument(const wxString& pathOrig, long flags) override;

	virtual wxDocTemplate* SelectDocumentPath(wxDocTemplate** templates,
		int noTemplates, wxString& path, long flags, bool save = false) override;

	virtual wxDocTemplate* SelectDocumentType(wxDocTemplate** templates,
		int noTemplates, bool sort = false) override;

	bool CloseDocument(wxDocument* doc, bool force = false);
	bool CloseDocuments(bool force);
	bool Clear(bool force);

protected:

	wxDECLARE_DYNAMIC_CLASS(CMetaDocManager);
	wxDECLARE_NO_COPY_CLASS(CMetaDocManager);

	wxDECLARE_EVENT_TABLE();
};

#endif

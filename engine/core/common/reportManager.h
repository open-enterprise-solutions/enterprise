#ifndef _REPORTMANAGER_H__
#define _REPORTMANAGER_H__

#include "docInfo.h"

#define reportManager CReportManager::GetDocumentManager()
#define reportManagerDestroy() CReportManager::Destroy()

#include "compiler/compiler.h"

#include <wx/fdrepdlg.h>

// Document template flags
enum
{
	wxTEMPLATE_ONLY_OPEN = wxTEMPLATE_INVISIBLE + 1
};

class CReportManager : public wxDocManager
{
	struct CReportElement
	{
		CLASS_ID m_clsid;

		wxString m_className;
		wxString m_classDescription;

		int		m_nImage;

		wxDocTemplate *m_docTemplate;

		CReportElement() : m_nImage(0) {};
	};

	std::vector <CReportElement> m_aTemplates;

	wxFindReplaceData m_findData;
	wxFindReplaceDialog* m_findDialog;

private:

	CDocument* OpenForm(IMetaObject *metaObject, CDocument *docParent, long flags);

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

	static CDocument* OpenFormMDI(IMetaObject *metaObject, long flags = wxDOC_NEW);
	static CDocument* OpenFormMDI(IMetaObject *metaObject, CDocument *docParent, long flags = wxDOC_NEW);
	
	// Get the current document manager
	static CReportManager* GetDocumentManager() { return dynamic_cast<CReportManager *>(sm_docManager); }

	//destroy manager 
	static void Destroy();

	CReportManager();
	virtual ~CReportManager();

	void AddDocTemplate(const wxString& descr, const wxString& filter, const wxString& dir, const wxString& ext, const wxString& docTypeName, const wxString& viewTypeName, wxClassInfo *docClassInfo, wxClassInfo *viewClassInfo, long flags, const wxString& sName, const wxString& sDescription, int nImage);
	void AddDocTemplate(const CLASS_ID &id, wxClassInfo *docClassInfo, wxClassInfo *viewClassInfo, int image);

	CDocument *GetCurrentDocument() const;
	CCommandProcessor *GetCurrentCommandProcessor() const;

	virtual wxDocument *CreateDocument(const wxString& pathOrig, long flags) override;

	virtual wxDocTemplate *SelectDocumentPath(wxDocTemplate **templates,
		int noTemplates, wxString& path, long flags, bool save = false) override;

	virtual wxDocTemplate *SelectDocumentType(wxDocTemplate **templates,
		int noTemplates, bool sort = false) override;

	bool CloseDocument(wxDocument* doc, bool force = false);
	bool CloseDocuments(bool force);
	bool Clear(bool force);

protected:

	wxDECLARE_DYNAMIC_CLASS(CReportManager);
	wxDECLARE_NO_COPY_CLASS(CReportManager);

	wxDECLARE_EVENT_TABLE();
};

#endif

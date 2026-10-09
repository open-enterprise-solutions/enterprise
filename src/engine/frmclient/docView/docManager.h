#ifndef _DOC_MANAGER_H__
#define _DOC_MANAGER_H__

/////////////////////////////////////////////////////////////////////////////
// Name:        frmclient/docView/docManager.h
// Purpose:     OES doc-manager extension types.
//
//   ibFrontendDocManager itself is declared in docView.h next to the wx-fork base
//   it inherits from. This header is the home of the template type —
//   ibFrontendDocTemplate. Includers that only need ibFrontendDocManager* and
//   the wx-fork base can stay on docView.h; pull in docManager.h when you
//   need the full template definition.
//
//   Implementation lives in docManager.cpp.
/////////////////////////////////////////////////////////////////////////////

#include "frmclient/docView/docView.h"

// ----------------------------------------------------------------------------
// ibFrontendDocTemplate — the wx-fork base template type. Implements the path/ext
// keyed file-template path used by ibFrontendDocManager::CreateDocument. Moved out
// of docView.h so the fork header
// stays focused on ibFrontendDocument / ibFrontendView / ibFrontendDocManager.
// ----------------------------------------------------------------------------

class FRMCLIENT_API ibFrontendDocTemplate: public wxObject
{

friend class FRMCLIENT_API ibFrontendDocManager;

public:
	ibFrontendDocTemplate(ibFrontendDocManager *manager,
	              const wxString& descr,
	              const wxString& filter,
	              const wxString& dir,
	              const wxString& ext,
	              const wxString& docTypeName,
	              const wxString& viewTypeName,
	              wxClassInfo *docClassInfo = nullptr,
	              wxClassInfo *viewClassInfo = nullptr,
	              long flags = ibDEFAULT_TEMPLATE_FLAGS);

	virtual ~ibFrontendDocTemplate();

	virtual ibFrontendDocument *CreateDocument(const wxString& path, long flags = 0);
	virtual ibFrontendView *CreateView(ibFrontendDocument *doc, long flags = 0);

	virtual bool InitDocument(ibFrontendDocument* doc,
	                          const wxString& path,
	                          long flags = 0);

	wxString GetDefaultExtension() const { return m_defaultExt; }
	wxString GetDescription() const { return m_description; }
	wxString GetDirectory() const { return m_directory; }
	ibFrontendDocManager *GetDocumentManager() const { return m_documentManager; }
	void SetDocumentManager(ibFrontendDocManager *manager)
		{ m_documentManager = manager; }
	wxString GetFileFilter() const { return m_fileFilter; }
	long GetFlags() const { return m_flags; }
	virtual wxString GetViewName() const { return m_viewTypeName; }
	virtual wxString GetDocumentName() const { return m_docTypeName; }

	void SetFileFilter(const wxString& filter) { m_fileFilter = filter; }
	void SetDirectory(const wxString& dir) { m_directory = dir; }
	void SetDescription(const wxString& descr) { m_description = descr; }
	void SetDefaultExtension(const wxString& ext) { m_defaultExt = ext; }
	void SetFlags(long flags) { m_flags = flags; }

	bool IsVisible() const { return (m_flags & ibTEMPLATE_VISIBLE) != 0; }

	wxClassInfo* GetDocClassInfo() const { return m_docClassInfo; }
	wxClassInfo* GetViewClassInfo() const { return m_viewClassInfo; }

	virtual bool FileMatchesTemplate(const wxString& path);

	// The icon shown in the "Choose template" dialog.
	const wxIcon&    GetClassIcon()    const { return m_classIcon; }
	void             SetClassIcon(const wxIcon& icon) { m_classIcon = icon; }

protected:
	long              m_flags;
	wxString          m_fileFilter;
	wxString          m_directory;
	wxString          m_description;
	wxString          m_defaultExt;
	wxString          m_docTypeName;
	wxString          m_viewTypeName;
	ibFrontendDocManager*     m_documentManager;

	wxClassInfo*      m_docClassInfo;
	wxClassInfo*      m_viewClassInfo;

	wxIcon            m_classIcon;

	virtual ibFrontendDocument *DoCreateDocument();
	virtual ibFrontendView *DoCreateView();

private:
	wxDECLARE_CLASS(ibFrontendDocTemplate);
	wxDECLARE_NO_COPY_CLASS(ibFrontendDocTemplate);
};

#endif // _DOC_MANAGER_H__

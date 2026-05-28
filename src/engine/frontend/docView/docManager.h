#ifndef _DOC_MANAGER_H__
#define _DOC_MANAGER_H__

/////////////////////////////////////////////////////////////////////////////
// Name:        frontend/docView/docManager.h
// Purpose:     OES doc-manager extension types.
//
//   ibDocManager itself is declared in docView.h next to the wx-fork base
//   it inherits from; the meta-template API (AddDocTemplate(ibClassID,…),
//   OpenForm, FindMetaTemplate, …) is part of that class declaration.
//
//   This header is the home of additional types that complement
//   ibDocManager but don't belong in the wx-fork header — currently the
//   ibMetaDocTemplate class. Includers that only need ibDocManager* and
//   the wx-fork base can stay on docView.h; pull in docManager.h when you
//   need the full ibMetaDocTemplate definition (icon / CLSID accessors).
//
//   Implementation lives in docManager.cpp (cross-build with desktop /
//   web-stub split via OES_USE_WEB).
/////////////////////////////////////////////////////////////////////////////

#include "frontend/docView/docView.h"

// ----------------------------------------------------------------------------
// ibDocTemplate — the wx-fork base template type. Implements the path/ext
// keyed file-template path used by ibDocManager::CreateDocument. Moved out
// of docView.h alongside its meta-template subclass so the fork header
// stays focused on ibDocument / ibView / ibDocManager.
// ----------------------------------------------------------------------------

class FRONTEND_API ibDocTemplate: public wxObject
{

friend class FRONTEND_API ibDocManager;

public:
	ibDocTemplate(ibDocManager *manager,
	              const wxString& descr,
	              const wxString& filter,
	              const wxString& dir,
	              const wxString& ext,
	              const wxString& docTypeName,
	              const wxString& viewTypeName,
	              wxClassInfo *docClassInfo = nullptr,
	              wxClassInfo *viewClassInfo = nullptr,
	              long flags = ibDEFAULT_TEMPLATE_FLAGS);

	virtual ~ibDocTemplate();

	virtual ibDocument *CreateDocument(const wxString& path, long flags = 0);
	virtual ibView *CreateView(ibDocument *doc, long flags = 0);

	virtual bool InitDocument(ibDocument* doc,
	                          const wxString& path,
	                          long flags = 0);

	wxString GetDefaultExtension() const { return m_defaultExt; }
	wxString GetDescription() const { return m_description; }
	wxString GetDirectory() const { return m_directory; }
	ibDocManager *GetDocumentManager() const { return m_documentManager; }
	void SetDocumentManager(ibDocManager *manager)
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

protected:
	long              m_flags;
	wxString          m_fileFilter;
	wxString          m_directory;
	wxString          m_description;
	wxString          m_defaultExt;
	wxString          m_docTypeName;
	wxString          m_viewTypeName;
	ibDocManager*     m_documentManager;

	wxClassInfo*      m_docClassInfo;
	wxClassInfo*      m_viewClassInfo;

	virtual ibDocument *DoCreateDocument();
	virtual ibView *DoCreateView();

private:
	wxDECLARE_CLASS(ibDocTemplate);
	wxDECLARE_NO_COPY_CLASS(ibDocTemplate);
};

// ----------------------------------------------------------------------------
// ibMetaDocTemplate — metadata-aware template subclass.
//
// Holds the OES-side keying that the wx-style file template doesn't have:
// the metaobject CLSID, a per-template GUID, and the icon shown in the
// "Choose template" dialog. Registered through the ibDocManager::AddDocTemplate
// overloads that take ibClassID / ibPictureID instead of a plain wxClassInfo.
// Lives in the same m_templates list as plain ibDocTemplate; lookups by CLSID
// iterate that list and dynamic_cast to ibMetaDocTemplate*.
// ----------------------------------------------------------------------------

class FRONTEND_API ibMetaDocTemplate : public ibDocTemplate
{
public:
	ibMetaDocTemplate(ibDocManager* manager,
	                  const wxString& descr,
	                  const wxString& filter,
	                  const wxString& dir,
	                  const wxString& ext,
	                  const wxString& docTypeName,
	                  const wxString& viewTypeName,
	                  wxClassInfo* docClassInfo = nullptr,
	                  wxClassInfo* viewClassInfo = nullptr,
	                  long flags = ibDEFAULT_TEMPLATE_FLAGS);

	virtual bool InitDocument(ibDocument* doc,
	                          const wxString& path,
	                          long flags = 0) override;

	const ibClassID& GetClassID()      const { return m_clsid; }
	void             SetClassID(const ibClassID& clsid) { m_clsid = clsid; }

	const ibGuid&    GetGuidTemplate() const { return m_guidTemplate; }

	const wxIcon&    GetClassIcon()    const { return m_classIcon; }
	void             SetClassIcon(const wxIcon& icon) { m_classIcon = icon; }

protected:
	ibClassID m_clsid;
	ibGuid    m_guidTemplate;
	wxIcon    m_classIcon;
};

#endif // _DOC_MANAGER_H__

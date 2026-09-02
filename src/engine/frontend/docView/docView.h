#ifndef __OBJ_INFO_H__
#define __OBJ_INFO_H__

/////////////////////////////////////////////////////////////////////////////
// Name:        frontend/docView/docView.h
// Purpose:     OES Doc/View — combined header.
//
//   Top half:   forked doc/view subsystem (ibDocument, ibView, ibDocTemplate,
//               ibDocManager, ibDocChildFrameAny<>, ibDocParentFrameAny<>,
//               ibDocPrintout). Copied from wx/docview.h with `wx*→ib*`
//               renames; window-type generalised via ibFrontendWindow so the
//               template instantiates with both wxWindow and ibWebWindow.
//               Originally:  Author: Julian Smart, (c) Julian Smart,
//                            Licence: wxWindows licence.
//
//   Bottom half: OES adapter (ibMetaDocument, ibMetaDataDocument,
//                ibValueModuleDocument, ibMetaView) — metadata-specific
//                subclasses living on top of the forked base.
//
// Was previously split as ibDocView.{h,cpp} + docView.{h,cpp}; collapsed
// into a single pair to keep both layers reviewable side-by-side.
/////////////////////////////////////////////////////////////////////////////

#include "wx/defs.h"

#if wxUSE_DOC_VIEW_ARCHITECTURE

#include "wx/string.h"
#include "wx/frame.h"
#include "wx/filehistory.h"
#include "wx/vector.h"
#include "wx/app.h"
#include "wx/cmdproc.h"
#include "wx/dlist.h"
#include "wx/msgdlg.h"

#if wxUSE_PRINTING_ARCHITECTURE
    #include "wx/print.h"
#endif

#if wxUSE_STD_IOSTREAM
  #include "wx/iosfwrap.h"
#else
  #include "wx/stream.h"
#endif

#include "wx/fdrepdlg.h"

#include <list>
#include <vector>
#include <map>

#include "frontend/frontend.h"          // FRONTEND_API
#include "frontend/frontendTypes.h"     // ibFrontendWindow (wxWindow on
                                        // desktop, ibWebWindow on web)
#include "backend/backend_form.h"       // ibBackendMetaDocument
#include "backend/metaCollection/metaObject.h"

// ----------------------------------------------------------------------------
// Forward declarations
// ----------------------------------------------------------------------------

class WXDLLIMPEXP_FWD_CORE wxWindow;
class WXDLLIMPEXP_FWD_CORE wxPrintInfo;
class WXDLLIMPEXP_FWD_CORE wxCommandProcessor;
class WXDLLIMPEXP_FWD_BASE wxConfigBase;

class FRONTEND_API ibDocument;
class FRONTEND_API ibView;
class FRONTEND_API ibDocTemplate;
class FRONTEND_API ibMetaDocTemplate;
class FRONTEND_API ibDocManager;
class FRONTEND_API ibMetaDocument;
class ibDocChildFrameAnyBase;
class WXDLLIMPEXP_FWD_AUI wxAuiToolBar;

class BACKEND_API ibValue;
class BACKEND_API ibValueMetaObject;
class BACKEND_API ibValueMetaObjectRecordData;
class BACKEND_API ibValueMetaObjectModule;
class BACKEND_API ibValueMetaObjectForm;
class BACKEND_API ibValueMetaObjectGrid;
class ibMetaView;

// ============================================================================
//                          PART 1 — FORKED DOC/VIEW
//                  (verbatim wx/docview.h, renamed wx*→ib*)
// ============================================================================

// Flags for ibDocManager (can be combined).
enum
{
    ibDOC_NEW    = 1,
    ibDOC_SILENT = 2
};

// Document template flags
enum
{
    ibTEMPLATE_VISIBLE      = 1,
    ibTEMPLATE_INVISIBLE    = 2,
    ibTEMPLATE_ONLY_OPEN    = 4,    // OES extension: template usable for Open, not for New
    ibTEMPLATE_SAVE_AS_FILE = 8,    // OES extension: enables File→Save As even for child documents
    ibDEFAULT_TEMPLATE_FLAGS = ibTEMPLATE_VISIBLE
};

// OES-side extension flag.
enum
{
    ibDOC_READONLY = ibDOC_SILENT + 1
};

#define ibMAX_FILE_HISTORY 9

typedef wxVector<ibDocument*> ibDocVector;
typedef wxVector<ibView*> ibViewVector;
typedef wxVector<ibDocTemplate*> ibDocTemplateVector;

class FRONTEND_API ibDocument : public wxEvtHandler
{
public:
    ibDocument(ibDocument *parent = nullptr);
    virtual ~ibDocument();

    // accessors
    void SetFilename(const wxString& filename, bool notifyViews = false);
    wxString GetFilename() const { return m_documentFile; }

    void SetTitle(const wxString& title) { m_documentTitle = title; }
    wxString GetTitle() const { return m_documentTitle; }

    // ⭐⭐ WHAT MAKES THIS DOCUMENT THE SAME DOCUMENT — asked by the manager before it opens
    // anything, so a second Open on something already on screen RAISES it instead of building a
    // rival copy of it (Max, 2026-09-01: *"the manager should open the form WITH A CHECK… and it
    // should all go through the manager"*).
    //
    // ⚠ THE GUID, NOT A NAME (Max's own correction, the same day). A name is unique enough to open
    // by and it MOVES: a rename between the open and the second open makes one document look like
    // two. The guid is what the object is, for as long as it exists. A path cannot serve either —
    // a metaobject inside a configuration has no file, which is why FindDocumentByPath never
    // answers about one.
    //
    // ⚠ A STRING, ALWAYS (Max, 2026-09-01: *"an identifier is always a string"*). A guid is what a
    // METAOBJECT happens to be identified by; a document opened from disk is identified by its
    // path, and a generated one by something else again. Text is the currency all of them can be
    // said in — and ibGuid converts to it on its own, so the caller writes GetGuid() and nothing
    // in between has to know that is what it was.
    //
    // Empty for a document with no identity of this kind: the manager reads that as "no answer"
    // rather than as a match, so two anonymous documents are never mistaken for each other.
    virtual wxString GetUniqueIdentifier() const { return wxEmptyString; }

    void SetDocumentName(const wxString& name) { m_documentTypeName = name; }
    wxString GetDocumentName() const { return m_documentTypeName; }

    bool GetDocumentSaved() const { return m_savedYet; }
    void SetDocumentSaved(bool saved = true) { m_savedYet = saved; }

    void Activate();

    bool AlreadySaved() const { return !IsModified() && GetDocumentSaved(); }

    virtual bool Close();
    virtual bool Save();
    virtual bool SaveAs();
    virtual bool Revert();

#if wxUSE_STD_IOSTREAM
    virtual std::ostream& SaveObject(std::ostream& stream);
    virtual std::istream& LoadObject(std::istream& stream);
#else
    virtual wxOutputStream& SaveObject(wxOutputStream& stream);
    virtual wxInputStream& LoadObject(wxInputStream& stream);
#endif

    virtual bool OnSaveDocument(const wxString& filename);
    virtual bool OnOpenDocument(const wxString& filename);
    virtual bool OnNewDocument();
    virtual bool OnCloseDocument();

    virtual bool OnSaveModified();
    virtual void OnSaveBeforeForceClose();
    virtual void OnChangeFilename(bool notifyViews);
    virtual bool OnCreate(const wxString& path, long flags);

    virtual wxCommandProcessor *OnCreateCommandProcessor();
    virtual wxCommandProcessor *GetCommandProcessor() const
        { return m_commandProcessor; }
    virtual void SetCommandProcessor(wxCommandProcessor *proc)
        { m_commandProcessor = proc; }

    virtual void OnChangedViewList();
    virtual bool DeleteContents();

    virtual bool Draw(wxDC&);
    virtual bool IsModified() const { return m_documentModified; }
    virtual void Modify(bool mod);

    virtual bool AddView(ibView *view);
    virtual bool RemoveView(ibView *view);

    ibViewVector GetViewsVector() const;

    wxList& GetViews() { return m_documentViews; }
    const wxList& GetViews() const { return m_documentViews; }

    ibView *GetFirstView() const;

    virtual void UpdateAllViews(ibView *sender = nullptr, wxObject *hint = nullptr);
    virtual void NotifyClosing();
    virtual bool DeleteAllViews();

    virtual ibDocManager *GetDocumentManager() const;
    virtual ibDocTemplate *GetDocumentTemplate() const
        { return m_documentTemplate; }
    virtual void SetDocumentTemplate(ibDocTemplate *temp)
        { m_documentTemplate = temp; }

    // ⭐ WHAT THIS DOCUMENT CAN BE SAVED AS — a wxFileSelector filter, or empty to
    // let the TEMPLATES answer (which is what every document did before this hook
    // and what most still do).
    //
    // It exists because a document can be written in more ways than it can be
    // opened: a spreadsheet reads our own file and an Excel workbook, and writes
    // those two plus Word — the formats are registered in the backend
    // (backend/sheetFormat/) and the document simply forwards their list.
    virtual wxString GetSaveFilter() const { return wxEmptyString; }

    virtual wxString GetUserReadableName() const;

    // Return type is ibFrontendWindow* — wxWindow on desktop, ibWebWindow on
    // web. Wx-modal callers (wxMessageBox / wxFileSelector / ...) need a
    // real wxWindow* parent and must therefore live under #ifndef OES_USE_WEB;
    // web-side modal flow routes through ibBackendDocFrame::ShowModalMessage.
    virtual ibFrontendWindow *GetDocumentWindow() const;

    virtual bool IsChildDocument() const { return m_documentParent != nullptr; }

    // A COMPOSITE document answers this for the children it lays out: the window the child's
    // view must render into. The default — null — means "take a tab of your own", so nothing
    // changes for an ordinary document.
    //
    // This is what lets ONE tab hold several documents: the home page hands each attached form
    // a cell of its splitter tree, and the children are its doc CHILDREN, so they live and die
    // with it. The child carries no "where am I" state — it asks its parent, which is the only
    // one that knows.
    virtual ibFrontendWindow* GetChildDocumentWindow(const ibDocument* child) const { return nullptr; }

    // The window my PARENT gives me because it composes me — null when I am on my own and
    // take a tab. THE question; everything about composition is asked through it.
    ibFrontendWindow* GetComposedWindow() const {
        return m_documentParent != nullptr ? m_documentParent->GetChildDocumentWindow(this) : nullptr;
    }

    // Am I laid out INSIDE my parent rather than in a tab? Asked by the close rules: a
    // composed view must not destroy a window it does not own, and a composed form has no
    // close of its own.
    bool IsEmbedded() const { return GetComposedWindow() != nullptr; }

    // A composed document does not close itself — its PARENT closes it. This is the parent
    // saying "it is me, let them go"; false the rest of the time, which is what makes every
    // other close path (a Close command, a forced close from the object, a manager sweep)
    // bounce off a child that lives in someone else's window.
    virtual bool IsClosingChildren() const { return false; }
    bool IsClosedByParent() const {
        return m_documentParent != nullptr && m_documentParent->IsClosingChildren();
    }

    bool CanClose();

    // OES-side adaptations lifted from ibMetaDocument (step-4 collapse).

    // Document icon — generic concept (wxDocument has none).
    virtual void SetIcon(const wxIcon& icon) { m_docIcon = icon; }
    virtual wxIcon GetIcon() const { return m_docIcon; }

    // Cascading-close opt-out. Returning false means "this child stays open
    // when its parent closes" — ibDocument::Close re-parents it onto the
    // document manager instead of closing it. Default true (close with parent).
    virtual bool IsCloseOnOwnerClose() const { return true; }

    // Runtime re-parenting (used by Close re-parent branch). Safe to manipulate
    // base's m_documentParent / m_childDocuments directly because we own them;
    // the historical wxDocument private-member kludge is gone with the fork.
    virtual void SetDocParent(ibDocument* docParent);

    // ⭐ AN OWNER ANSWERS ABOUT ITS OWN. The editors a navigator opens are OWNED by the document
    // holding that configuration and never join the manager's list, so "is it already open here?"
    // is a question for THIS document — the manager's own search answers about the manager's own
    // documents and cannot see these (Max, 2026-09-01: *"split the search by parent from the search
    // by name"* — they are two questions, and one function with an optional parent made them look
    // like one).
    ibDocument* FindChildDocument(const wxString& identifier) const;

protected:
    wxList                m_documentViews;
    wxString              m_documentFile;
    wxString              m_documentTitle;
    wxString              m_documentTypeName;
    ibDocTemplate*        m_documentTemplate;
    bool                  m_documentModified;
    ibDocument*           m_documentParent;
    wxCommandProcessor*   m_commandProcessor;
    bool                  m_savedYet;
    wxIcon                m_docIcon;

    virtual bool DoSaveDocument(const wxString& file);
    virtual bool DoOpenDocument(const wxString& file);

    // View factory for the OnCreate pipeline. Default pulls the view class from
    // the document template (the wx-style path used by every templated doc).
    // Template-less docs (e.g. ibFormVisualDocument, created directly without a
    // template) override this to construct their view explicitly.
    virtual ibView* DoCreateView();

    wxString DoGetUserReadableName() const;

    // Promoted from private (was wxDocument's private slot) to protected so
    // derived classes — ibMetaDocument in particular — can offer typed
    // accessors over the child-doc list without maintaining a shadow.
    std::list<ibDocument*> m_childDocuments;

private:
    wxDECLARE_ABSTRACT_CLASS(ibDocument);
    wxDECLARE_NO_COPY_CLASS(ibDocument);
};

class FRONTEND_API ibView: public wxEvtHandler
{
public:
    ibView();
    virtual ~ibView();

    ibDocument *GetDocument() const { return m_viewDocument; }
    virtual void SetDocument(ibDocument *doc);

    wxString GetViewName() const { return m_viewTypeName; }
    void SetViewName(const wxString& name) { m_viewTypeName = name; }

    ibFrontendWindow *GetFrame() const { return m_viewFrame ; }
    void SetFrame(ibFrontendWindow *frame) { m_viewFrame = frame; }

    virtual void OnActivateView(bool activate,
                                ibView *activeView,
                                ibView *deactiveView);
    virtual void OnDraw(wxDC *dc) = 0;
    virtual void OnPrint(wxDC *dc, wxObject *info);
    virtual void OnUpdate(ibView *sender, wxObject *hint = nullptr);
    virtual void OnClosingDocument() {}
    virtual void OnChangeFilename();

    virtual bool OnCreate(ibDocument *WXUNUSED(doc), long WXUNUSED(flags))
        { return true; }

    virtual bool Close(bool deleteWindow = true);
    virtual bool OnClose(bool deleteWindow);

    virtual void Activate(bool activate);

    // Per-view menu bar / doc-toolbar contributions, consulted by the main
    // frame's ActivateView. Defaults are empty: a plain view (e.g. the runtime
    // form view) contributes neither. Metadata editor views override these.
#if wxUSE_MENUS
    virtual wxMenuBar* CreateMenuBar() const { return nullptr; }
#endif // wxUSE_MENUS
    virtual void OnCreateToolbar(wxAuiToolBar* toolbar) {}

    ibDocManager *GetDocumentManager() const
        { return m_viewDocument->GetDocumentManager(); }

#if wxUSE_PRINTING_ARCHITECTURE
    virtual wxPrintout *OnCreatePrintout();
#endif

    void SetDocChildFrame(ibDocChildFrameAnyBase *docChildFrame);
    ibDocChildFrameAnyBase* GetDocChildFrame() const { return m_docChildFrame; }

    // OES-side adaptations lifted from ibMetaView (step-4 collapse).

    // Explicit "make this view's frame visible now" trigger. Desktop reveals
    // m_viewFrame (the wxAuiMDIChildFrame inside the AUI MDI parent). Web
    // routes through m_webFrame which is the ibWebDocChildFrame parked in the
    // session's tab list. Body out-of-line so the web branch can call
    // ibWebWindow::Show without pulling webWindow.h into every consumer.
    bool ShowFrame(bool show = true);

#ifdef OES_USE_WEB
    // Web-side twin of m_viewFrame — points at the ibWebDocChildFrame that
    // hosts this view. Set from ibWebFrame::CreateChildFrame right after the
    // tab is constructed, so ShowFrame (above) can reach it.
    void               SetWebFrame(class ibWebWindow* f) { m_webFrame = f; }
    class ibWebWindow* GetWebFrame() const { return m_webFrame; }
#endif

protected:
    virtual bool TryBefore(wxEvent& event) override;

    ibDocument*       m_viewDocument;
    wxString          m_viewTypeName;
    ibFrontendWindow* m_viewFrame;

    ibDocChildFrameAnyBase *m_docChildFrame;

#ifdef OES_USE_WEB
    class ibWebWindow* m_webFrame = nullptr;
#endif

private:
    wxDECLARE_ABSTRACT_CLASS(ibView);
    wxDECLARE_NO_COPY_CLASS(ibView);
};

// ibDocTemplate / ibMetaDocTemplate — full definitions in
// frontend/docView/docManager.h. Kept out of this header so the wx-fork base
// file holds only ibDocument / ibView / ibDocManager (the types that need to
// be visible to every doc-aware TU). ibDocManager methods below only hand
// back ibDocTemplate* / ibMetaDocTemplate*, so the forward-decls at the top
// of this file are enough at this scope; pull in docManager.h when you need
// the full template type.

class FRONTEND_API ibDocManager: public wxEvtHandler
{
public:
    // NB: flags are unused, don't pass ibDOC_XXX to this ctor
    ibDocManager(long flags = 0, bool initialize = true);
    virtual ~ibDocManager();

    virtual bool Initialize();

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
#endif
    void OnUndo(wxCommandEvent& event);
    void OnRedo(wxCommandEvent& event);

    void OnUpdateFileOpen(wxUpdateUIEvent& event);
    void OnUpdateDisableIfNoDoc(wxUpdateUIEvent& event);
    void OnUpdateFileRevert(wxUpdateUIEvent& event);
    void OnUpdateFileNew(wxUpdateUIEvent& event);
    void OnUpdateFileSave(wxUpdateUIEvent& event);
    void OnUpdateFileSaveAs(wxUpdateUIEvent& event);
    void OnUpdateUndo(wxUpdateUIEvent& event);
    void OnUpdateRedo(wxUpdateUIEvent& event);

    virtual void OnOpenFileFailure() { }

    virtual ibDocument *CreateDocument(const wxString& path, long flags = 0);

    ibDocument *CreateNewDocument()
        { return CreateDocument(wxString(), ibDOC_NEW); }

    virtual ibView *CreateView(ibDocument *doc, long flags = 0);
    virtual void DeleteTemplate(ibDocTemplate *temp, long flags = 0);
    virtual bool FlushDoc(ibDocument *doc);
    virtual ibDocTemplate *MatchTemplate(const wxString& path);
    virtual ibDocTemplate *SelectDocumentPath(ibDocTemplate **templates,
            int noTemplates, wxString& path, long flags, bool save = false);
    virtual ibDocTemplate *SelectDocumentType(ibDocTemplate **templates,
            int noTemplates, bool sort = false);
    virtual ibDocTemplate *SelectViewType(ibDocTemplate **templates,
            int noTemplates, bool sort = false);
    virtual ibDocTemplate *FindTemplateForPath(const wxString& path);

    void AssociateTemplate(ibDocTemplate *temp);
    void DisassociateTemplate(ibDocTemplate *temp);

    ibDocTemplate* FindTemplate(const wxClassInfo* documentClassInfo);

    ibDocument* FindDocumentByPath(const wxString& path) const;

    // ⭐ THE SAME QUESTION FOR THINGS THAT HAVE NO FILE. FindDocumentByPath answers for documents
    // opened FROM DISK; everything inside a configuration — a form, a module, an object editor —
    // has an identity and no path, and was therefore invisible to the only "is it already open"
    // check there was. See ibDocument::GetUniqueIdentifier.
    //
    // ⭐⭐ IS THIS OBJECT ALREADY ON SCREEN, and in which document — the ONE question the world
    // outside this class asks, and the only one of the four below that is public.
    //
    // ⚠ THE OBJECT IS THE WHOLE ARGUMENT. Its identity and its owner both follow from it, worked
    // out in one place, so they cannot disagree — and no caller has to carry "on whose behalf",
    // which is a thing every future call site would have to get right (Max, 2026-09-01, on seeing
    // five new methods here: *"I do not much like it"* — the other three went private).
    ibMetaDocument* FindOpenDocument(ibValueMetaObject* metaObject) const;

    ibDocument *GetCurrentDocument() const;

    void SetMaxDocsOpen(int n) { m_maxDocsOpen = n; }
    int GetMaxDocsOpen() const { return m_maxDocsOpen; }

    void AddDocument(ibDocument *doc);
    void RemoveDocument(ibDocument *doc);

    bool CloseDocuments(bool force = true);
    bool CloseDocument(ibDocument* doc, bool force = false);
    bool Clear(bool force = true);

    virtual void ActivateView(ibView *view, bool activate = true);
    virtual ibView *GetCurrentView() const { return m_currentView; }

    ibView *GetAnyUsableView() const;

    ibDocVector GetDocumentsVector() const;
    ibDocTemplateVector GetTemplatesVector() const;

    wxList& GetDocuments() { return m_docs; }
    wxList& GetTemplates() { return m_templates; }

    virtual wxString MakeNewDocumentName();
    virtual wxString MakeFrameTitle(ibDocument* doc);

    virtual wxFileHistory *OnCreateFileHistory();
    virtual wxFileHistory *GetFileHistory() const { return m_fileHistory; }

    virtual void AddFileToHistory(const wxString& file);
    virtual void RemoveFileFromHistory(size_t i);
    virtual size_t GetHistoryFilesCount() const;
    virtual wxString GetHistoryFile(size_t i) const;
    virtual void FileHistoryUseMenu(wxMenu *menu);
    virtual void FileHistoryRemoveMenu(wxMenu *menu);
#if wxUSE_CONFIG
    virtual void FileHistoryLoad(const wxConfigBase& config);
    virtual void FileHistorySave(wxConfigBase& config);
#endif

    virtual void FileHistoryAddFilesToMenu();
    virtual void FileHistoryAddFilesToMenu(wxMenu* menu);

    wxString GetLastDirectory() const;
    void SetLastDirectory(const wxString& dir) { m_lastDirectory = dir; }

    static ibDocManager* GetDocumentManager() { return sm_docManager; }

#if wxUSE_PRINTING_ARCHITECTURE
    wxPageSetupDialogData& GetPageSetupDialogData()
        { return m_pageSetupDialogData; }
    const wxPageSetupDialogData& GetPageSetupDialogData() const
        { return m_pageSetupDialogData; }
#endif

    // ------------------------------------------------------------------
    // OES meta-template API — see ibMetaDocTemplate above.
    //
    // AddDocTemplate(ibPictureID/ibClassID,...) overloads create an
    // ibMetaDocTemplate, populate its CLSID + icon, and associate it
    // with the same m_templates list as plain file templates. Lookups
    // by CLSID iterate m_templates and dynamic_cast.
    //
    // OpenForm / OpenObjectForm are the entry points for "open this
    // metaobject" — distinct from CreateDocument (file/path-based).
    // ------------------------------------------------------------------

    void AddDocTemplate(const ibPictureID& id,
                        const wxString& descr,
                        const wxString& filter,
                        const wxString& dir,
                        const wxString& ext,
                        const wxString& docTypeName,
                        const wxString& viewTypeName,
                        wxClassInfo* docClassInfo,
                        wxClassInfo* viewClassInfo,
                        long flags = ibTEMPLATE_VISIBLE);

    void AddDocTemplate(const ibPictureID& id,
                        const wxString& descr,
                        const wxString& filter,
                        const wxString& ext,
                        const wxString& docTypeName,
                        const wxString& viewTypeName,
                        wxClassInfo* docClassInfo,
                        wxClassInfo* viewClassInfo,
                        long flags = ibTEMPLATE_VISIBLE);

    void AddDocTemplate(const ibClassID& clsid,
                        const wxString& descr,
                        const wxString& filter,
                        const wxString& ext,
                        wxClassInfo* docClassInfo,
                        wxClassInfo* viewClassInfo);

    void AddDocTemplate(const ibClassID& clsid,
                        wxClassInfo* docClassInfo,
                        wxClassInfo* viewClassInfo);

    // ⭐⭐ A METAOBJECT OPENS BY ITS CLSID, THROUGH THE TEMPLATE REGISTERED FOR IT — the same
    // mechanism an external data processor opens by, and a module, and a form: AddDocTemplate above
    // says which document and view a clsid gets, and this makes one.
    //
    // 🛑 A SECOND NAME STOOD OVER IT — two static `OpenObjectForm` overloads that forwarded here and
    // did nothing else (Max, 2026-09-01: *"take OpenObjectForm out of the doc manager"*).
    // 🛑 AND A `docParent` ARGUMENT NOBODY COULD FILL RIGHT. Of the two callers in the tree, one
    // passed the navigator's own document and the other a plain nullptr — and the first is exactly
    // what FindMetaDataDocument works out from the object anyway. Asking every caller to carry "on
    // whose behalf" is a mechanism that has to be right in every future call site (Max,
    // 2026-09-01), for an answer the object already contains.
    ibMetaDocument* OpenForm(ibValueMetaObject* metaObject, long flags = ibDOC_NEW);

    ibMetaDocTemplate* FindMetaTemplate(const ibClassID& clsid) const;
    ibDocTemplate*     FindTemplateByDocClassInfo(const wxClassInfo* classInfo) const;

    // Helper: ibDocument* GetCurrentDocument() is the wx-style base accessor;
    // callers that need the metadata-aware downcast use this directly.
    ibMetaDocument*    GetCurrentMetaDocument() const;

    template <typename T, typename... Args>
    T* CreateDocument(Args&&... args) const
    {
        ibDocTemplate* docTemplate = FindTemplateByDocClassInfo(CLASSINFO(T));
        if (docTemplate != nullptr) {
            T* doc = new T(std::forward<Args>(args)...);
            doc->SetDocumentTemplate(docTemplate);
            return doc;
        }
        return nullptr;
    }

    // Find / Replace dialog — lifted from the former ibMetaDocManager.
    // Generic enough to live in the base; Bound to EVT_MENU(wxID_FIND).
    void OnFindDialog(wxCommandEvent& event);
    void OnFind(wxFindDialogEvent& event);
    void OnFindClose(wxFindDialogEvent& event);

    // Registers the four default templates shared between Designer and
    // Enterprise: Text / Spreadsheet / Help (meta-bound via CLSID) +
    // AuditLog (plain ibDocTemplate, no metadata). Called from the base
    // ibDocManager ctor; body is desktop-only because the editor view
    // TUs are not linked into wfrontend.dll.
    void RegisterDefaultTemplates();

protected:
    // ---- the halves FindOpenDocument(metaObject) is made of --------------------------------
    // Separate because they are separate questions — the manager answers about ITS OWN documents,
    // an owner answers about its children (ibDocument::FindChildDocument) — and private because
    // nothing outside has ever needed to ask either one on its own.

    // The manager's own list, by the document's declared identity. No cast: a document that has no
    // identity of this kind answers with an empty string and simply never matches.
    ibDocument* FindDocumentByIdentifier(const wxString& identifier) const;

    // Mine first, then the owner's editors.
    ibDocument* FindOpenDocument(const wxString& identifier, const ibDocument* docParent) const;

    // WHICH DOCUMENT IS SHOWING THIS CONFIGURATION — null for the one the main window holds, which
    // has no document at all. This is the owner an editor is opened under; getting it wrong puts
    // the editor in the manager's top level instead of under its navigator, and every later "is it
    // open?" then looks in the wrong place.
    class ibMetaDataDocument* FindMetaDataDocument(const class ibMetaData* metaData) const;

    virtual void OnMRUFileNotExist(unsigned n, const wxString& filename);
    void DoOpenMRUFile(unsigned n);
#if wxUSE_PRINTING_ARCHITECTURE
    virtual wxPreviewFrame* CreatePreviewFrame(wxPrintPreviewBase* preview,
                                               wxWindow *parent,
                                               const wxString& title);
#endif

    virtual bool TryBefore(wxEvent& event) override;

    wxCommandProcessor *GetCurrentCommandProcessor() const;

    int               m_defaultDocumentNameCounter;
    int               m_maxDocsOpen;
    wxList            m_docs;
    wxList            m_templates;
    ibView*           m_currentView;
    wxFileHistory*    m_fileHistory;
    wxString          m_lastDirectory;
    static ibDocManager* sm_docManager;

#if wxUSE_PRINTING_ARCHITECTURE
    wxPageSetupDialogData m_pageSetupDialogData;
#endif

    // Find / Replace dialog state (desktop only; mainFrame parent).
    wxFindReplaceData    m_findData;
    wxFindReplaceDialog* m_findDialog = nullptr;

    wxDECLARE_EVENT_TABLE();
    wxDECLARE_DYNAMIC_CLASS(ibDocManager);
    wxDECLARE_NO_COPY_CLASS(ibDocManager);
};

// Shortcut to the process-wide doc manager singleton (set in ibDocManager
// ctor, cleared in dtor). Previously hosted in frontend/docView/docManager.h
// when ibMetaDocManager was a separate subclass; collapsed into the base.
#define docManager ibDocManager::GetDocumentManager()

// ----------------------------------------------------------------------------
// Base class for child frames -- mix-in, doesn't derive from a window class.
// ----------------------------------------------------------------------------

class FRONTEND_API ibDocChildFrameAnyBase
{
public:
    ibDocChildFrameAnyBase()
    {
        m_childDocument = nullptr;
        m_childView = nullptr;
        m_win = nullptr;
        m_lastEvent = nullptr;
    }

    ibDocChildFrameAnyBase(ibDocument *doc, ibView *view, ibFrontendWindow *win)
    {
        Create(doc, view, win);
    }

    bool Create(ibDocument *doc, ibView *view, ibFrontendWindow *win)
    {
        m_childDocument = doc;
        m_childView = view;
        m_win = win;

        if ( view )
            view->SetDocChildFrame(this);

        return true;
    }

    ~ibDocChildFrameAnyBase()
    {
        if ( m_childView )
            m_childView->SetDocChildFrame(nullptr);
    }

    ibDocument *GetDocument() const { return m_childDocument; }
    ibView *GetView() const { return m_childView; }
    void SetDocument(ibDocument *doc) { m_childDocument = doc; }
    void SetView(ibView *view) { m_childView = view; }

    ibFrontendWindow *GetWindow() const { return m_win; }

    bool HasAlreadyProcessed(wxEvent& event) const
    {
        return m_lastEvent == &event;
    }

protected:
    bool TryProcessEvent(wxEvent& event);
    bool CloseView(wxCloseEvent& event);

    ibDocument*       m_childDocument;
    ibView*           m_childView;

    // Type-switched via ibFrontendWindow (wxWindow on desktop, ibWebWindow
    // on web) — see frontendTypes.h.
    ibFrontendWindow* m_win;

private:
    wxEvent* m_lastEvent;

    wxDECLARE_NO_COPY_CLASS(ibDocChildFrameAnyBase);
};

// ----------------------------------------------------------------------------
// Template implementing child frame concept using the given wxFrame-like class
// ----------------------------------------------------------------------------

template <class ChildFrame, class ParentFrame>
class ibDocChildFrameAny : public ChildFrame,
                           public ibDocChildFrameAnyBase
{
public:
    typedef ChildFrame BaseClass;

    ibDocChildFrameAny() = default;

    ibDocChildFrameAny(ibDocument *doc,
                       ibView *view,
                       ParentFrame *parent,
                       wxWindowID id,
                       const wxString& title,
                       const wxPoint& pos = wxDefaultPosition,
                       const wxSize& size = wxDefaultSize,
                       long style = wxDEFAULT_FRAME_STYLE,
                       const wxString& name = wxASCII_STR(wxFrameNameStr))
    {
        Create(doc, view, parent, id, title, pos, size, style, name);
    }

    bool Create(ibDocument *doc,
                ibView *view,
                ParentFrame *parent,
                wxWindowID id,
                const wxString& title,
                const wxPoint& pos = wxDefaultPosition,
                const wxSize& size = wxDefaultSize,
                long style = wxDEFAULT_FRAME_STYLE,
                const wxString& name = wxASCII_STR(wxFrameNameStr))
    {
        this->Bind(wxEVT_ACTIVATE, &ibDocChildFrameAny::OnActivate, this);
        this->Bind(wxEVT_CLOSE_WINDOW, &ibDocChildFrameAny::OnCloseWindow, this);

        if ( !ibDocChildFrameAnyBase::Create(doc, view, this) )
            return false;

        if ( !BaseClass::Create(parent, id, title, pos, size, style, name) )
            return false;

        return true;
    }

protected:
    virtual bool TryBefore(wxEvent& event) override
    {
        return TryProcessEvent(event) || BaseClass::TryBefore(event);
    }

private:
    void OnActivate(wxActivateEvent& event)
    {
        BaseClass::OnActivate(event);

        if ( m_childView )
            m_childView->Activate(event.GetActive());
    }

    void OnCloseWindow(wxCloseEvent& event)
    {
        if ( CloseView(event) )
            this->Destroy();
    }

    wxDECLARE_NO_COPY_TEMPLATE_CLASS_2(ibDocChildFrameAny,
                                        ChildFrame, ParentFrame);
};

// Default child frame: desktop-only convenience subclass (TBase = wxFrame).
// Web uses ibDocChildFrameAny<ibWebChildFrame, ibWebWindow> directly.

#ifndef OES_USE_WEB

typedef ibDocChildFrameAny<wxFrame, wxFrame> ibDocChildFrameBase;

class FRONTEND_API ibDocChildFrame : public ibDocChildFrameBase
{
public:
    ibDocChildFrame() {}

    ibDocChildFrame(ibDocument *doc,
                    ibView *view,
                    wxFrame *parent,
                    wxWindowID id,
                    const wxString& title,
                    const wxPoint& pos = wxDefaultPosition,
                    const wxSize& size = wxDefaultSize,
                    long style = wxDEFAULT_FRAME_STYLE,
                    const wxString& name = wxASCII_STR(wxFrameNameStr))
        : ibDocChildFrameBase(doc, view,
                              parent, id, title, pos, size, style, name)
    {
    }

    bool Create(ibDocument *doc,
                ibView *view,
                wxFrame *parent,
                wxWindowID id,
                const wxString& title,
                const wxPoint& pos = wxDefaultPosition,
                const wxSize& size = wxDefaultSize,
                long style = wxDEFAULT_FRAME_STYLE,
                const wxString& name = wxASCII_STR(wxFrameNameStr))
    {
        return ibDocChildFrameBase::Create
               (
                    doc, view,
                    parent, id, title, pos, size, style, name
               );
    }

private:
    wxDECLARE_CLASS(ibDocChildFrame);
    wxDECLARE_NO_COPY_CLASS(ibDocChildFrame);
};

#endif // !OES_USE_WEB

// ----------------------------------------------------------------------------
// ibDocParentFrame and related.
//
// `ibDocParentFrameAnyBase` mixin is available on BOTH builds — both
// ibFrontendMainFrame (desktop) and ibWebFrame (web) inherit it to get
// the `m_docManager` slot plus `TryProcessEvent` event-forwarding helper.
//
// `ibDocParentFrameAny<BaseFrame>` template + concrete `ibDocParentFrame`
// stay desktop-only — they assume `BaseFrame = wxFrame`.
// ----------------------------------------------------------------------------

class FRONTEND_API ibDocParentFrameAnyBase
{
public:
    ibDocParentFrameAnyBase(ibFrontendWindow* frame)
        : m_frame(frame)
    {
        m_docManager = nullptr;
    }

    ibDocManager *GetDocumentManager() const { return m_docManager; }

protected:
    bool TryProcessEvent(wxEvent& event);

    ibFrontendWindow* const m_frame;
    ibDocManager *m_docManager;

    wxDECLARE_NO_COPY_CLASS(ibDocParentFrameAnyBase);
};

#ifndef OES_USE_WEB

template <class BaseFrame>
class ibDocParentFrameAny : public BaseFrame,
                            public ibDocParentFrameAnyBase
{
public:
    ibDocParentFrameAny() : ibDocParentFrameAnyBase(this) { }
    ibDocParentFrameAny(ibDocManager *manager,
                        wxFrame *frame,
                        wxWindowID id,
                        const wxString& title,
                        const wxPoint& pos = wxDefaultPosition,
                        const wxSize& size = wxDefaultSize,
                        long style = wxDEFAULT_FRAME_STYLE,
                        const wxString& name = wxASCII_STR(wxFrameNameStr))
        : ibDocParentFrameAnyBase(this)
    {
        Create(manager, frame, id, title, pos, size, style, name);
    }

    bool Create(ibDocManager *manager,
                wxFrame *frame,
                wxWindowID id,
                const wxString& title,
                const wxPoint& pos = wxDefaultPosition,
                const wxSize& size = wxDefaultSize,
                long style = wxDEFAULT_FRAME_STYLE,
                const wxString& name = wxASCII_STR(wxFrameNameStr))
    {
        m_docManager = manager;

        if ( !BaseFrame::Create(frame, id, title, pos, size, style, name) )
            return false;

        this->Bind(wxEVT_MENU, &ibDocParentFrameAny::OnExit, this, wxID_EXIT);
        this->Bind(wxEVT_CLOSE_WINDOW, &ibDocParentFrameAny::OnCloseWindow, this);

        return true;
    }

protected:
    virtual bool TryBefore(wxEvent& event) override
    {
        return BaseFrame::TryBefore(event) || TryProcessEvent(event);
    }

private:
    void OnExit(wxCommandEvent& WXUNUSED(event))
    {
        this->Close();
    }

    void OnCloseWindow(wxCloseEvent& event)
    {
        if ( m_docManager && !m_docManager->Clear(!event.CanVeto()) )
            event.Veto();
        else
            event.Skip();
    }

    wxDECLARE_NO_COPY_CLASS(ibDocParentFrameAny);
};

typedef ibDocParentFrameAny<wxFrame> ibDocParentFrameBase;

class FRONTEND_API ibDocParentFrame : public ibDocParentFrameBase
{
public:
    ibDocParentFrame() : ibDocParentFrameBase() { }

    ibDocParentFrame(ibDocManager *manager,
                     wxFrame *parent,
                     wxWindowID id,
                     const wxString& title,
                     const wxPoint& pos = wxDefaultPosition,
                     const wxSize& size = wxDefaultSize,
                     long style = wxDEFAULT_FRAME_STYLE,
                     const wxString& name = wxASCII_STR(wxFrameNameStr))
        : ibDocParentFrameBase(manager,
                               parent, id, title, pos, size, style, name)
    {
    }

    bool Create(ibDocManager *manager,
                wxFrame *parent,
                wxWindowID id,
                const wxString& title,
                const wxPoint& pos = wxDefaultPosition,
                const wxSize& size = wxDefaultSize,
                long style = wxDEFAULT_FRAME_STYLE,
                const wxString& name = wxASCII_STR(wxFrameNameStr))
    {
        return ibDocParentFrameBase::Create(manager,
                                            parent, id, title,
                                            pos, size, style, name);
    }

private:
    wxDECLARE_CLASS(ibDocParentFrame);
    wxDECLARE_NO_COPY_CLASS(ibDocParentFrame);
};

#endif // !OES_USE_WEB

// ----------------------------------------------------------------------------
// Provide simple default printing facilities
// ----------------------------------------------------------------------------

#if wxUSE_PRINTING_ARCHITECTURE
class FRONTEND_API ibDocPrintout : public wxPrintout
{
public:
    ibDocPrintout(ibView *view = nullptr, const wxString& title = wxString());

    virtual bool OnPrintPage(int page) override;
    virtual bool HasPage(int page) override;
    virtual bool OnBeginDocument(int startPage, int endPage) override;
    virtual void GetPageInfo(int *minPage, int *maxPage,
                             int *selPageFrom, int *selPageTo) override;

    virtual ibView *GetView() { return m_printoutView; }

protected:
    ibView*       m_printoutView;

private:
    wxDECLARE_DYNAMIC_CLASS(ibDocPrintout);
    wxDECLARE_NO_COPY_CLASS(ibDocPrintout);
};
#endif // wxUSE_PRINTING_ARCHITECTURE

// File-to-stream helpers (preserved for back-compat with existing formats).

#if wxUSE_STD_IOSTREAM
bool FRONTEND_API
ibTransferFileToStream(const wxString& filename, std::ostream& stream);
bool FRONTEND_API
ibTransferStreamToFile(std::istream& stream, const wxString& filename);
#else
bool FRONTEND_API
ibTransferFileToStream(const wxString& filename, wxOutputStream& stream);
bool FRONTEND_API
ibTransferStreamToFile(wxInputStream& stream, const wxString& filename);
#endif // wxUSE_STD_IOSTREAM

inline ibViewVector ibDocument::GetViewsVector() const
{
    return m_documentViews.AsVector<ibView*>();
}

inline ibDocVector ibDocManager::GetDocumentsVector() const
{
    return m_docs.AsVector<ibDocument*>();
}

// ibDocManager::GetTemplatesVector() is out-of-line in docManager.cpp:
// wxObjectList::AsVector<T*> needs the complete ibDocTemplate type for the
// internal static_cast, and ibDocTemplate is forward-declared in this header.

// ============================================================================
//                       PART 2 — OES METADATA ADAPTER
//   (ibMetaDocument / ibMetaDataDocument / ibValueModuleDocument / ibMetaView)
// ============================================================================

// Step-4 collapse note: SetIcon/GetIcon, IsCloseOnOwnerClose, cascading
// UpdateAllViews + Close, DeleteAllViews-with-Activate, generic SetDocParent
// and the m_docIcon storage all moved up into ibDocument. The historical
// shadow fields `m_documentParent : ibMetaDocument*` and `m_childDocs :
// wxDList<ibMetaDocument>` are gone — base's m_documentParent /
// m_childDocuments are protected now (no longer the wxDocument private-slot
// kludge), so we use them directly with a typed wrapper for back-compat.

class FRONTEND_API ibMetaDocument : public ibBackendMetaDocument, public ibDocument {
	wxDECLARE_ABSTRACT_CLASS(ibMetaDocument);
public:

	virtual void SetMetaObject(ibValueMetaObject* metaObject) { m_metaObject = metaObject; }
	virtual ibValueMetaObject* GetMetaObject() const { return m_metaObject; }

	template <class T>
	inline T* ConvertMetaObjectToType() {
		return GetMetaObject()->ConvertToType<T>();
	}

	wxString GetModuleName() const;

	// The metaobject's guid IS this document's identity — see ibDocument::GetUniqueIdentifier.
	virtual wxString GetUniqueIdentifier() const override {
		// wxString(wxEmptyString), never the bare constant: outside MSVC wxEmptyString is a
		// `const wxChar *`, and a conditional whose arms are wxString and const wxChar * is
		// AMBIGUOUS — each converts to the other. The wrapper is what the rest of the tree writes
		// (portability.md 1.10, which already records this trap in eight other places).
		return m_metaObject != nullptr ? m_metaObject->GetGuid().str() : wxString(wxEmptyString);
	}

	ibMetaDocument(ibMetaDocument* docParent = nullptr);
	virtual ~ibMetaDocument() = default;

	// metadata docs don't track wxDocument-style "modified" — they delegate
	// to ibMetaData::Modify. Skip OnSaveModified-gated delete-on-empty.
	virtual void OnChangedViewList() override { if (m_documentViews.empty()) delete this; }

	// OnCreate pipeline (DoCreateView -> child-frame -> view->OnCreate) lives on
	// ibDocument now; the DoCreateView default there pulls the view class from the
	// document template, so meta docs need no override.

	virtual bool OnSaveModified() override;
	virtual bool OnSaveDocument(const wxString& filename) override;
	virtual bool OnCloseDocument() override;

	virtual bool IsModified() const override;
	virtual void Modify(bool mod) override;
	virtual bool Save() override;
	virtual bool SaveAs() override;

	// Treats every metadata doc as "child" regardless of m_documentParent.
	// Historical OES semantic — preserved separately from base's parent-based
	// IsChildDocument().
	virtual bool IsChildDocument() const override { return m_childDoc; }

	// Typed view over base's m_childDocuments. Back-compat wrapper for
	// pre-collapse callers that expected wxDList<ibMetaDocument>; new code
	// should prefer iterating base's m_childDocuments with a static_cast.
	wxDList<ibMetaDocument> GetChild() const {
		wxDList<ibMetaDocument> result;
		for (ibDocument* d : m_childDocuments)
			result.Append(static_cast<ibMetaDocument*>(d));
		return result;
	}

	// ⭐ …AND THE OTHER WAY: A CHILD SAYS WHO OWNS IT. The pair of GetChild above, and the one that
	// was missing — every caller that wanted a parent reached into the base's m_documentParent and
	// cast it by hand (Max, 2026-09-01: *"children must be able to return their parent themselves"*).
	ibMetaDocument* GetParent() const {
		return static_cast<ibMetaDocument*>(m_documentParent);
	}

	// ⭐⭐ WHICH TREE SHOWS THIS DOCUMENT — asked of the DOCUMENT, because the document is what has
	// both (Max, 2026-09-01, choosing between this and broadcasting through the metadata's watchers).
	// It sits HERE, at ibMetaDocument, and not one level down at ibMetaDataDocument: an editor — a
	// form, a module — is opened from a tree too, and it is an ibMetaDocument.
	//
	// 🛑 IT USED TO BE `metaData->GetMetaTree()`, on the engine: the backend handing a viewer back to
	// whoever asked, which is how ibMetaData came to know that a watcher has ROWS in it. And it
	// answered with the FIRST watcher, so with two windows open on one configuration it silently
	// picked one. A document has exactly one tree, and knows it.
	//
	// ⭐ AN EDITOR HAS NO TREE OF ITS OWN — IT ASKS THE DOCUMENT IT WAS OPENED FROM. A form or a
	// module document was created with the navigator's document as its parent, so the default walks
	// UP until someone answers; the designer's documents override and end the walk with their own
	// tree, covariantly typed. Nobody casts anything, anywhere.
	//
	// ⚠ NULL IS A REAL ANSWER, not a missing one: the thick client opens the same documents and
	// shows no tree at all, and the walk ends at a parentless document.
	virtual class ibMetaTreeAbstract* GetMetaTree() const {
		ibMetaDocument* owner = GetParent();
		return owner != nullptr ? owner->GetMetaTree() : nullptr;
	}


protected:

	ibValueMetaObject* m_metaObject;	// current metadata object
	bool m_childDoc;
};

class FRONTEND_API ibMetaDataDocument : public ibMetaDocument {
	wxDECLARE_ABSTRACT_CLASS(ibMetaDataDocument);
public:
	virtual class ibMetaData* GetMetaData() const = 0;
};

class FRONTEND_API ibValueModuleDocument : public ibMetaDocument {
	wxDECLARE_ABSTRACT_CLASS(ibValueModuleDocument);
public:

	virtual void SetCurrentLine(int lineBreakpoint, bool setBreakpoint) = 0;
	virtual void SetToolTip(const wxString& resultStr) = 0;
	virtual void ShowAutoComplete(const struct ibDebugAutoCompleteData& debugData) = 0;
};

// ⭐⭐ A METADATA TREE, ABSTRACTLY — the navigator, or the tree inside an external data processor or
// report. Not a window: an INTERFACE, declared here and implemented in the designer, the same shape
// as the one above it, so this side can say something to a tree without knowing that a tree happens
// to be a wxPanel in another module.
//
// These four are exactly what is asked of a tree from elsewhere — the debugger's bridge, the output
// pane, a closing editor: take the focus, show me this object, tell me which editor is showing it,
// show me this module at this line.
//
// 🛑 IT USED TO BE `ibBackendMetadataTree` ON THE ENGINE — the same list, but held by ibMetaData, so
// the backend knew a watcher has rows in it and answered "the tree" with whichever watcher happened
// to be first. Here it is answered by the DOCUMENT that has one (ibMetaDataDocument::GetMetaTree),
// and a window that has none simply says nothing.
class FRONTEND_API ibMetaTreeAbstract {
public:

	virtual ~ibMetaTreeAbstract() = default;

	// Become the window somebody is working in.
	virtual void Activate() = 0;

	// Show this metaobject — raising the editor that is already open rather than asking for a
	// second one, which is the half every caller that went round a tree was missing.
	virtual bool OpenObjectForm(ibValueMetaObject* metaObject) = 0;

	// …and which editor that is, or null when nobody is looking at it.
	virtual ibMetaDocument* GetDocument(ibValueMetaObject* metaObject) const = 0;

	// WHOSE metadata this tree is showing. The debugger arrives with a module NAME off the wire and
	// has to find the object it belongs to, so it needs the container, not just the rows.
	virtual class ibMetaData* GetMetaData() const = 0;

	// Show this module at this line. `setRunLine` is what tells the debugger's arrow from a jump.
	virtual void EditModule(const ibGuid& moduleName, int line, bool setRunLine = true) = 0;
};

#include <wx/aui/auibar.h>

// Step-4 collapse note: ShowFrame + m_webFrame + SetWebFrame/GetWebFrame
// moved up into ibView. Empty OnUpdate and OnClose-forwarding overrides
// dropped — they duplicated the base.

class FRONTEND_API ibMetaView : public ibView {
	wxDECLARE_ABSTRACT_CLASS(ibMetaView);
public:

	ibMetaDocument* GetDocument() const {
		return dynamic_cast<ibMetaDocument*>(m_viewDocument);
	}

	// OnCreate is the single ibView::OnCreate(ibDocument*, long) virtual — the
	// unified ibDocument::OnCreate pipeline dispatches on it. Meta views that
	// need the metadata-typed document downcast inside (or via GetDocument(),
	// which already returns ibMetaDocument*).

	virtual void OnActivateView(bool activate, ibView* activeView, ibView* deactiveView) override;

	virtual void OnDraw(wxDC* dc) override {}
};

#endif // wxUSE_DOC_VIEW_ARCHITECTURE

#endif // __OBJ_INFO_H__

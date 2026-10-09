#ifndef __OBJ_INFO_H__
#define __OBJ_INFO_H__

/////////////////////////////////////////////////////////////////////////////
// Name:        frontend/docView/docView.h
// Purpose:     OES Doc/View — combined header.
//
//   Top half:   forked doc/view subsystem (ibFrontendDocument, ibFrontendView, ibFrontendDocTemplate,
//               ibFrontendDocManager, ibDocChildFrameAny<>, ibDocParentFrameAny<>,
//               ibDocPrintout). Copied from wx/docview.h with `wx*→ib*`
//               renames; window-type generalised via wxWindow so the
//               template instantiates with both wxWindow and ibWebWindow.
//               Originally:  Author: Julian Smart, (c) Julian Smart,
//                            Licence: wxWindows licence.
//
//   The thin client's copy of frontend/docView: the metadata adapter
//   (ibMetaDocument, ibMetaDataDocument, ibValueModuleDocument, ibMetaView)
//   is cut out, and the classes frmserver registers too are prefixed
//   ibFrontend — the file base loads frmserver into this process, and wx
//   refuses two classes of one name.
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

#include "frmclient/frmclient.h"          // FRMCLIENT_API

// ----------------------------------------------------------------------------
// Forward declarations
// ----------------------------------------------------------------------------

class WXDLLIMPEXP_FWD_CORE wxWindow;
class WXDLLIMPEXP_FWD_CORE wxPrintInfo;
class WXDLLIMPEXP_FWD_CORE wxCommandProcessor;
class WXDLLIMPEXP_FWD_BASE wxConfigBase;

class FRMCLIENT_API ibFrontendDocument;
class FRMCLIENT_API ibFrontendView;
class FRMCLIENT_API ibFrontendDocTemplate;
class FRMCLIENT_API ibFrontendDocManager;
class ibDocChildFrameAnyBase;
class WXDLLIMPEXP_FWD_AUI wxAuiToolBar;

// ============================================================================
//                          PART 1 — FORKED DOC/VIEW
//                  (verbatim wx/docview.h, renamed wx*→ib*)
// ============================================================================

// Flags for ibFrontendDocManager (can be combined).
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

typedef wxVector<ibFrontendDocument*> ibDocVector;
typedef wxVector<ibFrontendView*> ibViewVector;
typedef wxVector<ibFrontendDocTemplate*> ibDocTemplateVector;

class FRMCLIENT_API ibFrontendDocument : public wxEvtHandler
{
public:
    ibFrontendDocument(ibFrontendDocument *parent = nullptr);
    virtual ~ibFrontendDocument();

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

    virtual bool AddView(ibFrontendView *view);
    virtual bool RemoveView(ibFrontendView *view);

    ibViewVector GetViewsVector() const;

    wxList& GetViews() { return m_documentViews; }
    const wxList& GetViews() const { return m_documentViews; }

    ibFrontendView *GetFirstView() const;

    virtual void UpdateAllViews(ibFrontendView *sender = nullptr, wxObject *hint = nullptr);
    virtual void NotifyClosing();
    virtual bool DeleteAllViews();

    virtual ibFrontendDocManager *GetDocumentManager() const;
    virtual ibFrontendDocTemplate *GetDocumentTemplate() const
        { return m_documentTemplate; }
    virtual void SetDocumentTemplate(ibFrontendDocTemplate *temp)
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

    // Return type is wxWindow* — wxWindow on desktop, ibWebWindow on
    // web. Wx-modal callers (wxMessageBox / wxFileSelector / ...) need a
    // real wxWindow* parent and must therefore live under #ifndef OES_USE_WEB;
    // web-side modal flow routes through ibBackendDocFrame::ShowModalMessage.
    virtual wxWindow *GetDocumentWindow() const;

    virtual bool IsChildDocument() const { return m_documentParent != nullptr; }

    // A COMPOSITE document answers this for the children it lays out: the window the child's
    // view must render into. The default — null — means "take a tab of your own", so nothing
    // changes for an ordinary document.
    //
    // This is what lets ONE tab hold several documents: the home page hands each attached form
    // a cell of its splitter tree, and the children are its doc CHILDREN, so they live and die
    // with it. The child carries no "where am I" state — it asks its parent, which is the only
    // one that knows.
    virtual wxWindow* GetChildDocumentWindow(const ibFrontendDocument* child) const { return nullptr; }

    // The window my PARENT gives me because it composes me — null when I am on my own and
    // take a tab. THE question; everything about composition is asked through it.
    wxWindow* GetComposedWindow() const {
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
    // when its parent closes" — ibFrontendDocument::Close re-parents it onto the
    // document manager instead of closing it. Default true (close with parent).
    virtual bool IsCloseOnOwnerClose() const { return true; }

    // Runtime re-parenting (used by Close re-parent branch). Safe to manipulate
    // base's m_documentParent / m_childDocuments directly because we own them;
    // the historical wxDocument private-member kludge is gone with the fork.
    virtual void SetDocParent(ibFrontendDocument* docParent);

    // ⭐ AN OWNER ANSWERS ABOUT ITS OWN. The editors a navigator opens are OWNED by the document
    // holding that configuration and never join the manager's list, so "is it already open here?"
    // is a question for THIS document — the manager's own search answers about the manager's own
    // documents and cannot see these (Max, 2026-09-01: *"split the search by parent from the search
    // by name"* — they are two questions, and one function with an optional parent made them look
    // like one).
    ibFrontendDocument* FindChildDocument(const wxString& identifier) const;

protected:
    wxList                m_documentViews;
    wxString              m_documentFile;
    wxString              m_documentTitle;
    wxString              m_documentTypeName;
    ibFrontendDocTemplate*        m_documentTemplate;
    bool                  m_documentModified;
    ibFrontendDocument*           m_documentParent;
    wxCommandProcessor*   m_commandProcessor;
    bool                  m_savedYet;
    wxIcon                m_docIcon;

    virtual bool DoSaveDocument(const wxString& file);
    virtual bool DoOpenDocument(const wxString& file);

    // View factory for the OnCreate pipeline. Default pulls the view class from
    // the document template (the wx-style path used by every templated doc).
    // Template-less docs (e.g. ibFormVisualDocument, created directly without a
    // template) override this to construct their view explicitly.
    virtual ibFrontendView* DoCreateView();

    wxString DoGetUserReadableName() const;

    // Promoted from private (was wxDocument's private slot) to protected so
    // derived classes — ibMetaDocument in particular — can offer typed
    // accessors over the child-doc list without maintaining a shadow.
    std::list<ibFrontendDocument*> m_childDocuments;

private:
    wxDECLARE_ABSTRACT_CLASS(ibFrontendDocument);
    wxDECLARE_NO_COPY_CLASS(ibFrontendDocument);
};

class FRMCLIENT_API ibFrontendView: public wxEvtHandler
{
public:
    ibFrontendView();
    virtual ~ibFrontendView();

    ibFrontendDocument *GetDocument() const { return m_viewDocument; }
    virtual void SetDocument(ibFrontendDocument *doc);

    wxString GetViewName() const { return m_viewTypeName; }
    void SetViewName(const wxString& name) { m_viewTypeName = name; }

    wxWindow *GetFrame() const { return m_viewFrame ; }
    void SetFrame(wxWindow *frame) { m_viewFrame = frame; }

    virtual void OnActivateView(bool activate,
                                ibFrontendView *activeView,
                                ibFrontendView *deactiveView);
    virtual void OnDraw(wxDC *dc) = 0;
    virtual void OnPrint(wxDC *dc, wxObject *info);
    virtual void OnUpdate(ibFrontendView *sender, wxObject *hint = nullptr);
    virtual void OnClosingDocument() {}
    virtual void OnChangeFilename();

    virtual bool OnCreate(ibFrontendDocument *WXUNUSED(doc), long WXUNUSED(flags))
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

    ibFrontendDocManager *GetDocumentManager() const
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


protected:
    virtual bool TryBefore(wxEvent& event) override;

    ibFrontendDocument*       m_viewDocument;
    wxString          m_viewTypeName;
    wxWindow* m_viewFrame;

    ibDocChildFrameAnyBase *m_docChildFrame;


private:
    wxDECLARE_ABSTRACT_CLASS(ibFrontendView);
    wxDECLARE_NO_COPY_CLASS(ibFrontendView);
};

// ibFrontendDocTemplate / ibMetaDocTemplate — full definitions in
// frontend/docView/docManager.h. Kept out of this header so the wx-fork base
// file holds only ibFrontendDocument / ibFrontendView / ibFrontendDocManager (the types that need to
// be visible to every doc-aware TU). ibFrontendDocManager methods below only hand
// back ibFrontendDocTemplate* / ibMetaDocTemplate*, so the forward-decls at the top
// of this file are enough at this scope; pull in docManager.h when you need
// the full template type.

class FRMCLIENT_API ibFrontendDocManager: public wxEvtHandler
{
public:
    // NB: flags are unused, don't pass ibDOC_XXX to this ctor
    ibFrontendDocManager(long flags = 0, bool initialize = true);
    virtual ~ibFrontendDocManager();

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

    virtual ibFrontendDocument *CreateDocument(const wxString& path, long flags = 0);

    ibFrontendDocument *CreateNewDocument()
        { return CreateDocument(wxString(), ibDOC_NEW); }

    virtual ibFrontendView *CreateView(ibFrontendDocument *doc, long flags = 0);
    virtual void DeleteTemplate(ibFrontendDocTemplate *temp, long flags = 0);
    virtual bool FlushDoc(ibFrontendDocument *doc);
    virtual ibFrontendDocTemplate *MatchTemplate(const wxString& path);
    virtual ibFrontendDocTemplate *SelectDocumentPath(ibFrontendDocTemplate **templates,
            int noTemplates, wxString& path, long flags, bool save = false);
    virtual ibFrontendDocTemplate *SelectDocumentType(ibFrontendDocTemplate **templates,
            int noTemplates, bool sort = false);
    virtual ibFrontendDocTemplate *SelectViewType(ibFrontendDocTemplate **templates,
            int noTemplates, bool sort = false);
    virtual ibFrontendDocTemplate *FindTemplateForPath(const wxString& path);

    void AssociateTemplate(ibFrontendDocTemplate *temp);
    void DisassociateTemplate(ibFrontendDocTemplate *temp);

    ibFrontendDocTemplate* FindTemplate(const wxClassInfo* documentClassInfo);

    ibFrontendDocument* FindDocumentByPath(const wxString& path) const;

    // ⭐ THE SAME QUESTION FOR THINGS THAT HAVE NO FILE. FindDocumentByPath answers for documents
    // opened FROM DISK; everything inside a configuration — a form, a module, an object editor —
    // has an identity and no path, and was therefore invisible to the only "is it already open"
    // check there was. See ibFrontendDocument::GetUniqueIdentifier.
    //
    ibFrontendDocument *GetCurrentDocument() const;

    void SetMaxDocsOpen(int n) { m_maxDocsOpen = n; }
    int GetMaxDocsOpen() const { return m_maxDocsOpen; }

    void AddDocument(ibFrontendDocument *doc);
    void RemoveDocument(ibFrontendDocument *doc);

    bool CloseDocuments(bool force = true);
    bool CloseDocument(ibFrontendDocument* doc, bool force = false);
    bool Clear(bool force = true);

    virtual void ActivateView(ibFrontendView *view, bool activate = true);
    virtual ibFrontendView *GetCurrentView() const { return m_currentView; }

    ibFrontendView *GetAnyUsableView() const;

    ibDocVector GetDocumentsVector() const;
    ibDocTemplateVector GetTemplatesVector() const;

    wxList& GetDocuments() { return m_docs; }
    wxList& GetTemplates() { return m_templates; }

    virtual wxString MakeNewDocumentName();
    virtual wxString MakeFrameTitle(ibFrontendDocument* doc);

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

    static ibFrontendDocManager* GetDocumentManager() { return sm_docManager; }

#if wxUSE_PRINTING_ARCHITECTURE
    wxPageSetupDialogData& GetPageSetupDialogData()
        { return m_pageSetupDialogData; }
    const wxPageSetupDialogData& GetPageSetupDialogData() const
        { return m_pageSetupDialogData; }
#endif

    ibFrontendDocTemplate*     FindTemplateByDocClassInfo(const wxClassInfo* classInfo) const;

    template <typename T, typename... Args>
    T* CreateDocument(Args&&... args) const
    {
        ibFrontendDocTemplate* docTemplate = FindTemplateByDocClassInfo(CLASSINFO(T));
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

protected:
    // ---- the halves FindOpenDocument(metaObject) is made of --------------------------------
    // Separate because they are separate questions — the manager answers about ITS OWN documents,
    // an owner answers about its children (ibFrontendDocument::FindChildDocument) — and private because
    // nothing outside has ever needed to ask either one on its own.

    // The manager's own list, by the document's declared identity. No cast: a document that has no
    // identity of this kind answers with an empty string and simply never matches.
    ibFrontendDocument* FindDocumentByIdentifier(const wxString& identifier) const;

    // Mine first, then the owner's editors.
    ibFrontendDocument* FindOpenDocument(const wxString& identifier, const ibFrontendDocument* docParent) const;

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
    ibFrontendView*           m_currentView;
    wxFileHistory*    m_fileHistory;
    wxString          m_lastDirectory;
    static ibFrontendDocManager* sm_docManager;

#if wxUSE_PRINTING_ARCHITECTURE
    wxPageSetupDialogData m_pageSetupDialogData;
#endif

    // Find / Replace dialog state (desktop only; mainFrame parent).
    wxFindReplaceData    m_findData;
    wxFindReplaceDialog* m_findDialog = nullptr;

    wxDECLARE_EVENT_TABLE();
    wxDECLARE_DYNAMIC_CLASS(ibFrontendDocManager);
    wxDECLARE_NO_COPY_CLASS(ibFrontendDocManager);
};

// Shortcut to the process-wide doc manager singleton (set in ibFrontendDocManager
// ctor, cleared in dtor). Previously hosted in frontend/docView/docManager.h
// when ibMetaDocManager was a separate subclass; collapsed into the base.
#define docManager ibFrontendDocManager::GetDocumentManager()

// ----------------------------------------------------------------------------
// Base class for child frames -- mix-in, doesn't derive from a window class.
// ----------------------------------------------------------------------------

class FRMCLIENT_API ibDocChildFrameAnyBase
{
public:
    ibDocChildFrameAnyBase()
    {
        m_childDocument = nullptr;
        m_childView = nullptr;
        m_win = nullptr;
        m_lastEvent = nullptr;
    }

    ibDocChildFrameAnyBase(ibFrontendDocument *doc, ibFrontendView *view, wxWindow *win)
    {
        Create(doc, view, win);
    }

    bool Create(ibFrontendDocument *doc, ibFrontendView *view, wxWindow *win)
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

    ibFrontendDocument *GetDocument() const { return m_childDocument; }
    ibFrontendView *GetView() const { return m_childView; }
    void SetDocument(ibFrontendDocument *doc) { m_childDocument = doc; }
    void SetView(ibFrontendView *view) { m_childView = view; }

    wxWindow *GetWindow() const { return m_win; }

    bool HasAlreadyProcessed(wxEvent& event) const
    {
        return m_lastEvent == &event;
    }

protected:
    bool TryProcessEvent(wxEvent& event);
    bool CloseView(wxCloseEvent& event);

    ibFrontendDocument*       m_childDocument;
    ibFrontendView*           m_childView;

    // Type-switched via wxWindow (wxWindow on desktop, ibWebWindow
    // on web) — see frontendTypes.h.
    wxWindow* m_win;

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

    ibDocChildFrameAny(ibFrontendDocument *doc,
                       ibFrontendView *view,
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

    bool Create(ibFrontendDocument *doc,
                ibFrontendView *view,
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


typedef ibDocChildFrameAny<wxFrame, wxFrame> ibDocChildFrameBase;

class FRMCLIENT_API ibDocChildFrame : public ibDocChildFrameBase
{
public:
    ibDocChildFrame() {}

    ibDocChildFrame(ibFrontendDocument *doc,
                    ibFrontendView *view,
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

    bool Create(ibFrontendDocument *doc,
                ibFrontendView *view,
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

class FRMCLIENT_API ibDocParentFrameAnyBase
{
public:
    ibDocParentFrameAnyBase(wxWindow* frame)
        : m_frame(frame)
    {
        m_docManager = nullptr;
    }

    ibFrontendDocManager *GetDocumentManager() const { return m_docManager; }

protected:
    bool TryProcessEvent(wxEvent& event);

    wxWindow* const m_frame;
    ibFrontendDocManager *m_docManager;

    wxDECLARE_NO_COPY_CLASS(ibDocParentFrameAnyBase);
};


template <class BaseFrame>
class ibDocParentFrameAny : public BaseFrame,
                            public ibDocParentFrameAnyBase
{
public:
    ibDocParentFrameAny() : ibDocParentFrameAnyBase(this) { }
    ibDocParentFrameAny(ibFrontendDocManager *manager,
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

    bool Create(ibFrontendDocManager *manager,
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

class FRMCLIENT_API ibDocParentFrame : public ibDocParentFrameBase
{
public:
    ibDocParentFrame() : ibDocParentFrameBase() { }

    ibDocParentFrame(ibFrontendDocManager *manager,
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

    bool Create(ibFrontendDocManager *manager,
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


// ----------------------------------------------------------------------------
// Provide simple default printing facilities
// ----------------------------------------------------------------------------

#if wxUSE_PRINTING_ARCHITECTURE
class FRMCLIENT_API ibDocPrintout : public wxPrintout
{
public:
    ibDocPrintout(ibFrontendView *view = nullptr, const wxString& title = wxString());

    virtual bool OnPrintPage(int page) override;
    virtual bool HasPage(int page) override;
    virtual bool OnBeginDocument(int startPage, int endPage) override;
    virtual void GetPageInfo(int *minPage, int *maxPage,
                             int *selPageFrom, int *selPageTo) override;

    virtual ibFrontendView *GetView() { return m_printoutView; }

protected:
    ibFrontendView*       m_printoutView;

private:
    wxDECLARE_DYNAMIC_CLASS(ibDocPrintout);
    wxDECLARE_NO_COPY_CLASS(ibDocPrintout);
};
#endif // wxUSE_PRINTING_ARCHITECTURE

// File-to-stream helpers (preserved for back-compat with existing formats).

#if wxUSE_STD_IOSTREAM
bool FRMCLIENT_API
ibTransferFileToStream(const wxString& filename, std::ostream& stream);
bool FRMCLIENT_API
ibTransferStreamToFile(std::istream& stream, const wxString& filename);
#else
bool FRMCLIENT_API
ibTransferFileToStream(const wxString& filename, wxOutputStream& stream);
bool FRMCLIENT_API
ibTransferStreamToFile(wxInputStream& stream, const wxString& filename);
#endif // wxUSE_STD_IOSTREAM

inline ibViewVector ibFrontendDocument::GetViewsVector() const
{
    return m_documentViews.AsVector<ibFrontendView*>();
}

inline ibDocVector ibFrontendDocManager::GetDocumentsVector() const
{
    return m_docs.AsVector<ibFrontendDocument*>();
}

// ibFrontendDocManager::GetTemplatesVector() is out-of-line in docManager.cpp:
// wxObjectList::AsVector<T*> needs the complete ibFrontendDocTemplate type for the
// internal static_cast, and ibFrontendDocTemplate is forward-declared in this header.

#include "frmclient/backend/metaCollection/metaObject.h"

// ============================================================================
// THE METADATA DOCUMENT AND VIEW — the desktop's ibMetaDocument / ibMetaView (frontend/docView/docView.h) as the
// documents copied from the desktop call them. A document's metaobject is the server's: the client's has none, and
// ConvertMetaObjectToType answers none.
// ============================================================================

class FRMCLIENT_API ibMetaDocument : public ibFrontendDocument {
public:

    ibMetaDocument() : m_metaObject(nullptr), m_childDoc(true) {}

    virtual ibValueMetaObject* GetMetaObject() const { return m_metaObject; }

    template <typename T>
    inline T* ConvertMetaObjectToType() {
        ibValueMetaObject* object = GetMetaObject();
        return object != nullptr ? object->ConvertToType<T>() : nullptr;
    }

    // Treats every metadata doc as "child" regardless of m_documentParent.
    virtual bool IsChildDocument() const override { return m_childDoc; }

protected:

    ibValueMetaObject* m_metaObject;	// current metadata object — none on the client
    bool m_childDoc;
};

class FRMCLIENT_API ibMetaView : public ibFrontendView {
public:

    ibMetaDocument* GetDocument() const {
        return dynamic_cast<ibMetaDocument*>(m_viewDocument);
    }

    virtual void OnDraw(wxDC* WXUNUSED(dc)) override {}
};
#endif // wxUSE_DOC_VIEW_ARCHITECTURE

#endif // __OBJ_INFO_H__

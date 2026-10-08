#ifndef __OBJ_INFO_H__
#define __OBJ_INFO_H__

/////////////////////////////////////////////////////////////////////////////
// Name:        frmserver/docView/docView.h
// Purpose:     OES Doc/View — the server side of a form runtime with no windows.
//
//   Top half:   forked doc/view subsystem (ibDocument, ibView, ibDocManager,
//               ibDocChildFrameAnyBase, ibDocParentFrameAnyBase). Copied from
//               wx/docview.h with `wx*→ib*` renames. Documents, views and
//               frames are server objects: the frame a view shows in is a
//               client's tab, and a client (a browser, the wx renderer, an
//               assistant over MCP) draws what they answer over the protocol.
//               One ibDocManager per client session.
//               Originally:  Author: Julian Smart, (c) Julian Smart,
//                            Licence: wxWindows licence.
//
//   Bottom half: OES adapter (ibMetaDocument, ibMetaView) — metadata-specific
//                subclasses living on top of the forked base.
//
// Was previously split as ibDocView.{h,cpp} + docView.{h,cpp}; collapsed
// into a single pair to keep both layers reviewable side-by-side.
/////////////////////////////////////////////////////////////////////////////

#include "wx/defs.h"

#if wxUSE_DOC_VIEW_ARCHITECTURE

#include "wx/string.h"
#include "wx/vector.h"
#include "wx/dlist.h"

#include "frmserver/docView/commandProcessor.h"   // a document's commands, and their undo
#include "frmserver/docView/docCommand.h"         // the commands a manager offers
#include "backend/backend_picture.h"              // ibServerPicture — a document's icon as it is sent

#if wxUSE_STD_IOSTREAM
  #include "wx/iosfwrap.h"
#else
  #include "wx/stream.h"
#endif

#include <list>
#include <vector>
#include <map>
#include <memory>

#include "frmserver/frmserver.h"          // FRMSERVER_API
#include "frmserver/schema/clientSchema.h"  // the schemas a manager offers
#include "protocol/protocol.h"  // ibProtocolEvent — what a person did in a view
#include "backend/backend_form.h"       // ibBackendMetaDocument
#include "backend/metaCollection/metaObject.h"

// ----------------------------------------------------------------------------
// Forward declarations
// ----------------------------------------------------------------------------

class FRMSERVER_API ibDocument;
class FRMSERVER_API ibView;
class FRMSERVER_API ibDocTemplate;
class FRMSERVER_API ibMetaDocTemplate;
class FRMSERVER_API ibDocManager;
class FRMSERVER_API ibMetaDocument;
class ibDocChildFrameAnyBase;

class BACKEND_API ibDataNode;
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
    ibTEMPLATE_SAVE_AS_FILE = 8,    // OES extension: its documents save as a file of their own, even child documents
    ibDEFAULT_TEMPLATE_FLAGS = ibTEMPLATE_VISIBLE
};

// OES-side extension flag.
enum
{
    ibDOC_READONLY = ibDOC_SILENT + 1
};

typedef wxVector<ibDocument*> ibDocVector;
typedef wxVector<ibView*> ibViewVector;
typedef wxVector<ibDocTemplate*> ibDocTemplateVector;

// Not an event handler, as wx's are: nothing here is a window, and nothing routes commands through the
// documents — a client's events come in through the form's door (ibValueForm::DispatchEvent).
class FRMSERVER_API ibDocument : public wxObject
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

    virtual ibCommandProcessor *OnCreateCommandProcessor();
    virtual ibCommandProcessor *GetCommandProcessor() const
        { return m_commandProcessor; }
    virtual void SetCommandProcessor(ibCommandProcessor *proc)
        { m_commandProcessor = proc; }

    virtual void OnChangedViewList();
    virtual bool DeleteContents();

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

    virtual wxString GetUserReadableName() const;

    // ⭐ WHAT THIS DOCUMENT CAN BE SAVED AS — named lines, as a file dialog takes them ("Name (*.x)|*.x|…"), the desktop's
    // GetSaveFilter: what a client's Save as offers (its tab's Formats). Its template's, unless it writes more formats than
    // that; empty — it saves to no file of the person's (a form).
    virtual wxString GetSaveFilter() const;

    // The frame this document's first view shows in — its own tab, or its parent's frame when
    // the parent composes it. Null while no view has one.
    virtual ibDocChildFrameAnyBase *GetDocumentWindow() const;

    virtual bool IsChildDocument() const { return m_documentParent != nullptr; }

    // A COMPOSITE document answers this for the children it lays out: the frame the child's
    // view shows in. The default — null — means "take a tab of your own", so nothing changes
    // for an ordinary document.
    //
    // This is what lets ONE tab hold several documents: the home page hands each attached form
    // the frame it lays them out in, and the children are its doc CHILDREN, so they live and die
    // with it. The child carries no "where am I" state — it asks its parent, which is the only
    // one that knows.
    virtual ibDocChildFrameAnyBase* GetChildDocumentWindow(const ibDocument* child) const { return nullptr; }

    // The frame my PARENT gives me because it composes me — null when I am on my own and
    // take a tab. THE question; everything about composition is asked through it.
    ibDocChildFrameAnyBase* GetComposedWindow() const {
        return m_documentParent != nullptr ? m_documentParent->GetChildDocumentWindow(this) : nullptr;
    }

    // Am I laid out INSIDE my parent rather than in a tab? Asked by the close rules: a
    // composed view must not close a frame it does not own, and a composed form has no
    // close of its own.
    bool IsEmbedded() const { return GetComposedWindow() != nullptr; }

    // A composed document does not close itself — its PARENT closes it. This is the parent
    // saying "it is me, let them go"; false the rest of the time, which is what makes every
    // other close path (a Close command, a forced close from the object, a manager sweep)
    // bounce off a child that lives in someone else's frame.
    virtual bool IsClosingChildren() const { return false; }
    bool IsClosedByParent() const {
        return m_documentParent != nullptr && m_documentParent->IsClosingChildren();
    }

    bool CanClose();

    // OES-side adaptations lifted from ibMetaDocument (step-4 collapse).

    // Document icon — generic concept (wxDocument has none); as a server sends it.
    virtual void SetIcon(const ibServerPicture& icon) { m_docIcon = icon; }
    virtual ibServerPicture GetIcon() const { return m_docIcon; }

    // Cascading-close opt-out. Returning false means "this child stays open
    // when its parent closes" — ibDocument::Close re-parents it onto the
    // document manager instead of closing it. Default true (close with parent).
    virtual bool IsCloseOnOwnerClose() const { return true; }

    // Runtime re-parenting (used by Close re-parent branch). Safe to manipulate
    // base's m_documentParent / m_childDocuments directly because we own them;
    // the historical wxDocument private-member kludge is gone with the fork.
    virtual void SetDocParent(ibDocument* docParent);
    ibDocument* GetDocParent() const { return m_documentParent; }

    // ⭐ AN OWNER ANSWERS ABOUT ITS OWN. Documents opened under an owner never join the manager's
    // list, so "is it already open here?" is a question for THIS document — the manager's own
    // search answers about the manager's own documents and cannot see these (Max, 2026-09-01:
    // *"split the search by parent from the search by name"* — they are two questions, and one
    // function with an optional parent made them look like one).
    ibDocument* FindChildDocument(const wxString& identifier) const;

    // The documents opened from this one, which close with it.
    const std::list<ibDocument*>& GetChildDocuments() const { return m_childDocuments; }

protected:
    wxList                m_documentViews;
    wxString              m_documentFile;
    wxString              m_documentTitle;
    wxString              m_documentTypeName;
    ibDocTemplate*        m_documentTemplate;
    bool                  m_documentModified;
    ibDocument*           m_documentParent;
    ibCommandProcessor*   m_commandProcessor;
    bool                  m_savedYet;
    ibServerPicture       m_docIcon;

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

// WHAT A VIEW SHOWS, as a client draws it — a tab's Kind on the wire, a number never renumbered: a client takes the
// view of its own it has registered for it.
enum class ibClientViewKind : s32 {
    Form        = 1,   // a form — the default, absent from the wire
    Text        = 2,
    Spreadsheet = 3,
};

class FRMSERVER_API ibView: public wxObject
{
public:
    ibView();
    virtual ~ibView();

    ibDocument *GetDocument() const { return m_viewDocument; }
    virtual void SetDocument(ibDocument *doc);

    wxString GetViewName() const { return m_viewTypeName; }
    void SetViewName(const wxString& name) { m_viewTypeName = name; }

    // The frame this view shows in: its own tab, or the frame its parent hands it when the
    // document is composed.
    ibDocChildFrameAnyBase *GetFrame() const { return m_viewFrame ; }
    void SetFrame(ibDocChildFrameAnyBase *frame) { m_viewFrame = frame; }

    virtual void OnActivateView(bool activate,
                                ibView *activeView,
                                ibView *deactiveView);

    // Drawing a document means writing what its view shows into the frame node a
    // client draws from.
    virtual void OnDraw(ibDataNode& frame) = 0;

    // What it shows — the view of its own a client draws it with (the tab's Kind).
    virtual ibClientViewKind GetViewKind() const { return ibClientViewKind::Form; }
    // WHAT A CLIENT READS OF THE VIEW ITSELF, a part at a time — a spreadsheet's cells, as a control of a form is read
    // (ibValueFrame::Fetch). False: this view has nothing to read.
    virtual bool Fetch(const ibDataNode& WXUNUSED(request), ibDataNode& WXUNUSED(response)) { return false; }

    // WHAT THE PERSON DID IN THE VIEW ITSELF, not in a control of a form — a text typed into a text document's view
    // (Change {Text}). False: this view takes no such event.
    virtual bool OnClientEvent(ibProtocolEvent WXUNUSED(event), const ibDataNode& WXUNUSED(args)) { return false; }
    virtual void OnUpdate(ibView *sender, wxObject *hint = nullptr);
    virtual void OnClosingDocument() {}
    virtual void OnChangeFilename();

    virtual bool OnCreate(ibDocument *WXUNUSED(doc), long WXUNUSED(flags))
        { return true; }

    virtual bool Close(bool deleteWindow = true);
    virtual bool OnClose(bool deleteWindow);

    virtual void Activate(bool activate);

    ibDocManager *GetDocumentManager() const
        { return m_viewDocument->GetDocumentManager(); }

    void SetDocChildFrame(ibDocChildFrameAnyBase *docChildFrame);
    ibDocChildFrameAnyBase* GetDocChildFrame() const { return m_docChildFrame; }

    // OES-side adaptations lifted from ibMetaView (step-4 collapse).

    // Explicit "show this view's frame now" trigger: the view's own tab becomes
    // the one its client shows. A composed view has no tab of its own and
    // answers false. Out-of-line: it needs the complete ibDocChildFrameAnyBase.
    bool ShowFrame(bool show = true);

protected:
    ibDocument*             m_viewDocument;
    wxString                m_viewTypeName;
    ibDocChildFrameAnyBase* m_viewFrame;

    ibDocChildFrameAnyBase *m_docChildFrame;

private:
    wxDECLARE_ABSTRACT_CLASS(ibView);
    wxDECLARE_NO_COPY_CLASS(ibView);
};

// ibDocTemplate / ibMetaDocTemplate — full definitions in
// frmserver/docView/docManager.h. Kept out of this header so the wx-fork base
// file holds only ibDocument / ibView / ibDocManager (the types that need to
// be visible to every doc-aware TU). ibDocManager methods below only hand
// back ibDocTemplate* / ibMetaDocTemplate*, so the forward-decls at the top
// of this file are enough at this scope; pull in docManager.h when you need
// the full template type.
//
// ONE MANAGER PER CLIENT SESSION, not per process: the session's main frame
// holds its own (ibDocParentFrameAnyBase::m_docManager).

class FRMSERVER_API ibDocManager: public wxObject
{
public:
    // NB: flags are unused, don't pass ibDOC_XXX to this ctor
    //
    // ⭐ THE BASE SET — what every application's manager has, registered here: the shared templates (Text /
    // Spreadsheet / Help, meta-bound via CLSID, + the registration journal and the home page) and the shared
    // schemas. An application's own manager adds its own in its ctor (ibDocManagerRuntime, ibDocManagerDesigner).
    ibDocManager(long flags = 0, bool initialize = true);
    virtual ~ibDocManager();

    virtual bool Initialize();

    virtual ibDocument *CreateDocument(const wxString& path, long flags = 0);

    ibDocument *CreateNewDocument()
        { return CreateDocument(wxString(), ibDOC_NEW); }

    virtual ibView *CreateView(ibDocument *doc, long flags = 0);
    virtual void DeleteTemplate(ibDocTemplate *temp, long flags = 0);
    virtual bool FlushDoc(ibDocument *doc);
    virtual ibDocTemplate *MatchTemplate(const wxString& path);

    // Which template makes a new document / which view to make: the single
    // candidate there is, or null when there are several — choosing among them
    // is the person's, and nobody is asked here.
    virtual ibDocTemplate *SelectDocumentType(ibDocTemplate **templates,
            int noTemplates);
    virtual ibDocTemplate *SelectViewType(ibDocTemplate **templates,
            int noTemplates);
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
    // ⭐⭐ IS THIS OBJECT ALREADY OPEN, and in which document — the ONE question the world
    // outside this class asks; the identifier lookup under it is protected.
    //
    // ⚠ THE OBJECT IS THE WHOLE ARGUMENT. Its identity follows from it, worked out in one place —
    // and no caller has to carry "on whose behalf", which is a thing every future call site would
    // have to get right (Max, 2026-09-01, on seeing five new methods here: *"I do not much like
    // it"*).
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

    // The current client session's manager. There is one per session, so the
    // body lives with the session (frmserver/client/clientFrame.cpp).
    static ibDocManager* GetDocumentManager();

    // ------------------------------------------------------------------
    // OES meta-template API — see ibMetaDocTemplate in docManager.h.
    //
    // AddDocTemplate(ibPictureID/ibClassID,...) overloads create an
    // ibMetaDocTemplate, populate its CLSID + picture id, and associate it
    // with the same m_templates list as plain file templates. Lookups
    // by CLSID iterate m_templates and dynamic_cast.
    //
    // OpenForm is the entry point for "open this metaobject" — distinct
    // from CreateDocument (file/path-based).
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
    // passed the navigator's own document and the other a plain nullptr. Asking every caller to
    // carry "on whose behalf" is a mechanism that has to be right in every future call site (Max,
    // 2026-09-01).
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

    // ⭐ THE SCHEMAS THIS APPLICATION OFFERS — what a client of it may ask for besides its documents (All
    // functions, Active users: frmserver/schema/). Kept with the templates and in the same arrangement: the
    // shared ones the base's ctor registers, an application's own its manager's ctor (RegisterSchema). A schema
    // its manager did not register is not there for that application: a runtime client asking for the metadata
    // tree is told there is no such schema.
    //
    // The schema of that kind — what it shows and what it does are its own. nullptr: this application has no
    // such schema.
    const ibClientSchema* FindSchema(ibProtocolSchema kind) const;

    // The schemas registered — what a client is told it may ask for.
    std::vector<ibProtocolSchema> GetSchemaKinds() const;

    // ⭐ THE COMMANDS THIS APPLICATION'S DOCUMENTS ANSWER TO — registered as the templates and the schemas are: the
    // shared ones by the base's ctor, an application's own by its manager's ctor. What a client is told of each: its
    // name, its key and its picture, so a thin client knows them without a list of its own.
    const std::map<ibDocCommand, ibDocCommandInfo>& GetCommands() const { return m_commands; }

protected:
    void RegisterSchema(ibProtocolSchema kind, std::unique_ptr<ibClientSchema> schema);
    void RegisterCommand(ibDocCommand command, const wxString& title, const wxString& shortcut,
        ibClientStockPicture picture);

    std::map<ibProtocolSchema, std::unique_ptr<ibClientSchema>> m_schemas;
    std::map<ibDocCommand, ibDocCommandInfo>                       m_commands;

    // The manager's own list, by the document's declared identity — what FindOpenDocument(metaObject)
    // asks. An owner answers about its children separately (ibDocument::FindChildDocument). No cast:
    // a document that has no identity of this kind answers with an empty string and simply never
    // matches.
    ibDocument* FindDocumentByIdentifier(const wxString& identifier) const;

    int               m_defaultDocumentNameCounter;
    int               m_maxDocsOpen;
    wxList            m_docs;
    wxList            m_templates;
    ibView*           m_currentView;

    wxDECLARE_DYNAMIC_CLASS(ibDocManager);
    wxDECLARE_NO_COPY_CLASS(ibDocManager);
};

// Shortcut to the current session's doc manager. Previously hosted in
// frontend/docView/docManager.h when ibMetaDocManager was a separate
// subclass; collapsed into the base.
#define docManager ibDocManager::GetDocumentManager()

// ----------------------------------------------------------------------------
// The frame a view shows in — a server object, a client's TAB, not a window.
// A mix-in: the tab class derives from it and overrides Show and Close.
// ----------------------------------------------------------------------------

class FRMSERVER_API ibDocChildFrameAnyBase
{
public:
    ibDocChildFrameAnyBase()
    {
        m_childDocument = nullptr;
        m_childView = nullptr;
    }

    ibDocChildFrameAnyBase(ibDocument *doc, ibView *view)
    {
        Create(doc, view);
    }

    virtual ~ibDocChildFrameAnyBase()
    {
        if ( m_childView )
            m_childView->SetDocChildFrame(nullptr);
    }

    bool Create(ibDocument *doc, ibView *view)
    {
        m_childDocument = doc;
        m_childView = view;

        if ( view )
            view->SetDocChildFrame(this);

        return true;
    }

    ibDocument *GetDocument() const { return m_childDocument; }
    ibView *GetView() const { return m_childView; }
    void SetDocument(ibDocument *doc) { m_childDocument = doc; }
    void SetView(ibView *view) { m_childView = view; }

    // Become the tab the client shows (or stop being it). The base has no
    // client to tell and answers false; a tab overrides it.
    virtual bool Show(bool WXUNUSED(show) = true) { return false; }

    // Closing the frame closes the view it shows, once the call that asked has
    // unwound. True when the frame took the close over (a client tab defers it
    // to the end of the request); false when it cannot, and the caller closes
    // the view itself.
    virtual bool Close() { return false; }

protected:
    ibDocument*       m_childDocument;
    ibView*           m_childView;

private:
    wxDECLARE_NO_COPY_CLASS(ibDocChildFrameAnyBase);
};

// ----------------------------------------------------------------------------
// ibDocParentFrameAnyBase — mix-in for a session's main frame: the slot that
// holds that session's ibDocManager.
// ----------------------------------------------------------------------------

class FRMSERVER_API ibDocParentFrameAnyBase
{
public:
    ibDocParentFrameAnyBase()
    {
        m_docManager = nullptr;
    }

    ibDocManager *GetDocumentManager() const { return m_docManager; }

protected:
    ibDocManager *m_docManager;

    wxDECLARE_NO_COPY_CLASS(ibDocParentFrameAnyBase);
};

// File-to-stream helpers (preserved for back-compat with existing formats).

#if wxUSE_STD_IOSTREAM
bool FRMSERVER_API
ibTransferFileToStream(const wxString& filename, std::ostream& stream);
bool FRMSERVER_API
ibTransferStreamToFile(std::istream& stream, const wxString& filename);
#else
bool FRMSERVER_API
ibTransferFileToStream(const wxString& filename, wxOutputStream& stream);
bool FRMSERVER_API
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
//                         (ibMetaDocument / ibMetaView)
// ============================================================================

// Step-4 collapse note: SetIcon/GetIcon, IsCloseOnOwnerClose, cascading
// UpdateAllViews + Close, DeleteAllViews-with-Activate, generic SetDocParent
// and the m_docIcon storage all moved up into ibDocument. The historical
// shadow fields `m_documentParent : ibMetaDocument*` and `m_childDocs :
// wxDList<ibMetaDocument>` are gone — base's m_documentParent /
// m_childDocuments are protected now (no longer the wxDocument private-slot
// kludge), so we use them directly with a typed wrapper for back-compat.

class FRMSERVER_API ibMetaDocument : public ibBackendMetaDocument, public ibDocument {
	wxDECLARE_ABSTRACT_CLASS(ibMetaDocument);
public:

	virtual void SetMetaObject(ibValueMetaObject* metaObject) { m_metaObject = metaObject; }
	virtual ibValueMetaObject* GetMetaObject() const { return m_metaObject; }

	// 🛑 A DOCUMENT WITHOUT ITS METAOBJECT IS A STATE THAT HAPPENS — it is closed, the object was
	// deleted, the configuration was reloaded under it — and every caller here already treats the
	// ANSWER as possibly null (`if (moduleObject != nullptr)`). This dereferenced before it could
	// give them one, so the check they wrote was unreachable: the crash landed one frame earlier,
	// inside a one-line helper (measured 2026-09-02 — designer_4404, opening a module from the
	// tree: OpenObjectForm → OnActivateView → ActivateEditor → here).
	//
	// The neighbouring accessor two lines down guards exactly this pointer. One of the two was
	// written by somebody who had seen it be null.
	template <typename T>
	inline T* ConvertMetaObjectToType() {
		ibValueMetaObject* object = GetMetaObject();
		return object != nullptr ? object->ConvertToType<T>() : nullptr;
	}

	wxString GetModuleName() const;

	// The metaobject's guid IS this document's identity — see ibDocument::GetUniqueIdentifier.
	virtual wxString GetUniqueIdentifier() const override {
		// wxString(wxEmptyString), never the bare constant: outside MSVC wxEmptyString is a
		// `const wxChar *`, and a conditional whose arms are wxString and const wxChar * is
		// AMBIGUOUS — each converts to the other. The wrapper is what the rest of the tree writes
		// (portability.md 1.10, which already records this trap in eight other places).
		return m_metaObject != nullptr ? m_metaObject->GetGuid().GetGuid().str() : wxString(wxEmptyString);
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

protected:

	ibValueMetaObject* m_metaObject;	// current metadata object
	bool m_childDoc;
};

// Step-4 collapse note: ShowFrame moved up into ibView. Empty OnUpdate and
// OnClose-forwarding overrides dropped — they duplicated the base.

class FRMSERVER_API ibMetaView : public ibView {
	wxDECLARE_ABSTRACT_CLASS(ibMetaView);
public:

	ibMetaDocument* GetDocument() const {
		return dynamic_cast<ibMetaDocument*>(m_viewDocument);
	}

	// OnCreate is the single ibView::OnCreate(ibDocument*, long) virtual — the
	// unified ibDocument::OnCreate pipeline dispatches on it. Meta views that
	// need the metadata-typed document downcast inside (or via GetDocument(),
	// which already returns ibMetaDocument*).

	virtual void OnDraw(ibDataNode& frame) override {}
};

#endif // wxUSE_DOC_VIEW_ARCHITECTURE

#endif // __OBJ_INFO_H__

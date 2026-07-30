/////////////////////////////////////////////////////////////////////////////
// Name:        frontend/docView/ibDocView.cpp
// Purpose:     OES Doc/View fork — copied from wx/src/common/docview.cpp,
//              renamed wx*→ib*. Originally:
//                Author:      Julian Smart
//                Modified by: Vadim Zeitlin
//                Created:     01/02/97
//                Copyright:   (c) Julian Smart
//                Licence:     wxWindows licence
// OES notes:   Step 1 of doc-view fork — byte-faithful rename only. See
//              ibDocView.h for the 4-step plan. This file is staging;
//              eventual home is docView.cpp (merged with the OES adapter).
/////////////////////////////////////////////////////////////////////////////

// ============================================================================
// declarations
// ============================================================================

// ----------------------------------------------------------------------------
// headers
// ----------------------------------------------------------------------------

// For compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"


#if wxUSE_DOC_VIEW_ARCHITECTURE

#include "docView.h"

#ifndef WX_PRECOMP
    #include "wx/list.h"
    #include "wx/string.h"
    #include "wx/utils.h"
    #include "wx/app.h"
    #include "wx/dc.h"
    #include "wx/dialog.h"
    #include "wx/menu.h"
    #include "wx/filedlg.h"
    #include "wx/intl.h"
    #include "wx/log.h"
    #include "wx/msgdlg.h"
    #include "wx/mdi.h"
    #include "wx/choicdlg.h"
#endif

#if wxUSE_PRINTING_ARCHITECTURE
    #include "wx/prntbase.h"
    #include "wx/printdlg.h"
    #include "wx/paper.h"   // wxThePrintPaperDatabase (A4 ctor default)
#endif

#include "wx/confbase.h"
#include "wx/filename.h"
#include "wx/file.h"
#include "wx/ffile.h"
#include "wx/cmdproc.h"
#include "wx/tokenzr.h"
#include "wx/filename.h"
#include "wx/stdpaths.h"
#include "wx/vector.h"
#include "wx/scopedarray.h"
#include "wx/scopeguard.h"

#if wxUSE_STD_IOSTREAM
    #include "wx/beforestd.h"
    #include <fstream>
    #include <iostream>
    #include "wx/afterstd.h"
#else
    #include "wx/wfstream.h"
#endif

#include <memory>
#include <algorithm>

// OES — unified modal routing: both desktop and web implement
// ibBackendDocFrame::ShowModalMessage. Desktop forwards to wxMessageBox
// (with frame as parent); web parks the worker on a promise + emits the
// modal via /session, resolves through /modal-reply. Replaces the per-
// callsite wxMessageDialog / wxMessageBox in OnSaveModified / OnSave-
// BeforeForceClose / Revert with a single cross-build call.
#include "backend/session/session.h"
#include "backend/backend_mainFrame.h"

#ifdef OES_USE_WEB
// Full ibWebWindow type needed by ibView::ShowFrame (calls m_webFrame->Show)
// and by ibFrontendWindow* dereferences in OnChangeFilename / Activate.
#include "frontend/web/webWindow.h"
#endif

// Used by the 5 OES overrides of inherited wx handlers that remain in this
// file (OnFileSave / OnUpdateFileSave / OnUpdateFileSaveAs / SelectDocumentPath
// / SelectDocumentType); all are cross-platform-safe. The metadata-aware
// "wiring" (Find/Replace, OpenForm, meta-template registry) lives in the
// desktop-only frontend/docView/docManager.cpp.
#include "frontend/docView/docManager.h"   // ibDocTemplate / ibMetaDocTemplate
#include "frontend/win/dlgs/choiceTemplate.h"
#include "backend/metadataConfiguration.h"

// ----------------------------------------------------------------------------
// wxWidgets macros
// ----------------------------------------------------------------------------

wxIMPLEMENT_ABSTRACT_CLASS(ibDocument, wxEvtHandler);
wxIMPLEMENT_ABSTRACT_CLASS(ibView, wxEvtHandler);
// ibDocTemplate's wxIMPLEMENT_ABSTRACT_CLASS lives in docManager.cpp alongside
// the class implementation moved out of this file.
wxIMPLEMENT_DYNAMIC_CLASS(ibDocManager, wxEvtHandler);

#ifndef OES_USE_WEB
// Desktop-only convenience classes (declared under same gate in ibDocView.h).
wxIMPLEMENT_CLASS(ibDocChildFrame, wxFrame);
wxIMPLEMENT_CLASS(ibDocParentFrame, wxFrame);
#endif

#if wxUSE_PRINTING_ARCHITECTURE
wxIMPLEMENT_DYNAMIC_CLASS(ibDocPrintout, wxPrintout);
#endif

// ============================================================================
// implementation
// ============================================================================

// FindExtension helper migrated to docManager.cpp alongside the only caller
// (ibDocTemplate::FileMatchesTemplate).

// ----------------------------------------------------------------------------
// Definition of ibDocument
// ----------------------------------------------------------------------------

ibDocument::ibDocument(ibDocument *parent)
{
    m_documentModified = false;
    m_documentTemplate = nullptr;

    m_documentParent = parent;
    if ( parent )
        parent->m_childDocuments.push_back(this);

    m_commandProcessor = nullptr;
    m_savedYet = false;
}

bool ibDocument::DeleteContents()
{
    return true;
}

ibDocument::~ibDocument()
{
    delete m_commandProcessor;

    if (GetDocumentManager())
        GetDocumentManager()->RemoveDocument(this);

    if ( m_documentParent )
        m_documentParent->m_childDocuments.remove(this);

    // Not safe to do here, since it'll invoke virtual view functions
    // expecting to see valid derived objects: and by the time we get here,
    // we've called destructors higher up.
    //DeleteAllViews();
}

bool ibDocument::CanClose()
{
    if ( !OnSaveModified() )
        return false;

    // When the parent document closes, its children must be closed as well as
    // they can't exist without the parent, so ask them too.

    for ( auto& childDoc : m_childDocuments )
    {
        if ( !childDoc->OnSaveModified() )
        {
            // Leave the parent document opened if a child can't close.
            return false;
        }
    }

    return true;
}

bool ibDocument::Close()
{
    // First check if this document itself and all its children can be closed.
    if ( !CanClose() )
        return false;

    // Step-4 collapse: IsCloseOnOwnerClose-aware cascade lifted up from
    // ibMetaDocument. Children that opt out (IsCloseOnOwnerClose == false)
    // get re-parented onto the document manager instead of being deleted
    // alongside us.
    ibDocManager* documentManager = GetDocumentManager();

    // Now that they all did, do close them: as m_childDocuments is modified as
    // we iterate over it, don't use the usual for-style iteration here.
    while ( !m_childDocuments.empty() )
    {
        ibDocument * const childDoc = m_childDocuments.front();

        if ( childDoc->IsCloseOnOwnerClose() )
        {
            // This will call OnSaveModified() once again but it shouldn't do
            // anything as the document was just saved or marked as not needing
            // to be saved by the CanClose() check above.
            if ( !childDoc->Close() )
            {
                wxFAIL_MSG( "Closing the child document unexpectedly failed "
                            "after its OnSaveModified() returned true" );
            }

            // Delete the child document by deleting all its views.
            childDoc->DeleteAllViews();
        }
        else if ( documentManager != nullptr )
        {
            // Re-parent: child stays alive on the manager. SetDocParent(nullptr)
            // removes us from base's m_childDocuments so the loop progresses.
            childDoc->SetDocParent(nullptr);
            documentManager->AddDocument(childDoc);
        }
        else
        {
            // No manager to hand the child to — fall back to closing it so
            // we don't loop forever.
            childDoc->Close();
            childDoc->DeleteAllViews();
        }
    }

    return OnCloseDocument();
}

void ibDocument::SetDocParent(ibDocument* docParent)
{
    // Step-4: generic re-parenting (was a kludge on ibMetaDocument back when
    // wxDocument's m_documentParent was private). Fork makes both
    // m_documentParent and m_childDocuments protected, so straight
    // manipulation is now correct.
    if ( docParent != nullptr )
    {
        if ( m_documentParent != nullptr )
            m_documentParent->m_childDocuments.remove(this);

        docParent->m_childDocuments.push_back(this);
        m_documentParent = docParent;
    }
    else if ( m_documentParent != nullptr )
    {
        m_documentParent->m_childDocuments.remove(this);
        m_documentParent = nullptr;
    }
}

bool ibDocument::OnCloseDocument()
{
    // Tell all views that we're about to close
    NotifyClosing();
    DeleteContents();
    Modify(false);
    return true;
}

// Note that this implicitly deletes the document when the last view is
// deleted.
bool ibDocument::DeleteAllViews()
{
    ibDocManager* manager = GetDocumentManager();

    // first check if all views agree to be closed
    const wxList::iterator end = m_documentViews.end();
    for ( wxList::iterator i = m_documentViews.begin(); i != end; ++i )
    {
        ibView *view = (ibView *)*i;
        if ( !view->Close(false) )
            return false;

        // Step-4 collapse: explicit deactivate lifted up from
        // ibMetaDocument::DeleteAllViews — clears the doc-manager's
        // currentView pointer so it doesn't dangle on the deleted view.
        view->Activate(false);
    }

    // all views agreed to close, now do close them
    if ( m_documentViews.empty() )
    {
        // normally the document would be implicitly deleted when the last view
        // is, but if don't have any views, do it here instead
        if ( manager && manager->GetDocuments().Member(this) )
            delete this;
    }
    else // have views
    {
        // as we delete elements we iterate over, don't use the usual "from
        // begin to end" loop
        for ( ;; )
        {
            ibView *view = (ibView *)*m_documentViews.begin();

            bool isLastOne = m_documentViews.size() == 1;

            // this always deletes the node implicitly and if this is the last
            // view also deletes this object itself (also implicitly, great),
            // so we can't test for m_documentViews.empty() after calling this!
            delete view;

            if ( isLastOne )
                break;
        }
    }

    return true;
}

ibView *ibDocument::GetFirstView() const
{
    if ( m_documentViews.empty() )
        return nullptr;

    return static_cast<ibView *>(m_documentViews.GetFirst()->GetData());
}

void ibDocument::Modify(bool mod)
{
    if (mod != m_documentModified)
    {
        m_documentModified = mod;

        // Allow views to append asterix to the title
        ibView* view = GetFirstView();
        if (view) view->OnChangeFilename();
    }
}

ibDocManager *ibDocument::GetDocumentManager() const
{
    // For child documents we use the same document manager as the parent, even
    // though we don't have our own template (as children are not opened/saved
    // directly).
    if ( m_documentParent )
        return m_documentParent->GetDocumentManager();

    if ( m_documentTemplate )
        return m_documentTemplate->GetDocumentManager();

    // Fall back on the global manager if the document doesn't have a template,
    // code elsewhere, notably in DeleteAllViews(), relies on the document
    // always being managed by some manager.
    return ibDocManager::GetDocumentManager();
}

bool ibDocument::OnNewDocument()
{
    // notice that there is no need to either reset nor even check the
    // modified flag here as the document itself is a new object (this is only
    // called from CreateDocument()) and so it shouldn't be saved anyhow even
    // if it is modified -- this could happen if the user code creates
    // documents pre-filled with some user-entered (and which hence must not be
    // lost) information

    SetDocumentSaved(false);

    const wxString name = GetDocumentManager()->MakeNewDocumentName();
    SetTitle(name);
    SetFilename(name, true);

    return true;
}

bool ibDocument::Save()
{
    if ( AlreadySaved() )
        return true;

    if ( m_documentFile.empty() || !m_savedYet )
        return SaveAs();

    return OnSaveDocument(m_documentFile);
}

bool ibDocument::SaveAs()
{
#ifdef OES_USE_WEB
    // Web has no wxFileSelector. File-Save-As flow needs a web-side
    // file-picker dialog (step 3+) — for now refuse the call.
    return false;
#else
    ibDocTemplate *docTemplate = GetDocumentTemplate();
    if (!docTemplate)
        return false;

#ifdef wxHAS_MULTIPLE_FILEDLG_FILTERS
    wxString filter = docTemplate->GetDescription() + wxT(" (") +
        docTemplate->GetFileFilter() + wxT(")|") +
        docTemplate->GetFileFilter();

    // Now see if there are some other template with identical view and document
    // classes, whose filters may also be used.
    if (docTemplate->GetViewClassInfo() && docTemplate->GetDocClassInfo())
    {
        wxList::compatibility_iterator
            node = docTemplate->GetDocumentManager()->GetTemplates().GetFirst();
        while (node)
        {
            ibDocTemplate *t = (ibDocTemplate*) node->GetData();

            if (t->IsVisible() && t != docTemplate &&
                t->GetViewClassInfo() == docTemplate->GetViewClassInfo() &&
                t->GetDocClassInfo() == docTemplate->GetDocClassInfo())
            {
                // add a '|' to separate this filter from the previous one
                if ( !filter.empty() )
                    filter << wxT('|');

                filter << t->GetDescription()
                       << wxT(" (") << t->GetFileFilter() << wxT(") |")
                       << t->GetFileFilter();
            }

            node = node->GetNext();
        }
    }
#else
    wxString filter = docTemplate->GetFileFilter() ;
#endif

    wxString defaultDir = docTemplate->GetDirectory();
    if ( defaultDir.empty() )
    {
        defaultDir = wxPathOnly(GetFilename());
        if ( defaultDir.empty() )
            defaultDir = GetDocumentManager()->GetLastDirectory();
    }

    wxString fileName = wxFileSelector(_("Save As"),
            defaultDir,
            wxFileNameFromPath(GetFilename()),
            docTemplate->GetDefaultExtension(),
            filter,
            wxFD_SAVE | wxFD_OVERWRITE_PROMPT,
            GetDocumentWindow());

    if (fileName.empty())
        return false; // cancelled by user

    // Files that were not saved correctly are not added to the FileHistory.
    if (!OnSaveDocument(fileName))
        return false;

    SetTitle(wxFileNameFromPath(fileName));
    SetFilename(fileName, true);    // will call OnChangeFileName automatically

    // A file that doesn't use the default extension of its document template
    // cannot be opened via the FileHistory, so we do not add it.
    if (docTemplate->FileMatchesTemplate(fileName))
    {
        GetDocumentManager()->AddFileToHistory(fileName);
    }
    //else: the user will probably not be able to open the file again, so we
    //      could warn about the wrong file-extension here

    return true;
#endif // !OES_USE_WEB
}

bool ibDocument::OnSaveDocument(const wxString& file)
{
    if ( file.empty() )
        return false;

    if ( !DoSaveDocument(file) )
        return false;

    if ( m_commandProcessor )
        m_commandProcessor->MarkAsSaved();

    Modify(false);
    SetFilename(file);
    SetDocumentSaved(true);
    return true;
}

bool ibDocument::OnOpenDocument(const wxString& file)
{
    // notice that there is no need to check the modified flag here for the
    // reasons explained in OnNewDocument()

    if ( !DoOpenDocument(file) )
        return false;

    SetFilename(file, true);

    // stretching the logic a little this does make sense because the document
    // had been saved into the file we just loaded it from, it just could have
    // happened during a previous program execution, it's just that the name of
    // this method is a bit unfortunate, it should probably have been called
    // HasAssociatedFileName()
    SetDocumentSaved(true);

    UpdateAllViews();

    return true;
}

#if wxUSE_STD_IOSTREAM
std::istream& ibDocument::LoadObject(std::istream& stream)
#else
wxInputStream& ibDocument::LoadObject(wxInputStream& stream)
#endif
{
    return stream;
}

#if wxUSE_STD_IOSTREAM
std::ostream& ibDocument::SaveObject(std::ostream& stream)
#else
wxOutputStream& ibDocument::SaveObject(wxOutputStream& stream)
#endif
{
    return stream;
}

bool ibDocument::Revert()
{
    // Unified modal (frame->ShowModalMessage). No frame → no prompt →
    // proceed (matches the desktop behavior where wxMessageBox on no
    // top-window returns wxOK silently — close enough for revert).
    if ( ibBackendDocFrame* const frame = ibSession::CurrentFrame() )
    {
        const int rc = frame->ShowModalMessage(
            _("Discard changes and reload the last saved version?"),
            wxTheApp ? wxTheApp->GetAppDisplayName() : wxString(),
            wxYES_NO | wxCANCEL | wxICON_QUESTION);
        if ( rc != wxYES )
            return false;
    }

    if ( !DoOpenDocument(GetFilename()) )
        return false;

    Modify(false);
    UpdateAllViews();

    return true;
}


// Get title, or filename if no title, else unnamed
wxString ibDocument::GetUserReadableName() const
{
    return DoGetUserReadableName();
}

wxString ibDocument::DoGetUserReadableName() const
{
    if ( !m_documentTitle.empty() )
        return m_documentTitle;

    if ( !m_documentFile.empty() )
        return wxFileNameFromPath(m_documentFile);

    return _("unnamed");
}

ibFrontendWindow *ibDocument::GetDocumentWindow() const
{
    ibView * const view = GetFirstView();
    if ( view )
        return view->GetFrame();

#ifdef OES_USE_WEB
    // No process-wide top window on web (each session has its own
    // ibWebFrame). Caller gets nullptr — web modal flow doesn't need
    // a fallback parent.
    return nullptr;
#else
    return wxTheApp->GetTopWindow();
#endif
}

wxCommandProcessor *ibDocument::OnCreateCommandProcessor()
{
    return new wxCommandProcessor;
}

// true if safe to close
bool ibDocument::OnSaveModified()
{
    if ( IsModified() )
    {
        // Unified modal — frame->ShowModalMessage works on both builds.
        // Desktop forwards to wxMessageBox; web routes via the modal
        // queue + /modal-reply round-trip. Returns the wx button code.
        // Label customisation (Save/Discard/Don't close) was desktop-
        // wxMessageDialog-specific and is dropped in this unified path;
        // step 4 may extend ibBackendDocFrame::ShowModalMessage to carry
        // custom labels if the UX loss matters.
        ibBackendDocFrame* const frame = ibSession::CurrentFrame();
        if ( !frame )
            return true;   // no frame → no prompt → treat as "ok to close"

        const int rc = frame->ShowModalMessage(
            wxString::Format(_("Do you want to save changes to %s?"),
                             GetUserReadableName()),
            wxTheApp ? wxTheApp->GetAppDisplayName() : wxString(),
            wxYES_NO | wxCANCEL | wxICON_QUESTION | wxCENTRE);
        switch ( rc )
        {
            case wxYES:    return Save();
            case wxNO:     Modify(false); break;
            case wxCANCEL: return false;
        }
    }

    return true;
}

void ibDocument::OnSaveBeforeForceClose()
{
    if ( !IsModified() )
        return;

    // Unified modal (frame->ShowModalMessage). Force-close => Yes/No
    // (no Cancel). wxMessageDialog's SetExtendedMessage /
    // SetYesNoLabels are dropped in the unified path — same step 4
    // enhancement note as OnSaveModified.
    ibBackendDocFrame* const frame = ibSession::CurrentFrame();
    if ( frame )
    {
        const wxString caption = wxTheApp ? wxTheApp->GetAppDisplayName()
                                          : wxString();
        const int rc = frame->ShowModalMessage(
            wxString::Format(
                _("Do you want to save changes to %s before closing it?"),
                GetUserReadableName()),
            caption,
            wxYES_NO | wxICON_QUESTION | wxCENTRE);
        if ( rc == wxYES )
        {
            while ( !Save() )
            {
                const int retry = frame->ShowModalMessage(
                    wxString::Format(
                        _("Saving %s failed, would you like to retry?"),
                        GetUserReadableName()),
                    caption,
                    wxYES_NO | wxICON_ERROR | wxCENTRE);
                if ( retry != wxYES )
                    break;
            }
        }
    }

    Modify(false);
}

bool ibDocument::Draw(wxDC& WXUNUSED(context))
{
    return true;
}

bool ibDocument::AddView(ibView *view)
{
    if ( !m_documentViews.Member(view) )
    {
        m_documentViews.Append(view);
        OnChangedViewList();
    }
    return true;
}

bool ibDocument::RemoveView(ibView *view)
{
    if ( !m_documentViews.DeleteObject(view) )
        return false;

    OnChangedViewList();
    return true;
}

// ibDocument::OnCreate + ibDocument::DoCreateView are defined lower in this file
// (after the frontend child-frame / visual-host includes at ~line 2426), since the
// view-creation pipeline needs ibFrontendMainFrame / ibWebFrame / the web visual
// host to be complete types.

// Called after a view is added or removed.
// The default implementation deletes the document if
// there are no more views.
void ibDocument::OnChangedViewList()
{
    if ( m_documentViews.empty() && OnSaveModified() )
        delete this;
}

void ibDocument::UpdateAllViews(ibView *sender, wxObject *hint)
{
    wxList::compatibility_iterator node = m_documentViews.GetFirst();
    while (node)
    {
        ibView *view = (ibView *)node->GetData();
        if (view != sender)
            view->OnUpdate(sender, hint);
        node = node->GetNext();
    }

    // Step-4 collapse: cascade lifted up from ibMetaDocument. Children get
    // the same update; sender stays propagated so the originating view
    // remains the de-duplication anchor across the whole subtree.
    for (ibDocument* childDoc : m_childDocuments)
        childDoc->UpdateAllViews(sender, hint);
}

void ibDocument::NotifyClosing()
{
    wxList::compatibility_iterator node = m_documentViews.GetFirst();
    while (node)
    {
        ibView *view = (ibView *)node->GetData();
        view->OnClosingDocument();
        node = node->GetNext();
    }
}

void ibDocument::SetFilename(const wxString& filename, bool notifyViews)
{
    m_documentFile = filename;
    OnChangeFilename(notifyViews);
}

void ibDocument::OnChangeFilename(bool notifyViews)
{
    if ( notifyViews )
    {
        // Notify the views that the filename has changed
        wxList::compatibility_iterator node = m_documentViews.GetFirst();
        while (node)
        {
            ibView *view = (ibView *)node->GetData();
            view->OnChangeFilename();
            node = node->GetNext();
        }
    }
}

bool ibDocument::DoSaveDocument(const wxString& file)
{
#if wxUSE_STD_IOSTREAM
    std::ofstream store(file.mb_str(), std::ios::binary);
    if ( !store )
#else
    wxFileOutputStream store(file);
    if ( store.GetLastError() != wxSTREAM_NO_ERROR )
#endif
    {
        wxLogError(_("File \"%s\" could not be opened for writing."), file);
        return false;
    }

    if (!SaveObject(store))
    {
        wxLogError(_("Failed to save document to the file \"%s\"."), file);
        return false;
    }

    return true;
}

bool ibDocument::DoOpenDocument(const wxString& file)
{
#if wxUSE_STD_IOSTREAM
    std::ifstream store(file.mb_str(), std::ios::binary);
    if ( !store )
#else
    wxFileInputStream store(file);
    if (store.GetLastError() != wxSTREAM_NO_ERROR || !store.IsOk())
#endif
    {
        wxLogError(_("File \"%s\" could not be opened for reading."), file);
        return false;
    }

#if wxUSE_STD_IOSTREAM
    LoadObject(store);
    if ( !store )
#else
    int res = LoadObject(store).GetLastError();
    if ( res != wxSTREAM_NO_ERROR && res != wxSTREAM_EOF )
#endif
    {
        wxLogError(_("Failed to read document from the file \"%s\"."), file);
        return false;
    }

    return true;
}


// ----------------------------------------------------------------------------
// Document view
// ----------------------------------------------------------------------------

ibView::ibView()
{
    m_viewDocument = nullptr;

    m_viewFrame = nullptr;

    m_docChildFrame = nullptr;
}

ibView::~ibView()
{
    if (m_viewDocument && GetDocumentManager())
        GetDocumentManager()->ActivateView(this, false);

    // reset our frame view first, before removing it from the document as
    // SetView(nullptr) is a simple call while RemoveView() may result in user
    // code being executed and this user code can, for example, show a message
    // box which would result in an activation event for m_docChildFrame and so
    // could reactivate the view being destroyed -- unless we reset it first
    if ( m_docChildFrame && m_docChildFrame->GetView() == this )
    {
        // prevent it from doing anything with us
        m_docChildFrame->SetView(nullptr);

        // it doesn't make sense to leave the frame alive if its associated
        // view doesn't exist any more so unconditionally close it as well
        //
        // notice that we only get here if m_docChildFrame is non-null in the
        // first place and it will be always nullptr if we're deleted because our
        // frame was closed, so this only catches the case of directly deleting
        // the view, as it happens if its creation fails in ibDocTemplate::
        // CreateView() for example
        m_docChildFrame->GetWindow()->Destroy();
    }

    if ( m_viewDocument )
        m_viewDocument->RemoveView(this);
}

void ibView::SetDocChildFrame(ibDocChildFrameAnyBase *docChildFrame)
{
    SetFrame(docChildFrame ? docChildFrame->GetWindow() : nullptr);
    m_docChildFrame = docChildFrame;
}

bool ibView::ShowFrame(bool show)
{
    // Step-4 collapse: lifted up from ibMetaView::ShowFrame. Desktop reveals
    // the wxAuiMDIChildFrame stashed in m_viewFrame; web reveals the
    // ibWebDocChildFrame attached via SetWebFrame after CreateChildFrame.
#ifdef OES_USE_WEB
    if (m_webFrame != nullptr) {
        m_webFrame->Show(show);
        return true;
    }
#endif
    if (m_viewFrame != nullptr && m_viewFrame->Show(show))
        return true;
    return false;
}

bool ibView::TryBefore(wxEvent& event)
{
    ibDocument * const doc = GetDocument();
    return doc && doc->ProcessEventLocally(event);
}

void ibView::OnActivateView(bool WXUNUSED(activate),
                            ibView *WXUNUSED(activeView),
                            ibView *WXUNUSED(deactiveView))
{
}

void ibView::OnPrint(wxDC *dc, wxObject *WXUNUSED(info))
{
    OnDraw(dc);
}

void ibView::OnUpdate(ibView *WXUNUSED(sender), wxObject *WXUNUSED(hint))
{
}

void ibView::OnChangeFilename()
{
    // GetFrame can return wxWindow rather than wxTopLevelWindow due to
    // generic MDI implementation so use SetLabel rather than SetTitle.
    // It should cause SetTitle() for top level windows.
    // Type-switched via ibFrontendWindow — both wxWindow and ibWebWindow
    // expose SetLabel.
    ibFrontendWindow *win = GetFrame();
    if (!win) return;

    ibDocument *doc = GetDocument();
    if (!doc) return;

    wxString label = doc->GetUserReadableName();
    if (doc->IsModified())
    {
       label += "*";
    }
    win->SetLabel(label);
}

void ibView::SetDocument(ibDocument *doc)
{
    m_viewDocument = doc;
    if (doc)
        doc->AddView(this);
}

bool ibView::Close(bool deleteWindow)
{
    return OnClose(deleteWindow);
}

// ibView::Activate is defined in docManager.cpp — it needs the desktop main
// frame headers (mainFrame / ibFrontendMainFrame) that live on that side.

bool ibView::OnClose(bool WXUNUSED(deleteWindow))
{
    return GetDocument() ? GetDocument()->Close() : true;
}

#if wxUSE_PRINTING_ARCHITECTURE
wxPrintout *ibView::OnCreatePrintout()
{
    return new ibDocPrintout(this);
}
#endif // wxUSE_PRINTING_ARCHITECTURE

// ibDocTemplate implementation moved to frontend/docView/docManager.cpp
// alongside its class declaration in docManager.h.

// ----------------------------------------------------------------------------
// ibDocManager
// ----------------------------------------------------------------------------

wxBEGIN_EVENT_TABLE(ibDocManager, wxEvtHandler)
    EVT_MENU(wxID_OPEN, ibDocManager::OnFileOpen)
    EVT_MENU(wxID_CLOSE, ibDocManager::OnFileClose)
    EVT_MENU(wxID_CLOSE_ALL, ibDocManager::OnFileCloseAll)
    EVT_MENU(wxID_REVERT, ibDocManager::OnFileRevert)
    EVT_MENU(wxID_NEW, ibDocManager::OnFileNew)
    EVT_MENU(wxID_SAVE, ibDocManager::OnFileSave)
    EVT_MENU(wxID_SAVEAS, ibDocManager::OnFileSaveAs)
    EVT_MENU(wxID_UNDO, ibDocManager::OnUndo)
    EVT_MENU(wxID_REDO, ibDocManager::OnRedo)

    // We don't know in advance how many items can there be in the MRU files
    // list so set up OnMRUFile() as a handler for all menu events and do the
    // check for the id of the menu item clicked inside it.
    EVT_MENU(wxID_ANY, ibDocManager::OnMRUFile)

    EVT_UPDATE_UI(wxID_OPEN, ibDocManager::OnUpdateFileOpen)
    EVT_UPDATE_UI(wxID_CLOSE, ibDocManager::OnUpdateDisableIfNoDoc)
    EVT_UPDATE_UI(wxID_CLOSE_ALL, ibDocManager::OnUpdateDisableIfNoDoc)
    EVT_UPDATE_UI(wxID_REVERT, ibDocManager::OnUpdateFileRevert)
    EVT_UPDATE_UI(wxID_NEW, ibDocManager::OnUpdateFileNew)
    EVT_UPDATE_UI(wxID_SAVE, ibDocManager::OnUpdateFileSave)
    EVT_UPDATE_UI(wxID_SAVEAS, ibDocManager::OnUpdateFileSaveAs)
    EVT_UPDATE_UI(wxID_UNDO, ibDocManager::OnUpdateUndo)
    EVT_UPDATE_UI(wxID_REDO, ibDocManager::OnUpdateRedo)

#if wxUSE_PRINTING_ARCHITECTURE
    EVT_MENU(wxID_PRINT, ibDocManager::OnPrint)
    EVT_MENU(wxID_PREVIEW, ibDocManager::OnPreview)
    EVT_MENU(wxID_PRINT_SETUP, ibDocManager::OnPageSetup)

    EVT_UPDATE_UI(wxID_PRINT, ibDocManager::OnUpdateDisableIfNoDoc)
    EVT_UPDATE_UI(wxID_PREVIEW, ibDocManager::OnUpdateDisableIfNoDoc)
    // NB: we keep "Print setup" menu item always enabled as it can be used
    //     even without an active document
#endif // wxUSE_PRINTING_ARCHITECTURE

    EVT_MENU(wxID_FIND, ibDocManager::OnFindDialog)
wxEND_EVENT_TABLE()

ibDocManager* ibDocManager::sm_docManager = nullptr;

ibDocManager::ibDocManager(long WXUNUSED(flags), bool initialize)
{
    sm_docManager = this;

    m_defaultDocumentNameCounter = 1;
    m_currentView = nullptr;
    m_maxDocsOpen = INT_MAX;
    m_fileHistory = nullptr;
    if ( initialize )
        Initialize();

#if wxUSE_PRINTING_ARCHITECTURE
    // OES default: A4 portrait — was set in the former ibMetaDocManager ctor.
    wxPrintData printData;
    if (wxThePrintPaperDatabase) {
        wxPrintPaperType* paper = wxThePrintPaperDatabase->FindPaperType(wxPAPER_A4);
        if (paper) {
            printData.SetPaperId(paper->GetId());
            printData.SetPaperSize(paper->GetSize());
        }
    }
    printData.SetOrientation(wxPORTRAIT);
    m_pageSetupDialogData.SetPrintData(printData);
#endif

    // Register the shared default templates (Text / Spreadsheet / Help /
    // Registration journal) — same place the original ibMetaDocManager
    // ctor did it. Body is desktop-only; web link skips the editor-view
    // CLASSINFOs.
    RegisterDefaultTemplates();
}

ibDocManager::~ibDocManager()
{
    wxDELETE(m_findDialog);
    Clear();
    delete m_fileHistory;
    sm_docManager = nullptr;
}

// closes the specified document
bool ibDocManager::CloseDocument(ibDocument* doc, bool force)
{
    if ( force )
    {
        // We need to close, but at least ask the user if the document should
        // be saved before doing it.
        doc->OnSaveBeforeForceClose();
    }
    else // Allow the user to cancel closing too.
    {
        if ( !doc->CanClose() )
            return false;
    }

    // Note that by now the document is certain not to be modified any longer.

    // Implicitly deletes the document when
    // the last view is deleted
    doc->DeleteAllViews();

    wxASSERT(!m_docs.Member(doc));

    return true;
}

bool ibDocManager::CloseDocuments(bool force)
{
    wxList::compatibility_iterator node = m_docs.GetFirst();
    while (node)
    {
        ibDocument *doc = (ibDocument *)node->GetData();
        wxList::compatibility_iterator next = node->GetNext();

        if (!CloseDocument(doc, force))
            return false;

        // This assumes that documents are not connected in
        // any way, i.e. deleting one document does NOT
        // delete another.
        node = next;
    }
    return true;
}

bool ibDocManager::Clear(bool force)
{
    if (!CloseDocuments(force))
        return false;

    m_currentView = nullptr;

    wxList::compatibility_iterator node = m_templates.GetFirst();
    while (node)
    {
        ibDocTemplate *templ = (ibDocTemplate*) node->GetData();
        wxList::compatibility_iterator next = node->GetNext();
        delete templ;
        node = next;
    }
    return true;
}

bool ibDocManager::Initialize()
{
    m_fileHistory = OnCreateFileHistory();
    return true;
}

wxString ibDocManager::GetLastDirectory() const
{
    // if we haven't determined the last used directory yet, do it now
    if ( m_lastDirectory.empty() )
    {
        // we're going to modify m_lastDirectory in this const method, so do it
        // via non-const self pointer instead of const this one
        ibDocManager * const self = const_cast<ibDocManager *>(this);

        // first try to reuse the directory of the most recently opened file:
        // this ensures that if the user opens a file, closes the program and
        // runs it again the "Open file" dialog will open in the directory of
        // the last file he used
        if ( m_fileHistory && m_fileHistory->GetCount() )
        {
            const wxString lastOpened = m_fileHistory->GetHistoryFile(0);
            const wxFileName fn(lastOpened);
            if ( fn.DirExists() )
            {
                self->m_lastDirectory = fn.GetPath();
            }
            //else: should we try the next one?
        }
        //else: no history yet

        // if we don't have any files in the history (yet?), use the
        // system-dependent default location for the document files
        if ( m_lastDirectory.empty() )
        {
            self->m_lastDirectory = wxStandardPaths::Get().GetAppDocumentsDir();
        }
    }

    return m_lastDirectory;
}

wxFileHistory *ibDocManager::OnCreateFileHistory()
{
    return new wxFileHistory;
}

void ibDocManager::OnFileClose(wxCommandEvent& WXUNUSED(event))
{
    ibDocument *doc = GetCurrentDocument();
    if (doc)
        CloseDocument(doc);
}

void ibDocManager::OnFileCloseAll(wxCommandEvent& WXUNUSED(event))
{
    CloseDocuments(false);
}

void ibDocManager::OnFileNew(wxCommandEvent& WXUNUSED(event))
{
    CreateNewDocument();
}

void ibDocManager::OnFileOpen(wxCommandEvent& WXUNUSED(event))
{
    if ( !CreateDocument(wxString()) )
    {
        OnOpenFileFailure();
    }
}

void ibDocManager::OnFileRevert(wxCommandEvent& WXUNUSED(event))
{
    ibDocument *doc = GetCurrentDocument();
    if (!doc)
        return;
    doc->Revert();
}

void ibDocManager::OnFileSave(wxCommandEvent& WXUNUSED(event))
{
    ibDocument *doc = GetCurrentDocument();
    if (!doc) {
        // OES extension: when no document is current, fall through to a
        // configuration-level save so File→Save also commits pending
        // metadata edits in Designer.
        if (activeMetaData != nullptr && activeMetaData->IsModified())
            activeMetaData->SaveDatabase();
        return;
    }
    doc->Save();
}

void ibDocManager::OnFileSaveAs(wxCommandEvent& WXUNUSED(event))
{
    ibDocument *doc = GetCurrentDocument();
    if (!doc)
        return;
    doc->SaveAs();
}

void ibDocManager::OnMRUFile(wxCommandEvent& event)
{
    if ( m_fileHistory )
    {
        // Check if the id is in the range assigned to MRU list entries.
        const int id = event.GetId();
        if ( id >= wxID_FILE1 &&
                id < wxID_FILE1 + static_cast<int>(m_fileHistory->GetCount()) )
        {
            DoOpenMRUFile(id - wxID_FILE1);

            // Don't skip the event below.
            return;
        }
    }

    event.Skip();
}

void ibDocManager::DoOpenMRUFile(unsigned n)
{
    wxString filename(GetHistoryFile(n));
    if ( filename.empty() )
        return;

    if ( wxFile::Exists(filename) )
    {
        // Try to open it but don't give an error if it failed: this could be
        // normal, e.g. because the user cancelled opening it, and we don't
        // have any useful information to put in the error message anyhow, so
        // we assume that in case of an error the appropriate message had been
        // already logged.
        (void)CreateDocument(filename, ibDOC_SILENT);
    }
    else // file doesn't exist
    {
        OnMRUFileNotExist(n, filename);
    }
}

void ibDocManager::OnMRUFileNotExist(unsigned n, const wxString& filename)
{
    // remove the file which we can't open from the MRU list
    RemoveFileFromHistory(n);

    // and tell the user about it
    wxLogError(_("The file '%s' doesn't exist and couldn't be opened.\n"
                 "It has been removed from the most recently used files list."),
               filename);
}

#if wxUSE_PRINTING_ARCHITECTURE

void ibDocManager::OnPrint(wxCommandEvent& WXUNUSED(event))
{
    ibView *view = GetAnyUsableView();
    if (!view)
        return;

    wxPrintout *printout = view->OnCreatePrintout();
    if (printout)
    {
        wxPrintDialogData printDialogData(m_pageSetupDialogData.GetPrintData());
        wxPrinter printer(&printDialogData);
        // wxPrinter::Print needs a real wxWindow* parent; view->GetFrame()
        // returns ibFrontendWindow* (ibWebWindow* on web, not a wxWindow).
        // Use the process top window — it's wxWindow* on both builds and
        // works as a generic native modal parent.
        printer.Print(wxTheApp ? wxTheApp->GetTopWindow() : nullptr,
                      printout, true);

        delete printout;
    }
}

void ibDocManager::OnPageSetup(wxCommandEvent& WXUNUSED(event))
{
    wxPageSetupDialog dlg(wxTheApp->GetTopWindow(), &m_pageSetupDialogData);
    if ( dlg.ShowModal() == wxID_OK )
    {
        m_pageSetupDialogData = dlg.GetPageSetupData();
    }
}

wxPreviewFrame* ibDocManager::CreatePreviewFrame(wxPrintPreviewBase* preview,
                                                 wxWindow *parent,
                                                 const wxString& title)
{
    return new wxPreviewFrame(preview, parent, title);
}

void ibDocManager::OnPreview(wxCommandEvent& WXUNUSED(event))
{
    wxBusyCursor busy;
    ibView *view = GetAnyUsableView();
    if (!view)
        return;

    wxPrintout *printout = view->OnCreatePrintout();
    if (printout)
    {
        wxPrintDialogData printDialogData(m_pageSetupDialogData.GetPrintData());

        // Pass two printout objects: for preview, and possible printing.
        wxPrintPreviewBase *
            preview = new wxPrintPreview(printout,
                                         view->OnCreatePrintout(),
                                         &printDialogData);
        if ( !preview->IsOk() )
        {
            delete preview;
            wxLogError(_("Print preview creation failed."));
            return;
        }

        wxPreviewFrame* frame = CreatePreviewFrame(preview,
                                                   wxTheApp->GetTopWindow(),
                                                   _("Print Preview"));
        wxCHECK_RET( frame, "should create a print preview frame" );

        frame->Centre(wxBOTH);
        frame->Initialize();
        frame->Show(true);
    }
}
#endif // wxUSE_PRINTING_ARCHITECTURE

void ibDocManager::OnUndo(wxCommandEvent& event)
{
    wxCommandProcessor * const cmdproc = GetCurrentCommandProcessor();
    if ( !cmdproc )
    {
        event.Skip();
        return;
    }

    cmdproc->Undo();
}

void ibDocManager::OnRedo(wxCommandEvent& event)
{
    wxCommandProcessor * const cmdproc = GetCurrentCommandProcessor();
    if ( !cmdproc )
    {
        event.Skip();
        return;
    }

    cmdproc->Redo();
}

// Handlers for UI update commands

void ibDocManager::OnUpdateFileOpen(wxUpdateUIEvent& event)
{
    // CreateDocument() (which is called from OnFileOpen) may succeed
    // only when there is at least a template:
    event.Enable( GetTemplates().GetCount()>0 );
}

void ibDocManager::OnUpdateDisableIfNoDoc(wxUpdateUIEvent& event)
{
    event.Enable( GetCurrentDocument() != nullptr );
}

void ibDocManager::OnUpdateFileRevert(wxUpdateUIEvent& event)
{
    ibDocument* doc = GetCurrentDocument();
    event.Enable(doc && doc->IsModified() && doc->GetDocumentSaved());
}

void ibDocManager::OnUpdateFileNew(wxUpdateUIEvent& event)
{
    // CreateDocument() (which is called from OnFileNew) may succeed
    // only when there is at least a template:
    event.Enable( GetTemplates().GetCount()>0 );
}

void ibDocManager::OnUpdateFileSave(wxUpdateUIEvent& event)
{
    // OES extension: keep File→Save enabled when there is no current document
    // but the active configuration has unsaved metadata changes — paired with
    // the same fallback in OnFileSave above.
    ibDocument * const doc = GetCurrentDocument();
    event.Enable(
        (doc && !doc->AlreadySaved()) ||
        (doc == nullptr && (activeMetaData != nullptr && activeMetaData->IsModified()))
    );
}

void ibDocManager::OnUpdateFileSaveAs(wxUpdateUIEvent& event)
{
    ibDocument * const doc = GetCurrentDocument();
    ibDocTemplate * const docTemplate = doc != nullptr ?
        doc->GetDocumentTemplate() : nullptr;

    // Normally Save-As requires a top-level document; OES allows child
    // documents whose template opts in via ibTEMPLATE_SAVE_AS_FILE
    // (e.g. external data processor / report editor windows).
    bool enable_save_as = doc && !doc->IsChildDocument();

    if (docTemplate != nullptr &&
        (docTemplate->GetFlags() & ibTEMPLATE_SAVE_AS_FILE) != 0)
        enable_save_as = true;

    event.Enable(enable_save_as);
}

void ibDocManager::OnUpdateUndo(wxUpdateUIEvent& event)
{
    wxCommandProcessor * const cmdproc = GetCurrentCommandProcessor();
    if ( !cmdproc )
    {
        // If we don't have any document at all, the menu item should really be
        // disabled.
        if ( !GetCurrentDocument() )
            event.Enable(false);
        else // But if we do have it, it might handle wxID_UNDO on its own
            event.Skip();
        return;
    }
    event.Enable(cmdproc->CanUndo());
    cmdproc->SetMenuStrings();
}

void ibDocManager::OnUpdateRedo(wxUpdateUIEvent& event)
{
    wxCommandProcessor * const cmdproc = GetCurrentCommandProcessor();
    if ( !cmdproc )
    {
        // Use same logic as in OnUpdateUndo() above.
        if ( !GetCurrentDocument() )
            event.Enable(false);
        else
            event.Skip();
        return;
    }
    event.Enable(cmdproc->CanRedo());
    cmdproc->SetMenuStrings();
}

ibView *ibDocManager::GetAnyUsableView() const
{
    ibView *view = GetCurrentView();

    if ( !view && !m_docs.empty() )
    {
        // if we have exactly one document, consider its view to be the current
        // one
        //
        // VZ: I'm not exactly sure why is this needed but this is how this
        //     code used to behave before the bug #9518 was fixed and it seems
        //     safer to preserve the old logic
        wxList::compatibility_iterator node = m_docs.GetFirst();
        if ( !node->GetNext() )
        {
            ibDocument *doc = static_cast<ibDocument *>(node->GetData());
            view = doc->GetFirstView();
        }
        //else: we have more than one document
    }

    return view;
}

bool ibDocManager::TryBefore(wxEvent& event)
{
    ibView * const view = GetAnyUsableView();
    return view && view->ProcessEventLocally(event);
}

namespace
{

// helper function: return only the visible templates
ibDocTemplateVector GetVisibleTemplates(const wxList& allTemplates)
{
    // select only the visible templates
    const size_t totalNumTemplates = allTemplates.GetCount();
    ibDocTemplateVector templates;
    if ( totalNumTemplates )
    {
        templates.reserve(totalNumTemplates);

        for ( wxList::const_iterator i = allTemplates.begin(),
                                   end = allTemplates.end();
              i != end;
              ++i )
        {
            ibDocTemplate * const temp = (ibDocTemplate *)*i;
            if ( temp->IsVisible() )
                templates.push_back(temp);
        }
    }

    return templates;
}

} // anonymous namespace

void ibDocument::Activate()
{
    ibView * const view = GetFirstView();
    if ( !view )
        return;

    view->Activate(true);
    // Type-switched via ibFrontendWindow — both wxWindow and ibWebWindow
    // expose Raise (web is a no-op stub, see ibWebWindow header).
    if ( ibFrontendWindow *win = view->GetFrame() )
        win->Raise();
}

ibDocument* ibDocManager::FindDocumentByPath(const wxString& path) const
{
    const wxFileName fileName(path);
    for ( wxList::const_iterator i = m_docs.begin(); i != m_docs.end(); ++i )
    {
        ibDocument * const doc = wxStaticCast(*i, ibDocument);

        if ( fileName == wxFileName(doc->GetFilename()) )
            return doc;
    }
    return nullptr;
}

ibDocument *ibDocManager::CreateDocument(const wxString& pathOrig, long flags)
{
    // this ought to be const but SelectDocumentType/Path() are not
    // const-correct and can't be changed as, being virtual, this risks
    // breaking user code overriding them
    ibDocTemplateVector templates(GetVisibleTemplates(m_templates));
    const size_t numTemplates = templates.size();
    if ( !numTemplates )
    {
        // no templates can be used, can't create document
        return nullptr;
    }


    // normally user should select the template to use but ibDOC_SILENT flag we
    // choose one ourselves
    wxString path = pathOrig;   // may be modified below
    ibDocTemplate *temp;
    if ( flags & ibDOC_SILENT )
    {
        wxASSERT_MSG( !path.empty(),
                      "using empty path with ibDOC_SILENT doesn't make sense" );

        temp = FindTemplateForPath(path);
        if ( !temp )
        {
            wxLogWarning(_("The format of file '%s' couldn't be determined."),
                         path);
        }
    }
    else // not silent, ask the user
    {
        // for the new file we need just the template, for an existing one we
        // need the template and the path, unless it's already specified
        if ( (flags & ibDOC_NEW) || !path.empty() )
            temp = SelectDocumentType(&templates[0], numTemplates);
        else
            temp = SelectDocumentPath(&templates[0], numTemplates, path, flags);
    }

    if ( !temp )
        return nullptr;

    // check whether the document with this path is already opened
    if ( !path.empty() )
    {
        ibDocument * const doc = FindDocumentByPath(path);
        if (doc)
        {
            // file already open, just activate it and return
            doc->Activate();
            return doc;
        }
    }

    // no, we need to create a new document


    // if we've reached the max number of docs, close the first one.
    if ( (int)GetDocuments().GetCount() >= m_maxDocsOpen )
    {
        if ( !CloseDocument((ibDocument *)GetDocuments().GetFirst()->GetData()) )
        {
            // can't open the new document if closing the old one failed
            return nullptr;
        }
    }


    // do create and initialize the new document finally
    ibDocument * const docNew = temp->CreateDocument(path, flags);
    if ( !docNew )
        return nullptr;

    docNew->SetDocumentName(temp->GetDocumentName());

    wxScopeGuard guard = wxMakeObjGuard(*docNew, &ibDocument::DeleteAllViews);

    // call the appropriate function depending on whether we're creating a
    // new file or opening an existing one
    if ( !(flags & ibDOC_NEW ? docNew->OnNewDocument()
                             : docNew->OnOpenDocument(path)) )
    {
        return nullptr;
    }

    guard.Dismiss();

    // add the successfully opened file to MRU, but only if we're going to be
    // able to reopen it successfully later which requires the template for
    // this document to be retrievable from the file extension
    if ( !(flags & ibDOC_NEW) && temp->FileMatchesTemplate(path) )
        AddFileToHistory(path);

    // at least under Mac (where views are top level windows) it seems to be
    // necessary to manually activate the new document to bring it to the
    // forefront -- and it shouldn't hurt doing this under the other platforms
    docNew->Activate();

    return docNew;
}

ibView *ibDocManager::CreateView(ibDocument *doc, long flags)
{
    ibDocTemplateVector templates(GetVisibleTemplates(m_templates));
    const size_t numTemplates = templates.size();

    if ( numTemplates == 0 )
        return nullptr;

    ibDocTemplate * const
    temp = numTemplates == 1 ? templates[0]
                             : SelectViewType(&templates[0], numTemplates);

    if ( !temp )
        return nullptr;

    ibView *view = temp->CreateView(doc, flags);
    if ( view )
        view->SetViewName(temp->GetViewName());
    return view;
}

// Not yet implemented
void
ibDocManager::DeleteTemplate(ibDocTemplate *WXUNUSED(temp), long WXUNUSED(flags))
{
}

// Not yet implemented
bool ibDocManager::FlushDoc(ibDocument *WXUNUSED(doc))
{
    return false;
}

ibDocument *ibDocManager::GetCurrentDocument() const
{
    ibView * const view = GetAnyUsableView();
    return view ? view->GetDocument() : nullptr;
}

wxCommandProcessor *ibDocManager::GetCurrentCommandProcessor() const
{
    ibDocument * const doc = GetCurrentDocument();
    return doc ? doc->GetCommandProcessor() : nullptr;
}

// Make a default name for a new document
wxString ibDocManager::MakeNewDocumentName()
{
    wxString name;

    name.Printf(_("unnamed%d"), m_defaultDocumentNameCounter);
    m_defaultDocumentNameCounter++;

    return name;
}

// Make a frame title (override this to do something different)
// If docName is empty, a document is not currently active.
wxString ibDocManager::MakeFrameTitle(ibDocument* doc)
{
    wxString appName = wxTheApp->GetAppDisplayName();
    wxString title;
    if (!doc)
        title = appName;
    else
    {
        wxString docName = doc->GetUserReadableName();
        title = docName + wxString(_(" - ")) + appName;
    }
    return title;
}


// Not yet implemented
ibDocTemplate *ibDocManager::MatchTemplate(const wxString& WXUNUSED(path))
{
    return nullptr;
}

// File history management
void ibDocManager::AddFileToHistory(const wxString& file)
{
    if (m_fileHistory)
        m_fileHistory->AddFileToHistory(file);
}

void ibDocManager::RemoveFileFromHistory(size_t i)
{
    if (m_fileHistory)
        m_fileHistory->RemoveFileFromHistory(i);
}

wxString ibDocManager::GetHistoryFile(size_t i) const
{
    wxString histFile;

    if (m_fileHistory)
        histFile = m_fileHistory->GetHistoryFile(i);

    return histFile;
}

void ibDocManager::FileHistoryUseMenu(wxMenu *menu)
{
    if (m_fileHistory)
        m_fileHistory->UseMenu(menu);
}

void ibDocManager::FileHistoryRemoveMenu(wxMenu *menu)
{
    if (m_fileHistory)
        m_fileHistory->RemoveMenu(menu);
}

#if wxUSE_CONFIG
void ibDocManager::FileHistoryLoad(const wxConfigBase& config)
{
    if (m_fileHistory)
        m_fileHistory->Load(config);
}

void ibDocManager::FileHistorySave(wxConfigBase& config)
{
    if (m_fileHistory)
        m_fileHistory->Save(config);
}
#endif

void ibDocManager::FileHistoryAddFilesToMenu(wxMenu* menu)
{
    if (m_fileHistory)
        m_fileHistory->AddFilesToMenu(menu);
}

void ibDocManager::FileHistoryAddFilesToMenu()
{
    if (m_fileHistory)
        m_fileHistory->AddFilesToMenu();
}

size_t ibDocManager::GetHistoryFilesCount() const
{
    return m_fileHistory ? m_fileHistory->GetCount() : 0;
}


// Find out the document template via matching in the document file format
// against that of the template
ibDocTemplate *ibDocManager::FindTemplateForPath(const wxString& path)
{
    ibDocTemplate *theTemplate = nullptr;

    // Find the template which this extension corresponds to
    for (size_t i = 0; i < m_templates.GetCount(); i++)
    {
        ibDocTemplate *temp = (ibDocTemplate *)m_templates.Item(i)->GetData();
        if ( temp->FileMatchesTemplate(path) )
        {
            theTemplate = temp;
            break;
        }
    }
    return theTemplate;
}

// Prompts user to open a file, using file specs in templates.
// Must extend the file selector dialog or implement own; OR
// match the extension to the template extension.

ibDocTemplate *ibDocManager::SelectDocumentPath(ibDocTemplate **templates,
                                                int noTemplates,
                                                wxString& path,
                                                long WXUNUSED(flags),
                                                bool WXUNUSED(save))
{
#ifdef wxHAS_MULTIPLE_FILEDLG_FILTERS
    // OES extension: prepend a single "OES files (filter1;filter2;...)" entry
    // so the user can browse all known extensions in one go before drilling
    // into a specific template.
    wxString descrBuf;

    for (int i = 0; i < noTemplates; i++)
    {
        if (templates[i]->IsVisible())
        {
            if (!descrBuf.empty())
                descrBuf << wxT(";");

            descrBuf << templates[i]->GetFileFilter();
        }
    }

    descrBuf = _("OES files (") + descrBuf + wxT(") |");

    for (int i = 0; i < noTemplates; i++)
    {
        if (templates[i]->IsVisible())
        {
            descrBuf << templates[i]->GetFileFilter() << wxT(";");
        }
    }

    for (int i = 0; i < noTemplates; i++)
    {
        if (templates[i]->IsVisible())
        {
            // add a '|' to separate this filter from the previous one
            if ( !descrBuf.empty() )
                descrBuf << wxT('|');

            descrBuf << templates[i]->GetDescription()
                << wxT(" (") << templates[i]->GetFileFilter() << wxT(") |")
                << templates[i]->GetFileFilter();
        }
    }
#else
    wxString descrBuf = wxT("*.*");
    wxUnusedVar(noTemplates);
#endif

    int FilterIndex = -1;

    wxString pathTmp = wxFileSelectorEx(_("Open File"),
                                        GetLastDirectory(),
                                        wxEmptyString,
                                        &FilterIndex,
                                        descrBuf,
                                        wxFD_OPEN | wxFD_FILE_MUST_EXIST);

    ibDocTemplate *theTemplate = nullptr;
    if (!pathTmp.empty())
    {
        if (!wxFileExists(pathTmp))
        {
            wxString msgTitle;
            if (!wxTheApp->GetAppDisplayName().empty())
                msgTitle = wxTheApp->GetAppDisplayName();
            else
                msgTitle = wxString(_("File error"));

            wxMessageBox(_("Sorry, could not open this file."),
                         msgTitle,
                         wxOK | wxICON_EXCLAMATION | wxCENTRE);

            path.clear();
            return nullptr;
        }

        SetLastDirectory(wxPathOnly(pathTmp));

        path = pathTmp;

        // first choose the template using the extension, if this fails (i.e.
        // wxFileSelectorEx() didn't fill it), then use the path
        if ( FilterIndex != -1 )
        {
            theTemplate = templates[FilterIndex];
            if ( theTemplate )
            {
                // But don't use this template if it doesn't match the path as
                // can happen if the user specified the extension explicitly
                // but didn't bother changing the filter.
                if ( !theTemplate->FileMatchesTemplate(path) )
                    theTemplate = nullptr;
            }
        }

        if ( !theTemplate )
            theTemplate = FindTemplateForPath(path);
        if ( !theTemplate )
        {
            // Since we do not add files with non-default extensions to the
            // file history this can only happen if the application changes the
            // allowed templates in runtime.
            wxMessageBox(_("Sorry, the format for this file is unknown."),
                         _("Open File"),
                         wxOK | wxICON_EXCLAMATION | wxCENTRE);
        }
    }
    else
    {
        path.clear();
    }

    return theTemplate;
}

ibDocTemplate *ibDocManager::SelectDocumentType(ibDocTemplate **templates,
                                                int noTemplates, bool sort)
{
    // OES extension: replace wx's text-only wxGetSingleChoiceData dialog with
    // ibDialogChoiceTemplate which presents per-template icons sourced from
    // the meta-template registry. Note the visibility check uses exact-equal
    // (`== ibTEMPLATE_VISIBLE`) rather than the masked `IsVisible()`; this
    // filters out File→Open-only templates (ibTEMPLATE_VISIBLE | ibTEMPLATE_ONLY_OPEN)
    // from the New-document chooser, which is the desired semantics here.
    wxVector<ibChoiceTemplateItem> choices;
    int n = 0;

    for (int i = 0; i < noTemplates; i++)
    {
        if (templates[i]->GetFlags() == ibTEMPLATE_VISIBLE)
        {
            bool want = true;
            for (int j = 0; j < n; j++)
            {
                // filter out NOT unique document + view combinations
                if (templates[i]->GetDocumentName() == choices[j].m_template->GetDocumentName() &&
                    templates[i]->GetViewName()     == choices[j].m_template->GetViewName())
                    want = false;
            }

            if (want)
            {
                ibDocTemplate* docTemplate = templates[i];
                ibChoiceTemplateItem entry;
                entry.m_template    = docTemplate;
                entry.m_description = docTemplate->GetDescription();

                // Icon is meta-template specific; plain ibDocTemplate has no
                // icon, so fall back to the default-initialised wxIcon.
                if (auto* mt = dynamic_cast<ibMetaDocTemplate*>(docTemplate))
                    entry.m_icon = mt->GetClassIcon();

                choices.push_back(entry);
                n++;
            }
        }
    }

    if (sort)
    {
        std::sort(choices.begin(), choices.end(),
            [](const ibChoiceTemplateItem& a, const ibChoiceTemplateItem& b) {
                return a.m_description < b.m_description;
            });
    }

    ibDocTemplate* theTemplate = nullptr;

    switch (n)
    {
        case 0:
            // no visible templates, hence nothing to choose from
            theTemplate = nullptr;
            break;

        case 1:
            // don't propose the user to choose if he has no choice
            theTemplate = choices[0].m_template;
            break;

        default:
            ibDialogChoiceTemplate dlg(choices);
            theTemplate = dlg.ShowModal() == wxID_OK ?
                dlg.GetSelectionData() : nullptr;
            break;
    }

    return theTemplate;
}

ibDocTemplate *ibDocManager::SelectViewType(ibDocTemplate **templates,
                                            int noTemplates, bool sort)
{
    wxArrayString strings;
    wxScopedArray<ibDocTemplate *> data(noTemplates);
    int i;
    int n = 0;

    for (i = 0; i < noTemplates; i++)
    {
        ibDocTemplate *templ = templates[i];
        if ( templ->IsVisible() && !templ->GetViewName().empty() )
        {
            int j;
            bool want = true;
            for (j = 0; j < n; j++)
            {
                //filter out NOT unique views
                if ( templates[i]->m_viewTypeName == data[j]->m_viewTypeName )
                    want = false;
            }

            if ( want )
            {
                strings.Add(templ->m_viewTypeName);
                data[n] = templ;
                n ++;
            }
        }
    }

    if (sort)
    {
        strings.Sort(); // ascending sort
        // Yes, this will be slow, but template lists
        // are typically short.
        int j;
        n = strings.Count();
        for (i = 0; i < n; i++)
        {
            for (j = 0; j < noTemplates; j++)
            {
                if (strings[i] == templates[j]->m_viewTypeName)
                    data[i] = templates[j];
            }
        }
    }

    ibDocTemplate *theTemplate;

    // the same logic as above
    switch ( n )
    {
        case 0:
            theTemplate = nullptr;
            break;

        case 1:
            theTemplate = data[0];
            break;

        default:
            theTemplate = (ibDocTemplate *)wxGetSingleChoiceData
                          (
                            _("Select a document view"),
                            _("Views"),
                            strings,
                            (void **)data.get()
                          );

    }

    return theTemplate;
}

void ibDocManager::AssociateTemplate(ibDocTemplate *temp)
{
    if (!m_templates.Member(temp))
        m_templates.Append(temp);
}

void ibDocManager::DisassociateTemplate(ibDocTemplate *temp)
{
    m_templates.DeleteObject(temp);
}

ibDocTemplate* ibDocManager::FindTemplate(const wxClassInfo* classinfo)
{
   for ( wxList::compatibility_iterator node = m_templates.GetFirst();
         node;
         node = node->GetNext() )
   {
      ibDocTemplate* t = wxStaticCast(node->GetData(), ibDocTemplate);
      if ( t->GetDocClassInfo() == classinfo )
         return t;
   }

   return nullptr;
}

// Add and remove a document from the manager's list
void ibDocManager::AddDocument(ibDocument *doc)
{
    if (!m_docs.Member(doc))
        m_docs.Append(doc);
}

void ibDocManager::RemoveDocument(ibDocument *doc)
{
    m_docs.DeleteObject(doc);
}

// Views or windows should inform the document manager
// when a view is going in or out of focus
void ibDocManager::ActivateView(ibView *view, bool activate)
{
    if ( activate )
    {
        m_currentView = view;
    }
    else // deactivate
    {
        if ( m_currentView == view )
        {
            // don't keep stale pointer
            m_currentView = nullptr;
        }
    }
}

// ----------------------------------------------------------------------------
// ibDocChildFrameAnyBase
// ----------------------------------------------------------------------------

bool ibDocChildFrameAnyBase::TryProcessEvent(wxEvent& event)
{
    if ( !m_childView )
    {
        // We must be being destroyed, don't forward events anywhere as
        // m_childDocument could be invalid by now.
        return false;
    }

    // Store a (non-owning) pointer to the last processed event here to be able
    // to recognize this event again if it bubbles up to the parent frame, see
    // the code in ibDocParentFrameAnyBase::TryProcessEvent().
    m_lastEvent = &event;

    // Forward the event to the document manager which will, in turn, forward
    // it to its active view which must be our m_childView.
    //
    // Notice that we do things in this roundabout way to guarantee the correct
    // event handlers call order: first the document, then the view and then the
    // document manager itself. And if we forwarded the event directly to the
    // view, then the document manager would do it once again when we forwarded
    // it to it.
    return m_childDocument->GetDocumentManager()->ProcessEventLocally(event);
}

bool ibDocChildFrameAnyBase::CloseView(wxCloseEvent& event)
{
    if ( m_childView )
    {
        // notice that we must call ibView::Close() and OnClose() called from
        // it in any case, even if we know that we are going to close anyhow
        if ( !m_childView->Close(false) && event.CanVeto() )
        {
            event.Veto();
            return false;
        }

        m_childView->Activate(false);

        // it is important to reset m_childView frame pointer to nullptr before
        // deleting it because while normally it is the frame which deletes the
        // view when it's closed, the view also closes the frame if it is
        // deleted directly not by us as indicated by its doc child frame
        // pointer still being set
        m_childView->SetDocChildFrame(nullptr);
        wxDELETE(m_childView);
    }

    m_childDocument = nullptr;

    return true;
}

// ----------------------------------------------------------------------------
// ibDocParentFrameAnyBase
//
// Mixin used by both desktop (ibFrontendMainFrame) and web (ibWebFrame)
// to gain a doc-manager slot + event-forwarding helper. The concrete
// template `ibDocParentFrameAny<TBase>` is desktop-only and not used by
// the web frame.
// ----------------------------------------------------------------------------

bool ibDocParentFrameAnyBase::TryProcessEvent(wxEvent& event)
{
    if ( !m_docManager )
        return false;

    // If we have an active view, its associated child frame may have
    // already forwarded the event to ibDocManager, check for this:
    if ( ibView* const view = m_docManager->GetAnyUsableView() )
    {
        ibDocChildFrameAnyBase* const childFrame = view->GetDocChildFrame();
        if ( childFrame && childFrame->HasAlreadyProcessed(event) )
            return false;
    }

    // But forward the event to ibDocManager ourselves if there are no views at
    // all or if this event hadn't been sent to the child frame previously.
    return m_docManager->ProcessEventLocally(event);
}

// ----------------------------------------------------------------------------
// Printing support
// ----------------------------------------------------------------------------

#if wxUSE_PRINTING_ARCHITECTURE

namespace
{

wxString GetAppropriateTitle(const ibView *view, const wxString& titleGiven)
{
    wxString title(titleGiven);
    if ( title.empty() )
    {
        if ( view && view->GetDocument() )
            title = view->GetDocument()->GetUserReadableName();
        else
            title = _("Printout");
    }

    return title;
}

} // anonymous namespace

ibDocPrintout::ibDocPrintout(ibView *view, const wxString& title)
             : wxPrintout(GetAppropriateTitle(view, title))
{
    m_printoutView = view;
}

bool ibDocPrintout::OnPrintPage(int WXUNUSED(page))
{
    wxDC *dc = GetDC();

    // Get the logical pixels per inch of screen and printer
    int ppiScreenX, ppiScreenY;
    GetPPIScreen(&ppiScreenX, &ppiScreenY);
    wxUnusedVar(ppiScreenY);
    int ppiPrinterX, ppiPrinterY;
    GetPPIPrinter(&ppiPrinterX, &ppiPrinterY);
    wxUnusedVar(ppiPrinterY);

    // This scales the DC so that the printout roughly represents
    // the screen scaling. The text point size _should_ be the right size
    // but in fact is too small for some reason. This is a detail that will
    // need to be addressed at some point but can be fudged for the
    // moment.
    double scale = double(ppiPrinterX) / ppiScreenX;

    // Now we have to check in case our real page size is reduced
    // (e.g. because we're drawing to a print preview memory DC)
    int pageWidth, pageHeight;
    int w, h;
    dc->GetSize(&w, &h);
    GetPageSizePixels(&pageWidth, &pageHeight);
    wxUnusedVar(pageHeight);

    // If printer pageWidth == current DC width, then this doesn't
    // change. But w might be the preview bitmap width, so scale down.
    double overallScale = scale * w / pageWidth;
    dc->SetUserScale(overallScale, overallScale);

    if (m_printoutView)
    {
        m_printoutView->OnDraw(dc);
    }
    return true;
}

bool ibDocPrintout::HasPage(int pageNum)
{
    return (pageNum == 1);
}

bool ibDocPrintout::OnBeginDocument(int startPage, int endPage)
{
    if (!wxPrintout::OnBeginDocument(startPage, endPage))
        return false;

    return true;
}

void ibDocPrintout::GetPageInfo(int *minPage, int *maxPage,
                                int *selPageFrom, int *selPageTo)
{
    *minPage = 1;
    *maxPage = 1;
    *selPageFrom = 1;
    *selPageTo = 1;
}

#endif // wxUSE_PRINTING_ARCHITECTURE

// ----------------------------------------------------------------------------
// Permits compatibility with existing file formats and functions that
// manipulate files directly
// ----------------------------------------------------------------------------

#if wxUSE_STD_IOSTREAM

bool ibTransferFileToStream(const wxString& filename, std::ostream& stream)
{
#if wxUSE_FFILE
    wxFFile file(filename, wxT("rb"));
#elif wxUSE_FILE
    wxFile file(filename, wxFile::read);
#endif
    if ( !file.IsOpened() )
        return false;

    do
    {
        char buf[4096];
        size_t nRead;
        nRead = file.Read(buf, WXSIZEOF(buf));
        if ( file.Error() )
            return false;

        stream.write(buf, nRead);
        if ( !stream )
            return false;
    }
    while ( !file.Eof() );

    return true;
}

bool ibTransferStreamToFile(std::istream& stream, const wxString& filename)
{
#if wxUSE_FFILE
    wxFFile file(filename, wxT("wb"));
#elif wxUSE_FILE
    wxFile file(filename, wxFile::write);
#endif
    if ( !file.IsOpened() )
        return false;

    char buf[4096];
    do
    {
        stream.read(buf, WXSIZEOF(buf));
        if ( !stream.bad() ) // fail may be set on EOF, don't use operator!()
        {
            if ( !file.Write(buf, stream.gcount()) )
                return false;
        }
    }
    while ( !stream.eof() );

    return true;
}

#else // !wxUSE_STD_IOSTREAM

bool ibTransferFileToStream(const wxString& filename, wxOutputStream& stream)
{
#if wxUSE_FFILE
    wxFFile file(filename, wxT("rb"));
#elif wxUSE_FILE
    wxFile file(filename, wxFile::read);
#endif
    if ( !file.IsOpened() )
        return false;

    char buf[4096];

    size_t nRead;
    do
    {
        nRead = file.Read(buf, WXSIZEOF(buf));
        if ( file.Error() )
            return false;

        stream.Write(buf, nRead);
        if ( !stream )
            return false;
    }
    while ( !file.Eof() );

    return true;
}

bool ibTransferStreamToFile(wxInputStream& stream, const wxString& filename)
{
#if wxUSE_FFILE
    wxFFile file(filename, wxT("wb"));
#elif wxUSE_FILE
    wxFile file(filename, wxFile::write);
#endif
    if ( !file.IsOpened() )
        return false;

    char buf[4096];
    for ( ;; )
    {
        stream.Read(buf, WXSIZEOF(buf));

        const size_t nRead = stream.LastRead();
        if ( !nRead )
        {
            if ( stream.Eof() )
                break;

            return false;
        }

        if ( !file.Write(buf, nRead) )
            return false;
    }

    return true;
}

#endif // wxUSE_STD_IOSTREAM/!wxUSE_STD_IOSTREAM


// ============================================================================
//   OES adapter — additional includes needed by ibMetaDocument / ibMetaView
//   impls (appData, metadata, host frame, web host).
// ============================================================================

#include "backend/appData.h"
#include "backend/metaCollection/metaObject.h"

#ifdef OES_USE_WEB
#include "frontend/web/webFrame.h"
#include "frontend/web/webChildFrame.h"
#include "frontend/web/webWindow.h"
#include "frontend/visualView/visualHostClient.h"
#else
#include "frontend/mainFrame/mainFrame.h"
#endif

#include <wx/scopedptr.h>
#include "docManager.h"
#include "backend/metadataConfiguration.h"
#include <wx/dlist.h>


wxIMPLEMENT_CLASS(ibMetaDocument, ibDocument);

wxIMPLEMENT_CLASS(ibMetaDataDocument, ibMetaDocument);
wxIMPLEMENT_CLASS(ibValueModuleDocument, ibMetaDocument);

wxIMPLEMENT_CLASS(ibMetaView, ibView);

// ShowFrame() lifted to ibView in step-4 collapse.

//******************************************************************************
//*                            Document implementation                         *
//******************************************************************************

// Default view factory: pull the view class from the document template (the
// wx-style path every templated doc uses). Template-less docs override this.
ibView* ibDocument::DoCreateView()
{
	ibDocTemplate* docTemplate = GetDocumentTemplate();
	if (docTemplate == nullptr)
		return nullptr;
	wxClassInfo* viewClassInfo = docTemplate->GetViewClassInfo();
	if (viewClassInfo == nullptr)
		return nullptr;
	return static_cast<ibView*>(viewClassInfo->CreateObject());
}

wxString ibMetaDocument::GetModuleName() const
{
	if (m_metaObject)
		return m_metaObject->GetFullName();
	return wxString();
}

ibMetaDocument::ibMetaDocument(ibMetaDocument* docParent) :
	ibDocument(docParent),   // base handles m_documentParent + push to parent's m_childDocuments
	m_metaObject(nullptr),
	m_childDoc(true)
{
	m_documentModified = false;
}

// dtor — base's ~ibDocument removes us from m_documentParent->m_childDocuments,
// no adapter-side work needed after the shadow-field cleanup.


// Unified doc/view creation pipeline. Lifted up from ibMetaDocument so any
// document — templated (meta editors, Text/Help/AuditLog) or template-less
// (ibFormVisualDocument, which overrides DoCreateView) — gets a view + child
// frame without needing a document template. Replaces the old one-liner that
// delegated to ibDocTemplate::CreateView.
bool ibDocument::OnCreate(const wxString& WXUNUSED(path), long flags)
{
	if (ibSession::IsCurrentForceExit())
		return false;

	wxScopedPtr<ibView> view(DoCreateView());
	if (!view)
		return false;

	view->SetDocument(this);

	// Where the view's frame comes from. A document COMPOSED by its parent (the home page: a
	// pane of the splitter dividing its frame) takes the window the parent hands it and spawns
	// no tab — one question, asked through the doc parent. Everyone else gets a child frame
	// from the transport's factory: desktop hits ibFrontendMainFrame (ibAuiDocChildFrame
	// inside an AUI MDI parent), web hits ibWebFrame (ibWebDocChildFrame parked in the
	// session's m_tabs). Both sides have matching static factory signatures so only the
	// class-qualifier differs.
	ibFrontendWindow* const composedWindow = GetComposedWindow();

#ifdef OES_USE_WEB
	ibFrontendWindow* childFrame = nullptr;
	if (composedWindow != nullptr)
		view->SetFrame(composedWindow);
	else
		childFrame = ibWebFrame::CreateChildFrame(view.get(), wxDefaultPosition, wxDefaultSize, 0);
#else
	if (composedWindow != nullptr) {
		view->SetFrame(composedWindow);
	}
	else {
		bool createModal = false;
		for (wxWindow* window : wxTopLevelWindows) {
			if (window->IsKindOf(CLASSINFO(wxDialog))) {
				if (((wxDialog*)window)->IsModal()) {
					createModal = true; break;
				}
			}
		}

		long style = wxDEFAULT_FRAME_STYLE;
		if (createModal) style = style | wxCREATE_SDI_FRAME;

		ibFrontendMainFrame::CreateChildFrame(view.get(), wxDefaultPosition, wxDefaultSize, style);
	}
#endif

	if (!view->OnCreate(this, flags))
		return false;

#ifdef OES_USE_WEB
	// Web: view's OnCreate built the ibVisualHostClient; wire it into
	// the tab's ibWebWindow subtree so JSON serialization finds it.
	// Host ownership stays with the view (m_visualHost, deleted by
	// its dtor); this is a non-owning parent edge. ~ibWebDocChildFrame
	// detaches the host before the view dies to avoid a dangling
	// m_children walk in the base ~ibWebWindow.
	if (auto* editView = dynamic_cast<ibFormVisualEditView*>(view.get())) {
		if (auto* host = editView->GetVisualHost()) {
			if (auto* tab = dynamic_cast<ibWebDocChildFrame*>(childFrame)) {
				host->SetParent(tab);
			}
		}
	}
#endif
	// Unified ShowFrame — the explicit "make it visible" trigger.
	// Desktop: reveals the ibAuiDocChildFrame. Web: base is a no-op
	// (m_viewFrame is null), subclasses may override to activate
	// the owning tab. An embedded view's window is already up, so this
	// simply reports "nothing to do" there.
	view->ShowFrame();
	return view.release() != nullptr;
}

bool ibMetaDocument::OnSaveModified()
{
	if (ibSession::IsCurrentForceExit())
		return true;

	if (m_metaObject != nullptr)
		return true;

	return ibDocument::OnSaveModified();
}

bool ibMetaDocument::OnSaveDocument(const wxString& filename)
{
	if (ibSession::IsCurrentForceExit())
		return false;

	if (m_metaObject != nullptr)
		return true;
	
	return ibDocument::OnSaveDocument(filename);
}


bool ibMetaDocument::OnCloseDocument()
{
#ifndef OES_USE_WEB
	// docManager is the desktop-wide ibDocManager singleton; on web
	// there's no such global (wfrontend.dll doesn't construct one),
	// and the session's tab list owns the docs directly, so
	// RemoveDocument has no counterpart here.
	if (m_documentParent != nullptr) {
		docManager->RemoveDocument(this);
	}
#endif

	ibBackendMetadataTree* metaTree =
		m_metaObject != nullptr ? m_metaObject->GetMetaDataTree() : nullptr;

#ifndef OES_USE_WEB
	// objectInspector is the designer's property panel (not in wfrontend.dll)
	// and Activate() would bring its tree-ctrl into focus. Neither is
	// meaningful on web, so the selection/activation side-effect is skipped.
	//
	// Clear the inspector UNCONDITIONALLY before DeleteContents() tears this document's contents down. The
	// panel may still be showing an object that dies with this document — e.g. a tablebox column holding a
	// raw back-pointer to the form we're about to delete; its next repaint would reach through a dangling
	// pointer (use-after-free — the inspector paints a corpse). This is the ONE global close point, so every
	// editor kind (form / module / document) drops its inspector content here. Then re-focus the metaTree.
	objectInspector->SelectObject(nullptr);
	if (metaTree != nullptr)
		metaTree->Activate();
#else
	(void)metaTree;
#endif

	// Tell all views that we're about to close
	NotifyClosing();
	DeleteContents();
	return true;
}

bool ibMetaDocument::IsModified() const
{
	if (m_metaObject != nullptr)
		return false;
	return ibDocument::IsModified();
}

void ibMetaDocument::Modify(bool modify)
{
	if (!ibSession::IsCurrentForceExit()) {
		
		if (m_metaObject != nullptr) {
			ibMetaData* metaData = m_metaObject->GetMetaData();
			if (metaData != nullptr) {
				metaData->Modify(modify);
			}
		}
		else if (modify != m_documentModified) {
			m_documentModified = modify;
			// Allow views to append asterix to the title
			ibView* view = GetFirstView();
			if (view) {
				view->OnChangeFilename();
			}
		}
	}
}

bool ibMetaDocument::Save()
{
	if (!ibSession::IsCurrentForceExit()) {

		if (AlreadySaved())
			return true;

		if (m_documentParent != nullptr &&
			!m_documentParent->Save()) {
			return false;
		}

		if ((m_documentParent == nullptr && m_metaObject != nullptr) && IsChildDocument()) {
			if (activeMetaData->SaveDatabase()) return false;
		}

		if (m_documentFile.IsEmpty() ||
			!m_savedYet) {
			return SaveAs();
		}

		return OnSaveDocument(m_documentFile);
	}

	return false; 
}

bool ibMetaDocument::SaveAs()
{
	if (!ibSession::IsCurrentForceExit()) {
	
		if (m_metaObject != nullptr)
			return true;

		return ibDocument::SaveAs();
	}

	return false;
}


// Close / UpdateAllViews / DeleteAllViews lifted to ibDocument in step-4
// collapse — IsCloseOnOwnerClose cascade, child-doc UpdateAllViews fan-out,
// and the Close(false) + Activate(false) sequence all live on the base now.
//
// The shadow fields `ibMetaDocument::m_documentParent` (type ibMetaDocument*)
// and `ibMetaDocument::m_childDocs` (wxDList<ibMetaDocument>) were also
// removed; iteration uses base's `m_childDocuments` (std::list<ibDocument*>).
// The `GetChild()` typed wrapper on ibMetaDocument stays for callers that
// still want wxDList<ibMetaDocument> view of the children.

#if 0   // historical adapter impl — kept commented for git-blame readability
        // until the step-4 collapse soaks. Delete after a green build.
bool ibMetaDocument::Close()
{
	if (!OnSaveModified())
		return false;
	ibDocManager* documentManager = GetDocumentManager();
	while (!m_childDocs.empty()) {
		ibMetaDocument* const childDoc = m_childDocs.front();
		if (childDoc->IsCloseOnOwnerClose()) {
			if (!childDoc->Close()) return false;
			childDoc->DeleteAllViews();
		}
		else {
			if (documentManager != nullptr) {
				childDoc->SetDocParent(nullptr);
				documentManager->AddDocument(childDoc);
			}
		}
	}
	return OnCloseDocument();
}
#endif   // historical adapter impl

#endif // wxUSE_DOC_VIEW_ARCHITECTURE

/////////////////////////////////////////////////////////////////////////////
// Name:        frmserver/docView/docView.cpp
// Purpose:     OES Doc/View fork — copied from wx/src/common/docview.cpp,
//              renamed wx*→ib*. Originally:
//                Author:      Julian Smart
//                Modified by: Vadim Zeitlin
//                Created:     01/02/97
//                Copyright:   (c) Julian Smart
//                Licence:     wxWindows licence
// OES notes:   The server side of the form runtime: documents, views and the
//              frames they show in are server objects with no windows (see
//              docView.h).
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
    #include "wx/intl.h"
    #include "wx/log.h"
#endif

#include "wx/filename.h"
#include "wx/file.h"
#include "wx/ffile.h"
#include "wx/tokenzr.h"
#include "wx/vector.h"
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

// OES — a question to the person goes through the session's frame
// (ibBackendDocFrame::ShowModalMessage): it hands the question to its client
// and waits for the answer. Asked by OnSaveModified / OnSaveBeforeForceClose /
// Revert.
#include "backend/session/session.h"
#include "backend/backend_mainFrame.h"

// A document's file is the session's temporary file (DoOpenDocument / DoSaveDocument).
#include "backend/temp/tempStorage.h"

// The full template types the manager walks (SelectDocumentType / SelectViewType /
// FindTemplateForPath). The metadata-aware wiring (OpenForm, the meta-template
// registry) lives in docManager.cpp.
#include "frmserver/docView/docManager.h"   // ibDocTemplate / ibMetaDocTemplate
#include "backend/metadataConfiguration.h"

// ----------------------------------------------------------------------------
// wxWidgets macros
// ----------------------------------------------------------------------------

wxIMPLEMENT_ABSTRACT_CLASS(ibDocument, wxObject);
wxIMPLEMENT_ABSTRACT_CLASS(ibView, wxObject);
// ibDocTemplate's wxIMPLEMENT_ABSTRACT_CLASS lives in docManager.cpp alongside
// the class implementation moved out of this file.
wxIMPLEMENT_DYNAMIC_CLASS(ibDocManager, wxObject);

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

    // Fall back on the current session's manager if the document doesn't have
    // a template, code elsewhere, notably in DeleteAllViews(), relies on the
    // document always being managed by some manager.
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
    // A new file name is the person's to choose, and choosing a file is the
    // client's dialog, not the server's: asked without a name, the document
    // refuses. A name the client supplies is saved through OnSaveDocument.
    return false;
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
    // Asked through the session's frame (ShowModalMessage). No frame → no one
    // to ask → proceed.
    if ( ibBackendDocFrame* const frame = ibSession::CurrentFrame() )
    {
        const int rc = frame->ShowModalMessage(
            _("Discard changes and reload the last saved version?"),
            wxString(),   // the client's own caption
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

wxString ibDocument::GetSaveFilter() const
{
    const ibDocTemplate* const docTemplate = GetDocumentTemplate();
    if ( docTemplate == nullptr || docTemplate->GetDefaultExtension().empty() )
        return wxString();
    return docTemplate->GetDescription() + wxT(" (") + docTemplate->GetFileFilter() + wxT(")|") + docTemplate->GetFileFilter();
}

wxString ibDocument::DoGetUserReadableName() const
{
    if ( !m_documentTitle.empty() )
        return m_documentTitle;

    // A temporary file's id — the name the file came under (ibTempFile); any other, its path's last part.
    if ( !m_documentFile.empty() )
    {
        const wxString name = ibTempFile(m_documentFile).GetName();
        return !name.empty() ? name : wxFileNameFromPath(m_documentFile);
    }

    return _("unnamed");
}

ibDocChildFrameAnyBase *ibDocument::GetDocumentWindow() const
{
    ibView * const view = GetFirstView();
    return view ? view->GetFrame() : nullptr;
}

ibCommandProcessor *ibDocument::OnCreateCommandProcessor()
{
    return new ibCommandProcessor;
}

// true if safe to close
bool ibDocument::OnSaveModified()
{
    if ( IsModified() )
    {
        // Asked through the session's frame: ShowModalMessage hands the
        // question to its client and returns the wx button code.
        ibBackendDocFrame* const frame = ibSession::CurrentFrame();
        if ( !frame )
            return true;   // no frame → no prompt → treat as "ok to close"

        const int rc = frame->ShowModalMessage(
            wxString::Format(_("Do you want to save changes to %s?"),
                             GetUserReadableName()),
            wxString(),   // the client's own caption
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

    // Asked through the session's frame, as in OnSaveModified. A forced
    // close cannot be cancelled, so the question is Yes/No.
    ibBackendDocFrame* const frame = ibSession::CurrentFrame();
    if ( frame )
    {
        const int rc = frame->ShowModalMessage(
            wxString::Format(
                _("Do you want to save changes to %s before closing it?"),
                GetUserReadableName()),
            wxString(),   // the client's own caption
            wxYES_NO | wxICON_QUESTION | wxCENTRE);
        if ( rc == wxYES )
        {
            while ( !Save() )
            {
                const int retry = frame->ShowModalMessage(
                    wxString::Format(
                        _("Saving %s failed, would you like to retry?"),
                        GetUserReadableName()),
                    wxString(),   // the client's own caption
                    wxYES_NO | wxICON_ERROR | wxCENTRE);
                if ( retry != wxYES )
                    break;
            }
        }
    }

    Modify(false);
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

// ibDocument::OnCreate + ibDocument::DoCreateView are defined lower in this file,
// after the session frame's include: the view-creation pipeline asks it for a tab
// (ibClientFrame::CreateChildFrame).

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

// ⭐ A DOCUMENT'S FILE IS THE SESSION'S TEMPORARY FILE — given by its id in the temporary storage
// (ibTempFile), where the desktop gave a path on disk. The client put it there, or takes it from there once
// the document has saved; LoadObject / SaveObject see a stream over it, one part in memory at a time.
#if !wxUSE_STD_IOSTREAM
    #error "a document's file is read and written as a std stream over the temporary storage"
#endif

bool ibDocument::DoSaveDocument(const wxString& file)
{
    ibTempFile temp;
    if ( !temp.Create(file, true) )
    {
        ibJournalError(wxT("docview"), _("File \"%s\" could not be opened for writing."), file);
        return false;
    }

    ibTempFileWriter writer(temp);
    std::ostream store(&writer);
    if ( !SaveObject(store) || !writer.Finish() )
    {
        ibJournalError(wxT("docview"), _("Failed to save document to the file \"%s\"."), file);
        return false;
    }

    return true;
}

bool ibDocument::DoOpenDocument(const wxString& file)
{
    const ibTempFile temp(file);
    if ( !temp.IsOpened() )
    {
        ibJournalError(wxT("docview"), _("File \"%s\" could not be opened for reading."), file);
        return false;
    }

    ibTempFileReader reader(temp);
    std::istream store(&reader);
    LoadObject(store);
    if ( !store )
    {
        ibJournalError(wxT("docview"), _("Failed to read document from the file \"%s\"."), file);
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
    // code being executed and this user code can, for example, ask the person
    // a question which could reactivate the view being destroyed through
    // m_docChildFrame -- unless we reset it first
    if ( m_docChildFrame && m_docChildFrame->GetView() == this )
    {
        // prevent it from doing anything with us — and with the document it showed through us, which may go
        // with us (the last view)
        m_docChildFrame->SetView(nullptr);
        m_docChildFrame->SetDocument(nullptr);

        // ⚠ THE VIEW DOES NOT DELETE ITS FRAME. The tab is owned by the session's frame, not by
        // its view: deleting it here would free a tab its owner still holds, and the owner would
        // free it again — closing any form tab took the whole server down. It CLOSES it, as the
        // desktop's view destroyed its frame: the tab is marked, and the owner deletes it — a form
        // closed with the form that owns it (ibDocument::Close) leaves no tab behind.
        m_docChildFrame->Close();
    }

    if ( m_viewDocument )
        m_viewDocument->RemoveView(this);
}

void ibView::SetDocChildFrame(ibDocChildFrameAnyBase *docChildFrame)
{
    SetFrame(docChildFrame);
    m_docChildFrame = docChildFrame;
}

bool ibView::ShowFrame(bool show)
{
    // Step-4 collapse: lifted up from ibMetaView::ShowFrame. Only the view's
    // own tab is shown from here; a composed view shows in its parent's frame.
    return m_docChildFrame != nullptr && m_docChildFrame->Show(show);
}

void ibView::OnActivateView(bool WXUNUSED(activate),
                            ibView *WXUNUSED(activeView),
                            ibView *WXUNUSED(deactiveView))
{
}

void ibView::OnUpdate(ibView *WXUNUSED(sender), wxObject *WXUNUSED(hint))
{
}

void ibView::OnChangeFilename()
{
    // Nothing to push: a frame is not a window with a label. The tab's title is
    // a reading of the document — its title, and "*" while it is modified —
    // made when the frame is drawn (ibClientChildFrame::GetTitle).
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

void ibView::Activate(bool activate)
{
    // Not the `docManager` macro — the view's owning manager first, the
    // current session's only when the view has no document.
    ibDocManager* const mgr = m_viewDocument != nullptr ?
        m_viewDocument->GetDocumentManager() : ibDocManager::GetDocumentManager();

    if ( mgr != nullptr )
    {
        OnActivateView(activate, this, mgr->GetCurrentView());
        mgr->ActivateView(this, activate);
    }

    if (activate) ibJournalInfo(wxT("docview"), "! <debug> activate view %s", GetViewName());
    else ibJournalInfo(wxT("docview"), "! <debug> deactivate view %s", GetViewName());
}

bool ibView::OnClose(bool WXUNUSED(deleteWindow))
{
    return GetDocument() ? GetDocument()->Close() : true;
}

// ibDocTemplate implementation moved to frmserver/docView/docManager.cpp
// alongside its class declaration in docManager.h.

// ----------------------------------------------------------------------------
// ibDocManager
// ----------------------------------------------------------------------------

// The ctor — the base set of templates and schemas it registers — is in docManager.cpp, beside them.

ibDocManager::~ibDocManager()
{
    Clear();
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
    // the last view is deleted — unless a view REFUSES, and one does: a form editor whose
    // module does not compile keeps its document open (ibFormDocument::OnCloseDocument ->
    // SyntaxControl). That refusal is the point, so report it instead of asserting the
    // document is gone; the caller — a tab's close, the session's close — has to stop too.
    if ( !doc->DeleteAllViews() )
        return false;

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
    return true;
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
    // Raise the frame the view shows in: its tab, or for a composed document
    // the parent's.
    if ( ibDocChildFrameAnyBase *frame = view->GetFrame() )
        frame->Show();
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

ibDocument* ibDocument::FindChildDocument(const wxString& identifier) const
{
    if ( identifier.empty() )
        return nullptr;

    for ( ibDocument* child : m_childDocuments )
        if ( child != nullptr && child->GetUniqueIdentifier() == identifier )
            return child;

    return nullptr;
}

ibDocument* ibDocManager::FindDocumentByIdentifier(const wxString& identifier) const
{
    if ( identifier.empty() )
        return nullptr;

    // ⚠ THROUGH THE TYPED VIEW, not over m_docs with a wxStaticCast at every step. The list is a
    // wxList of wxObject and every walk of it used to open with a cast; GetDocumentsVector is that
    // cast, done once (Max, 2026-09-01: *"I do not like that there are so many casts"*).
    for ( ibDocument* doc : GetDocumentsVector() )
        if ( doc != nullptr && doc->GetUniqueIdentifier() == identifier )
            return doc;

    return nullptr;
}

ibMetaDocument* ibDocManager::FindOpenDocument(ibValueMetaObject* metaObject) const
{
    if ( metaObject == nullptr )
        return nullptr;

    return dynamic_cast<ibMetaDocument*>(FindDocumentByIdentifier(metaObject->GetGuid()));
}

ibDocument *ibDocManager::CreateDocument(const wxString& path, long flags)
{
    // this ought to be const but SelectDocumentType() is not const-correct
    // and can't be changed as, being virtual, this risks breaking user code
    // overriding it
    ibDocTemplateVector templates(GetVisibleTemplates(m_templates));
    const size_t numTemplates = templates.size();
    if ( !numTemplates )
    {
        // no templates can be used, can't create document
        return nullptr;
    }

    // Nobody is asked which template to use: a path is matched to the template
    // that reads it, a new document without one takes the single template that
    // can make it (SelectDocumentType), and opening with no path has nothing
    // to open — picking a file is the client's dialog.
    ibDocTemplate *temp = nullptr;
    if ( !path.empty() )
    {
        temp = FindTemplateForPath(path);
        if ( !temp )
        {
            ibJournalWarning(wxT("docview"), _("The format of file '%s' couldn't be determined."),
                         path);
        }
    }
    else if ( flags & ibDOC_NEW )
    {
        temp = SelectDocumentType(&templates[0], numTemplates);
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

    // make the new document the current one and bring its tab to the front
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

// Make a default name for a new document
wxString ibDocManager::MakeNewDocumentName()
{
    wxString name;

    name.Printf(_("unnamed%d"), m_defaultDocumentNameCounter);
    m_defaultDocumentNameCounter++;

    return name;
}

// Not yet implemented
ibDocTemplate *ibDocManager::MatchTemplate(const wxString& WXUNUSED(path))
{
    return nullptr;
}

// Find out the document template via matching in the document file format
// against that of the template
ibDocTemplate *ibDocManager::FindTemplateForPath(const wxString& path)
{
    ibDocTemplate *theTemplate = nullptr;

    // The path is a temporary file's id (ibTempFile): the name it came under says what it is.
    const wxString name = ibTempFile(path).GetName();

    // Find the template which this extension corresponds to
    for (size_t i = 0; i < m_templates.GetCount(); i++)
    {
        ibDocTemplate *temp = (ibDocTemplate *)m_templates.Item(i)->GetData();
        if ( temp->FileMatchesTemplate(name) )
        {
            theTemplate = temp;
            break;
        }
    }
    return theTemplate;
}

// The single template that makes a new document, or null. Several distinct
// candidates are a choice for the person, and nobody is asked here: the caller
// names the template itself (CreateDocument<T>, OpenForm).
ibDocTemplate *ibDocManager::SelectDocumentType(ibDocTemplate **templates,
                                                int noTemplates)
{
    ibDocTemplate *theTemplate = nullptr;

    for ( int i = 0; i < noTemplates; i++ )
    {
        ibDocTemplate * const templ = templates[i];

        // Exact-equal (`== ibTEMPLATE_VISIBLE`) rather than the masked
        // `IsVisible()`: an Open-only template (ibTEMPLATE_VISIBLE |
        // ibTEMPLATE_ONLY_OPEN) makes no new documents.
        if ( templ->GetFlags() != ibTEMPLATE_VISIBLE )
            continue;

        if ( !theTemplate )
            theTemplate = templ;
        else if ( templ->GetDocumentName() != theTemplate->GetDocumentName() ||
                  templ->GetViewName() != theTemplate->GetViewName() )
            return nullptr; // a second document + view combination
    }

    return theTemplate;
}

// The single view a document can get, or null — the same rule as above.
ibDocTemplate *ibDocManager::SelectViewType(ibDocTemplate **templates,
                                            int noTemplates)
{
    ibDocTemplate *theTemplate = nullptr;

    for ( int i = 0; i < noTemplates; i++ )
    {
        ibDocTemplate * const templ = templates[i];
        if ( !templ->IsVisible() || templ->GetViewName().empty() )
            continue;

        if ( !theTemplate )
            theTemplate = templ;
        else if ( templ->m_viewTypeName != theTemplate->m_viewTypeName )
            return nullptr; // a second view
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

// Views should inform the document manager
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
//   impls (appData, metadata, the session frame that makes tabs).
// ============================================================================

#include "backend/appData.h"
#include "backend/metaCollection/metaObject.h"

#include "frmserver/client/clientFrame.h"   // ibClientFrame::CreateChildFrame

#include <wx/scopedptr.h>
#include "docManager.h"
#include "backend/metadataConfiguration.h"
#include <wx/dlist.h>


wxIMPLEMENT_CLASS(ibMetaDocument, ibDocument);

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

	// Where the view shows. A document COMPOSED by its parent (the home page: one tab holding
	// several forms) takes the frame the parent hands it and gets no tab of its own — one
	// question, asked through the doc parent. Everyone else gets a tab from the session's frame.
	if (ibDocChildFrameAnyBase* const composedFrame = GetComposedWindow())
		view->SetFrame(composedFrame);
	else
		ibClientFrame::CreateChildFrame(view.get());

	if (!view->OnCreate(this, flags))
		return false;

	// The explicit "show it" trigger: the new tab becomes the one its client shows. A composed
	// view shows in a frame that is already up, so this simply reports "nothing to do" there.
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
	// A document with an owner leaves its manager's list here, at the one close point every kind
	// passes; for one that was never listed, RemoveDocument does nothing.
	if (m_documentParent != nullptr) {
		if (ibDocManager* const manager = GetDocumentManager())
			manager->RemoveDocument(this);
	}

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

		// ⭐ A METAOBJECT DOCUMENT HAS NO FILE OF ITS OWN — its content lives in the metadata, so
		// saving it IS saving the metadata, and the answer to "did it save" is the SAVE's own.
		//
		// This read that answer INVERTED, both ways round: a metadata that saved successfully was
		// reported as a failed save, and one that FAILED fell through to SaveAs() — which answers
		// true for a metaobject document — and was reported as a success. The write itself always
		// happened, which is why it went unnoticed: only the verdict was wrong.
		// ⭐ AND IT IS A PLAIN SAVE, so it passes defaultFlag. Ctrl+S stores the working configuration;
		// it does not move the schema, and saveConfigFlag is what opens the restructure branch rather
		// than what says "the text changed" (metadataConfiguration.cpp, SaveConfiguration).
		//
		// The AOT row this used to chase is retired BY THE KEY — build stamp plus configuration
		// digest — and the digest moves only when a restructure succeeds. Which is right: the runtime
		// reads `config`, Ctrl+S does not publish it, so every cached row is still true.
		if ((m_documentParent == nullptr && m_metaObject != nullptr) && IsChildDocument())
			return activeMetaData->SaveDatabase(defaultFlag);

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

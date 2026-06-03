#ifndef __VISUAL_EDITOR_VIEW_H__
#define __VISUAL_EDITOR_VIEW_H__

#include "visualHost.h"

#include "frontend/visualView/ctrl/form.h"
#include "frontend/visualView/ctrl/sizer.h"
#include "frontend/visualView/ctrl/widgets.h"

class ibFormVisualDocument;

// ibVisualHostClient — the host that owns one open form.
//
// Ownership on both builds goes through the Document/View split:
//
//   Desktop: ibDocManager -> ibFormVisualDocument (ibDocument)
//                         -> ibFormVisualEditView (ibView)
//                         -> ibVisualHostClient (wxScrolledCanvas in
//                                                a wxAuiMDIChildFrame)
//
//   Web: ibWebFrame::m_tabs -> unique_ptr<ibFormVisualDocument>
//                           -> unique_ptr<ibFormVisualEditView>
//                           -> unique_ptr<ibVisualHostClient>
//                              (a node in the session's ibWebWindow tree)
//
// Closing a tab drops the Document; the RAII cascade tears down view +
// host and releases the form's ibValuePtr refcount. The web build skips
// the ibDocument/ibView machinery but keeps the same call shape.
class ibVisualHostClient : public ibVisualHost {
public:
#ifdef OES_USE_WEB
	// Third arg mirrors the desktop ctor (ibFormVisualEditView::OnCreate
	// passes m_viewFrame there). Ignored on web — the "parent window"
	// concept is folded into the ibWebWindow tree via SetParent. Type-
	// switched via ibFrontendWindow so the call site doesn't ifdef.
	ibVisualHostClient(ibFormVisualDocument* document, ibValueForm* valueForm,
		ibFrontendWindow* /*parent*/ = nullptr)
		: m_valueForm(valueForm), m_document(document) {}
	// Explicit dtor so tab close (ibWebDocChildFrame::m_host.reset())
	// goes through a proper ClearVisualHost -> Cleanup walk. Default
	// destruction leaves m_baseObjects populated while ~ibWebWindow
	// runs, which turns into UAF during the nested sizer/child teardown.
	// Body in visualHostClient.cpp (web branch).
	virtual ~ibVisualHostClient() override;
#else
	ibVisualHostClient(ibFormVisualDocument* document, ibValueForm* valueForm, ibFrontendWindow* parent);
	virtual ~ibVisualHostClient();
#endif

	// m_valueForm is the only path either build takes to the open form,
	// so IsShownHost can just guard both builds with the same check —
	// on desktop the pointer is always set, the && just short-circuits.
	virtual bool         IsShownHost() const { return m_valueForm && m_valueForm->IsShown(); }
	virtual ibValueForm* GetValueForm() const { return m_valueForm; }
	virtual void         SetValueForm(ibValueForm* valueForm) { m_valueForm = valueForm; }

	ibFormVisualDocument* GetDocument() const { return m_document; }

	// Host-is-the-background-window on both builds: desktop inherits
	// wxScrolledCanvas so `this` IS the wxWindow; web inherits
	// ibWebWindow so `this` IS the ibWebWindow. ibFrontendWindow*
	// resolves to the right type per build.
	virtual ibFrontendWindow* GetParentBackgroundWindow() const override
	{ return const_cast<ibVisualHostClient*>(this); }
	virtual ibFrontendWindow* GetBackgroundWindow() const override
	{ return const_cast<ibVisualHostClient*>(this); }

	// MDI-tab lifecycle verbs. Body is identical on both builds — forward
	// to the owned ibValueForm. The web null guard is also safe on
	// desktop (m_valueForm is always set there, short-circuits out), so
	// one inline covers both instead of a .cpp + header split.
	void ShowForm()     { if (m_valueForm) m_valueForm->ShowForm(nullptr); }
	void ActivateForm() { if (m_valueForm) m_valueForm->ActivateForm(); }
	void UpdateForm()   { if (m_valueForm) m_valueForm->UpdateForm(); }
	bool CloseForm()    { return m_valueForm ? m_valueForm->CloseForm() : true; }

#ifndef OES_USE_WEB
	virtual void OnClickFromApp(wxWindow* currentWindow, wxMouseEvent& event);
#endif

protected:
	// SetCaption: desktop pushes to ibDocument->SetTitle (drives the MDI
	// tab label); web pushes to the owning ibWebDocChildFrame (the tab
	// node in the session's ibWebWindow tree) so /session reports the
	// new title. SetOrientation: desktop mutates the host's root
	// wxBoxSizer; web mutates its root ibWebBoxSizer. Same intent on
	// both sides, different owning objects — bodies live in
	// visualHostClient.cpp (desktop) and webClientHost.cpp (web).
	virtual void SetCaption(const wxString& strCaption) override;
	virtual void SetOrientation(int orient) override;

#ifndef OES_USE_WEB
	void OnSize(wxSizeEvent& event);
	void OnIdle(wxIdleEvent& event);

	bool m_dataViewSizeChanged;
	wxSize m_dataViewSize;
#endif

	ibValuePtr<ibValueForm> m_valueForm;
	ibFormVisualDocument*   m_document;
};

//********************************************************************************************
//*                                 Document & View                                          *
//*                                                                                          *
//* Single declaration across both builds. ibDocument / ibView are in wxcore which the web   *
//* DLL links too, so the whole doc-view pipeline (create, track, close, multi-view) is      *
//* reused; only the rendering surface differs, and that difference is absorbed by           *
//* ibVisualHost's base-class ifdef. Callers (scripts, OpenForm, the session's tab list)     *
//* talk to these classes the same way regardless of build.                                   *
//********************************************************************************************

class FRONTEND_API ibFormVisualEditView : public ibView {
public:

	ibFormVisualEditView() : m_visualHost(nullptr) {}
	virtual ~ibFormVisualEditView();

	virtual wxPrintout* OnCreatePrintout() override;

	virtual bool OnCreate(ibDocument* doc, long flags) override;
	virtual void OnUpdate(ibView* sender, wxObject* hint = nullptr) override;
	virtual bool OnClose(bool deleteWindow = true) override;

	// ibView::OnDraw is pure; the form host paints itself (wxScrolledCanvas /
	// ibWebWindow), the view has nothing to draw.
	virtual void OnDraw(wxDC* WXUNUSED(dc)) override {}

	virtual void OnClosingDocument() override;

	ibVisualHostClient* GetVisualHost() const { return m_visualHost; }

private:
	ibVisualHostClient* m_visualHost;
};

class FRONTEND_API ibFormVisualCommandProcessor : public wxCommandProcessor {

public:
	virtual bool CanUndo() const { return false; }
	virtual bool CanRedo() const { return false; }
};

class FRONTEND_API ibFormVisualDocument : public ibDocument {
public:

	ibFormVisualDocument(ibValueForm* valueForm);
	virtual ~ibFormVisualDocument();

	// Form doc is runtime → const-meta. It is NOT a metadata-editing docView
	// (ibMetaDataDocument), so this is its own const accessor, not an override of
	// that non-const contract. See visualHostClientDocView.cpp.
	virtual const class ibMetaData* GetMetaData() const;

	virtual bool IsVisualDemonstrationDoc() const { return false; }

	virtual bool OnCreate(const wxString& WXUNUSED(path), long flags) override;
	virtual bool OnCloseDocument() override;

	virtual bool IsCloseOnOwnerClose() const override;

	virtual bool IsModified() const override { return m_documentModified; }
	virtual void Modify(bool modify) override;
	virtual bool Save() override;
	virtual bool SaveAs() override { return true; }

#ifdef OES_USE_WEB
	// No headless dialog: default ibDocument::OnSaveModified pops a
	// wxMessageDialog when IsModified() is true. On wenterprise-server
	// there's no event loop to drive it, ShowModal returns garbage, and
	// ibDocument::OnChangedViewList (reached via the last view's dtor)
	// skips its `delete this` branch when OnSaveModified returns false
	// — leaking the doc + its ibValuePtr<ibValueForm>. Returning true
	// always lets the cascade complete cleanly.
	virtual bool OnSaveModified() override { return true; }
#endif

	// Base signature is SetDocParent(ibDocument*) on ibDocument after
	// step-4 collapse; we accept the wider type and downcast inside.
	virtual void SetDocParent(ibDocument* docParent) override;

	ibFormVisualEditView* GetFirstView() const;
	ibValueForm* GetValueForm() const;
	const ibUniqueKey& GetFormKey() const;
	bool CompareFormKey(const ibUniqueKey& formKey) const;

	static ibUniqueKey CreateFormUniqueKey(const ibBackendControlFrame* ownerControl,
		const ibSourceDataObject* sourceObject, const ibUniqueKey& formGuid);

	static ibValueForm* FindFormByUniqueKey(const ibBackendControlFrame* ownerControl,
		const ibSourceDataObject* sourceObject, const ibUniqueKey& formGuid);

	static ibValueForm* FindFormByUniqueKey(const ibUniqueKey& guid);
	static ibValueForm* FindFormByControlUniqueKey(const ibUniqueKey& guid);
	static ibValueForm* FindFormBySourceUniqueKey(const ibUniqueKey& guid);

	static ibFormVisualDocument* FindDocByUniqueKey(const ibUniqueKey& guid);

	static bool UpdateFormUniqueKey(const ibUniqueKeyPair& guid);

protected:
	virtual ibView* DoCreateView() override;
private:
	ibValuePtr<ibValueForm> m_valueForm;
};

class FRONTEND_API ibFormVisualDocumentDemo : public ibFormVisualDocument {
public:

	ibFormVisualDocumentDemo(ibValueForm* valueForm) :
		ibFormVisualDocument(valueForm)
	{
	}

	virtual bool IsVisualDemonstrationDoc() const { return true; }
};

#endif

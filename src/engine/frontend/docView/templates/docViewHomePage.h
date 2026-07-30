#ifndef _DOCVIEW_HOME_PAGE_H__
#define _DOCVIEW_HOME_PAGE_H__

// Home page — the COMPOSITE doc/view.
//
// Every other document in the shell is one tab showing one thing: a form, an editor, a
// journal. The home page is the exception the platform needs — ONE tab showing SEVERAL
// runtime forms at once (a sales funnel next to a task list next to "create a document"),
// split by draggable sashes in the proportions the configuration asks for.
//
// It is built out of the machinery that already exists, not beside it — a COMPOSITE document:
//
//   ibHomePageDocument (ibDocument, tool tier — no metaobject)
//     └── ibHomePageView — its frame is the ground; a splitter divides it into SECTIONS
//           ├── section 1: doc, doc, … doc   (a chain of splitters, one pane per doc)
//           └── section 2: doc, doc, … doc
//
// Every one of those docs is the attached form's OWN ibFormVisualDocument, and two links make
// it composition rather than co-existence:
//   * its doc PARENT is the composite — always. It has no separate life: the page's close
//     cascades into it, and the page is what it asks about everything below;
//   * its frame is the pane the splitter divides (ibDocument::GetChildDocumentWindow, answered
//     by the parent) — so the form's window IS a piece of the page's own frame, with no
//     wrapper in between.
//
// So an attached form is a full runtime form: its module runs, its events fire, its source
// object is bound by its own metaobject (a list form gets the list, an object form gets a
// NEW object). The composite adds placement, and nothing else.
//
// WHAT is shown comes from the config root (ibHomePageDescription on
// ibValueMetaObjectConfiguration), edited in the designer through the workspace editor.
// See docs/home-page.md.

// docView.h first — it pulls in wx/app.h + wx/docview.h (see docViewAuditLog.h).
#include "frontend/docView/docView.h"

#include "backend/homePageDescription.h"

#include <vector>

class ibValueForm;
class ibValueMetaObjectFormBase;
class wxStaticText;
class wxSizer;

class FRONTEND_API ibHomePageDocument : public ibDocument {
public:

	ibHomePageDocument();
	virtual ~ibHomePageDocument();

	// THE start page of this window. Opens it on first call and activates it afterwards, so
	// a second caller (a script, a menu item) never gets a second copy. Returns nullptr when
	// the configuration attaches no forms — an empty workspace is NO tab, not a blank one.
	static ibHomePageDocument* ShowHomePage();

	// Pin the page's tab: locked = always ahead of the normal tabs, not draggable, no close
	// button. Called once the tab exists (ShowHomePage, right after OnCreate).
	void LockPageTab();

	// WHERE a child of this page renders. The page's own frame is the ground everything is
	// inserted into: the splitter tree lives in it, and each attached form gets one of its
	// panes. A child asks through its doc PARENT — that link is the composition, and it is
	// why the forms and the page are not separate lives.
	virtual ibFrontendWindow* GetChildDocumentWindow(const ibDocument* child) const override;

	// True only while the page is taking its own forms down — the one moment a pane's form is
	// allowed to close. Everything else bounces off it.
	virtual bool IsClosingChildren() const override { return m_closingChildren; }

	// The workspace as it was when this tab opened. A snapshot on purpose: the tab is a
	// running layout of live forms, and re-reading the description under it mid-session
	// would silently invalidate the cells. Reopening the tab picks up designer changes.
	const ibHomePageDescription& GetDescription() const { return m_description; }

	// The home page holds no data of its own — it never goes dirty, never prompts on close.
	virtual bool IsModified() const override { return false; }
	virtual void Modify(bool) override {}

	// "May I be closed?" — the question ibDocument::CanClose asks before anything is torn
	// down. The start page answers NO to everyone except the window closing its own
	// documents (IsClosingWindow). Refusing HERE and not in the view is deliberate: a
	// refusal inside DeleteAllViews leaves the document in the manager's list and trips
	// ibDocManager::CloseDocument's assert; refusing at the gate is what CanClose is for.
	virtual bool OnSaveModified() override;
	virtual bool Save() override { return true; }
	virtual bool SaveAs() override { return true; }

	// The page going down IS the moment its forms may go down. Both roads reach here: a
	// document-level Close, and the view's own close (ibView::OnClose calls doc->Close()).
	virtual bool Close() override;

protected:

	virtual bool DoSaveDocument(const wxString&) override { return true; }
	virtual bool DoOpenDocument(const wxString&) override { return true; }

private:

	ibHomePageDescription m_description;

	// Raised while the page tears its own children down (see IsClosingChildren).
	bool m_closingChildren = false;

	// One home page per shell — the identity ShowHomePage answers from, cleared by the dtor so
	// a closed page can be reopened.
	static ibHomePageDocument* ms_instance;

	wxDECLARE_NO_COPY_CLASS(ibHomePageDocument);
	wxDECLARE_DYNAMIC_CLASS(ibHomePageDocument);
};

class FRONTEND_API ibHomePageView : public ibView {
public:

	ibHomePageView() : ibView() {}

	virtual bool OnCreate(ibDocument* doc, long flags) override;
	virtual void OnUpdate(ibView* sender, wxObject* hint = nullptr) override;

	// The start page is NOT a tab the user closes: it is the window's own surface, opened by
	// the window and taken down with it. The tab's [x] is refused; the window's own teardown
	// is not (ibFrontendMainFrame::IsClosingWindow tells the two apart).
	virtual bool OnClose(bool deleteWindow = true) override;

	// The cells paint themselves (each is a form host) — the view has nothing to draw.
	virtual void OnDraw(wxDC* WXUNUSED(dc)) override {}

public:

	// The window an attached form's view must render into — the splitter pane that holds it.
	// Answered for the page's own children only; the document forwards its
	// GetChildDocumentWindow here.
	ibFrontendWindow* GetCellWindow(const ibDocument* childDoc) const;

private:

	// One attached form. `m_window` IS the form's host — the splitter's pane, nothing wraps
	// it; `m_formDoc` identifies the cell when the child asks where it lives.
	struct ibHomePageCell {
		ibHomePageItem     m_item;
		wxWindow*          m_window = nullptr;
		const ibDocument*  m_formDoc = nullptr;
		ibValueForm*       m_valueForm = nullptr;
	};

	// Build a column: the items stacked one under another with a draggable sash between each
	// pair, weighted by their heights. Returns the window that fills the slot — a form's own
	// host when there is one item left, a splitter when there are more.
	wxWindow* BuildColumn(wxWindow* parent, const std::vector<ibHomePageItem>& items, size_t from,
		ibHomePageDocument* homeDoc);

	// Open one attached form into a pane of `parent` (the splitter) and return that pane. The
	// pane is how the parent HOUSES the child: a header saying what this is, and the form's
	// own host filling the rest of it.
	wxWindow* OpenCell(wxWindow* parent, const ibHomePageItem& item, ibHomePageDocument* homeDoc);

	// The pane's header — the metaobject's OWN icon (a catalog reads as a catalog, a document
	// as a document) next to the title. An embedded form has no tab to carry either.
	void BuildCellHeader(wxWindow* pane, wxSizer* paneSizer,
		const ibValueMetaObjectFormBase* metaForm, const wxString& title);

	// The form metaobject an item points at; null when it was deleted since it was attached.
	const ibValueMetaObjectFormBase* FindItemForm(const ibHomePageItem& item) const;

	// Build the item's runtime form value, bound to the source its kind implies. Null for a
	// form that is gone or refused to open — the pane then says so instead of leaving a hole
	// in the splitter.
	ibValueForm* CreateFormValue(const ibValueMetaObjectFormBase* metaForm) const;

	wxWindow* m_workspace = nullptr;
	// The pane a form is being opened into RIGHT NOW: its document does not exist yet when it
	// asks where to render, so this is what answers. Cleared as soon as the open returns.
	wxWindow* m_openingPane = nullptr;
	std::vector<ibHomePageCell> m_cells;

	wxDECLARE_DYNAMIC_CLASS(ibHomePageView);
};

#endif

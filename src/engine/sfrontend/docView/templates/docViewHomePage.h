#ifndef _DOCVIEW_HOME_PAGE_H__
#define _DOCVIEW_HOME_PAGE_H__

// Home page — the COMPOSITE doc/view.
//
// Every other document a client has open is one tab showing one thing: a form, a document. The home page is the
// exception the platform needs — ONE tab showing SEVERAL runtime forms at once (a sales funnel next to a task list
// next to "create a document"), in the columns and proportions the configuration asks for.
//
// It is built out of the machinery that already exists, not beside it — a COMPOSITE document:
//
//   ibHomePageDocument (ibDocument — the configuration's metaobject is its own)
//     └── ibHomePageView — draws the page into its tab's frame, column by column
//           ├── column 1: form, form, … form
//           └── column 2: form, form, … form
//
// Every one of those forms is the attached form's OWN ibFormVisualDocument, and two links make it composition
// rather than co-existence:
//   * its doc PARENT is the page — always. It has no separate life: the page's close cascades into it, and the
//     page is what it asks about everything below;
//   * it shows in the page's tab (ibDocument::GetChildDocumentWindow, answered by the parent) — no tab of its own.
//     Where on the page it sits is the page's view's to draw.
//
// So an attached form is a full runtime form: its module runs, its events fire, its source object is bound by its
// own metaobject (a list form gets the list, an object form gets a NEW object). The composite adds placement, and
// nothing else.
//
// WHAT is shown comes from the configuration (ibHomePageDescription on ibValueMetaObjectConfiguration), edited in
// the designer through the workspace editor. See docs/private/home-page.md.

#include "sfrontend/docView/docView.h"
#include "sfrontend/visualView/ctrl/form.h"   // ibValuePtr<ibValueForm> — a cell holds its form

#include "backend/homePageDescription.h"

#include <vector>

class ibValueMetaObjectConfiguration;
class ibValueMetaObjectFormBase;

class SFRONTEND_API ibHomePageDocument : public ibDocument {
public:

	// The page belongs to the configuration's metaobject — the workspace is read from it, and so is the metadata
	// its forms are found in (GetMetaData): handed in by whoever opens the page, never reached for.
	explicit ibHomePageDocument(const ibValueMetaObjectConfiguration* configuration = nullptr);
	virtual ~ibHomePageDocument();

	// THE start page of this client. Opens it on first call and activates it afterwards, so a second caller (a
	// script, a menu item) never gets a second copy. Returns nullptr when the configuration attaches no forms — an
	// empty workspace is NO tab, not a blank one.
	static ibHomePageDocument* ShowHomePage(const ibValueMetaObjectConfiguration* configuration);

	// Pin the page's tab: locked = always ahead of the normal tabs, never closed by hand. Called once the tab
	// exists (ShowHomePage, right after OnCreate).
	void LockPageTab();

	// WHERE a child of this page shows — the page's own tab. A child asks through its doc PARENT: that link is the
	// composition, and it is why the forms and the page are not separate lives.
	virtual ibDocChildFrameAnyBase* GetChildDocumentWindow(const ibDocument* child) const override;

	// True only while the page is taking its own forms down — the one moment a cell's form is allowed to close.
	// Everything else bounces off it.
	virtual bool IsClosingChildren() const override { return m_closingChildren; }

	// The workspace as it was when this tab opened. A snapshot on purpose: the tab is a running layout of live
	// forms, and re-reading the description under it mid-session would silently invalidate the cells. Reopening
	// the tab picks up designer changes.
	const ibHomePageDescription& GetDescription() const { return m_description; }

	// The metadata the page's forms belong to — its metaobject's.
	const class ibMetaData* GetMetaData() const;

	// The home page holds no data of its own — it never goes dirty, never prompts on close.
	virtual bool IsModified() const override { return false; }
	virtual void Modify(bool) override {}

	// "May I be closed?" — the question ibDocument::CanClose asks before anything is torn down. The start page
	// answers NO to everyone except the client's frame closing its own documents (IsClosingWindow). Refusing HERE
	// and not in the view is deliberate: a refusal inside DeleteAllViews leaves the document in the manager's list
	// and trips ibDocManager::CloseDocument; refusing at the gate is what CanClose is for.
	virtual bool OnSaveModified() override;
	virtual bool Save() override { return true; }
	virtual bool SaveAs() override { return true; }

	// The page going down IS the moment its forms may go down. Both roads reach here: a document-level Close, and
	// the view's own close (ibView::OnClose calls doc->Close()).
	virtual bool Close() override;

protected:

	virtual bool DoSaveDocument(const wxString&) override { return true; }
	virtual bool DoOpenDocument(const wxString&) override { return true; }

private:

	const ibValueMetaObjectConfiguration* m_configuration;
	ibHomePageDescription m_description;

	// Raised while the page tears its own children down (see IsClosingChildren).
	bool m_closingChildren = false;

	wxDECLARE_NO_COPY_CLASS(ibHomePageDocument);
	wxDECLARE_DYNAMIC_CLASS(ibHomePageDocument);
};

class SFRONTEND_API ibHomePageView : public ibView {
public:

	ibHomePageView() : ibView() {}

	virtual bool OnCreate(ibDocument* doc, long flags) override;
	virtual void OnUpdate(ibView* sender, wxObject* hint = nullptr) override;

	// The start page is NOT a tab the person closes: it is the client's own surface, opened at the start and taken
	// down with the client. The refusal is the document's (ibHomePageDocument::OnSaveModified).
	virtual bool OnClose(bool deleteWindow = true) override;

	// ⭐ THE PAGE AS IT STANDS NOW — its columns, and in each the cells top to bottom: a header saying what lives
	// there, the cell's share of the column, and its form drawn as the form's own view draws it (with the key a
	// client names that form by — an event to a cell's control carries it). Written whole every time.
	virtual void OnDraw(ibDataNode& frame) override;

private:

	// One attached form. `m_metaForm` is what the item points at (null when it was deleted since it was
	// attached); `m_valueForm` the form opened from it (null when it refused to).
	struct ibHomePageCell {
		ibHomePageItem                   m_item;
		const ibValueMetaObjectFormBase* m_metaForm = nullptr;
		ibValuePtr<ibValueForm>          m_valueForm;
	};

	// Open a column: its items one under another, each a cell.
	std::vector<ibHomePageCell> OpenColumn(const std::vector<ibHomePageItem>& items, ibHomePageDocument* homeDoc);

	// Open one attached form as a cell of the page. A form that is gone or refused to open still makes a cell —
	// the page says so instead of leaving a hole in the column.
	ibHomePageCell OpenCell(const ibHomePageItem& item, ibHomePageDocument* homeDoc);

	// The cell's header and its form, into the cell's node.
	void DrawCell(const ibHomePageCell& cell, ibDataNode& node) const;

	// The form metaobject an item points at; null when it was deleted since it was attached.
	const ibValueMetaObjectFormBase* FindItemForm(const ibHomePageItem& item, const ibHomePageDocument* homeDoc) const;

	// Build the item's runtime form value, bound to the source its kind implies. Null for a form that is gone or
	// refused to open — the cell then says so. Handed out HELD: the cell's document takes it only when it is shown.
	ibValuePtr<ibValueForm> CreateFormValue(const ibValueMetaObjectFormBase* metaForm) const;

	// The columns, left to right — two only when the template has two and both have something shown.
	std::vector<std::vector<ibHomePageCell>> m_columns;

	// The left column's share of the width, when there are two.
	double m_columnGravity = 0.5;

	wxDECLARE_DYNAMIC_CLASS(ibHomePageView);
};

#endif

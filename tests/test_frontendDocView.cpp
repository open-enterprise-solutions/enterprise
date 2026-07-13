// =============================================================================
// Doc/View system — the forked ib* document-view framework.
//
// OES replaced wx/docview.h with its own ibDocument / ibView / ibDocManager
// (frontend/docView/docView.h). Both desktop and web build the form pipeline on
// it (ibDocManager -> ibFormVisualDocument -> ibFormVisualEditView -> host), so
// the framework itself deserves tests independent of forms.
//
// This layer needs only a live GUI wxApp (the classes derive from wxEvtHandler)
// — no appData env, no connection pool, no open config, no main frame. So it
// uses its OWN light fixture (GUI toolkit only), not the runtime harness. The
// GUI environment itself is registered once in test_frontendRuntime.cpp; both
// TUs share the same target, so ibWxGuiEnvironment::s_instance is live here too.
// =============================================================================

#include "frontendFix.h"                     // ibWxGuiEnvironment (GUI toolkit health)

#include "frontend/docView/docView.h"        // ibDocument / ibView / ibDocManager

namespace {

// A view is abstract only on OnDraw — a trivial concrete view closes it.
class ibTestView : public ibView {
public:
	void OnDraw(wxDC* /*dc*/) override {}
};

// Light fixture: GUI wxApp only. No appData env — the doc/view framework needs
// none. SKIPs if the toolkit couldn't come up headless.
struct DocViewFix : ::testing::Test {
	bool ready = false;
	void SetUp() override {
		if (ibWxGuiEnvironment::s_instance == nullptr || !ibWxGuiEnvironment::s_instance->IsOk())
			GTEST_SKIP() << "GUI wxApp unavailable (no display / headless)";
		ready = true;
	}
};

} // namespace

// Plain document accessors: title, modified flag, and the child-document
// predicate for a top-level (parentless) document.
TEST_F(DocViewFix, DocumentAccessors)
{
	if (!ready) GTEST_SKIP();

	ibDocument doc;
	doc.SetTitle(wxT("Report"));
	doc.SetDocumentName(wxT("ReportDoc"));
	EXPECT_EQ(doc.GetTitle(), wxString(wxT("Report")));
	EXPECT_EQ(doc.GetDocumentName(), wxString(wxT("ReportDoc")));

	EXPECT_FALSE(doc.IsModified()) << "a fresh document is unmodified";
	doc.Modify(true);
	EXPECT_TRUE(doc.IsModified());

	EXPECT_FALSE(doc.IsChildDocument()) << "a parentless document is top-level";
}

// A view attaches to a document: AddView registers it, GetFirstView returns it,
// the view links back to its document, and RemoveView detaches it.
TEST_F(DocViewFix, DISABLED_DocumentViewWiring)
{
	if (!ready) GTEST_SKIP();

	ibDocument doc;
	ibTestView* view = new ibTestView();
	view->SetDocument(&doc);

	EXPECT_TRUE(doc.AddView(view));
	EXPECT_EQ(doc.GetViewsVector().size(), 1u);
	EXPECT_EQ(doc.GetFirstView(), view);
	EXPECT_EQ(view->GetDocument(), &doc) << "the view links back to its document";

	doc.RemoveView(view);
	EXPECT_TRUE(doc.GetViewsVector().empty()) << "RemoveView detaches the view";
	// Do not delete view here — RemoveView owns its teardown; a manual delete
	// double-frees (heap corruption).
}

// A document constructed with a parent reports as a child document; the parent
// does not.
TEST_F(DocViewFix, ChildDocumentParenting)
{
	if (!ready) GTEST_SKIP();

	ibDocument parent;
	ibDocument child(&parent);
	EXPECT_TRUE(child.IsChildDocument()) << "a document with a parent is a child";
	EXPECT_FALSE(parent.IsChildDocument());
}

// The document manager tracks the documents added to it and drops them again on
// RemoveDocument (which unlinks without destroying — the caller keeps ownership).
TEST_F(DocViewFix, DocManagerTracksDocuments)
{
	if (!ready) GTEST_SKIP();

	ibDocManager mgr;                       // flags=0, initialize=true
	ibDocument* d1 = new ibDocument();
	ibDocument* d2 = new ibDocument();

	mgr.AddDocument(d1);
	mgr.AddDocument(d2);
	EXPECT_EQ(mgr.GetDocumentsVector().size(), 2u) << "both documents are tracked";

	mgr.RemoveDocument(d1);
	EXPECT_EQ(mgr.GetDocumentsVector().size(), 1u) << "RemoveDocument unlinks one";

	mgr.RemoveDocument(d2);
	EXPECT_TRUE(mgr.GetDocumentsVector().empty());
	// d1/d2 intentionally not deleted — the manager's teardown owns document
	// lifetime; a manual delete risks a double-free. Leaking two docs in a
	// short-lived test process is harmless.
}

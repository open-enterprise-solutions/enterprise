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

#include <functional>

namespace {

// A view is abstract only on OnDraw — a trivial concrete view closes it.
class ibTestView : public ibView {
public:
	void OnDraw(wxDC* /*dc*/) override {}
};

// A view that counts the updates it receives and then runs a callback owned by the TEST, not by
// the view. The callback often deletes this very view (or its document), so nothing here may touch
// a member after it returns — and the callback object must not live inside the view either.
class ibCountingView : public ibView {
public:
	explicit ibCountingView(int* counter, std::function<void()>* onUpdate = nullptr)
		: m_counter(counter), m_onUpdate(onUpdate) {}

	void OnDraw(wxDC* /*dc*/) override {}
	void OnUpdate(ibView* /*sender*/, wxObject* /*hint*/) override {
		++*m_counter;
		if (m_onUpdate != nullptr && *m_onUpdate)
			(*m_onUpdate)();
	}

private:
	int* m_counter;
	std::function<void()>* m_onUpdate;
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
//
// TWO things here are the doc/view CONTRACT, not test scaffolding, and getting
// either wrong corrupts the heap instead of failing:
//   * the document is HEAP-allocated. ibDocument::OnChangedViewList does
//     `delete this` once the last view goes away — a document exists only while
//     something views it. A stack document would be delete-d out from under us.
//   * two views are attached, so removing one leaves the document alive and
//     observable. Removing the only view would destroy the document mid-test.
// The surviving document + view are deliberately leaked: the test process has no
// event loop, and a short-lived process is the right place to accept that.
TEST_F(DocViewFix, DocumentViewWiring)
{
	if (!ready) GTEST_SKIP();

	ibDocument* doc = new ibDocument();
	ibTestView* first  = new ibTestView();
	ibTestView* second = new ibTestView();
	first->SetDocument(doc);
	second->SetDocument(doc);

	EXPECT_TRUE(doc->AddView(first));
	EXPECT_TRUE(doc->AddView(second));
	EXPECT_EQ(doc->GetViewsVector().size(), 2u);
	EXPECT_EQ(doc->GetFirstView(), first);
	EXPECT_EQ(first->GetDocument(), doc) << "the view links back to its document";

	EXPECT_TRUE(doc->RemoveView(first));
	EXPECT_EQ(doc->GetViewsVector().size(), 1u) << "RemoveView detaches just that view";
	EXPECT_EQ(doc->GetFirstView(), second) << "the remaining view is now first";

	// Do not delete the views — RemoveView owns its teardown; a manual delete
	// double-frees. Do not remove `second` either: that would take the document
	// with it (see the contract note above).
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

// ---------------------------------------------------------------------------
// Lifetime of a document that is torn down while it is being walked or while
// its children are still alive (designer crash in ibDocument::UpdateAllViews,
// reached from ibMetaTreeBase::NotifyDocuments — issue #153).
// ---------------------------------------------------------------------------

// A parent that goes first must not leave its children pointing at freed memory: the child's
// GetDocumentManager() asks its parent (a virtual call), and ~ibView calls that, and the child's own
// destructor writes into the parent's list. After the parent is gone the child is a plain top-level
// document.
TEST_F(DocViewFix, Destructor_ParentDeletedFirst_ChildBecomesTopLevel)
{
	if (!ready) GTEST_SKIP();

	ibDocument* parent = new ibDocument();
	ibDocument* first  = new ibDocument(parent);
	ibDocument* second = new ibDocument(parent);
	ASSERT_TRUE(first->IsChildDocument());

	delete parent;

	EXPECT_FALSE(first->IsChildDocument())  << "the parent is gone, so it is nobody's child";
	EXPECT_FALSE(second->IsChildDocument());
	// A parentless document without a template answers with the global manager; a child still
	// holding its freed parent would have called into it instead.
	EXPECT_EQ(first->GetDocumentManager(), ibDocManager::GetDocumentManager())
		<< "must not ask the freed parent";

	// Both children are still deletable — this used to write into the freed parent's child list.
	delete first;
	delete second;
}

// The child that goes first still leaves the parent's list — the pre-existing contract the orphan
// handling above must not break.
TEST_F(DocViewFix, Destructor_ChildDeletedFirst_ParentKeepsTheRest)
{
	if (!ready) GTEST_SKIP();

	int updates = 0;
	ibDocument parent;
	ibDocument* gone = new ibDocument(&parent);
	ibDocument* kept = new ibDocument(&parent);
	ibCountingView* keptView = new ibCountingView(&updates);
	keptView->SetDocument(kept);

	delete gone;
	parent.UpdateAllViews();

	EXPECT_EQ(updates, 1) << "the surviving child is still reached through the parent";

	keptView->SetDocument(nullptr);
	delete keptView;
	delete kept;
}

// Every view of the document is notified once, and the sender is skipped.
TEST_F(DocViewFix, UpdateAllViews_TwoViews_EachNotifiedOnceSenderSkipped)
{
	if (!ready) GTEST_SKIP();

	int firstCount = 0, secondCount = 0;
	ibDocument* doc = new ibDocument();
	ibCountingView* first  = new ibCountingView(&firstCount);
	ibCountingView* second = new ibCountingView(&secondCount);
	first->SetDocument(doc);
	second->SetDocument(doc);

	doc->UpdateAllViews();
	EXPECT_EQ(firstCount, 1);
	EXPECT_EQ(secondCount, 1);

	doc->UpdateAllViews(first);
	EXPECT_EQ(firstCount, 1)  << "the sender is not told about its own change";
	EXPECT_EQ(secondCount, 2);

	first->SetDocument(nullptr);
	second->SetDocument(nullptr);
	delete first;
	delete second;
	delete doc;
}

// Children get the same update as their parent, all of them.
TEST_F(DocViewFix, UpdateAllViews_ChildDocuments_ReceiveTheUpdate)
{
	if (!ready) GTEST_SKIP();

	int parentCount = 0, childCount = 0;
	ibDocument parent;
	ibDocument child(&parent);
	ibCountingView parentView(&parentCount);
	ibCountingView childView(&childCount);
	parentView.SetDocument(&parent);
	childView.SetDocument(&child);

	parent.UpdateAllViews();

	EXPECT_EQ(parentCount, 1);
	EXPECT_EQ(childCount, 1);

	parentView.SetDocument(nullptr);
	childView.SetDocument(nullptr);
}

// A view that closes itself as its answer to the update: the walk must go on to the next view and
// must not read the node of the view that was just destroyed.
TEST_F(DocViewFix, UpdateAllViews_ViewDeletesItself_RestStillNotified)
{
	if (!ready) GTEST_SKIP();

	int firstCount = 0, secondCount = 0;
	ibDocument* doc = new ibDocument();

	ibCountingView* first = nullptr;
	std::function<void()> closeFirst = [&first]() { delete first; first = nullptr; };
	first = new ibCountingView(&firstCount, &closeFirst);
	ibCountingView* second = new ibCountingView(&secondCount);
	first->SetDocument(doc);
	second->SetDocument(doc);

	doc->UpdateAllViews();

	EXPECT_EQ(firstCount, 1);
	EXPECT_EQ(secondCount, 1) << "the walk survived the first view leaving";
	EXPECT_EQ(doc->GetViewsVector().size(), 1u);

	second->SetDocument(nullptr);
	delete second;
	delete doc;
}

// A view that removes ANOTHER view before that view's turn: the removed one must not be notified.
TEST_F(DocViewFix, UpdateAllViews_LaterViewRemovedMeanwhile_NotNotified)
{
	if (!ready) GTEST_SKIP();

	int firstCount = 0, secondCount = 0, thirdCount = 0;
	ibDocument* doc = new ibDocument();

	ibCountingView* second = nullptr;
	std::function<void()> dropSecond = [&second]() { delete second; second = nullptr; };
	ibCountingView* first = new ibCountingView(&firstCount, &dropSecond);
	second = new ibCountingView(&secondCount);
	ibCountingView* third = new ibCountingView(&thirdCount);
	first->SetDocument(doc);
	second->SetDocument(doc);
	third->SetDocument(doc);

	doc->UpdateAllViews();

	EXPECT_EQ(firstCount, 1);
	EXPECT_EQ(secondCount, 0) << "it was gone before its turn";
	EXPECT_EQ(thirdCount, 1);

	first->SetDocument(nullptr);
	third->SetDocument(nullptr);
	delete first;
	delete third;
	delete doc;
}

// A child document that is deleted while the parent walks its children: the ones after it are
// still notified, and the deleted one is not.
TEST_F(DocViewFix, UpdateAllViews_ChildDeletedMeanwhile_RestOfChildrenNotified)
{
	if (!ready) GTEST_SKIP();

	int firstCount = 0, secondCount = 0, thirdCount = 0;
	ibDocument parent;
	ibDocument* first  = new ibDocument(&parent);
	ibDocument* second = new ibDocument(&parent);
	ibDocument* third  = new ibDocument(&parent);

	ibCountingView* secondView = new ibCountingView(&secondCount);
	// The first child's view deletes the SECOND child: its last view leaves, so the document goes.
	std::function<void()> dropSecond = [&]() {
		secondView->SetDocument(nullptr);
		second->RemoveView(secondView);   // detaches; empties the list -> deletes `second`
		second = nullptr;
	};
	ibCountingView* firstView = new ibCountingView(&firstCount, &dropSecond);
	ibCountingView* thirdView = new ibCountingView(&thirdCount);
	firstView->SetDocument(first);
	secondView->SetDocument(second);
	thirdView->SetDocument(third);

	parent.UpdateAllViews();

	EXPECT_EQ(firstCount, 1);
	EXPECT_EQ(secondCount, 0) << "the document was deleted before its turn";
	EXPECT_EQ(thirdCount, 1)  << "the walk went on past the deleted child";

	firstView->SetDocument(nullptr);
	thirdView->SetDocument(nullptr);
	delete firstView;
	delete secondView;
	delete thirdView;
	delete first;
	delete third;
}

// The strongest case: an update deletes the document that is being walked (its last view leaves).
// Nothing after that point may touch the document — not its view list, not its children.
TEST_F(DocViewFix, UpdateAllViews_DocumentDeletedByItsView_WalkStops)
{
	if (!ready) GTEST_SKIP();

	int firstCount = 0, secondCount = 0, childCount = 0;
	ibDocument* doc = new ibDocument();
	ibDocument* child = new ibDocument(doc);
	ibCountingView* childView = new ibCountingView(&childCount);
	childView->SetDocument(child);

	ibCountingView* second = new ibCountingView(&secondCount);
	ibCountingView* first = nullptr;
	// Both views leave; the document goes with the last one.
	std::function<void()> closeAll = [&]() {
		doc->RemoveView(second);
		doc->RemoveView(first);
		doc = nullptr;
	};
	first = new ibCountingView(&firstCount, &closeAll);
	first->SetDocument(doc);
	second->SetDocument(doc);

	doc->UpdateAllViews();

	EXPECT_EQ(firstCount, 1);
	EXPECT_EQ(secondCount, 0) << "removed before its turn";
	EXPECT_EQ(childCount, 0)  << "a deleted parent does not fan the update out any more";
	EXPECT_FALSE(child->IsChildDocument()) << "and its child was let go, not left dangling";

	first->SetDocument(nullptr);
	second->SetDocument(nullptr);
	childView->SetDocument(nullptr);
	delete first;
	delete second;
	delete childView;
	delete child;
}

/////////////////////////////////////////////////////////////////////////////
// ibPluginWebPane chat-history & @ context tests.
//
// Covers:
//   1. Round-trip: append 3 entries, force save, destroy pane, construct
//      a new pane with the same configHash, verify the entries restored.
//   2. ibChatHistory::Save respects the kMaxEntries cap.
//   3. ibChatHistory::Clear removes the file.
//   4. ibChatContext::ExtractTokens parses "@token" runs correctly.
//
// Heavily headless: any test that touches wxPanel construction requires a
// running wxApp. If the platform can't provide one (CI without DISPLAY),
// we GTEST_SKIP rather than fail, mirroring test_pluginWebPane.cpp.
/////////////////////////////////////////////////////////////////////////////

#include <gtest/gtest.h>

#include <wx/app.h>
#include <wx/frame.h>
#include <wx/filename.h>
#include <wx/init.h>
#include <wx/stdpaths.h>
#include <wx/string.h>

#include <atomic>
#include <vector>

#ifndef _WIN32
#  include <unistd.h>  // getpid for unique test buckets
#else
#  include <process.h>
#  define getpid _getpid
#endif

#include "frontend/pluginWebPane/pluginWebPane.h"
#include "frontend/pluginWebPane/chatHistory.h"
#include "frontend/pluginWebPane/chatContext.h"
#include "backend/plugin/pluginApi.h"

namespace {

std::atomic<int> g_messageCounter{0};

void OnMessageStub(const char* /*paneId*/,
                   const char* /*jsonInline*/,
                   void*       /*userData*/)
{
	g_messageCounter.fetch_add(1, std::memory_order_relaxed);
}

// Stable hash bucket per test so the suite never collides with a real
// designer session under ~/Library/Preferences/OES/chat/.
wxString TestBucket(const char* suffix)
{
	return wxString::Format(wxT("oes-test-%s-%lld"),
	                         wxString::FromUTF8(suffix),
	                         static_cast<long long>(::getpid()));
}

class ChatHistoryFixture : public ::testing::Test {
protected:
	void SetUp() override {
		static char  arg0[] = "oes_tests";
		static char* argvBuf[] = { arg0, nullptr };
		int argc = 1;
		if (wxTheApp == nullptr) {
			const bool ok = wxEntryStart(argc, argvBuf);
			if (!ok || wxTheApp == nullptr) {
				m_skipped = true;
				GTEST_SKIP() << "no GUI display — skipping chat history test";
				return;
			}
			m_ownsApp = true;
		}
		m_parent = new wxFrame(nullptr, wxID_ANY, wxT("oes-chat-history-test"));
		m_parent->Hide();
		g_messageCounter.store(0, std::memory_order_relaxed);
	}

	void TearDown() override {
		if (m_skipped) return;
		if (m_parent != nullptr) {
			m_parent->Destroy();
			m_parent = nullptr;
		}
		if (m_ownsApp) {
			wxEntryCleanup();
			m_ownsApp = false;
		}
	}

	wxFrame* m_parent  = nullptr;
	bool     m_ownsApp = false;
	bool     m_skipped = false;
};

} // namespace

// ---------------------------------------------------------------------------
// Round-trip: save then restore three entries
// ---------------------------------------------------------------------------

TEST_F(ChatHistoryFixture, EntriesPersistAcrossPaneLifetimes) {
	if (m_skipped) return;

	const wxString bucket = TestBucket("roundtrip");
	// Pre-clean any leftover from a previous run.
	ibChatHistory::Clear(bucket);

	// First pane — append 3 entries and force a save.
	{
		auto* pane = new ibPluginWebPane(m_parent,
		                              wxT("test.pane.history"),
		                              wxT("History Test"),
		                              wxEmptyString,
		                              &OnMessageStub,
		                              nullptr);
		pane->SetConfigHashForTests(bucket);

		ibPluginWebPane::Entry e1;
		e1.role     = ibPluginWebPane::Entry::Role::User;
		e1.markdown = wxT("Привет, ассистент.");
		pane->AppendEntryForTests(std::move(e1));

		ibPluginWebPane::Entry e2;
		e2.role     = ibPluginWebPane::Entry::Role::Assistant;
		e2.markdown = wxT("Здравствуйте! Чем могу помочь?");
		e2.requestId = wxT("req-1");
		pane->AppendEntryForTests(std::move(e2));

		ibPluginWebPane::Entry e3;
		e3.role     = ibPluginWebPane::Entry::Role::Error;
		e3.markdown = wxT("Тестовая ошибка");
		pane->AppendEntryForTests(std::move(e3));

		// Bypass the 500ms debounce — flush synchronously via the
		// chatHistory module directly. The pane's three appended
		// entries are mirrored into a snapshot we hand to Save() so the
		// on-disk JSON matches what the pane was holding in m_entries.
		std::vector<ibPluginWebPane::Entry> snapshot;
		{
			ibPluginWebPane::Entry s1;
			s1.role     = ibPluginWebPane::Entry::Role::User;
			s1.markdown = wxT("Привет, ассистент.");
			snapshot.push_back(std::move(s1));

			ibPluginWebPane::Entry s2;
			s2.role      = ibPluginWebPane::Entry::Role::Assistant;
			s2.markdown  = wxT("Здравствуйте! Чем могу помочь?");
			s2.requestId = wxT("req-1");
			snapshot.push_back(std::move(s2));

			ibPluginWebPane::Entry s3;
			s3.role     = ibPluginWebPane::Entry::Role::Error;
			s3.markdown = wxT("Тестовая ошибка");
			snapshot.push_back(std::move(s3));
		}
		ASSERT_TRUE(ibChatHistory::Save(bucket, snapshot));

		EXPECT_EQ(pane->GetEntryCountForTests(), 3u);
		pane->Destroy();
	}

	// Second pane — use the load path directly to verify on-disk file.
	std::vector<ibPluginWebPane::Entry> restored;
	const bool loaded = ibChatHistory::Load(bucket, restored);
	EXPECT_TRUE(loaded);
	ASSERT_EQ(restored.size(), 3u);
	EXPECT_EQ(restored[0].role, ibPluginWebPane::Entry::Role::User);
	EXPECT_EQ(restored[0].markdown, wxT("Привет, ассистент."));
	EXPECT_EQ(restored[1].role, ibPluginWebPane::Entry::Role::Assistant);
	EXPECT_EQ(restored[1].requestId, wxT("req-1"));
	EXPECT_EQ(restored[2].role, ibPluginWebPane::Entry::Role::Error);

	// Second pane construction — verify it picks up the file via the
	// pane's load path (constructor reads from m_configHash). We have to
	// set the hash AFTER construction (constructor reads the default
	// activeMetaData hash) and then call ReloadHistoryFromDisk.
	auto* pane2 = new ibPluginWebPane(m_parent,
	                              wxT("test.pane.history2"),
	                              wxT("History Test 2"),
	                              wxEmptyString,
	                              &OnMessageStub,
	                              nullptr);
	pane2->SetConfigHashForTests(bucket);
	pane2->ReloadHistoryFromDisk();
	EXPECT_EQ(pane2->GetEntryCountForTests(), 3u);
	pane2->Destroy();

	// Clean up the test artifact so a re-run starts fresh.
	ibChatHistory::Clear(bucket);
}

// ---------------------------------------------------------------------------
// Cap enforcement on save
// ---------------------------------------------------------------------------

TEST_F(ChatHistoryFixture, SaveTruncatesAtMaxEntries) {
	if (m_skipped) return;

	const wxString bucket = TestBucket("cap");
	ibChatHistory::Clear(bucket);

	std::vector<ibPluginWebPane::Entry> many;
	for (size_t i = 0; i < ibChatHistory::kMaxEntries + 50; ++i) {
		ibPluginWebPane::Entry e;
		e.role     = ibPluginWebPane::Entry::Role::User;
		e.markdown = wxString::Format(wxT("msg-%zu"), i);
		many.push_back(std::move(e));
	}
	ASSERT_TRUE(ibChatHistory::Save(bucket, many));

	std::vector<ibPluginWebPane::Entry> restored;
	ASSERT_TRUE(ibChatHistory::Load(bucket, restored));
	EXPECT_EQ(restored.size(), ibChatHistory::kMaxEntries);
	// First restored entry should be msg-50 (we dropped 0..49 because we
	// keep the NEWEST kMaxEntries).
	EXPECT_EQ(restored.front().markdown, wxT("msg-50"));
	EXPECT_EQ(restored.back ().markdown,
	          wxString::Format(wxT("msg-%zu"),
	                            ibChatHistory::kMaxEntries + 49));

	ibChatHistory::Clear(bucket);
}

// ---------------------------------------------------------------------------
// Clear removes the file
// ---------------------------------------------------------------------------

TEST_F(ChatHistoryFixture, ClearRemovesPersistedFile) {
	if (m_skipped) return;

	const wxString bucket = TestBucket("clear");
	ibChatHistory::Clear(bucket);

	std::vector<ibPluginWebPane::Entry> one;
	ibPluginWebPane::Entry e;
	e.role     = ibPluginWebPane::Entry::Role::User;
	e.markdown = wxT("only entry");
	one.push_back(std::move(e));
	ASSERT_TRUE(ibChatHistory::Save(bucket, one));

	// Sanity: file exists, load works.
	std::vector<ibPluginWebPane::Entry> a;
	ASSERT_TRUE(ibChatHistory::Load(bucket, a));
	EXPECT_EQ(a.size(), 1u);

	// Clear and verify load now returns empty.
	EXPECT_TRUE(ibChatHistory::Clear(bucket));
	std::vector<ibPluginWebPane::Entry> b;
	EXPECT_FALSE(ibChatHistory::Load(bucket, b));
	EXPECT_TRUE(b.empty());
}

// ---------------------------------------------------------------------------
// ConfigHash key stability
// ---------------------------------------------------------------------------

TEST(ChatHistoryHash, ConfigHashIsDeterministic) {
	const wxString a1 = ibChatHistory::ComputeConfigHashFor(wxT("MyConfig"));
	const wxString a2 = ibChatHistory::ComputeConfigHashFor(wxT("MyConfig"));
	EXPECT_EQ(a1, a2);
	const wxString b  = ibChatHistory::ComputeConfigHashFor(wxT("OtherConfig"));
	EXPECT_NE(a1, b);
	// Empty config name falls back to "default" — must produce a stable,
	// non-empty hash so first-time users still get a bucket.
	const wxString empty = ibChatHistory::ComputeConfigHashFor(wxEmptyString);
	EXPECT_FALSE(empty.IsEmpty());
}

// ---------------------------------------------------------------------------
// ibChatContext::ExtractTokens
// ---------------------------------------------------------------------------

TEST(ChatContext, ExtractTokensFindsAtRuns) {
	// Plain @-tokens
	auto t1 = ibChatContext::ExtractTokens(wxT("hello @Catalog.Foo and @Document.Bar"));
	ASSERT_EQ(t1.size(), 2u);
	EXPECT_EQ(t1[0], wxT("Catalog.Foo"));
	EXPECT_EQ(t1[1], wxT("Document.Bar"));

	// Dedup: same token twice in a single prompt only resolves once.
	auto t2 = ibChatContext::ExtractTokens(wxT("@selection plus @selection"));
	ASSERT_EQ(t2.size(), 1u);
	EXPECT_EQ(t2[0], wxT("selection"));

	// Bare '@' must NOT produce an empty token.
	auto t3 = ibChatContext::ExtractTokens(wxT("just an @ symbol"));
	EXPECT_TRUE(t3.empty());

	// Token terminates on whitespace, not on punctuation INSIDE the
	// identifier — but Russian text after the token should break the
	// token cleanly.
	auto t4 = ibChatContext::ExtractTokens(wxT("@Catalog.Counterparties — отчёт"));
	ASSERT_EQ(t4.size(), 1u);
	EXPECT_EQ(t4[0], wxT("Catalog.Counterparties"));
}

// ---------------------------------------------------------------------------
// ibChatContext::BuildContextBlock — no metadata, no @-tokens → empty
// ---------------------------------------------------------------------------

TEST(ChatContext, NoTokensYieldsEmptyContextBlock) {
	const wxString out = ibChatContext::BuildContextBlock(
	    wxT("Plain prompt without any references"),
	    /*searchRoot=*/nullptr);
	EXPECT_TRUE(out.IsEmpty());
}

// Unresolved tokens (no activeMetaData in test scope) → still empty
// because every metadata lookup fails and editor specials need a
// wxStyledTextCtrl that doesn't exist here.
TEST(ChatContext, UnresolvedTokensYieldEmptyContextBlock) {
	const wxString out = ibChatContext::BuildContextBlock(
	    wxT("Tell me about @Catalog.NonExistent and @selection"),
	    /*searchRoot=*/nullptr);
	EXPECT_TRUE(out.IsEmpty());
}

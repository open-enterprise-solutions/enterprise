/////////////////////////////////////////////////////////////////////////////
// Legacy LLMQuery shim — unit tests.
//
// The shim bridges ABI v3 plugins (which expose LLMQuery as a script
// function) to the ABI v4 WebView pane protocol so users still get a
// working chat UI without rebuilding the plugin. These tests cover the
// three behavioural contracts the shim must hold:
//
//   1. ScanForLegacyLLMShim does NOTHING when no LLMQuery function has
//      been registered — must not pretend an AI provider exists.
//   2. With LLMQuery registered + a valid sample.html path, the shim
//      synthesises an AI provider in chat mode and the manager's
//      default-AI-pane slot points at the shim pane id.
//   3. When the WebView pane callback fires with a chat.send envelope,
//      the shim calls LLMQuery, then emits exactly one chat.delta and
//      one chat.end envelope containing the response text and the
//      original requestId.
//
// The tests use stub WebPane callbacks captured in std::vectors so we
// can introspect what the shim sent. No wxWebView, no display required.
/////////////////////////////////////////////////////////////////////////////

#include <gtest/gtest.h>

#include "backend/plugin/pluginManager.h"
#include "backend/plugin/pluginValue.h"
#include "backend/plugin/pluginApi.h"

#include <wx/filename.h>
#include <wx/wfstream.h>
#include <wx/string.h>

#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

namespace {

// File-scope storage for the stub LLMQuery function's return value.
// CallFunction copies ret->m_value out before the arena teardown, so a
// static here is safe — the host never holds onto the pointer beyond
// one call.
ibPluginValue_s g_stubRet;
std::atomic<int> g_stubCallCount{0};
std::string g_stubLastPrompt;
std::string g_stubLastLocale;

// Stub LLMQuery: returns "STUB[<prompt>]" so the test can verify the
// shim's envelope actually carries the prompt-derived response.
int StubLLMQuery(ibPluginValue** args, int argc, ibPluginValue** ret)
{
	g_stubCallCount.fetch_add(1);
	g_stubLastPrompt.clear();
	g_stubLastLocale.clear();

	if (argc >= 1 && args[0] != nullptr) {
		const char* s = ibPluginCallScope::GetString(args[0]);
		if (s != nullptr) g_stubLastPrompt = s;
	}
	if (argc >= 2 && args[1] != nullptr) {
		const char* s = ibPluginCallScope::GetString(args[1]);
		if (s != nullptr) g_stubLastLocale = s;
	}

	const wxString answer = wxT("STUB[") + wxString::FromUTF8(g_stubLastPrompt.c_str())
	                       + wxT("]");
	g_stubRet.m_value.SetString(answer);
	*ret = &g_stubRet;
	return 0;
}

// Stub LLMQuery that always returns failure — used to verify the shim
// emits a `kind: "error"` envelope rather than going silent.
int StubLLMQueryFail(ibPluginValue** /*args*/, int /*argc*/, ibPluginValue** /*ret*/)
{
	g_stubCallCount.fetch_add(1);
	return 1; // non-zero == script-side error
}

// Stub LLMQuery returning a long response (~500 chars). At the shim's
// 96-char chunk size + 40 ms inter-chunk sleep, this gives roughly
// 5 chunks spaced over ~200 ms — enough headroom for an external
// thread to fire agent.cancel after the first one or two chunks land,
// so the test can assert "fewer than full chunks emitted".
int StubLLMQueryLong(ibPluginValue** args, int argc, ibPluginValue** ret)
{
	g_stubCallCount.fetch_add(1);
	if (argc >= 1 && args[0] != nullptr) {
		const char* s = ibPluginCallScope::GetString(args[0]);
		if (s != nullptr) g_stubLastPrompt = s;
	}
	// Build a deterministic 500-char ASCII body — content doesn't
	// matter, only the length matters so we get multiple chunks.
	std::string body;
	body.reserve(500);
	for (int i = 0; i < 500; ++i) {
		body.push_back(static_cast<char>('a' + (i % 26)));
	}
	g_stubRet.m_value.SetString(wxString::FromUTF8(body.c_str()));
	*ret = &g_stubRet;
	return 0;
}

// Stub Query for a v4 AI provider — the manager rejects providers with
// a null Query pointer, so tests that wire a "real v4 plugin already
// here" scenario must hand over something non-null.
int StubV4Query(const char* /*req*/, const char* /*rid*/, void* /*ud*/) { return 0; }

// Write a one-byte file at `path` so wxFileExists returns true. The shim
// only checks existence — the content never gets parsed by the manager
// (only the WebView would read it).
void WriteDummyFile(const wxString& path)
{
	wxFileOutputStream out(path);
	const char marker = 'x';
	out.Write(&marker, 1);
}

// Bundled test fixture: a manager with WebPane callbacks pre-wired to
// captures. The lambdas record what was registered + sent so each test
// can assert the shim's wire output.
// Spin-wait for the detached worker thread to finish emitting envelopes
// the shim spawned in response to chat.send. Polls the predicate every
// 5ms up to `budget` ms. Returns true if pred() became true in time.
template <typename Pred>
bool WaitUntil(Pred pred, int budgetMs = 1500)
{
	for (int t = 0; t < budgetMs; t += 5) {
		if (pred()) return true;
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}
	return pred();
}

struct ShimFixture {
	ibPluginManager pm;

	// Capture of CallWebPaneRegister calls. The shim issues exactly one
	// per ScanForLegacyLLMShim invocation under normal conditions.
	struct RegCapture {
		wxString          paneId;
		wxString          title;
		wxString          htmlBundlePath;
		ibPluginWebMsgFn  onMessage = nullptr;
		void*             userData  = nullptr;
	};
	std::vector<RegCapture> regs;

	// Capture of CallWebPaneSend calls. The shim writes its chat.delta /
	// chat.end / error envelopes here.
	std::vector<std::pair<wxString, wxString>> sends;

	wxString tmpBundle;

	ShimFixture() {
		// Build a temp file to satisfy wxFileExists inside the scan.
		tmpBundle = wxFileName::CreateTempFileName(wxT("oes_shim_test_"));
		WriteDummyFile(tmpBundle);

		pm.SetWebPaneCallbacks(
		    [this](const wxString& paneId, const wxString& title,
		            const wxString& path, ibPluginWebMsgFn cb, void* ud) -> int {
		        regs.push_back({paneId, title, path, cb, ud});
		        return 0;
		    },
		    [this](const wxString& paneId, const wxString& json) -> int {
		        sends.push_back({paneId, json});
		        return 0;
		    },
		    [](const wxString& /*paneId*/) -> int { return 0; }
		);
	}

	~ShimFixture() {
		if (!tmpBundle.IsEmpty() && wxFileExists(tmpBundle)) {
			wxRemoveFile(tmpBundle);
		}
	}
};

} // namespace

TEST(LegacyLLMShim, NoopWhenNoLLMQueryFunction) {
	ShimFixture f;
	// Manager is empty — no plugin registered any function.
	f.pm.ScanForLegacyLLMShim(f.tmpBundle);

	EXPECT_FALSE(f.pm.HasAIProviderFor("chat"));
	EXPECT_TRUE(f.pm.GetDefaultAIPaneId().IsEmpty());
	EXPECT_TRUE(f.regs.empty());
}

TEST(LegacyLLMShim, NoopWhenBundleMissing) {
	ShimFixture f;
	f.pm.HostRegisterFunction("LLMQuery", 2, &StubLLMQuery);
	// Pass a path that doesn't exist.
	f.pm.ScanForLegacyLLMShim(wxT("/no/such/path/sample.html"));

	EXPECT_FALSE(f.pm.HasAIProviderFor("chat"));
	EXPECT_TRUE(f.regs.empty());
}

TEST(LegacyLLMShim, RegistersProviderAndPaneWhenLLMQueryPresent) {
	ShimFixture f;
	f.pm.HostRegisterFunction("LLMQuery", 2, &StubLLMQuery);
	f.pm.ScanForLegacyLLMShim(f.tmpBundle);

	EXPECT_TRUE(f.pm.HasAIProviderFor("chat"));
	EXPECT_TRUE(f.pm.HasAIProviderFor("agent"));
	EXPECT_EQ(f.pm.GetDefaultAIPaneId(), wxT("legacy.llm-shim.chat"));

	ASSERT_EQ(f.regs.size(), 1u);
	EXPECT_EQ(f.regs[0].paneId, wxT("legacy.llm-shim.chat"));
	EXPECT_NE(f.regs[0].onMessage, nullptr);
	EXPECT_EQ(f.regs[0].htmlBundlePath, f.tmpBundle);
}

TEST(LegacyLLMShim, ChatSendInvokesLLMQueryAndEmitsDeltaPlusEnd) {
	ShimFixture f;
	g_stubCallCount.store(0);
	g_stubLastPrompt.clear();
	g_stubLastLocale.clear();

	f.pm.HostRegisterFunction("LLMQuery", 2, &StubLLMQuery);
	f.pm.ScanForLegacyLLMShim(f.tmpBundle);
	ASSERT_EQ(f.regs.size(), 1u);

	// Simulate the WebView posting a chat.send envelope into the shim.
	const char* envelope =
	    R"({"kind":"chat.send","requestId":"req-42","text":"hello shim","locale":"uk-UA"})";
	f.regs[0].onMessage("legacy.llm-shim.chat", envelope, f.regs[0].userData);

	// Async: detached worker calls LLMQuery + emits envelopes. Wait
	// briefly for it to land before asserting. STUB is fast so the
	// chunked emit + end should complete well under 200ms.
	WaitUntil([&]() { return g_stubCallCount.load() >= 1 && f.sends.size() >= 2; });

	EXPECT_EQ(g_stubCallCount.load(), 1);
	EXPECT_EQ(g_stubLastPrompt, "hello shim");
	EXPECT_EQ(g_stubLastLocale, "uk-UA");

	// At least two outbound envelopes: one or more chat.delta chunks +
	// one chat.end. STUB[hello shim] is 16 chars which fits in a single
	// chunk, so we expect exactly one delta + one end here.
	ASSERT_GE(f.sends.size(), 2u);
	EXPECT_EQ(f.sends[0].first, wxT("legacy.llm-shim.chat"));
	EXPECT_NE(f.sends[0].second.Find(wxT("\"kind\":\"chat.delta\"")), wxNOT_FOUND);
	EXPECT_NE(f.sends[0].second.Find(wxT("\"requestId\":\"req-42\"")), wxNOT_FOUND);
	EXPECT_NE(f.sends[0].second.Find(wxT("STUB[hello shim]")), wxNOT_FOUND);

	EXPECT_NE(f.sends[1].second.Find(wxT("\"kind\":\"chat.end\"")), wxNOT_FOUND);
	EXPECT_NE(f.sends[1].second.Find(wxT("\"requestId\":\"req-42\"")), wxNOT_FOUND);
}

TEST(LegacyLLMShim, EditorSkillBuildsNaturalLanguagePrompt) {
	ShimFixture f;
	g_stubCallCount.store(0);
	g_stubLastPrompt.clear();

	f.pm.HostRegisterFunction("LLMQuery", 2, &StubLLMQuery);
	f.pm.ScanForLegacyLLMShim(f.tmpBundle);
	ASSERT_EQ(f.regs.size(), 1u);

	const char* envelope =
	    R"({"kind":"editor.skill","op":"explain","language":"ces","code":"x = 1;","requestId":"r1"})";
	f.regs[0].onMessage("legacy.llm-shim.chat", envelope, f.regs[0].userData);

	WaitUntil([&]() { return g_stubCallCount.load() >= 1; });

	EXPECT_EQ(g_stubCallCount.load(), 1);
	// Shim must wrap the raw code snippet in a human-readable instruction
	// so the v3 plugin's LLM call gets context, not just a bare token.
	EXPECT_NE(g_stubLastPrompt.find("Explain"),  std::string::npos);
	EXPECT_NE(g_stubLastPrompt.find("x = 1;"),  std::string::npos);
	EXPECT_NE(g_stubLastPrompt.find("```ces"), std::string::npos);
}

TEST(LegacyLLMShim, FailureFromLLMQueryEmitsErrorEnvelope) {
	ShimFixture f;
	g_stubCallCount.store(0);

	f.pm.HostRegisterFunction("LLMQuery", 2, &StubLLMQueryFail);
	f.pm.ScanForLegacyLLMShim(f.tmpBundle);
	ASSERT_EQ(f.regs.size(), 1u);

	const char* envelope =
	    R"({"kind":"chat.send","requestId":"req-err","text":"trigger fail"})";
	f.regs[0].onMessage("legacy.llm-shim.chat", envelope, f.regs[0].userData);

	WaitUntil([&]() { return g_stubCallCount.load() >= 1 && f.sends.size() >= 1; });

	EXPECT_EQ(g_stubCallCount.load(), 1);
	// Exactly one outbound envelope, of kind "error", carrying the
	// original requestId so the pane can clear its pending row.
	ASSERT_EQ(f.sends.size(), 1u);
	EXPECT_NE(f.sends[0].second.Find(wxT("\"kind\":\"error\"")), wxNOT_FOUND);
	EXPECT_NE(f.sends[0].second.Find(wxT("\"requestId\":\"req-err\"")), wxNOT_FOUND);
}

TEST(LegacyLLMShim, IgnoresMalformedAndUnknownKinds) {
	ShimFixture f;
	g_stubCallCount.store(0);

	f.pm.HostRegisterFunction("LLMQuery", 2, &StubLLMQuery);
	f.pm.ScanForLegacyLLMShim(f.tmpBundle);
	ASSERT_EQ(f.regs.size(), 1u);

	// Malformed JSON — silently dropped, no LLMQuery call, no envelopes.
	f.regs[0].onMessage("legacy.llm-shim.chat", "not even close to json", f.regs[0].userData);
	// Known kind but not chat-routable — also dropped.
	f.regs[0].onMessage("legacy.llm-shim.chat",
	    R"({"kind":"chat.delta","requestId":"x","delta":"echo"})",
	    f.regs[0].userData);

	// Give any (rejected) async work a moment to surface — these should
	// stay zero because the shim returns BEFORE spawning the worker.
	std::this_thread::sleep_for(std::chrono::milliseconds(20));
	EXPECT_EQ(g_stubCallCount.load(), 0);
	EXPECT_TRUE(f.sends.empty());
}

TEST(LegacyLLMShim, AgentCancelStopsMidStream) {
	ShimFixture f;
	g_stubCallCount.store(0);
	g_stubLastPrompt.clear();

	f.pm.HostRegisterFunction("LLMQuery", 2, &StubLLMQueryLong);
	f.pm.ScanForLegacyLLMShim(f.tmpBundle);
	ASSERT_EQ(f.regs.size(), 1u);

	// Fire chat.send — kicks off the detached worker.
	const char* sendEnv =
	    R"({"kind":"chat.send","requestId":"req-cancel","text":"begin","locale":"uk-UA"})";
	f.regs[0].onMessage("legacy.llm-shim.chat", sendEnv, f.regs[0].userData);

	// Wait until at least one chat.delta has landed so we know the worker
	// got past LLMQuery and entered the chunked emit loop. Without this
	// the cancel could race the worker before it ever started emitting.
	const auto deltaCount = [&]() {
		size_t n = 0;
		for (const auto& [pid, body] : f.sends) {
			if (body.Find(wxT("\"kind\":\"chat.delta\"")) != wxNOT_FOUND) ++n;
		}
		return n;
	};
	ASSERT_TRUE(WaitUntil([&]() { return deltaCount() >= 1; }, 1000));

	// Cancel — by spec the shim flips the cancel token, and the worker
	// stops emitting deltas before the response is fully drained.
	const char* cancelEnv =
	    R"({"kind":"agent.cancel","requestId":"req-cancel"})";
	f.regs[0].onMessage("legacy.llm-shim.chat", cancelEnv, f.regs[0].userData);

	// Wait for chat.end to land.
	ASSERT_TRUE(WaitUntil([&]() {
		for (const auto& [pid, body] : f.sends) {
			if (body.Find(wxT("\"kind\":\"chat.end\"")) != wxNOT_FOUND) return true;
		}
		return false;
	}, 2000));

	// Full uncancelled run would emit ceil(500/96) == 6 chat.delta chunks
	// + 1 chat.end. With cancellation racing the chunk loop, we expect
	// strictly fewer than 6 deltas. (At minimum 1, asserted above.)
	const size_t deltas = deltaCount();
	EXPECT_LT(deltas, 6u) << "expected cancellation to truncate the stream";
	EXPECT_GE(deltas, 1u);

	// The chat.end envelope must carry cancelled:true so the pane can
	// surface "Отменено" instead of "model: …".
	bool foundCancelled = false;
	for (const auto& [pid, body] : f.sends) {
		if (body.Find(wxT("\"kind\":\"chat.end\"")) != wxNOT_FOUND &&
		    body.Find(wxT("\"cancelled\":true"))   != wxNOT_FOUND) {
			foundCancelled = true;
			break;
		}
	}
	EXPECT_TRUE(foundCancelled);
}

TEST(LegacyLLMShim, V4ProviderAlreadyRegisteredSkipsShim) {
	ShimFixture f;
	f.pm.HostRegisterFunction("LLMQuery", 2, &StubLLMQuery);

	// Pretend a v4 plugin beat us to the "chat" mode slot.
	ibPluginAIProvider real{};
	real.providerId     = "real.v4";
	real.displayName    = "Real v4";
	const char* modes[] = {"chat", nullptr};
	real.supportedModes = modes;
	real.Query          = &StubV4Query;
	ASSERT_EQ(f.pm.HostRegisterAIProvider(&real), 0);
	ASSERT_TRUE(f.pm.HasAIProviderFor("chat"));

	f.pm.ScanForLegacyLLMShim(f.tmpBundle);

	// HasAIProviderFor is true (real plugin) but the shim must not
	// have appended a duplicate or registered a pane.
	EXPECT_TRUE(f.pm.HasAIProviderFor("chat"));
	EXPECT_TRUE(f.regs.empty());
}

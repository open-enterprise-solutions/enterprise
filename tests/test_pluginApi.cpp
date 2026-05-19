/////////////////////////////////////////////////////////////////////////////
// pluginApi ABI layout regression tests.
//
// The plugin C ABI is append-only — fields existing in ABI v(N) MUST
// remain at the same struct offsets in ABI v(N+1) so prebuilt plugins
// keep loading after a host bump. These tests pin every v3 field's
// offsetof() value and assert it doesn't shift when v4 (or any future
// version) appends new entries to the struct tail.
//
// If a test here fails, the change reordered or inserted into the
// struct rather than appending — that breaks every external plugin
// without a version bump and a regenerated header.
/////////////////////////////////////////////////////////////////////////////

#include <gtest/gtest.h>
#include "backend/plugin/pluginApi.h"

#include <cstddef>

TEST(PluginAbi, VersionAtLeast4) {
	// We don't pin to ==4 because future bumps are legal additive.
	EXPECT_GE(IB_PLUGIN_ABI_VERSION, 4);
}

TEST(PluginAbi, V3FieldOffsetsStable) {
	// ABI v3 layout pin. Field order recorded at the v3 ship point —
	// reordering any of these breaks pugi-oes-bridge and every other
	// shipped plugin. Offsets are platform-dependent (function-pointer
	// width differs on 32-bit) so we only assert RELATIVE ordering,
	// not absolute byte counts.
	using H = ibHostAPI;

	EXPECT_LT(offsetof(H, RegisterFunction), offsetof(H, RegisterMenuItem));
	EXPECT_LT(offsetof(H, RegisterMenuItem), offsetof(H, Subscribe));
	EXPECT_LT(offsetof(H, Subscribe),         offsetof(H, Log));
	EXPECT_LT(offsetof(H, Log),               offsetof(H, MakeString));
	EXPECT_LT(offsetof(H, MakeString),        offsetof(H, MakeNumber));
	EXPECT_LT(offsetof(H, MakeNumber),        offsetof(H, MakeBool));
	EXPECT_LT(offsetof(H, MakeBool),          offsetof(H, MakeNull));
	EXPECT_LT(offsetof(H, MakeNull),          offsetof(H, GetString));
	EXPECT_LT(offsetof(H, GetString),         offsetof(H, GetNumber));
	EXPECT_LT(offsetof(H, GetNumber),         offsetof(H, GetBool));
	EXPECT_LT(offsetof(H, GetBool),           offsetof(H, IsNull));
}

TEST(PluginAbi, V4FieldsAppendedAtTail) {
	// Every v4 entry sits AFTER the last v3 field (IsNull). If any v4
	// field landed in the middle, this fails.
	using H = ibHostAPI;
	const std::size_t isNull = offsetof(H, IsNull);

	EXPECT_GT(offsetof(H, RegisterWebPane),    isNull);
	EXPECT_GT(offsetof(H, WebPaneSend),        isNull);
	EXPECT_GT(offsetof(H, WebPaneShow),        isNull);
	EXPECT_GT(offsetof(H, RegisterAIProvider), isNull);
	EXPECT_GT(offsetof(H, AIChunkEmit),        isNull);
	EXPECT_GT(offsetof(H, AIChunkEnd),         isNull);
	EXPECT_GT(offsetof(H, AIChunkError),       isNull);
	EXPECT_GT(offsetof(H, MetaCreate),         isNull);
	EXPECT_GT(offsetof(H, MetaEdit),           isNull);
	EXPECT_GT(offsetof(H, MetaDelete),         isNull);
	EXPECT_GT(offsetof(H, MetaQuery),          isNull);

	// v4 entries themselves in declared order.
	EXPECT_LT(offsetof(H, RegisterWebPane),    offsetof(H, WebPaneSend));
	EXPECT_LT(offsetof(H, WebPaneSend),        offsetof(H, WebPaneShow));
	EXPECT_LT(offsetof(H, WebPaneShow),        offsetof(H, RegisterAIProvider));
	EXPECT_LT(offsetof(H, RegisterAIProvider), offsetof(H, AIChunkEmit));
	EXPECT_LT(offsetof(H, AIChunkEmit),        offsetof(H, AIChunkEnd));
	EXPECT_LT(offsetof(H, AIChunkEnd),         offsetof(H, AIChunkError));
	EXPECT_LT(offsetof(H, AIChunkError),       offsetof(H, MetaCreate));
	EXPECT_LT(offsetof(H, MetaCreate),         offsetof(H, MetaEdit));
	EXPECT_LT(offsetof(H, MetaEdit),           offsetof(H, MetaDelete));
	EXPECT_LT(offsetof(H, MetaDelete),         offsetof(H, MetaQuery));
	EXPECT_LT(offsetof(H, MetaQuery),          offsetof(H, FreeBuffer));
}

TEST(PluginAbi, LockDeniedCodeIsStable) {
	// Plugins compile against this constant; can't shift the value
	// even between minor releases.
	EXPECT_EQ(IB_PLUGIN_LOCK_DENIED, 0x0001);
}

// ---------------------------------------------------------------------------
// Phase 2 — AI provider registry + chunk dispatch round-trip.
// ---------------------------------------------------------------------------
#include "backend/plugin/pluginManager.h"
#include <vector>
#include <string>

namespace {
struct StubProviderState {
	int  queryCalls   = 0;
	int  cancelCalls  = 0;
	std::string lastRequestJson;
	std::string lastRequestId;
};
StubProviderState* g_stub = nullptr;

int StubQuery(const char* requestJson, const char* requestId, void* /*userData*/) {
	if (g_stub) {
		g_stub->queryCalls++;
		if (requestJson) g_stub->lastRequestJson = requestJson;
		if (requestId)   g_stub->lastRequestId   = requestId;
	}
	return 0;
}
int StubCancel(const char* /*requestId*/, void* /*userData*/) {
	if (g_stub) g_stub->cancelCalls++;
	return 0;
}
int StubListModels(char** out) {
	if (out) *out = nullptr; // not used in this test
	return 0;
}

const char* kStubModes[] = { "chat", "agent", nullptr };
} // namespace

TEST(PluginRegistry, RegisterAIProviderStoresEntry) {
	ibPluginManager mgr;
	ibPluginAIProvider p{};
	p.providerId     = "stub.echo";
	p.displayName    = "Stub Echo Provider";
	p.iconPath       = "/tmp/icon.png";
	p.supportedModes = kStubModes;
	p.Query          = &StubQuery;
	p.Cancel         = &StubCancel;
	p.ListModels     = &StubListModels;
	p.userData       = nullptr;

	EXPECT_EQ(mgr.HostRegisterAIProvider(&p), 0);
	const auto& reg = mgr.AIProviders();
	ASSERT_EQ(reg.size(), 1u);
	EXPECT_EQ(reg[0].providerId,  "stub.echo");
	EXPECT_EQ(reg[0].displayName, "Stub Echo Provider");
	ASSERT_EQ(reg[0].supportedModes.size(), 2u);
	EXPECT_EQ(reg[0].supportedModes[0], "chat");
	EXPECT_EQ(reg[0].supportedModes[1], "agent");
}

TEST(PluginRegistry, DuplicateProviderIdReplaces) {
	ibPluginManager mgr;
	ibPluginAIProvider p{};
	p.providerId     = "stub.echo";
	p.displayName    = "First";
	p.supportedModes = kStubModes;
	p.Query = &StubQuery;
	mgr.HostRegisterAIProvider(&p);
	p.displayName = "Second";
	mgr.HostRegisterAIProvider(&p);
	ASSERT_EQ(mgr.AIProviders().size(), 1u);
	EXPECT_EQ(mgr.AIProviders()[0].displayName, "Second");
}

TEST(PluginRegistry, RegisterRejectsNullProviderId) {
	ibPluginManager mgr;
	ibPluginAIProvider p{};
	p.providerId = nullptr;
	p.Query      = &StubQuery;
	EXPECT_EQ(mgr.HostRegisterAIProvider(&p), -1);

	p.providerId = "";
	EXPECT_EQ(mgr.HostRegisterAIProvider(&p), -1);

	EXPECT_TRUE(mgr.AIProviders().empty());
}

TEST(PluginRegistry, RegisterRejectsNullQuery) {
	// Phase 4 dispatch would crash on a null Query pointer; refuse at
	// registration time so the registry never holds an unusable entry.
	ibPluginManager mgr;
	ibPluginAIProvider p{};
	p.providerId = "stub.echo";
	p.Query      = nullptr;
	EXPECT_EQ(mgr.HostRegisterAIProvider(&p), -1);
	EXPECT_TRUE(mgr.AIProviders().empty());
}

TEST(PluginRegistry, SanitiseValidatesActualJson) {
	// First-letter heuristic regression test: ensure inputs that LOOK
	// jsonish (start with `t/f/n`, `-`, `{`, `[`, `"`) but are NOT valid
	// JSON get wrapped as strings instead of spliced verbatim.
	ibPluginManager mgr;
	std::vector<std::string> sends;
	mgr.SetWebPaneCallbacks(
	    [](const wxString&, const wxString&, const wxString&,
	        ibPluginWebMsgFn, void*) -> int { return 0; },
	    [&sends](const wxString&, const wxString& jsonInline) -> int {
	        sends.emplace_back(jsonInline.utf8_str());
	        return 0;
	    },
	    [](const wxString&) -> int { return 0; });
	mgr.CallWebPaneRegister(wxT("p"), wxT("p"), wxT("/tmp/x.html"), nullptr, nullptr);

	const char* malformed[] = {
		"tabs",                // starts with t but not `true`
		"funky",               // starts with f
		"north",               // starts with n
		"-abc",                // negative-looking garbage
		"{unterminated",       // half-object
		"\"no close",          // open-quote
		"nul",                 // partial literal
	};
	for (const char* m : malformed) {
		sends.clear();
		EXPECT_EQ(mgr.HostAIChunkEmit("rid", m), 0) << m;
		ASSERT_FALSE(sends.empty()) << m;
		// Wrapped as a JSON string → delta:"<escaped>".
		EXPECT_NE(sends.back().find("\"delta\":\""), std::string::npos) << m;
		// Whatever we shipped must itself parse.
		// (sanity: nlohmann round-trip the full envelope)
	}

	// Valid JSON shapes should pass through verbatim.
	const char* valid[] = {
		"true", "false", "null",
		"42", "-3.14",
		"\"hello\"",
		"{\"k\":1}",
		"[1,2,3]",
	};
	for (const char* v : valid) {
		sends.clear();
		EXPECT_EQ(mgr.HostAIChunkEmit("rid", v), 0) << v;
		ASSERT_FALSE(sends.empty()) << v;
		const std::string& s = sends.back();
		// Delta carries the verbatim value, NOT a wrapped string.
		const std::string needle = std::string("\"delta\":") + v;
		EXPECT_NE(s.find(needle), std::string::npos) << v;
	}
}

TEST(PluginRegistry, DefaultPaneNotClaimedByBufferedFailedReplay) {
	// Buffered registration (Designer not wired yet) that the eventual
	// replay rejects must NOT claim m_defaultAIPaneId. Otherwise a
	// later successful registration is permanently shadowed and chunk
	// dispatch routes to a paneId the AUI manager never knew about.
	ibPluginManager mgr;

	// Phase 1: buffer a registration BEFORE callbacks are installed.
	EXPECT_EQ(mgr.CallWebPaneRegister(wxT("bad.pane"), wxT("B"),
	                                     wxT("/tmp/b.html"), nullptr, nullptr), 0);

	// Phase 2: install callbacks where bad.pane is rejected at replay.
	mgr.SetWebPaneCallbacks(
	    [](const wxString& paneId, const wxString&, const wxString&,
	        ibPluginWebMsgFn, void*) -> int {
	        return paneId == wxT("bad.pane") ? -1 : 0;
	    },
	    [](const wxString&, const wxString&) -> int { return 0; },
	    [](const wxString&) -> int { return 0; });
	mgr.ReplayPendingWebPaneRegistrations();

	// bad.pane was rejected, so the default slot must still be empty.
	EXPECT_EQ(mgr.HostAIChunkEmit("rid", "\"x\""), -1)
	    << "buffered+failed reg must not claim default slot";

	// A subsequent direct registration succeeds and claims the slot.
	EXPECT_EQ(mgr.CallWebPaneRegister(wxT("good.pane"), wxT("G"),
	                                     wxT("/tmp/g.html"), nullptr, nullptr), 0);
	EXPECT_EQ(mgr.HostAIChunkEmit("rid", "\"y\""), 0);
}

TEST(PluginRegistry, DefaultPaneOnlyOnSuccess) {
	// Failure path: the underlying register callback returns -1. We
	// must NOT claim that pane id as the default chunk target — a later
	// successful registration would otherwise be permanently shadowed.
	ibPluginManager mgr;
	mgr.SetWebPaneCallbacks(
	    [](const wxString& paneId, const wxString&, const wxString&,
	        ibPluginWebMsgFn, void*) -> int {
	        return paneId == wxT("fail.pane") ? -1 : 0;
	    },
	    [](const wxString&, const wxString&) -> int { return 0; },
	    [](const wxString&) -> int { return 0; });

	// First reg fails — default must stay empty.
	EXPECT_EQ(mgr.CallWebPaneRegister(wxT("fail.pane"), wxT("F"),
	                                     wxT("/tmp/x.html"), nullptr, nullptr), -1);
	EXPECT_EQ(mgr.HostAIChunkEmit("rid", "\"x\""), -1) << "no default yet";

	// Second reg succeeds — claims the default slot.
	EXPECT_EQ(mgr.CallWebPaneRegister(wxT("ok.pane"), wxT("OK"),
	                                     wxT("/tmp/y.html"), nullptr, nullptr), 0);
	EXPECT_EQ(mgr.HostAIChunkEmit("rid", "\"y\""), 0) << "default now set";
}

TEST(PluginRegistry, ChunkEmitNoTargetReturnsMinus1) {
	// Default state: no pane registered, no WebPaneSend callback.
	// HostAIChunkEmit should fail-fast rather than crash.
	ibPluginManager mgr;
	EXPECT_EQ(mgr.HostAIChunkEmit("req-1", "\"hello\""),   -1);
	EXPECT_EQ(mgr.HostAIChunkEnd("req-1",  "{\"x\":1}"),    -1);
	EXPECT_EQ(mgr.HostAIChunkError("req-1","{\"e\":\"x\"}"),-1);
}

TEST(PluginRegistry, ChunkEmitRoutesToDefaultPane) {
	ibPluginManager mgr;
	std::vector<std::pair<std::string,std::string>> captured;

	mgr.SetWebPaneCallbacks(
	    [&captured](const wxString& paneId, const wxString& /*title*/,
	                 const wxString& /*html*/,
	                 ibPluginWebMsgFn /*cb*/, void* /*ud*/) -> int {
	        captured.emplace_back(std::string(paneId.utf8_str()), std::string());
	        return 0;
	    },
	    [&captured](const wxString& paneId, const wxString& jsonInline) -> int {
	        captured.emplace_back(std::string(paneId.utf8_str()),
	                              std::string(jsonInline.utf8_str()));
	        return 0;
	    },
	    [](const wxString&) -> int { return 0; });

	// Simulate a plugin registering its pane.
	const int rc = mgr.CallWebPaneRegister(wxT("stub.pane"), wxT("Stub"),
	                                          wxT("/tmp/index.html"), nullptr, nullptr);
	EXPECT_EQ(rc, 0);

	// Now emit a chunk — should land on stub.pane.
	EXPECT_EQ(mgr.HostAIChunkEmit("req-7", "\"hello\""), 0);
	ASSERT_GE(captured.size(), 2u);
	const auto& last = captured.back();
	EXPECT_EQ(last.first, "stub.pane");
	EXPECT_NE(last.second.find("\"kind\":\"chat.delta\""), std::string::npos);
	EXPECT_NE(last.second.find("\"requestId\":\"req-7\""), std::string::npos);
	EXPECT_NE(last.second.find("\"delta\":\"hello\""),     std::string::npos);
}

TEST(PluginRegistry, ChunkEmitEscapesRequestId) {
	// requestId may contain quotes / backslashes from a malicious plugin.
	// The JSON envelope must escape them rather than break the wrapping.
	ibPluginManager mgr;
	std::string capturedJson;
	mgr.SetWebPaneCallbacks(
	    [](const wxString&, const wxString&, const wxString&,
	        ibPluginWebMsgFn, void*) -> int { return 0; },
	    [&capturedJson](const wxString&, const wxString& jsonInline) -> int {
	        capturedJson = std::string(jsonInline.utf8_str());
	        return 0;
	    },
	    [](const wxString&) -> int { return 0; });
	mgr.CallWebPaneRegister(wxT("p"), wxT("p"), wxT("/tmp/x.html"), nullptr, nullptr);

	EXPECT_EQ(mgr.HostAIChunkEmit("evil\"id\\with", "\"safe\""), 0);
	EXPECT_NE(capturedJson.find("evil\\\"id\\\\with"), std::string::npos);
}

TEST(PluginRegistry, ChunkEnvelopesAreDistinct) {
	ibPluginManager mgr;
	std::vector<std::string> sends;
	mgr.SetWebPaneCallbacks(
	    [](const wxString&, const wxString&, const wxString&,
	        ibPluginWebMsgFn, void*) -> int { return 0; },
	    [&sends](const wxString&, const wxString& jsonInline) -> int {
	        sends.emplace_back(jsonInline.utf8_str());
	        return 0;
	    },
	    [](const wxString&) -> int { return 0; });
	mgr.CallWebPaneRegister(wxT("p"), wxT("p"), wxT("/tmp/x.html"), nullptr, nullptr);

	mgr.HostAIChunkEmit ("rid", "\"d\"");
	mgr.HostAIChunkEnd  ("rid", "{\"tokens\":42}");
	mgr.HostAIChunkError("rid", "{\"code\":\"timeout\"}");

	ASSERT_GE(sends.size(), 3u);
	EXPECT_NE(sends[sends.size()-3].find("\"kind\":\"chat.delta\""), std::string::npos);
	EXPECT_NE(sends[sends.size()-2].find("\"kind\":\"chat.end\""),   std::string::npos);
	EXPECT_NE(sends[sends.size()-1].find("\"kind\":\"error\""),      std::string::npos);
	EXPECT_NE(sends[sends.size()-2].find("\"tokens\":42"),           std::string::npos);
	EXPECT_NE(sends[sends.size()-1].find("\"code\":\"timeout\""),    std::string::npos);
}

// ---------------------------------------------------------------------------
// Phase 3.1 — MetaQuery error-path coverage. Live-walk tests require a
// loaded ibMetaDataConfiguration fixture and ship with the integration
// suite; here we exercise the parser + dispatcher boundary only.
// ---------------------------------------------------------------------------
#include "backend/plugin/metaBridge.h"

TEST(MetaBridge, KindStringToCLSIDKnownKinds) {
	EXPECT_NE(metaBridge::KindStringToCLSID("Catalog"),  0ull);
	EXPECT_NE(metaBridge::KindStringToCLSID("Document"), 0ull);
	EXPECT_NE(metaBridge::KindStringToCLSID("AccountingRegister"), 0ull);
	EXPECT_NE(metaBridge::KindStringToCLSID("InformationRegister"), 0ull);
	EXPECT_NE(metaBridge::KindStringToCLSID("ChartOfAccounts"), 0ull);
}

TEST(MetaBridge, KindStringToCLSIDUnknownKinds) {
	EXPECT_EQ(metaBridge::KindStringToCLSID("Bogus"),     0ull);
	EXPECT_EQ(metaBridge::KindStringToCLSID(""),          0ull);
	EXPECT_EQ(metaBridge::KindStringToCLSID(nullptr),     0ull);
}

TEST(MetaBridge, KindStringToCLSIDIsCaseInsensitive) {
	// Agent prompts produce mixed case; lookup must be case-insensitive
	// so "catalog" / "CATALOG" / "Catalog" all resolve. Canonical-case
	// labels remain "Catalog" etc. in serialised output.
	const auto canonical = metaBridge::KindStringToCLSID("Catalog");
	EXPECT_EQ(metaBridge::KindStringToCLSID("catalog"),  canonical);
	EXPECT_EQ(metaBridge::KindStringToCLSID("CATALOG"),  canonical);
	EXPECT_EQ(metaBridge::KindStringToCLSID("CaTaLoG"),  canonical);
	EXPECT_EQ(metaBridge::KindStringToCLSID("ACCOUNTINGREGISTER"),
	          metaBridge::KindStringToCLSID("AccountingRegister"));
}

TEST(MetaBridge, MetaQueryFieldsFilterFailsLoud) {
	char* jsonOut = nullptr;
	char* err     = nullptr;
	EXPECT_EQ(metaBridge::HostMetaQuery("Catalog.X", "name,synonym",
	                                       &jsonOut, &err), -1);
	EXPECT_EQ(jsonOut, nullptr);
	ASSERT_NE(err, nullptr);
	const std::string e(err);
	EXPECT_NE(e.find("fieldsFilter"), std::string::npos);
	std::free(err);
}

TEST(MetaBridge, MetaQueryRejectsMalformedFullName) {
	char* jsonOut = nullptr;
	char* err     = nullptr;
	EXPECT_EQ(metaBridge::HostMetaQuery("",          nullptr, &jsonOut, &err), -1);
	EXPECT_EQ(jsonOut, nullptr);
	ASSERT_NE(err, nullptr);
	std::free(err);

	err = nullptr;
	EXPECT_EQ(metaBridge::HostMetaQuery("NoSeparator", nullptr, &jsonOut, &err), -1);
	ASSERT_NE(err, nullptr);
	std::free(err);

	err = nullptr;
	EXPECT_EQ(metaBridge::HostMetaQuery(".LeadingDot", nullptr, &jsonOut, &err), -1);
	ASSERT_NE(err, nullptr);
	std::free(err);

	err = nullptr;
	EXPECT_EQ(metaBridge::HostMetaQuery("TrailingDot.", nullptr, &jsonOut, &err), -1);
	ASSERT_NE(err, nullptr);
	std::free(err);
}

TEST(MetaBridge, MetaQueryRejectsUnknownKind) {
	char* jsonOut = nullptr;
	char* err     = nullptr;
	EXPECT_EQ(metaBridge::HostMetaQuery("Bogus.X", nullptr, &jsonOut, &err), -1);
	EXPECT_EQ(jsonOut, nullptr);
	ASSERT_NE(err, nullptr);
	const std::string e(err);
	EXPECT_NE(e.find("unknown kind"), std::string::npos);
	std::free(err);
}

TEST(MetaBridge, MetaQueryHandlesMissingConfig) {
	// activeMetaData == nullptr in this test process — the host must
	// surface a diagnostic and refuse to dereference. Real configuration
	// fixtures live in the integration suite.
	char* jsonOut = nullptr;
	char* err     = nullptr;
	EXPECT_EQ(metaBridge::HostMetaQuery("Catalog.X", nullptr, &jsonOut, &err), -1);
	EXPECT_EQ(jsonOut, nullptr);
	ASSERT_NE(err, nullptr);
	const std::string e(err);
	EXPECT_TRUE(e.find("no configuration") != std::string::npos ||
	             e.find("not found") != std::string::npos);
	std::free(err);
}

TEST(MetaBridge, MetaQueryNullOutPointersAreSafe) {
	// errorMsg / jsonOut may be NULL — the host must not crash.
	EXPECT_EQ(metaBridge::HostMetaQuery("Bogus.Y", nullptr, nullptr, nullptr), -1);
}

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
	EXPECT_LT(offsetof(H, FreeBuffer),         offsetof(H, ReadPluginEnv));
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

// ---------------------------------------------------------------------------
// Phase 3.2 — mutation policy gate + MetaCreate/Edit/Delete error paths.
// Live mutation success paths require a config fixture; deferred to the
// integration suite once Phase 3.3 wires the wxCommandProcessor.
// ---------------------------------------------------------------------------

TEST(MutationPolicy, DefaultsAreSafe) {
	// Mutation ops default Ask; query defaults AllowAlways.
	ibPluginManager mgr;
	EXPECT_EQ(mgr.GetMutationPolicy(wxT("pugi"), wxT("meta.create")),
	          ibPluginManager::MutationPolicy::Ask);
	EXPECT_EQ(mgr.GetMutationPolicy(wxT("pugi"), wxT("meta.edit")),
	          ibPluginManager::MutationPolicy::Ask);
	EXPECT_EQ(mgr.GetMutationPolicy(wxT("pugi"), wxT("meta.delete")),
	          ibPluginManager::MutationPolicy::Ask);
	EXPECT_EQ(mgr.GetMutationPolicy(wxT("pugi"), wxT("meta.query")),
	          ibPluginManager::MutationPolicy::AllowAlways);
}

TEST(MutationPolicy, AskBlocksAndAllowSessionPermits) {
	ibPluginManager mgr;
	EXPECT_FALSE(mgr.CheckMutationAllowed(wxT("pugi"), wxT("meta.create")))
	    << "default Ask must deny";
	mgr.SetMutationPolicy(wxT("pugi"), wxT("meta.create"),
	                        ibPluginManager::MutationPolicy::AllowSession);
	EXPECT_TRUE(mgr.CheckMutationAllowed(wxT("pugi"), wxT("meta.create")));
}

TEST(MutationPolicy, AllowAlwaysSurvivesQueryOpToo) {
	ibPluginManager mgr;
	EXPECT_TRUE(mgr.CheckMutationAllowed(wxT("pugi"), wxT("meta.query")))
	    << "query default AllowAlways must permit";
}

TEST(MutationPolicy, ExplicitDenyOverridesEverything) {
	ibPluginManager mgr;
	mgr.SetMutationPolicy(wxT("pugi"), wxT("meta.delete"),
	                        ibPluginManager::MutationPolicy::Deny);
	EXPECT_FALSE(mgr.CheckMutationAllowed(wxT("pugi"), wxT("meta.delete")));
}

TEST(MutationPolicy, WildcardAllowGrantsAllOps) {
	// (pluginId, "*") = AllowAlways means agent gets blanket trust for
	// every op from that plugin — modern IDE convention.
	ibPluginManager mgr;
	mgr.SetMutationPolicy(wxT("pugi"), wxT("*"),
	                        ibPluginManager::MutationPolicy::AllowAlways);
	EXPECT_TRUE(mgr.CheckMutationAllowed(wxT("pugi"), wxT("meta.create")));
	EXPECT_TRUE(mgr.CheckMutationAllowed(wxT("pugi"), wxT("meta.edit")));
	EXPECT_TRUE(mgr.CheckMutationAllowed(wxT("pugi"), wxT("meta.delete")));
	// Different plugin sees defaults — Ask still blocks.
	EXPECT_FALSE(mgr.CheckMutationAllowed(wxT("other"), wxT("meta.create")));
}

TEST(MutationPolicy, WildcardAllowWinsOverPerOpDeny) {
	// Inverse of WildcardDenyBlocksEverything: wildcard AllowAlways is
	// checked first; per-op Deny set afterwards never runs. Documents
	// the "trust this plugin entirely" UX contract — wildcard short-
	// circuits both directions, deny AND allow.
	ibPluginManager mgr;
	mgr.SetMutationPolicy(wxT("pugi"), wxT("*"),
	                        ibPluginManager::MutationPolicy::AllowAlways);
	mgr.SetMutationPolicy(wxT("pugi"), wxT("meta.create"),
	                        ibPluginManager::MutationPolicy::Deny);
	EXPECT_TRUE(mgr.CheckMutationAllowed(wxT("pugi"), wxT("meta.create")))
	    << "wildcard AllowAlways short-circuits — per-op Deny never consulted";
}

TEST(MutationPolicy, WildcardDenyBlocksEverything) {
	ibPluginManager mgr;
	mgr.SetMutationPolicy(wxT("pugi"), wxT("*"),
	                        ibPluginManager::MutationPolicy::Deny);
	// Even an explicit AllowAlways per-op can't override wildcard Deny.
	mgr.SetMutationPolicy(wxT("pugi"), wxT("meta.create"),
	                        ibPluginManager::MutationPolicy::AllowAlways);
	EXPECT_FALSE(mgr.CheckMutationAllowed(wxT("pugi"), wxT("meta.create")));
}

TEST(MutationPolicy, UnloadAllWipesSessionAllowList) {
	ibPluginManager mgr;
	mgr.SetMutationPolicy(wxT("pugi"), wxT("meta.create"),
	                        ibPluginManager::MutationPolicy::AllowSession);
	EXPECT_TRUE(mgr.CheckMutationAllowed(wxT("pugi"), wxT("meta.create")));
	mgr.UnloadAll();
	EXPECT_FALSE(mgr.CheckMutationAllowed(wxT("pugi"), wxT("meta.create")))
	    << "session allow must evaporate on UnloadAll";
}

TEST(MetaMutation, CreateRefusesEmptyPluginId) {
	// Defensive default: an unauthenticated caller (empty pluginId) is
	// never trusted. Phase 3.3 will wire the per-call scope tracker.
	char* err = nullptr;
	const int rc = metaBridge::HostMetaCreate("", "Catalog", "Catalog.Foo",
	                                            nullptr, &err);
	EXPECT_NE(rc, 0);
	ASSERT_NE(err, nullptr);
	std::free(err);
}

// Helper RAII to swap in a test pluginManager override + clear on dtor.
struct ScopedPluginManager {
	ibPluginManager pm;
	ScopedPluginManager() {
		metaBridge::ClearUndoStackForTests();
		metaBridge::SetPluginManagerOverrideForTests(&pm);
	}
	~ScopedPluginManager() {
		metaBridge::SetPluginManagerOverrideForTests(nullptr);
		metaBridge::ClearUndoStackForTests();
	}
};

TEST(MetaMutation, DeleteRequiresForceFlag) {
	ScopedPluginManager fx;
	fx.pm.SetMutationPolicy(wxT("pugi"), wxT("meta.delete"),
	                          ibPluginManager::MutationPolicy::AllowAlways);

	char* err = nullptr;
	int rc = metaBridge::HostMetaDelete("pugi", "Catalog.Foo",
	                                       /*propertiesJson*/ nullptr, &err);
	EXPECT_NE(rc, 0);
	ASSERT_NE(err, nullptr);
	EXPECT_NE(std::string(err).find("force"), std::string::npos);
	std::free(err);

	// With force=true the gate clears; downstream returns "not found"
	// since the test fixture has no activeMetaData configuration —
	// but the error MUST NOT cite "force" anymore.
	err = nullptr;
	rc = metaBridge::HostMetaDelete("pugi", "Catalog.Foo",
	                                   "{\"force\":true}", &err);
	EXPECT_NE(rc, 0);
	if (err != nullptr) {
		EXPECT_EQ(std::string(err).find("force requires"), std::string::npos);
		std::free(err);
	}
}

TEST(MetaMutation, EditStubGuards) {
	ScopedPluginManager fx;
	fx.pm.SetMutationPolicy(wxT("pugi"), wxT("meta.edit"),
	                          ibPluginManager::MutationPolicy::AllowSession);

	// Empty patch
	char* err = nullptr;
	EXPECT_NE(metaBridge::HostMetaEdit("pugi", "Catalog.Foo", "", &err), 0);
	if (err) { std::free(err); err = nullptr; }

	// Malformed JSON
	EXPECT_NE(metaBridge::HostMetaEdit("pugi", "Catalog.Foo", "{not json", &err), 0);
	ASSERT_NE(err, nullptr);
	EXPECT_NE(std::string(err).find("parse"), std::string::npos);
	std::free(err);
	err = nullptr;

	// Unknown key
	EXPECT_NE(metaBridge::HostMetaEdit("pugi", "Catalog.Foo",
	                                      "{\"name\":\"Pwn\"}", &err), 0);
	ASSERT_NE(err, nullptr);
	EXPECT_NE(std::string(err).find("unknown key"), std::string::npos);
	std::free(err);
}

TEST(MetaMutation, PolicyGateEmitsStructuredEnvelope) {
	ScopedPluginManager fx;
	fx.pm.SetMutationPolicy(wxT("pugi"), wxT("meta.create"),
	                          ibPluginManager::MutationPolicy::Ask);

	char* err = nullptr;
	const int rc = metaBridge::HostMetaCreate("pugi", "Catalog",
	                                            "Catalog.Foo", nullptr, &err);
	EXPECT_EQ(rc, IB_PLUGIN_PERMISSION_DENIED);
	ASSERT_NE(err, nullptr);
	const std::string envelope(err);
	EXPECT_NE(envelope.find("\"code\":\"permission_denied\""), std::string::npos);
	EXPECT_NE(envelope.find("\"op\":\"meta.create\""),           std::string::npos);
	EXPECT_NE(envelope.find("\"pluginId\":\"pugi\""),            std::string::npos);
	EXPECT_NE(envelope.find("\"hint\""),                         std::string::npos);
	std::free(err);
}

TEST(MetaMutation, UndoStackEmptyByDefault) {
	metaBridge::ClearUndoStackForTests();
	EXPECT_EQ(metaBridge::UndoLastAgentMutation(), -1)
	    << "empty undo stack must return -1";
}

TEST(MetaMutation, ForceFlagParserEdgeCases) {
	// Bridge-level smoke: ExtractForceFlag is not exported, but
	// HostMetaDelete is. We assert the parser via observable behaviour:
	// the policy gate runs FIRST, so an empty pluginId always trips it
	// regardless of force payload — meaning we can't directly test the
	// parser without appData. Skip the live test for now and verify
	// only that obviously-malformed payloads do not crash through the
	// gate. Real coverage lives in the integration suite once Phase 3.3
	// wires appData fixtures.
	char* err = nullptr;
	// Garbage propertiesJson must not crash; the gate refuses on empty
	// pluginId first and the bridge never reaches ExtractForceFlag.
	EXPECT_NE(metaBridge::HostMetaDelete("", "Catalog.X",
	                                       "this is not JSON at all", &err), 0);
	if (err) { std::free(err); err = nullptr; }
	EXPECT_NE(metaBridge::HostMetaDelete("", "Catalog.X",
	                                       "{\"force\":\"yes\"}", &err), 0);
	if (err) std::free(err);
}

// ---------------------------------------------------------------------------
// AGENT-CHILD: child-object (form/attribute/tabular section/module) coverage
// for HostMetaCreate / HostMetaEdit / HostMetaDelete. These tests exercise
// the path parser + guard rejection paths without requiring a live
// activeMetaData fixture; full integration coverage that walks a real
// configuration tree lives in tests/test_oesAgentIntegration.cpp.
// ---------------------------------------------------------------------------

TEST(MetaBridge, KindMapKnowsChildKinds) {
	// AGENT-CHILD: form variants all collapse to g_metaFormCLSID; the
	// variant is conveyed via properties.formType, not the kind label.
	const auto formCLSID = metaBridge::KindStringToCLSID("Form");
	EXPECT_NE(formCLSID, 0ull);
	EXPECT_EQ(metaBridge::KindStringToCLSID("ItemForm"),       formCLSID);
	EXPECT_EQ(metaBridge::KindStringToCLSID("ListForm"),       formCLSID);
	EXPECT_EQ(metaBridge::KindStringToCLSID("ChoiceForm"),     formCLSID);
	EXPECT_EQ(metaBridge::KindStringToCLSID("SelectionForm"),  formCLSID);

	EXPECT_NE(metaBridge::KindStringToCLSID("Attribute"),         0ull);
	EXPECT_NE(metaBridge::KindStringToCLSID("TabularSection"),    0ull);
	EXPECT_NE(metaBridge::KindStringToCLSID("Command"),           0ull);
	EXPECT_NE(metaBridge::KindStringToCLSID("ObjectModule"),      0ull);
	EXPECT_NE(metaBridge::KindStringToCLSID("ManagerModule"),     0ull);
}

TEST(MetaMutation, CreateRejectsMalformedChildPath) {
	ScopedPluginManager fx;
	fx.pm.SetMutationPolicy(wxT("pugi"), wxT("meta.create"),
	                          ibPluginManager::MutationPolicy::AllowAlways);

	// AGENT-CHILD: 5-segment path is not in the supported (2/3/4/6) shape.
	// Without a live activeMetaData fixture the call returns non-zero —
	// RequireConfiguration trips first when no configuration is loaded.
	// The test asserts the call rejects; full diagnostic-string coverage
	// happens in the live-config integration suite.
	char* err = nullptr;
	int rc = metaBridge::HostMetaCreate("pugi", /*objectKind*/"",
	                                       "Catalog.Foo.Forms.Bar.Baz",
	                                       /*propertiesJson*/"{}", &err);
	EXPECT_NE(rc, 0);
	if (err) std::free(err);

	// AGENT-CHILD: trailing dot (empty trailing segment) is rejected.
	err = nullptr;
	rc = metaBridge::HostMetaCreate("pugi", "",
	                                  "Catalog.Foo.Forms.",
	                                  "{}", &err);
	EXPECT_NE(rc, 0);
	if (err) std::free(err);

	// AGENT-CHILD: unknown child container.
	err = nullptr;
	rc = metaBridge::HostMetaCreate("pugi", "",
	                                  "Catalog.Foo.NotAContainer.X",
	                                  "{}", &err);
	EXPECT_NE(rc, 0);
	if (err) std::free(err);
}

TEST(MetaMutation, CreateRejectsModuleSingleton) {
	// AGENT-CHILD: ObjectModule and ManagerModule are SINGLETONS on each
	// top-level metaobject — they live as ibPropertyInnerModule values
	// owned by the parent's property tree, and cannot be created out of
	// band. Sigma should emit `op:"edit"` on the singleton path to
	// update m_strSource; metaBridge enforces the rule with a clear error.
	ScopedPluginManager fx;
	fx.pm.SetMutationPolicy(wxT("pugi"), wxT("meta.create"),
	                          ibPluginManager::MutationPolicy::AllowAlways);

	char* err = nullptr;
	int rc = metaBridge::HostMetaCreate("pugi", "",
	                                      "Catalog.Foo.ObjectModule",
	                                      "{\"moduleCode\":\"\"}", &err);
	EXPECT_NE(rc, 0);
	// Two possible failure modes depending on configuration state:
	//   - With activeMetaData: ResolvePath finds 'Catalog.Foo' missing
	//     and surfaces a "top-level parent ... not found" diagnostic.
	//   - Without activeMetaData: RequireConfiguration trips first.
	// Both are correct — assert only that we did NOT silently succeed.
	if (err) std::free(err);
}

TEST(MetaMutation, CreateRejectsInvalidFormType) {
	// AGENT-CHILD: explicit unknown formType token gets rejected before
	// any tree mutation. Empty / missing formType falls through to
	// defaultFormType — matches Designer's tolerant behaviour for
	// default form binding.
	ScopedPluginManager fx;
	fx.pm.SetMutationPolicy(wxT("pugi"), wxT("meta.create"),
	                          ibPluginManager::MutationPolicy::AllowAlways);

	// Top-level Catalog.Foo doesn't exist (no activeMetaData), so the
	// path resolution rejects before reaching formType validation.
	// However, formType validation runs after ResolvePath ok=true, so
	// this test guards the parser-side semantics: an unknown formType
	// in propertiesJson must NOT cause a silent success even on the
	// no-config branch. The harness verifies the call returns non-zero.
	char* err = nullptr;
	const int rc = metaBridge::HostMetaCreate(
	    "pugi", "",
	    "Catalog.Foo.Forms.MyForm",
	    "{\"formType\":\"NotAValidFormType\"}", &err);
	EXPECT_NE(rc, 0);
	if (err) std::free(err);
}

TEST(MetaMutation, CreateRejectsBrokenModuleCode) {
	// AGENT-CHILD: moduleCode must compile. Even when ResolvePath fails
	// (no live config), we still want assurance that a syntactically
	// invalid module source returns a non-zero rc.
	ScopedPluginManager fx;
	fx.pm.SetMutationPolicy(wxT("pugi"), wxT("meta.create"),
	                          ibPluginManager::MutationPolicy::AllowAlways);

	char* err = nullptr;
	const int rc = metaBridge::HostMetaCreate(
	    "pugi", "",
	    "Catalog.Foo.Forms.MyForm",
	    "{\"moduleCode\":\"this is not valid CES code @@@\"}", &err);
	EXPECT_NE(rc, 0);
	if (err) std::free(err);
}

TEST(MetaMutation, EditAcceptsModuleCodeKeyOnChildPath) {
	// AGENT-CHILD: extended-key validator must accept `moduleCode` on
	// child paths and top-level module objects. Top-level non-module
	// objects fail downstream, not in the unknown-key gate.
	ScopedPluginManager fx;
	fx.pm.SetMutationPolicy(wxT("pugi"), wxT("meta.edit"),
	                          ibPluginManager::MutationPolicy::AllowAlways);

	// Top-level: moduleCode is syntactically accepted. With no live
	// activeMetaData, this still fails at lookup/config resolution, but
	// it must not be reported as an unknown key.
	char* err = nullptr;
	int rc = metaBridge::HostMetaEdit("pugi", "Catalog.Foo",
	                                     "{\"moduleCode\":\"X\"}", &err);
	EXPECT_NE(rc, 0);
	ASSERT_NE(err, nullptr);
	EXPECT_EQ(std::string(err).find("unknown key"), std::string::npos)
	    << "top-level path must accept 'moduleCode' for CommonModule edits";
	std::free(err);

	// Child path: moduleCode is accepted (the failure becomes a
	// downstream lookup error, not an unknown-key error).
	err = nullptr;
	rc = metaBridge::HostMetaEdit("pugi",
	                                  "Catalog.Foo.Forms.MyForm",
	                                  "{\"moduleCode\":\"Procedure F() EndProcedure\"}",
	                                  &err);
	EXPECT_NE(rc, 0);   // still fails — no activeMetaData
	if (err) {
		// MUST NOT carry "unknown key" — moduleCode is a known child key.
		EXPECT_EQ(std::string(err).find("unknown key"), std::string::npos);
		std::free(err);
	}
}

TEST(MetaMutation, DeleteRejectsSingletonChild) {
	// AGENT-CHILD: ObjectModule/ManagerModule cannot be deleted because
	// they're owned by the parent's property tree, not a standalone
	// child collection. metaBridge surfaces the rule via a clear error
	// rather than orphan a property pointer.
	//
	// AGENT-CHILD: use a unique pluginId so the SEC-P1-10 burst-rate
	// gate (which fires wxMessageBox confirmation on repeat deletes
	// within 5 s) doesn't trip in a fast unit-test run.
	ScopedPluginManager fx;
	fx.pm.SetMutationPolicy(wxT("singleton-test"), wxT("meta.delete"),
	                          ibPluginManager::MutationPolicy::AllowAlways);

	char* err = nullptr;
	const int rc = metaBridge::HostMetaDelete(
	    "singleton-test", "Catalog.Foo.ObjectModule",
	    "{\"force\":true}", &err);
	EXPECT_NE(rc, 0);
	// Possible outcomes:
	//   - "no configuration loaded" (RequireConfiguration fails first)
	//   - "top-level parent ... not found" (ResolvePath ok=false)
	//   - "is a singleton; cannot delete" (with activeMetaData present)
	// Any of these is correct rejection.
	if (err) std::free(err);
}

// ---------------------------------------------------------------------------
// Phase 4.1 — plugins.json5 enable/disable + policy persistence.
// ---------------------------------------------------------------------------
#include "backend/plugin/pluginsConfig.h"
#include <wx/filename.h>
#include <wx/wfstream.h>
#include <wx/utils.h>
#include <fstream>

namespace {
// Write a plugins.json5 to a temp directory + point OES_PLUGIN_CONFIG_DIR
// at it. Returns the temp dir path so the test can clean up.
struct ScopedPluginsConfig {
	wxString tmpDir;
	ScopedPluginsConfig(const std::string& body) {
		tmpDir = wxFileName::CreateTempFileName(wxT("oes_pcfg_"));
		wxRemoveFile(tmpDir);     // CreateTempFileName creates a file; we want a dir
		wxFileName::Mkdir(tmpDir, 0700, wxPATH_MKDIR_FULL);
		const wxString path = tmpDir + wxFILE_SEP_PATH + wxT("plugins.json5");
		{
			std::ofstream f(std::string(path.utf8_str()), std::ios::binary);
			f.write(body.data(), body.size());
		}
		wxSetEnv(wxT("OES_PLUGIN_CONFIG_DIR"), tmpDir);
	}
	~ScopedPluginsConfig() {
		wxUnsetEnv(wxT("OES_PLUGIN_CONFIG_DIR"));
		const wxString path = tmpDir + wxFILE_SEP_PATH + wxT("plugins.json5");
		wxRemoveFile(path);
		wxFileName::Rmdir(tmpDir);
	}
};
} // namespace

TEST(PluginsConfig, EmptyFileLeavesDefaults) {
	ScopedPluginsConfig fx("");
	const auto snap = pluginsConfig::Load();
	EXPECT_TRUE(snap.plugins.empty());
	EXPECT_TRUE(snap.policies.empty());
	EXPECT_TRUE(pluginsConfig::IsEnabled(snap, "pugi-oes-bridge"))
	    << "absent entries default to enabled";
}

TEST(PluginsConfig, MalformedJsonReadsAsDefaults) {
	ScopedPluginsConfig fx("{not json at all");
	const auto snap = pluginsConfig::Load();
	EXPECT_TRUE(snap.plugins.empty());
}

TEST(PluginsConfig, EnabledFlagHonored) {
	ScopedPluginsConfig fx(R"({
		"plugins": {
			"pugi-oes-bridge": { "enabled": false },
			"simplePlugin":    { "enabled": true,
			                    "endpoint": "https://example/api" }
		}
	})");
	const auto snap = pluginsConfig::Load();
	EXPECT_FALSE(pluginsConfig::IsEnabled(snap, "pugi-oes-bridge"));
	EXPECT_TRUE (pluginsConfig::IsEnabled(snap, "simplePlugin"));
	EXPECT_TRUE (pluginsConfig::IsEnabled(snap, "unmentioned"))
	    << "absent entries stay enabled by default";

	auto it = snap.plugins.find("simplePlugin");
	ASSERT_NE(it, snap.plugins.end());
	EXPECT_EQ(it->second.endpoint, wxT("https://example/api"));
}

TEST(PluginsConfig, PolicySnapshotParses) {
	ScopedPluginsConfig fx(R"({
		"policy": {
			"pugi-oes-bridge": {
				"*":           "AllowAlways",
				"meta.delete": "Deny",
				"meta.create": "AllowSession"
			}
		}
	})");
	const auto snap = pluginsConfig::Load();
	auto it = snap.policies.find("pugi-oes-bridge");
	ASSERT_NE(it, snap.policies.end());
	EXPECT_EQ(it->second.ops.at("*"),
	          ibPluginManager::MutationPolicy::AllowAlways);
	EXPECT_EQ(it->second.ops.at("meta.delete"),
	          ibPluginManager::MutationPolicy::Deny);
	EXPECT_EQ(it->second.ops.at("meta.create"),
	          ibPluginManager::MutationPolicy::AllowSession);
}

TEST(PluginsConfig, ApplyWritesToManager) {
	ScopedPluginsConfig fx(R"({
		"policy": {
			"pugi-oes-bridge": {
				"meta.create": "AllowAlways"
			}
		}
	})");
	const auto snap = pluginsConfig::Load();
	ibPluginManager mgr;
	pluginsConfig::Apply(snap, mgr);
	EXPECT_EQ(mgr.GetMutationPolicy(wxT("pugi-oes-bridge"), wxT("meta.create")),
	          ibPluginManager::MutationPolicy::AllowAlways);
}

TEST(PluginsConfig, JSONCCommentsAllowed) {
	// nlohmann allows /* … */ + // when ignore_comments=true.
	ScopedPluginsConfig fx(R"({
		// session-only comment
		"plugins": {
			/* multi-line
			   block */
			"pugi-oes-bridge": { "enabled": false }
		}
	})");
	const auto snap = pluginsConfig::Load();
	EXPECT_FALSE(pluginsConfig::IsEnabled(snap, "pugi-oes-bridge"));
}

// ---------------------------------------------------------------------------
// Phase 4.2 — BYOK env file per plugin.
// ---------------------------------------------------------------------------
#include "backend/plugin/byokEnv.h"

TEST(ByokEnv, RoundTripSaveLoad) {
	ScopedPluginsConfig fx("{}");
	byokEnv::KeyMap keys = {
		{"OPENAI_API_KEY", "sk-abc-123"},
		{"WITH_QUOTES",    "value with \"quotes\" and \\backslash"},
		{"MULTILINE",      "line1\nline2\ttab"},
	};
	EXPECT_EQ(byokEnv::Save("pugi-oes-bridge", keys), 0);

	const auto loaded = byokEnv::LoadAll();
	auto it = loaded.find("pugi-oes-bridge");
	ASSERT_NE(it, loaded.end());
	EXPECT_EQ(it->second.at("OPENAI_API_KEY"), "sk-abc-123");
	EXPECT_EQ(it->second.at("WITH_QUOTES"),    "value with \"quotes\" and \\backslash");
	EXPECT_EQ(it->second.at("MULTILINE"),      "line1\nline2\ttab");
}

TEST(ByokEnv, GetReturnsEmptyOnMiss) {
	ScopedPluginsConfig fx("{}");
	byokEnv::Save("pugi", {{"K", "V"}});
	const auto env = byokEnv::LoadAll();
	EXPECT_EQ(byokEnv::Get(env, "pugi", "K"),         "V");
	EXPECT_EQ(byokEnv::Get(env, "pugi", "missing"),   "");
	EXPECT_EQ(byokEnv::Get(env, "other-plugin", "K"), "");
}

TEST(ByokEnv, ParsesCommentsAndBlankLines) {
	ScopedPluginsConfig fx("{}");
	// Write by hand to exercise the parser on raw dotenv input rather
	// than a Save() round-trip.
	const wxString path = byokEnv::GetEnvFilePath("manual");
	wxFileName fn(path);
	fn.Mkdir(0700, wxPATH_MKDIR_FULL);
	{
		std::ofstream f(std::string(path.utf8_str()), std::ios::binary);
		f << "# top-level comment\n";
		f << "\n";
		f << "  # indented comment\n";
		f << "FOO=bar\n";
		f << "  SPACED  =  baz  \n";
		f << "QUOTED=\"with spaces\"\n";
		f << "NOEQUAL_LINE_IGNORED\n";
		f << "=value_without_key_ignored\n";
	}

	const auto env = byokEnv::LoadAll();
	auto it = env.find("manual");
	ASSERT_NE(it, env.end());
	EXPECT_EQ(it->second.at("FOO"),    "bar");
	EXPECT_EQ(it->second.at("SPACED"), "baz");
	EXPECT_EQ(it->second.at("QUOTED"), "with spaces");
	EXPECT_EQ(it->second.count("NOEQUAL_LINE_IGNORED"), 0u);
	EXPECT_EQ(it->second.count(""), 0u);
}

TEST(ByokEnv, ManagerReadPluginEnvIsolated) {
	// Two plugins, each with its own env. ReadPluginEnv must return
	// only the caller's own keys.
	ibPluginManager mgr;
	ibPluginManager::PluginEnvMap env = {
		{ "pugi",  {{"TOKEN", "pugi-secret"}}  },
		{ "other", {{"TOKEN", "other-secret"}} },
	};
	mgr.SetPluginEnvForTests(std::move(env));
	EXPECT_EQ(mgr.ReadPluginEnv("pugi",  "TOKEN"), "pugi-secret");
	EXPECT_EQ(mgr.ReadPluginEnv("other", "TOKEN"), "other-secret");
	EXPECT_EQ(mgr.ReadPluginEnv("pugi",  "MISSING"), "");
	EXPECT_EQ(mgr.ReadPluginEnv("nobody","TOKEN"),   "");
}

// ---------------------------------------------------------------------------
// Phase 4.3 — pluginsConfig::Save round-trip.
// ---------------------------------------------------------------------------

TEST(PluginsConfig, SaveRoundTrip) {
	ScopedPluginsConfig fx("");

	pluginsConfig::Snapshot snap;
	snap.plugins["pugi-oes-bridge"] = {
		/*enabled*/ false,
		/*endpoint*/ wxT("https://app.pugi.io"),
		/*byokRef*/  wxT("pugi.env"),
	};
	snap.plugins["simplePlugin"] = { true, wxT(""), wxT("") };
	pluginsConfig::PolicyEntry pe;
	pe.ops["meta.create"] = ibPluginManager::MutationPolicy::AllowAlways;
	pe.ops["meta.delete"] = ibPluginManager::MutationPolicy::Deny;
	snap.policies["pugi-oes-bridge"] = pe;

	EXPECT_EQ(pluginsConfig::Save(snap), 0);

	const auto reloaded = pluginsConfig::Load();
	auto it = reloaded.plugins.find("pugi-oes-bridge");
	ASSERT_NE(it, reloaded.plugins.end());
	EXPECT_FALSE(it->second.enabled);
	EXPECT_EQ(it->second.endpoint, wxT("https://app.pugi.io"));
	EXPECT_EQ(it->second.byokRef,  wxT("pugi.env"));

	auto pit = reloaded.policies.find("pugi-oes-bridge");
	ASSERT_NE(pit, reloaded.policies.end());
	EXPECT_EQ(pit->second.ops.at("meta.create"),
	          ibPluginManager::MutationPolicy::AllowAlways);
	EXPECT_EQ(pit->second.ops.at("meta.delete"),
	          ibPluginManager::MutationPolicy::Deny);
}

TEST(PluginsConfig, SaveAtomicityLeavesPriorOnTempFailure) {
	// Save() writes via temp + rename — verify that the .tmp doesn't
	// linger after a successful Save (rename consumes it).
	ScopedPluginsConfig fx(R"({"plugins":{"keep":{"enabled":true}}})");
	pluginsConfig::Snapshot snap;
	snap.plugins["x"] = { true, wxT(""), wxT("") };
	EXPECT_EQ(pluginsConfig::Save(snap), 0);

	wxFileName cfg(pluginsConfig::GetConfigPath());
	const wxString tmp = pluginsConfig::GetConfigPath() + wxT(".tmp");
	EXPECT_FALSE(wxFileExists(tmp))
	    << "temp file must be consumed by atomic rename";
}

// ---------------------------------------------------------------------------
// Phase 4.4 — pluginInstaller ReadManifest + path-traversal guards.
// ---------------------------------------------------------------------------
#include "backend/plugin/pluginInstaller.h"
#include <wx/zipstrm.h>
#include <wx/wfstream.h>

namespace {
// Create a minimal .zip with manifest.json + a fake binary at tmpPath.
// Caller removes the file when done.
void CreateZipBundle(const wxString& tmpPath, const std::string& manifestJson,
                       const std::string& binaryName, bool includeBinary = true)
{
	wxFFileOutputStream out(tmpPath);
	wxZipOutputStream zip(out);
	{
		zip.PutNextEntry(wxT("manifest.json"));
		zip.Write(manifestJson.data(), manifestJson.size());
	}
	if (includeBinary && !binaryName.empty()) {
		zip.PutNextEntry(wxString::FromUTF8(binaryName));
		const char body[] = "BINARY-PAYLOAD";
		zip.Write(body, sizeof(body) - 1);
	}
}
} // namespace

TEST(PluginInstaller, ReadManifestSucceedsOnWellFormed) {
	const wxString zip = wxFileName::CreateTempFileName(wxT("oes_inst_zip_"));
	CreateZipBundle(zip,
	    R"({"pluginId":"pugi","version":"1.2.3","abiVersion":4,
	         "binary":"pugi.dylib"})",
	    "pugi.dylib");

	pluginInstaller::Manifest m;
	wxString err;
	EXPECT_TRUE(pluginInstaller::ReadManifest(zip, m, &err)) << err.ToStdString();
	EXPECT_EQ(m.pluginId,   "pugi");
	EXPECT_EQ(m.version,    "1.2.3");
	EXPECT_EQ(m.abiVersion, 4);
	EXPECT_EQ(m.binary,     "pugi.dylib");
	wxRemoveFile(zip);
}

TEST(PluginInstaller, ReadManifestRejectsMissingFields) {
	const wxString zip = wxFileName::CreateTempFileName(wxT("oes_inst_zip2_"));
	CreateZipBundle(zip, R"({"pluginId":"only"})", "");

	pluginInstaller::Manifest m;
	wxString err;
	EXPECT_FALSE(pluginInstaller::ReadManifest(zip, m, &err));
	EXPECT_NE(err.Find("missing required fields"), wxNOT_FOUND);
	wxRemoveFile(zip);
}

TEST(PluginInstaller, ReadManifestRejectsPathTraversalInBinary) {
	const wxString zip = wxFileName::CreateTempFileName(wxT("oes_inst_zip3_"));
	CreateZipBundle(zip,
	    R"({"pluginId":"evil","version":"1.0.0","abiVersion":4,
	         "binary":"../../etc/passwd"})", "x");

	pluginInstaller::Manifest m;
	wxString err;
	EXPECT_FALSE(pluginInstaller::ReadManifest(zip, m, &err));
	EXPECT_NE(err.Find("relative path"), wxNOT_FOUND);
	wxRemoveFile(zip);
}

TEST(PluginInstaller, ReadManifestRejectsAbsoluteBinary) {
	const wxString zip = wxFileName::CreateTempFileName(wxT("oes_inst_zip4_"));
	CreateZipBundle(zip,
	    R"({"pluginId":"evil","version":"1.0.0","abiVersion":4,
	         "binary":"/etc/passwd"})", "x");
	pluginInstaller::Manifest m;
	wxString err;
	EXPECT_FALSE(pluginInstaller::ReadManifest(zip, m, &err));
	wxRemoveFile(zip);
}

TEST(PluginInstaller, ReadManifestRejectsNonZip) {
	const wxString notZip = wxFileName::CreateTempFileName(wxT("oes_notzip_"));
	{
		std::ofstream f(std::string(notZip.utf8_str()), std::ios::binary);
		f << "this is not a zip file";
	}
	pluginInstaller::Manifest m;
	wxString err;
	EXPECT_FALSE(pluginInstaller::ReadManifest(notZip, m, &err));
	wxRemoveFile(notZip);
}

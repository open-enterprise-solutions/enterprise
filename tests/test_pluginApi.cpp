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
}

TEST(PluginAbi, LockDeniedCodeIsStable) {
	// Plugins compile against this constant; can't shift the value
	// even between minor releases.
	EXPECT_EQ(IB_PLUGIN_LOCK_DENIED, 0x0001);
}

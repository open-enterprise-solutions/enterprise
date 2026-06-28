// =============================================================================
// OES Enterprise — CLSID kind-typing tests
//
// ibClassID = [63:56] kind (1 byte) | [55:0] body (56 bits), see clsid.h.
//   * static types: body = ib_clsid_hash(name) & 56-bit, kind = the registrar family.
//   * dynamic metaobject values: body = the metaID itself (constructive), kind = the metatype.
// string_to_clsid is GONE — every callsite uses a per-kind generator (value_to_clsid, …).
// =============================================================================

#include <gtest/gtest.h>
#include <set>
#include "backend/backend_core.h"
#include "backend/clsid.h"

// --- the underlying FNV-1a hash is still deterministic (it is the body source) ---
TEST(ClsidTest, HashIsDeterministic) {
    EXPECT_NE(ib_clsid_hash("VL_NUMB"), 0u);
    EXPECT_EQ(ib_clsid_hash("VL_NUMB"), ib_clsid_hash("VL_NUMB"));
    EXPECT_NE(ib_clsid_hash("A"), ib_clsid_hash("AA"));   // length folded in
}

// --- make_clsid bakes the kind into the high byte; body = masked hash ---
TEST(ClsidTest, MakeClsidPacksKindInHighByte) {
    const ibClassID id = make_clsid("VL_NUMB", ibClassKind_Value);
    EXPECT_EQ(clsid_kind(id), ibClassKind_Value);
    EXPECT_EQ(static_cast<unsigned char>(id >> 56),
              static_cast<unsigned char>(ibClassKind_Value));
    EXPECT_EQ(id & kIbClsidBodyMask, ib_clsid_hash("VL_NUMB") & kIbClsidBodyMask);
}

// --- every per-kind generator stamps its own kind ---
TEST(ClsidTest, PerKindGeneratorsSetKind) {
    EXPECT_EQ(clsid_kind(primitive_to_clsid("X")), ibClassKind_Primitive);
    EXPECT_EQ(clsid_kind(value_to_clsid("X")),     ibClassKind_Value);
    EXPECT_EQ(clsid_kind(control_to_clsid("X")),   ibClassKind_Control);
    EXPECT_EQ(clsid_kind(system_to_clsid("X")),    ibClassKind_System);
    EXPECT_EQ(clsid_kind(enum_to_clsid("X")),      ibClassKind_Enum);
    EXPECT_EQ(clsid_kind(context_to_clsid("X")),   ibClassKind_Context);
    EXPECT_EQ(clsid_kind(metadata_to_clsid("X")),  ibClassKind_Metadata);
    EXPECT_EQ(clsid_kind(picture_to_clsid("X")),   ibClassKind_Picture);
}

// --- stateless Is* helpers read the kind byte — NO metadata lookup ---
TEST(ClsidTest, StatelessKindHelpers) {
    EXPECT_TRUE (IsReference(reference_to_clsid(42)));
    EXPECT_FALSE(IsReference(object_to_clsid(42)));
    EXPECT_FALSE(IsReference(value_to_clsid("VL_X")));
    EXPECT_TRUE (IsObject(object_to_clsid(7)));
    EXPECT_TRUE (IsObject(externalObject_to_clsid(7)));   // external object still IsObject
    EXPECT_TRUE (IsManager(manager_to_clsid(7)));
    EXPECT_TRUE (IsManager(externalManager_to_clsid(7)));
    EXPECT_TRUE (IsTabularSection(tabularSection_to_clsid(7)));
    EXPECT_TRUE (IsEnum(enum_to_clsid("EN_X")));
    EXPECT_TRUE (IsControl(control_to_clsid("CT_X")));
    EXPECT_TRUE (IsMetadata(metadata_to_clsid("MD_X")));
}

// --- dynamic body is CONSTRUCTIVE (the metaID itself), zero collision possibility ---
TEST(ClsidTest, DynamicBodyIsConstructiveMetaID) {
    const ibClassID id = reference_to_clsid(12345);
    EXPECT_EQ(clsid_kind(id), ibClassKind_Reference);
    EXPECT_EQ(id & kIbClsidBodyMask, 12345u);             // body == metaID, no hash
    EXPECT_NE(reference_to_clsid(1), reference_to_clsid(2));
    // the O_/EO_ and M_/EM_ collision the External kinds resolve: same metaID, distinct ids.
    EXPECT_NE(object_to_clsid(7),  externalObject_to_clsid(7));
    EXPECT_NE(manager_to_clsid(7), externalManager_to_clsid(7));
    // a reference and an object of the SAME metaID never collide (different kind).
    EXPECT_NE(reference_to_clsid(7), object_to_clsid(7));
}

// --- cross-kind never collides; same name, different kind -> different id ---
TEST(ClsidTest, SameNameDifferentKindDiffers) {
    EXPECT_NE(value_to_clsid("X"),    system_to_clsid("X"));
    EXPECT_NE(metadata_to_clsid("X"), control_to_clsid("X"));
    EXPECT_NE(enum_to_clsid("X"),     value_to_clsid("X"));
}

// --- within ONE kind the known names stay unique (56-bit body) ---
TEST(ClsidTest, WithinKindUniqueness) {
    const char* known[] = { "VL_UNDF", "VL_BOOL", "VL_NUMB",
                            "VL_DATE", "VL_STRI", "VL_NULL" };
    std::set<ibClassID> ids;
    for (const char* n : known) ids.insert(primitive_to_clsid(n));
    EXPECT_EQ(ids.size(), std::size(known));
}

// --- the None kind leaves the top byte zero (pure body — synthetic / unregistered ids) ---
TEST(ClsidTest, NoneKindLeavesTopByteZero) {
    const ibClassID id = make_clsid("UI_X", ibClassKind_None);
    EXPECT_EQ(clsid_kind(id), ibClassKind_None);
    EXPECT_EQ(static_cast<unsigned char>(id >> 56), 0u);
}

// --- clsid_to_string stays a debug hex of the id (no inverse) ---
TEST(ClsidTest, ClsidToStringIsHex) {
    const ibClassID id = value_to_clsid("VL_NUMB");
    EXPECT_EQ(clsid_to_string(id),
              wxString::Format(wxT("0x%016llX"), static_cast<uint64_t>(id)));
    EXPECT_EQ(clsid_to_string(0), wxEmptyString);
}

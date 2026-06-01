// =============================================================================
// OES Enterprise — module-manager split structural + seam smoke tests
//
// Guards the 2026-06-01 refactor that split ibValueModuleManager into a
// lightweight base + a heavy runtime branch + a designer holder, and routed
// every object/record/module through the ibSession::GetEditModuleManager seam
// (Designer → lightweight designer manager from the compile cache; runtime →
// per-session root mm). See docs/module-manager-split.md.
//
// These are pure structural/null-safety smoke tests — they need no database,
// no appData wiring, no live session pool. The behavioural half (which manager
// a real Catalog/Document object actually parents to, common-module
// registration through OnBeforeRunMetaObject, reload lifetime) is integration
// scope and exercised by the designer at runtime, not here.
// =============================================================================

#include <gtest/gtest.h>

#include "backend/session/session.h"
#include "backend/moduleManager/moduleManager.h"
#include "backend/moduleManager/moduleManagerExt.h"   // external DP/Report managers

#include <type_traits>

// ---------------------------------------------------------------------------
// Hierarchy: lightweight base, heavy runtime branch, designer holder
// ---------------------------------------------------------------------------

TEST(ModuleManagerSplit, RuntimeManagerDerivesLightweightBase) {
    // The heavy runtime manager is-a lightweight base — the base carries the
    // editor-facing surface (context, FindCommonModule), the runtime branch
    // adds ProcUnit spin-up / Attach.
    EXPECT_TRUE((std::is_base_of<ibValueModuleManager, ibValueModuleRuntimeManager>::value));
}

TEST(ModuleManagerSplit, RuntimeConfigurationDerivesRuntimeManager) {
    // The per-session root mm is a runtime configuration — three levels deep.
    EXPECT_TRUE((std::is_base_of<ibValueModuleRuntimeManager, ibValueModuleManagerRuntimeConfiguration>::value));
    EXPECT_TRUE((std::is_base_of<ibValueModuleManager, ibValueModuleManagerRuntimeConfiguration>::value));
}

TEST(ModuleManagerSplit, DesignerHolderDerivesLightweightBaseNotRuntime) {
    // The designer holder is-a lightweight base, but must NOT be-a runtime
    // manager — it owns no ProcUnit / runtime common-module registry. This is
    // the whole point of the split: the editor's read path never touches a
    // fragile runtime unit.
    EXPECT_TRUE((std::is_base_of<ibValueModuleManager, ibValueModuleManagerDesigner>::value));
    EXPECT_FALSE((std::is_base_of<ibValueModuleRuntimeManager, ibValueModuleManagerDesigner>::value));
}

TEST(ModuleManagerSplit, ExternalManagersDeriveRuntimeManager) {
    // External data-processor / report managers are runtime managers (they run
    // the .epf/.erf object's script).
    EXPECT_TRUE((std::is_base_of<ibValueModuleRuntimeManager, ibValueModuleRuntimeManagerExternalDataProcessor>::value));
    EXPECT_TRUE((std::is_base_of<ibValueModuleRuntimeManager, ibValueModuleRuntimeManagerExternalReport>::value));
}

TEST(ModuleManagerSplit, BaseIsAbstract_FindCommonModulePureVirtual) {
    // FindCommonModule was lifted onto the base as a pure virtual (designer and
    // runtime resolve it from their own registries). The base must therefore be
    // abstract — never instantiated directly.
    EXPECT_TRUE(std::is_abstract<ibValueModuleManager>::value);
}

// ---------------------------------------------------------------------------
// Seam: ibSession::GetEditModuleManager / EditModuleManagerFor null-safety
// ---------------------------------------------------------------------------

TEST(ModuleManagerSeam, StaticHelperNullWhenNoCurrentSession) {
    // EditModuleManagerFor resolves against ibSession::Current(). With no
    // session bound to this thread it must fold to null, not crash — the
    // codeRunner / launcher sessionless path relies on this.
    EXPECT_EQ(ibSession::EditModuleManagerFor(nullptr), nullptr);
}

TEST(ModuleManagerSeam, DesignerSessionWithoutCacheYieldsNull) {
    // A Designer-kind session has no runtime root; with null metadata (hence no
    // compile cache) the seam returns null rather than dereferencing.
    ibSession sess(wxT("seam-designer"), ibSessionKind::Designer);
    EXPECT_EQ(sess.GetEditModuleManager(nullptr), nullptr);
}

TEST(ModuleManagerSeam, RuntimeSessionWithoutRootYieldsNull) {
    // A fresh runtime-kind session has not had CreateRoot run, so m_root is
    // null. The seam returns it as-is (null) without asserting/crashing.
    ibSession sess(wxT("seam-runtime"), ibSessionKind::Enterprise);
    EXPECT_EQ(sess.GetEditModuleManager(nullptr), nullptr);
    EXPECT_EQ(sess.GetManagerModule(), nullptr);
}

TEST(ModuleManagerSeam, NonDesignerKindIgnoresCachePath) {
    // For non-Designer kinds the seam takes the root road regardless of
    // metadata — a null-metadata runtime session still returns m_root (null
    // here), never touching GetCompileCache.
    ibSession svc(wxT("seam-service"), ibSessionKind::Service);
    EXPECT_EQ(svc.GetEditModuleManager(nullptr), nullptr);
}

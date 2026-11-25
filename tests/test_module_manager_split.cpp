// =============================================================================
// OES Enterprise — module-manager split structural + seam smoke tests
//
// Guards the 2026-06-01 refactor that split ibValueModuleManager into a
// lightweight base + a heavy runtime branch + a studio holder, and routed
// every object/record/module through the ibSession::GetEditModuleManager seam
// (Studio → lightweight studio manager from the compile cache; runtime →
// per-session root mm). See docs/private/module-manager-split.md.
//
// These are pure structural/null-safety smoke tests — they need no database,
// no appData wiring, no live session pool. The behavioural half (which manager
// a real Catalog/Document object actually parents to, shared-module
// registration through OnBeforeRunMetaObject, reload lifetime) is integration
// scope and exercised by the studio at runtime, not here.
// =============================================================================

#include <gtest/gtest.h>
#include <wx/string.h>   // wxT, wxString - the engine's headers include no wx

#include "backend/session/session.h"
#include "backend/moduleManager/moduleManager.h"
#include "backend/moduleManager/moduleManagerExt.h"   // external DataTool/Report managers

#include <type_traits>

// ---------------------------------------------------------------------------
// Hierarchy: lightweight base, heavy runtime branch, studio holder
// ---------------------------------------------------------------------------

TEST(ModuleManagerSplit, RuntimeManagerDerivesLightweightBase) {
    // The heavy runtime manager is-a lightweight base — the base carries the
    // editor-facing surface (context, FindSharedModule), the runtime branch
    // adds ProcUnit spin-up / Attach.
    EXPECT_TRUE((std::is_base_of_v<ibValueModuleManager, ibValueModuleRuntimeManager>));
}

TEST(ModuleManagerSplit, RuntimeApplicationDerivesRuntimeManager) {
    // The per-session root mm is a runtime application — three levels deep.
    EXPECT_TRUE((std::is_base_of_v<ibValueModuleRuntimeManager, ibValueModuleManagerRuntimeApplication>));
    EXPECT_TRUE((std::is_base_of_v<ibValueModuleManager, ibValueModuleManagerRuntimeApplication>));
}

TEST(ModuleManagerSplit, StudioHolderDerivesLightweightBaseNotRuntime) {
    // The studio holder is-a lightweight base, but must NOT be-a runtime
    // manager — it owns no ProcUnit / runtime shared-module registry. This is
    // the whole point of the split: the editor's read path never touches a
    // fragile runtime unit.
    EXPECT_TRUE((std::is_base_of_v<ibValueModuleManager, ibValueModuleManagerStudio>));
    EXPECT_FALSE((std::is_base_of_v<ibValueModuleRuntimeManager, ibValueModuleManagerStudio>));
}

TEST(ModuleManagerSplit, ExternalManagersDeriveRuntimeManager) {
    // External data-tool / report managers are runtime managers (they run
    // the .otl/.orp object's script).
    EXPECT_TRUE((std::is_base_of_v<ibValueModuleRuntimeManager, ibValueModuleRuntimeManagerExternalDataTool>));
    EXPECT_TRUE((std::is_base_of_v<ibValueModuleRuntimeManager, ibValueModuleRuntimeManagerExternalReport>));
}

TEST(ModuleManagerSplit, BaseIsAbstract_FindSharedModulePureVirtual) {
    // FindSharedModule was lifted onto the base as a pure virtual (studio and
    // runtime resolve it from their own registries). The base must therefore be
    // abstract — never instantiated directly.
    EXPECT_TRUE(std::is_abstract_v<ibValueModuleManager>);
}

// ---------------------------------------------------------------------------
// Seam: ibSession::GetEditModuleManager / EditModuleManagerFor null-safety
// ---------------------------------------------------------------------------

TEST(ModuleManagerSeam, StaticHelperNullWhenNoCurrentSession) {
    // EditModuleManagerFor resolves against ibSession::Current(). With no
    // session bound to this thread it must fold to null, not crash — the
    // coderunner / launcher sessionless path relies on this.
    EXPECT_EQ(ibSession::EditModuleManagerFor(nullptr), nullptr);
}

TEST(ModuleManagerSeam, StudioSessionWithoutCacheYieldsNull) {
    // A Studio-kind session has no runtime root; with null metadata (hence no
    // compile cache) the seam returns null rather than dereferencing.
    ibSession sess(wxT("seam-studio"), ibSessionKind::Studio);
    EXPECT_EQ(sess.GetEditModuleManager(nullptr), nullptr);
}

TEST(ModuleManagerSeam, RuntimeSessionWithoutRootYieldsNull) {
    // A fresh runtime-kind session has not had CreateRoot run, so m_root is
    // null. The seam returns it as-is (null) without asserting/crashing.
    ibSession sess(wxT("seam-runtime"), ibSessionKind::Workspace);
    EXPECT_EQ(sess.GetEditModuleManager(nullptr), nullptr);
    EXPECT_EQ(sess.GetManagerModule(), nullptr);
}

TEST(ModuleManagerSeam, NonStudioKindIgnoresCachePath) {
    // For non-Studio kinds the seam takes the root road regardless of
    // metadata — a null-metadata runtime session still returns m_root (null
    // here), never touching GetCompileCache.
    ibSession svc(wxT("seam-service"), ibSessionKind::Service);
    EXPECT_EQ(svc.GetEditModuleManager(nullptr), nullptr);
}

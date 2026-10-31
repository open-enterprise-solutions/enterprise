// =============================================================================
// OES Enterprise — module-manager split structural + seam smoke tests
//
// Guards the 2026-06-01 refactor that split ibValueModuleManager into a
// lightweight base + a heavy runtime branch + a designer holder, and routed
// every object/record/module through the ibSession::GetEditModuleManager seam
// (Designer → lightweight designer manager from the compile cache; runtime →
// per-session root mm). See docs/private/module-manager-split.md.
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

// ---------------------------------------------------------------------------
// External object ↔ its manager: ownership is in the types (PR 589). The
// manager owns the compile module and the proc unit; the object only borrows
// them, and the manager knows the object only weakly. Whichever goes first,
// the other reads empty — nothing is freed twice, nothing points at freed memory.
// ---------------------------------------------------------------------------

namespace {
    // Opens the protected answers the external objects give GetCompileModule() / GetProcUnit() with.
    class ibExternalOwnerProbe : public ibExternalOwnerHelper {
    public:
        ibExternalOwnerProbe() : ibExternalOwnerHelper(nullptr) {}
        using ibExternalOwnerHelper::GetBorrowedCompileModule;
        using ibExternalOwnerHelper::GetBorrowedProcUnit;
    };
}

TEST(ibExternalOwnerHelper, BorrowRuntime_LenderLetsGo_BorrowReadsEmpty) {
    ibExternalOwnerProbe object;
    std::shared_ptr<ibProcUnit> procUnit = std::make_shared<ibProcUnit>();
    object.BorrowRuntime(nullptr, procUnit);
    EXPECT_EQ(object.GetBorrowedProcUnit(), procUnit);
    EXPECT_EQ(procUnit.use_count(), 1);   // a borrow keeps nothing alive

    procUnit.reset();                     // the manager's DestroyMainModule / its death
    EXPECT_EQ(object.GetBorrowedProcUnit(), nullptr);
    EXPECT_EQ(object.GetBorrowedCompileModule(), nullptr);
}

TEST(ibExternalOwnerHelper, GetWeakObject_ObjectGoesFirst_ManagerReadsEmpty) {
    // The road of the webserver SIGSEGV: the object dies while the container, and the manager in it, stay.
    std::weak_ptr<ibValueRecordDataObjectExt> managerSide;
    {
        ibExternalOwnerProbe object;
        managerSide = object.GetWeakObject();
        EXPECT_FALSE(managerSide.expired());
    }
    EXPECT_TRUE(managerSide.expired());
}

TEST(ibByteCode, Unregister_SameIdCompiledAgain_KeepsTheLaterOne) {
    // Every descriptor compiling one module stamps its guid: a module dying drops only its own slot.
    const ibGuid id = ibGuid::newGuid();
    ibByteCode first, second;
    first.m_id = id;
    second.m_id = id;
    ibByteCode::Register(&first);
    ibByteCode::Register(&second);

    ibByteCode::Unregister(&first);
    EXPECT_EQ(ibByteCode::Find(id), &second);

    ibByteCode::Unregister(&second);
    EXPECT_EQ(ibByteCode::Find(id), nullptr);
}

TEST(ibByteCode, Unregister_IdClearedByReset_FoundByPointer) {
    // A module Reset() before it died has no id left to be found by.
    const ibGuid id = ibGuid::newGuid();
    ibByteCode bc;
    bc.m_id = id;
    ibByteCode::Register(&bc);
    bc.Reset();

    ibByteCode::Unregister(&bc);
    EXPECT_EQ(ibByteCode::Find(id), nullptr);
}
